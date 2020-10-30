/*
 * Copyright (c) 2020, Broadband Forum
 * Copyright (c) 2020, AT&T Communications
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * UDP Speed Test - udpst.c
 *
 * This file contains main() and handles configuration initialization as well
 * as parameter processing.
 *
 * Author                  Date          Comments
 * --------------------    ----------    ----------------------------------
 * Len Ciavattone          01/16/2019    Created
 * Len Ciavattone          10/18/2019    Add param for load sample period
 * Len Ciavattone          11/04/2019    Add minimum delays to summary
 * Len Ciavattone          06/16/2020    Add dual-stack (IPv6) support
 * Len Ciavattone          07/02/2020    Added (HMAC-SHA256) authentication
 * Len Ciavattone          08/04/2020    Rearranged source files
 * Len Ciavattone          09/03/2020    Added __linux__ conditionals
 * Len Ciavattone          10/09/2020    Add parameter for bimodal support
 *
 */

#include "config.h"

#define UDPST
#ifdef __linux__
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef AUTH_KEY_ENABLE
#include <openssl/hmac.h>
#include <openssl/x509.h>
#endif
#else
#include "../udpst_alt1.h"
#endif
//
#include "udpst_common.h"
#include "udpst_protocol.h"
#include "udpst.h"
#include "udpst_control.h"
#include "udpst_data.h"
#include "udpst_srates.h"
#ifndef __linux__
#include "../udpst_alt2.h"
#endif

//----------------------------------------------------------------------------
//
// Internal function prototypes
//
void signal_alrm(int);
void signal_exit(int);
int proc_parameters(int, char **, int);
int param_error(int, int, int);

//----------------------------------------------------------------------------
//
// Global data
//
int errConn = -1, monConn = -1;            // Error and monitoring connections
char scratch[STRING_SIZE];                 // General purpose scratch buffer
struct configuration conf;                 // Configuration data structure
struct repository repo;                    // Repository of global data
struct connection *conn;                   // Connection table (array)
static volatile sig_atomic_t sig_alrm = 0; // Interrupt indicator
static volatile sig_atomic_t sig_exit = 0; // Interrupt indicator
static char *boolText[]               = {"Disabled", "Enabled"};

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------
//
// Program entry point
//
int main(int argc, char **argv) {
        pid_t pid;
        int i, j, var, var2, readyfds, pristatus, secstatus, fd = -1;
        int appstatus = 0, outputfd = STDOUT_FILENO, logfilefd = -1;
        struct itimerval itime;
        struct sigaction saction;
        struct epoll_event epoll_events[MAX_EPOLL_EVENTS];
        struct stat statbuf;

        //
        // Verify and process parameters, initialize configuration and repository
        //
        if (proc_parameters(argc, argv, outputfd) != 0) {
                return -1;
        }

        //
        // Execute as daemon if requested
        //
        if (conf.isDaemon) {
                //
                // Open log file before child creation
                //
                if (conf.logFile != NULL) {
                        if ((logfilefd = open(conf.logFile, LOGFILE_FLAGS, LOGFILE_MODE)) < 0) {
                                var = snprintf(scratch, STRING_SIZE, "OPEN ERROR: <%s> %s\n", conf.logFile, strerror(errno));
                                var = write(outputfd, scratch, var);
                                return -1;
                        }
                        if (fstat(logfilefd, &statbuf) < 0) {
                                var = snprintf(scratch, STRING_SIZE, "FSTAT ERROR: <%s> %s\n", conf.logFile, strerror(errno));
                                var = write(outputfd, scratch, var);
                                return -1;
                        }
                        repo.logFileSize = (int) statbuf.st_size; // Initialize log file size
                        outputfd         = logfilefd;
                }
                //
                // Create child
                //
                if ((pid = fork()) < 0) {
                        var = sprintf(scratch, "ERROR: fork() failed\n");
                        var = write(outputfd, scratch, var);
                        return -1;
                } else if (pid != 0) {
                        return 0; // Parent exits
                }
                //
                // Initialize child
                //
                setsid();
                var = chdir("/");
                umask(0);
                if ((i = open("/dev/null", O_RDWR)) >= 0) {
                        dup2(i, STDIN_FILENO);
                        dup2(i, STDOUT_FILENO);
                        dup2(i, STDERR_FILENO);
                        if ((i != STDIN_FILENO) && (i != STDOUT_FILENO) && (i != STDERR_FILENO))
                                close(i);
                }
        }

        //
        // Print banner
        //
        var = sprintf(scratch, SOFTWARE_TITLE "\nSoftware Ver: %s, Protocol Ver: %d, Built: " __DATE__ " " __TIME__ "\n",
                      SOFTWARE_VER, PROTOCOL_VER);
        var = write(outputfd, scratch, var);
        if (repo.isServer)
                var = sprintf(scratch, "Mode: Server, Jumbo Datagrams: %s", boolText[conf.jumboStatus]);
        else
                var = sprintf(scratch, "Mode: Client, Jumbo Datagrams: %s", boolText[conf.jumboStatus]);
#ifdef AUTH_KEY_ENABLE
        var += sprintf(&scratch[var], ", Authentication: Available\n");
#else
        var += sprintf(&scratch[var], ", Authentication: Unavailable\n");
#endif
        var = write(outputfd, scratch, var);

        //
        // Allocate and initialize buffers
        //
        repo.sendingRates = calloc(1, MAX_SENDING_RATES * sizeof(struct sendingRate));
        repo.sndBuffer    = calloc(1, SND_BUFFER_SIZE);
        repo.defBuffer    = calloc(1, DEF_BUFFER_SIZE);
        conn              = malloc(MAX_CONNECTIONS * sizeof(struct connection));
        if (repo.sendingRates == NULL || repo.sndBuffer == NULL || repo.defBuffer == NULL || conn == NULL) {
                var = sprintf(scratch, "ERROR: Memory allocation(s) failed\n");
                var = write(outputfd, scratch, var);
                return -1;
        }
        for (i = 0; i < MAX_CONNECTIONS; i++)
                init_conn(i, FALSE);

        //
        // Define sending rate table
        //
        if ((var = def_sending_rates()) > 0) {
                var = write(outputfd, scratch, var);
                return -1;
        }

        //
        // Display resulting sending rate table if requested and exit
        //
        if (conf.showSendingRates) {
                show_sending_rates(outputfd);
                return 0;
        }

        //
        // Check for needed clock resolution
        //
        if (clock_getres(CLOCK_REALTIME, &repo.systemClock) == -1) {
                var = sprintf(scratch, "CLOCK_GETRES ERROR: %s\n", strerror(errno));
                var = write(outputfd, scratch, var);
                return -1;
        }
        if (repo.systemClock.tv_nsec > 1) {
                var = sprintf(scratch, "ERROR: Clock resolution (%ld ns) out of range\n", repo.systemClock.tv_nsec);
                var = write(outputfd, scratch, var);
                return -1;
        }

        //
        // Initialize local copy of system time clock and set alarm signal handler
        //
        clock_gettime(CLOCK_REALTIME, &repo.systemClock);
        saction.sa_handler = signal_alrm;
        sigemptyset(&saction.sa_mask);
        saction.sa_flags = SA_RESTART;
        if (sigaction(SIGALRM, &saction, NULL) != 0) {
                var = sprintf(scratch, "SIGALRM ERROR: %s\n", strerror(errno));
                var = write(outputfd, scratch, var);
                return -1;
        }

        //
        // Create system interval timer used to drive all local timers
        //
        itime.it_interval.tv_sec = itime.it_value.tv_sec = 0;
        itime.it_interval.tv_usec = itime.it_value.tv_usec = MIN_INTERVAL_USEC;
        if (setitimer(ITIMER_REAL, &itime, NULL) != 0) {
                var = sprintf(scratch, "ITIMER ERROR: %s\n", strerror(errno));
                var = write(outputfd, scratch, var);
                return -1;
        }

        //
        // Set exit signal handler
        //
        saction.sa_handler = signal_exit;
        sigemptyset(&saction.sa_mask);
        saction.sa_flags = 0;
        pristatus        = 0;
        pristatus += sigaction(SIGTERM, &saction, NULL);
        pristatus += sigaction(SIGINT, &saction, NULL);
        pristatus += sigaction(SIGQUIT, &saction, NULL);
        pristatus += sigaction(SIGTSTP, &saction, NULL);
        if (pristatus != 0) {
                var = sprintf(scratch, "ERROR: Unable to install exit signal handler\n");
                var = write(outputfd, scratch, var);
                return -1;
        }

        //
        // Open epoll file descriptor to process I/O events
        //
        if ((repo.epollFD = epoll_create1(0)) < 0) {
                var = sprintf(scratch, "ERROR: Unable to open epoll file descriptor\n");
                var = write(outputfd, scratch, var);
                return -1;
        }

        //
        // Set standard FDs as non-blocking
        //
        pristatus = 0;
        var       = fcntl(STDIN_FILENO, F_GETFL, 0);
        pristatus += fcntl(STDIN_FILENO, F_SETFL, var | O_NONBLOCK);
        var = fcntl(STDOUT_FILENO, F_GETFL, 0);
        pristatus += fcntl(STDOUT_FILENO, F_SETFL, var | O_NONBLOCK);
        var = fcntl(STDERR_FILENO, F_GETFL, 0);
        pristatus += fcntl(STDERR_FILENO, F_SETFL, var | O_NONBLOCK);
        if (pristatus != 0) {
                var       = sprintf(scratch, "ERROR: Unable to modify standard I/O FDs\n");
                var       = write(outputfd, scratch, var);
                appstatus = -1;
                sig_exit  = 1;
        }

        //
        // Create default connection for console, log file or null output
        //
        if (!sig_exit) {
                //
                // Select FD and connection type
                //
                if (!conf.isDaemon) {
                        var  = STDIN_FILENO;
                        var2 = T_CONSOLE;
                } else {
                        if (conf.logFile != NULL) {
                                var  = logfilefd;
                                var2 = T_LOG;
                        } else {
                                var  = STDIN_FILENO;
                                var2 = T_NULL;
                        }
                }
                errConn = new_conn(var, NULL, 0, var2, &recv_proc, &null_action);
                if (conf.verbose)
                        monConn = errConn;
        }

        //
        // If specified, validate server IP or resolve name into IP
        //
        if (repo.serverName != NULL) {
                if ((var = sock_mgmt(-1, repo.serverName, 0, repo.serverIp, SMA_LOOKUP)) != 0) {
                        send_proc(errConn, scratch, var);
                        appstatus = -1;
                        sig_exit  = 1;
                }
        }

        //
        // If server, create a connection for control port to process inbound setup requests,
        // else create a connection for client testing and send setup request to server
        //
        if (!sig_exit) {
                if (repo.isServer) {
                        if ((i = new_conn(-1, repo.serverIp, conf.controlPort, T_UDP, &recv_proc, &service_setupreq)) < 0) {
                                appstatus = -1;
                                sig_exit  = 1;
                        } else if (monConn >= 0) {
                                var =
                                    sprintf(scratch, "[%d]Awaiting setup requests on %s:%d\n", i, conn[i].locAddr, conn[i].locPort);
                                send_proc(monConn, scratch, var);
                        }
                } else {
                        if ((i = new_conn(-1, NULL, 0, T_UDP, &recv_proc, &service_setupresp)) < 0) {
                                appstatus = -1;
                                sig_exit  = 1;
                        } else if (send_setupreq(i) < 0) {
                                appstatus = -1;
                                sig_exit  = 1;
                        }
                }
        }

        //
        // Primary control loop
        //
        while (!sig_exit) {
                //
                // Await ready FD(s) OR an alarm signal interrupt
                //
                var = -1;
                if (sig_alrm > 0)
                        var = 0; // Return immediately if alarm was already received
                readyfds = epoll_wait(repo.epollFD, epoll_events, MAX_EPOLL_EVENTS, var);

                //
                // Process FD(s)
                //
                if (readyfds > 0) {
                        //
                        // Update local copy of system time clock
                        //
                        clock_gettime(CLOCK_REALTIME, &repo.systemClock);

                        //
                        // Step through ready FD(s)
                        //
                        for (j = 0; j < readyfds; j++) {
                                //
                                // Extract connection from user data
                                //
                                i = (int) epoll_events[j].data.u32;
                                if (i < 0 || i > repo.maxConnIndex) {
                                        var = sprintf(scratch, "ERROR: Invalid epoll_wait user data %d\n", i);
                                        send_proc(errConn, scratch, var);
                                        continue;
                                } else if ((fd = conn[i].fd) < 0) {
                                        var = sprintf(scratch, "[%d]ERROR: Invalid fd %d from epoll_wait\n", i, fd);
                                        send_proc(errConn, scratch, var);
                                        continue;
                                }

                                //
                                // Call connection action routines
                                //
                                do {
                                        //
                                        // Execute primary and secondary actions
                                        //
                                        secstatus = 0;
                                        pristatus = (conn[i].priAction)(i);
                                        if (pristatus > 0)
                                                secstatus = (conn[i].secAction)(i);

                                        //
                                        // Check for close/cleanup request
                                        //
                                        if ((pristatus < 0) || (secstatus < 0)) {
                                                init_conn(i, TRUE);
                                        }
                                } while (pristatus > 0 && secstatus == 0); // Process until empty

                                //
                                // Check for exit
                                //
                                if (sig_exit)
                                        break;
                        }
                }

                //
                // Process timers
                //
                if (sig_alrm > 0) {
                        //
                        // Clear alarm signal counter
                        //
                        sig_alrm = 0;

                        //
                        // Update local copy of system time clock
                        //
                        clock_gettime(CLOCK_REALTIME, &repo.systemClock);

                        //
                        // Check each connection for timer expiry
                        //
                        for (i = 0; i <= repo.maxConnIndex; i++) {
                                //
                                // Check connection end time first
                                //
                                if (tspecisset(&conn[i].endTime)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].endTime, >)) {
                                                if (monConn >= 0) {
                                                        var = sprintf(scratch, "[%d]End time reached\n", i);
                                                        send_proc(monConn, scratch, var);
                                                }
                                                init_conn(i, TRUE);
                                                if (!repo.isServer) {
                                                        appstatus = repo.endTimeStatus;
                                                        sig_exit  = 1;
                                                }
                                                continue;
                                        }
                                }

                                //
                                // Must be in data state to continue
                                //
                                if (conn[i].state != S_DATA)
                                        continue;

                                //
                                // Process timer action routines using elapsed time
                                //
                                if (tspecisset(&conn[i].timer1Thresh)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].timer1Thresh, >)) {
                                                (conn[i].timer1Action)(i);
                                        }
                                }
                                if (tspecisset(&conn[i].timer2Thresh)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].timer2Thresh, >)) {
                                                (conn[i].timer2Action)(i);
                                        }
                                }
                                if (tspecisset(&conn[i].timer3Thresh)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].timer3Thresh, >)) {
                                                (conn[i].timer3Action)(i);
                                        }
                                }
                        }
                }
        }

        //
        // Close files and epoll FD
        //
        if (logfilefd >= 0)
                close(logfilefd);
        if (repo.epollFD >= 0)
                close(repo.epollFD);

        //
        // Cleanup and free memory
        //
        free(repo.sendingRates);
        free(repo.sndBuffer);
        free(repo.defBuffer);
        free(conn);

        //
        // Stop system timer
        //
        timerclear(&itime.it_interval);
        timerclear(&itime.it_value);
        setitimer(ITIMER_REAL, &itime, NULL);

        //
        // Reset standard FDs to normal
        //
        var = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, var & ~O_NONBLOCK);
        var = fcntl(STDOUT_FILENO, F_GETFL, 0);
        fcntl(STDOUT_FILENO, F_SETFL, var & ~O_NONBLOCK);
        var = fcntl(STDERR_FILENO, F_GETFL, 0);
        fcntl(STDERR_FILENO, F_SETFL, var & ~O_NONBLOCK);

        return appstatus;
}
//----------------------------------------------------------------------------
//
// Signal handlers
//
void signal_alrm(int signal) {
        (void) (signal);

        //
        // Increment alarm signal indicator
        //
        sig_alrm++;

        return;
}
void signal_exit(int signal) {
        (void) (signal);

        //
        // Set exit signal indicator
        //
        sig_exit = 1;

        return;
}
//----------------------------------------------------------------------------
//
// Process command-line parameters
//
int proc_parameters(int argc, char **argv, int fd) {
        int i, var, value;
        char *optstring = "ud46xevsjDSri:oa:m:I:t:P:p:b:L:U:F:c:h:q:l:k:?";

        //
        // Clear configuration and global repository data
        //
        memset(&conf, 0, sizeof(conf));
        memset(&repo, 0, sizeof(repo));

        //
        // Parse direction parameters
        //
        value  = opterr;
        opterr = 0;
        while ((i = getopt(argc, argv, optstring)) != -1) {
                switch (i) {
                case 'u':
                        conf.usTesting = TRUE;
                        break;
                case 'd':
                        conf.dsTesting = TRUE;
                        break;
                }
        }

        //
        // Save hostname/IP of server or IP of local interface on server
        //
        if ((optind + 1) == argc) {
                repo.serverName = argv[optind];
        } else if ((optind + 1) < argc) {
                var = sprintf(scratch, "ERROR: Unexpected parameter %s\n", argv[optind + 1]);
                var = write(fd, scratch, var);
                return -1;
        }

        //
        // Validate direction parameters and determine mode
        //
        if (conf.usTesting && conf.dsTesting) {
                var = sprintf(scratch, "ERROR: %s and %s options are mutually exclusive\n", USTEST_TEXT, DSTEST_TEXT);
                var = write(fd, scratch, var);
                return -1;
        } else if (!conf.usTesting && !conf.dsTesting) {
                repo.isServer = TRUE;
        } else if (repo.serverName == NULL) {
                var = sprintf(scratch, "ERROR: Server hostname or IP address required when client\n");
                var = write(fd, scratch, var);
                return -1;
        }

        //
        // Continue to initialize non-zero configuration data
        //
        conf.addrFamily  = AF_UNSPEC;
        conf.errSuppress = TRUE;
        conf.jumboStatus = DEF_JUMBO_STATUS;
        conf.useOwDelVar = DEF_USE_OWDELVAR;
        if (!repo.isServer) {
                // Default values
                conf.ipTosByte   = DEF_IPTOS_BYTE;
                conf.srIndexConf = DEF_SRINDEX_CONF;
                conf.testIntTime = DEF_TESTINT_TIME;
        } else {
                // Configured maximums
                conf.ipTosByte   = MAX_IPTOS_BYTE;
                conf.srIndexConf = MAX_SRINDEX_CONF;
                conf.testIntTime = MAX_TESTINT_TIME;
        }
        conf.subIntPeriod   = DEF_SUBINT_PERIOD;
        conf.controlPort    = DEF_CONTROL_PORT;
        conf.sockSndBuf     = DEF_SOCKET_BUF;
        conf.sockRcvBuf     = DEF_SOCKET_BUF;
        conf.lowThresh      = DEF_LOW_THRESH;
        conf.upperThresh    = DEF_UPPER_THRESH;
        conf.trialInt       = DEF_TRIAL_INT;
        conf.slowAdjThresh  = DEF_SLOW_ADJ_TH;
        conf.highSpeedDelta = DEF_HS_DELTA;
        conf.seqErrThresh   = DEF_SEQ_ERR_TH;
        conf.logFileMax     = DEF_LOGFILE_MAX * 1000;
        //
        // Continue to initialize non-zero repository data
        //
        repo.epollFD       = -1; // No file descriptor
        repo.maxConnIndex  = -1; // No connections allocated
        repo.endTimeStatus = -1; // Default to errored exit

        //
        // Parse remaining parameters
        //
        optind = 0;
        opterr = value;
        while ((i = getopt(argc, argv, optstring)) != -1) {
                switch (i) {
                case '4':
                        conf.addrFamily = AF_INET;
                        break;
                case '6':
                        conf.addrFamily = AF_INET6;
                        conf.ipv6Only   = TRUE;
                        break;
                case 'x':
                        if (!repo.isServer) {
                                var = sprintf(scratch, "ERROR: Execution as daemon only valid when server\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.isDaemon = TRUE;
                        break;
                case 'e':
                        conf.errSuppress = FALSE;
                        break;
                case 'v':
                        conf.verbose = TRUE;
                        break;
                case 's':
                        conf.summaryOnly = TRUE;
                        break;
                case 'j':
                        conf.jumboStatus = !DEF_JUMBO_STATUS; // Not the default
                        break;
                case 'D':
                        conf.debug = TRUE;
                        break;
                case 'S':
                        conf.showSendingRates = TRUE;
                        break;
                case 'r':
                        conf.showLossRatio = TRUE;
                        break;
                case 'i':
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_BIMODAL_COUNT, MAX_BIMODAL_COUNT)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.bimodalCount = value;
                        break;
                case 'o':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: One-Way Delay option only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.useOwDelVar = !DEF_USE_OWDELVAR; // Not the default
                        break;
                case 'a':
#ifdef AUTH_KEY_ENABLE
                        if (strlen(optarg) > AUTH_KEY_SIZE) {
                                var = sprintf(scratch, "ERROR: Authentication key exceeds %d characters\n", AUTH_KEY_SIZE);
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        strncpy(conf.authKey, optarg, AUTH_KEY_SIZE + 1);
                        conf.authKey[AUTH_KEY_SIZE] = '\0';
#else
                        var = sprintf(scratch, "ERROR: Built without authentication functionality\n");
                        var = write(fd, scratch, var);
                        return -1;
#endif
                        break;
                case 'm':
                        // Server will use as configured maximum
                        value = (int) strtol(optarg, NULL, 0); // Allow hex values (0x00-0xff)
                        if ((var = param_error(value, MIN_IPTOS_BYTE, MAX_IPTOS_BYTE)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.ipTosByte = value;
                        break;
                case 'I':
                        // Server will use as configured maximum
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_SRINDEX_CONF, MAX_SRINDEX_CONF)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.srIndexConf = value;
                        break;
                case 't':
                        // Server will use as configured maximum
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_TESTINT_TIME, MAX_TESTINT_TIME)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.testIntTime = value;
                        break;
                case 'P':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Sub-interval period only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_SUBINT_PERIOD, MAX_SUBINT_PERIOD)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.subIntPeriod = value;
                        break;
                case 'p':
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_CONTROL_PORT, MAX_CONTROL_PORT)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.controlPort = value;
                        break;
                case 'b':
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_SOCKET_BUF, MAX_SOCKET_BUF)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.sockSndBuf = conf.sockRcvBuf = value;
                        break;
                case 'L':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Low delay variation threshold only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_LOW_THRESH, MAX_LOW_THRESH)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.lowThresh = value;
                        break;
                case 'U':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Upper delay variation threshold only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_UPPER_THRESH, MAX_UPPER_THRESH)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.upperThresh = value;
                        break;
                case 'F':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Status feedback/trial interval only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_TRIAL_INT, MAX_TRIAL_INT)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.trialInt = value;
                        break;
                case 'c':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Congestion slow adjustment threshold only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_SLOW_ADJ_TH, MAX_SLOW_ADJ_TH)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.slowAdjThresh = value;
                        break;
                case 'h':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: High-speed delta only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_HS_DELTA, MAX_HS_DELTA)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.highSpeedDelta = value;
                        break;
                case 'q':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Sequence error threshold only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_SEQ_ERR_TH, MAX_SEQ_ERR_TH)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.seqErrThresh = value;
                        break;
                case 'l':
                        if (!repo.isServer) {
                                var = sprintf(scratch, "ERROR: Log file only valid when server\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.logFile = optarg;
                        break;
                case 'k':
                        if (!repo.isServer) {
                                var = sprintf(scratch, "ERROR: Log file maximum size only valid when server\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_LOGFILE_MAX, MAX_LOGFILE_MAX)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.logFileMax = value * 1000;
                        break;
                case '?':
                        var = sprintf(scratch,
                                      "%s\nUsage: %s [option]... [server]\n\n"
                                      "Specify '-u' or '-d' to test as a client (a server parameter is then required),\n"
                                      "else run as a server and await client test requests.\n\n"
                                      "Options:\n"
                                      "(c)    -u|-d        Test %s OR %s as client\n"
                                      "       -4           Use only IPv4 address family (AF_INET)\n"
                                      "       -6           Use only IPv6 address family (AF_INET6)\n"
                                      "(s)    -x           Execute server as background (daemon) process\n"
                                      "(e)    -e           Disable suppression of socket (send/receive) errors\n"
                                      "       -v           Enable verbose output messaging\n"
                                      "       -s           Summary output only (no sub-interval output)\n"
                                      "(j)    -j           Disable jumbo datagram sizes above 1 Gbps\n"
                                      "       -D           Enable debug output messaging (requires '-v')\n",
                                      SOFTWARE_TITLE, argv[0], USTEST_TEXT, DSTEST_TEXT);
                        var = write(fd, scratch, var);
                        var = sprintf(scratch,
                                      "       -S           Show server sending rate table and exit\n"
                                      "       -r           Display loss ratio instead of delivered percentage\n"
                                      "       -i count     Display bimodal maxima (specify initial sub-intervals)\n"
                                      "(c)    -o           Use One-Way Delay instead of RTT for delay variation\n"
                                      "       -a key       Authentication key (%d characters max)\n"
                                      "(m,v)  -m value     Packet marking octet (IP_TOS/IPV6_TCLASS) [Default %d]\n"
                                      "(m)    -I index     Index for static sending rate (see '-S') [Default <Auto>]\n"
                                      "(m)    -t time      Test interval time in seconds [Default %d, Max %d]\n"
                                      "(c)    -P period    Sub-interval period in seconds [Default %d]\n"
                                      "       -p port      Port number used for control [Default %d]\n",
                                      AUTH_KEY_SIZE, DEF_IPTOS_BYTE, DEF_TESTINT_TIME, MAX_TESTINT_TIME, DEF_SUBINT_PERIOD,
                                      DEF_CONTROL_PORT);
                        var = write(fd, scratch, var);
                        var = sprintf(scratch,
                                      "       -b buffer    Socket buffer request size (SO_SNDBUF/SO_RCVBUF)\n"
                                      "(c)    -L delvar    Low delay variation threshold in ms [Default %d]\n"
                                      "(c)    -U delvar    Upper delay variation threshold in ms [Default %d]\n"
                                      "(c)    -F interval  Status feedback/trial interval in ms [Default %d]\n"
                                      "(c)    -c thresh    Congestion slow adjustment threshold [Default %d]\n"
                                      "(c)    -h delta     High-speed (row adjustment) delta [Default %d]\n"
                                      "(c)    -q seqerr    Sequence error threshold [Default %d]\n"
                                      "(s)    -l logfile   Log file name when executing as daemon\n"
                                      "(s)    -k logsize   Log file maximum size in KBytes [Default %d]\n\n",
                                      DEF_LOW_THRESH, DEF_UPPER_THRESH, DEF_TRIAL_INT, DEF_SLOW_ADJ_TH, DEF_HS_DELTA,
                                      DEF_SEQ_ERR_TH, DEF_LOGFILE_MAX);
                        var = write(fd, scratch, var);
                        var = sprintf(scratch, "Parameters:\n"
                                               "      server       Hostname/IP of server (or local interface IP if server)\n\n"
                                               "Notes:\n"
                                               "(c) = Used only by client.\n"
                                               "(s) = Used only by server.\n"
                                               "(e) = Suppressed due to expected errors with overloaded network interfaces.\n"
                                               "(j) = Datagram sizes that would result in jumbo frames if available.\n"
                                               "(m) = Used as a request by the client or a maximum by the server. Client\n"
                                               "      requests that exceed server maximum are automatically coerced down.\n"
                                               "(v) = Values can be specified as decimal (0 - 255) or hex (0x00 - 0xff).\n");
                        var = write(fd, scratch, var);
                        return -1;
                }
        }

        //
        // Validate remaining parameters
        //
        if (!repo.isServer && conf.isDaemon) {
                var = sprintf(scratch, "ERROR: Execution as daemon only valid in server mode\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if ((conf.logFile != NULL) && !conf.isDaemon) {
                var = sprintf(scratch, "ERROR: Log file only supported when executing as daemon\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if (!conf.verbose && conf.debug) {
                var = sprintf(scratch, "ERROR: Debug only available when used with verbose\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if (conf.subIntPeriod > conf.testIntTime) {
                var = sprintf(scratch, "ERROR: Sub-interval period is greater than test interval time\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if (conf.lowThresh > conf.upperThresh) {
                var = sprintf(scratch, "ERROR: Low delay variation threshold > upper delay variation threshold\n");
                var = write(fd, scratch, var);
                return -1;
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Check parameter range
//
// Populate scratch buffer and return length on error
//
int param_error(int param, int min, int max) {
        int var = 0;

        if ((param < min) || (param > max)) {
                var = sprintf(scratch, "ERROR: Parameter <%d> out-of-range (%d-%d)\n", param, min, max);
        }
        return var;
}
//----------------------------------------------------------------------------
