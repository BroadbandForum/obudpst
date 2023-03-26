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
 * Len Ciavattone          11/10/2020    Add option to ignore OoO/Dup
 * Len Ciavattone          06/08/2021    Add DISABLE_INT_TIMER conditional
 *                                       to support older client devices
 * Len Ciavattone          10/13/2021    Refresh with clang-format
 *                                       Limit format options to client
 *                                       Add TR-181 fields in JSON
 *                                       Add JSON error status and message
 * Len Ciavattone          11/18/2021    Add backward compat. protocol version
 *                                       Add bandwidth management support
 * Len Ciavattone          12/08/2021    Add starting sending rate
 * Len Ciavattone          12/17/2021    Add payload randomization
 * Len Ciavattone          12/21/2021    Add traditional (1500 byte) MTU
 * Len Ciavattone          02/02/2022    Add rate adj. algo. selection
 * Len Ciavattone          12/29/2022    Add single test option on server
 * Len Ciavattone          01/14/2023    Add multi-connection support
 * Len Ciavattone          02/14/2023    Add per-server port selection and
 *                                       clock updates based on rx packets
 * Len Ciavattone          03/04/2023    Load balance returned epoll events
 * Len Ciavattone          03/22/2023    Add optimization output to banner
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
#include <net/if.h>
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
#include "cJSON.h"
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
int json_finish(void);

//----------------------------------------------------------------------------
//
// Global data
//
#define ENDTEXT_BASIC "[%d]End time reached"
#define ENDTEXT_NEWBW ENDTEXT_BASIC " (New USBW: %d, DSBW: %d)\n"
int errConn = -1, monConn = -1, aggConn = -1; // Error, monitoring, and aggregate
char scratch[STRING_SIZE];                    // General purpose scratch buffer
struct configuration conf;                    // Configuration data structure
struct repository repo;                       // Repository of global data
struct connection *conn;                      // Connection table (array)
static volatile sig_atomic_t sig_alrm = 0;    // Interrupt indicator
static volatile sig_atomic_t sig_exit = 0;    // Interrupt indicator
struct epoll_event epoll_events[MAX_EPOLL_EVENTS];
char *boolText[]    = {"Disabled", "Enabled"};
char *rateAdjAlgo[] = {"B", "C"}; // Aligned to CHTA_RA_ALGO_x
//
cJSON *json_top = NULL, *json_output = NULL, *json_siArray = NULL;
char json_errbuf[STRING_SIZE];

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------
//
// Program entry point
//
int main(int argc, char **argv) {
        pid_t pid;
        int i, j, var, var2, readyfds, fdpass, pristatus, secstatus;
        int appstatus = 0, outputfd = STDOUT_FILENO, logfilefd = -1;
        struct itimerval itime;
        struct sigaction saction;
        struct stat statbuf;

        //
        // Sanity check that rate adjustment algorithm identifiers align with protocol
        //
        if (sizeof(rateAdjAlgo) / sizeof(char *) != CHTA_RA_ALGO_MAX + 1) {
                var = sprintf(scratch, "ERROR: Invalid number of rate adjustment algorithm identifiers\n");
                var = write(outputfd, scratch, var);
                return -1;
        }
        for (var = CHTA_RA_ALGO_MIN; var <= CHTA_RA_ALGO_MAX; var++) {
                if (rateAdjAlgo[var] == NULL) {
                        var = sprintf(scratch, "ERROR: Null pointer for rate adjustment algorithm identifier\n");
                        var = write(outputfd, scratch, var);
                        return -1;
                }
        }

        //
        // Verify and process parameters, initialize configuration and repository
        //
        if (proc_parameters(argc, argv, outputfd) != 0) {
                return -1;
        }

        //
        // Create top-level JSON output object if needed
        //
        if (conf.jsonOutput) {
                json_top = cJSON_CreateObject();
        }
        *json_errbuf = '\0'; // Initialize to no error

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
        // Initialize local copy of system time clock and seed RNG
        //
        clock_gettime(CLOCK_REALTIME, &repo.systemClock);
        srandom((unsigned int) repo.systemClock.tv_nsec);

        //
        // Print banner or initialize JSON output object
        //
        if (!conf.jsonOutput) {
                var = sprintf(scratch, SOFTWARE_TITLE "\nSoftware Ver: %s", SOFTWARE_VER);
                if (repo.isServer)
                        var += sprintf(&scratch[var], ", Protocol Ver: %d-%d", PROTOCOL_MIN, PROTOCOL_VER);
                else
                        var += sprintf(&scratch[var], ", Protocol Ver: %d", PROTOCOL_VER); // Client is always the latest
                var += sprintf(&scratch[var], ", Built: " __DATE__ " " __TIME__ "\n");
                var = write(outputfd, scratch, var);
                //
                var = 0;
                if (conf.ipv6Only)
                        var = IPV6_ADDSIZE;
                if (conf.traditionalMTU)
                        i = MAX_TPAYLOAD_SIZE - var;
                else
                        i = MAX_PAYLOAD_SIZE - var;
                if (conf.jumboStatus)
                        j = MAX_JPAYLOAD_SIZE - var;
                else
                        j = i;
                if (repo.isServer)
                        var = sprintf(scratch, "Mode: Server, Payload Default[Max]: %d[%d]", i, j);
                else
                        var = sprintf(scratch, "Mode: Client, Payload Default[Max]: %d[%d]", i, j);
#ifdef AUTH_KEY_ENABLE
                var += sprintf(&scratch[var], ", Authentication: Available");
#else
                var += sprintf(&scratch[var], ", Authentication: Unavailable");
#endif // AUTH_KEY_ENABLE
                var += sprintf(&scratch[var], ", Optimizations:");
#ifdef HAVE_SENDMMSG
                var += sprintf(&scratch[var], " SendMMsg()");
#ifdef HAVE_GSO
                var += sprintf(&scratch[var], "+GSO");
#endif // HAVE_GSO
#endif // HAVE_SENDMMSG
#ifdef HAVE_RECVMMSG
                var += sprintf(&scratch[var], " RecvMMsg()+Trunc");
#endif // HAVE_RECVMMSG
                scratch[var++] = '\n';
                var            = write(outputfd, scratch, var);
        } else {
                //
                // Add initial items to top-level object
                //
                if (!conf.jsonBrief) {
                        cJSON_AddNumberToObject(json_top, "IPLayerMaxConnections", MAX_MC_COUNT);
                        cJSON_AddNumberToObject(json_top, "IPLayerMaxIncrementalResult", MAX_TESTINT_TIME / MIN_SUBINT_PERIOD);
                        cJSON *json_supported = cJSON_CreateObject();
                        cJSON_AddStringToObject(json_supported, "SoftwareVersion", SOFTWARE_VER);
                        cJSON_AddNumberToObject(json_supported, "ControlProtocolVersion", PROTOCOL_VER);
                        cJSON_AddStringToObject(json_supported, "Metrics", "IPLR,Sampled_RTT,IPDV,IPRR,RIPR");
                        cJSON_AddItemToObject(json_top, "IPLayerCapSupported", json_supported);
                }
        }

        //
        // Allocate and initialize buffers
        //
        repo.sendingRates = calloc(1, MAX_SENDING_RATES * sizeof(struct sendingRate));
        repo.sndBuffer    = calloc(1, SND_BUFFER_SIZE);
        repo.defBuffer    = calloc(1, RCV_BUFFER_SIZE);
        repo.randData     = malloc(MAX_JPAYLOAD_SIZE);
        repo.sndBufRand   = malloc(SND_BUFFER_SIZE);
        conn              = malloc(conf.maxConnections * sizeof(struct connection));
        if (repo.sendingRates == NULL || repo.sndBuffer == NULL || repo.defBuffer == NULL || repo.randData == NULL ||
            repo.sndBufRand == NULL || conn == NULL) {
                var = sprintf(scratch, "ERROR: Memory allocation(s) failed\n");
                var = write(outputfd, scratch, var);
                return -1;
        }
        for (i = 0; i < conf.maxConnections; i++)
                init_conn(i, FALSE);
        for (i = 0; i < MAX_JPAYLOAD_SIZE / sizeof(int); i++)
                ((int *) repo.randData)[i] = random();

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
#ifndef DISABLE_INT_TIMER
        if (clock_getres(CLOCK_REALTIME, &repo.systemClock) == -1) {
                var = sprintf(scratch, "CLOCK_GETRES ERROR: %s\n", strerror(errno));
                var = write(outputfd, scratch, var);
                return -1;
        }
        if (repo.systemClock.tv_nsec > 1) {
                var =
                    sprintf(scratch, "ERROR: Clock resolution (%ld ns) out of range [see compile-time option DISABLE_INT_TIMER]\n",
                            repo.systemClock.tv_nsec);
                var = write(outputfd, scratch, var);
                return -1;
        }
        clock_gettime(CLOCK_REALTIME, &repo.systemClock); // Reinitialize local copy of system time clock
#endif

        //
        // Set alarm signal handler
        //
#ifndef DISABLE_INT_TIMER
        saction.sa_handler = signal_alrm;
        sigemptyset(&saction.sa_mask);
        saction.sa_flags = SA_RESTART;
        if (sigaction(SIGALRM, &saction, NULL) != 0) {
                var = sprintf(scratch, "SIGALRM ERROR: %s\n", strerror(errno));
                var = write(outputfd, scratch, var);
                return -1;
        }
#endif

        //
        // Create system interval timer used to drive all local timers
        //
#ifndef DISABLE_INT_TIMER
        itime.it_interval.tv_sec = itime.it_value.tv_sec = 0;
        itime.it_interval.tv_usec = itime.it_value.tv_usec = MIN_INTERVAL_USEC;
        if (setitimer(ITIMER_REAL, &itime, NULL) != 0) {
                var = sprintf(scratch, "ITIMER ERROR: %s\n", strerror(errno));
                var = write(outputfd, scratch, var);
                return -1;
        }
#endif

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
                if (!repo.isServer)
                        aggConn = errConn; // Use initial connection as aggregate connection
        }

        //
        // If specified, validate server IP addresses or resolve names into IP addresses
        //
        for (i = 0; i < repo.serverCount; i++) {
                if ((var = sock_mgmt(-1, repo.server[i].name, 0, repo.server[i].ip, SMA_LOOKUP)) != 0) {
                        send_proc(errConn, scratch, var);
                        appstatus = -1;
                        if (!repo.isServer && conf.jsonOutput) {
                                tspeccpy(&conn[errConn].endTime, &repo.systemClock); // Schedule immediate exit
                        } else {
                                sig_exit = 1;
                        }
                        break;
                }
        }

        //
        // If server, create a connection for control port to process inbound setup requests,
        // else create connections for client testing and send setup requests to server(s)
        //
        if (appstatus == 0) {
                if (repo.isServer) {
                        if ((i = new_conn(-1, repo.server[0].ip, repo.server[0].port, T_UDP, &recv_proc, &service_setupreq)) < 0) {
                                appstatus = -1;
                                sig_exit  = 1;
                        } else if (conf.verbose) {
                                var =
                                    sprintf(scratch, "[%d]Awaiting setup requests on %s:%d\n", i, conn[i].locAddr, conn[i].locPort);
                                send_proc(monConn, scratch, var);
                        }
                        if (conf.oneTest)
                                appstatus = -1; // Default to hard error, require explicit end time status success
                } else {
                        var2 = 0; // Server index (distribute connections across servers)
                        for (j = 0; j < conf.maxConnCount; j++) {
                                if ((i = new_conn(-1, NULL, 0, T_UDP, &recv_proc, &service_setupresp)) < 0) {
                                        appstatus = -1;
                                        if (conf.jsonOutput) {
                                                tspeccpy(&conn[errConn].endTime, &repo.systemClock); // Schedule immediate exit
                                        } else {
                                                sig_exit = 1;
                                        }
                                        break;
                                } else if (send_setupreq(i, j, var2) < 0) {
                                        appstatus = -1;
                                        if (conf.jsonOutput) {
                                                tspeccpy(&conn[errConn].endTime, &repo.systemClock); // Schedule immediate exit
                                        } else {
                                                sig_exit = 1;
                                        }
                                        break;
                                }
                                if (++var2 >= repo.serverCount) // Step to next server and restart list when needed
                                        var2 = 0;
                        }
                }
        }

        //
        // Primary control loop
        //
        while (!sig_exit) {
#ifdef DISABLE_INT_TIMER
                sig_alrm = 1; // Simulate expiry of system interval timer
#endif
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
                        fdpass = 0;
                        do {
                                //
                                // Do single read (up to RECVMMSG_SIZE) from each ready FD
                                //
                                var2 = 0; // Track if any data is read on this pass
                                for (j = 0; j < readyfds; j++) {
                                        //
                                        // Extract connection from user data
                                        //
                                        i = (int) epoll_events[j].data.u32;
                                        if (i < 0 || i > repo.maxConnIndex) {
                                                if (fdpass == 0) {
                                                        var = sprintf(scratch, "ERROR: Invalid epoll_wait user data %d\n", i);
                                                        send_proc(errConn, scratch, var);
                                                }
                                                continue;
                                        } else if (conn[i].fd < 0) {
                                                if (fdpass == 0) {
                                                        var = sprintf(scratch, "[%d]ERROR: Invalid fd (%d) from epoll_wait\n", i,
                                                                      conn[i].fd);
                                                        send_proc(errConn, scratch, var);
                                                }
                                                continue;
                                        }

                                        //
                                        // Set connection as data ready on first pass, else check if all data has been read
                                        //
                                        if (fdpass == 0) {
                                                conn[i].dataReady = TRUE;
                                        } else if (!conn[i].dataReady) {
                                                continue; // Nothing to do for this connection
                                        }

                                        //
                                        // Update local copy of system time clock
                                        //
                                        clock_gettime(CLOCK_REALTIME, &repo.systemClock);

                                        //
                                        // Execute primary and secondary actions
                                        //
                                        secstatus = 0;
                                        pristatus = (conn[i].priAction)(i);
                                        if (pristatus > 0) {
                                                var2++; // Indicate data was read on this pass
                                                secstatus = (conn[i].secAction)(i);
                                        } else if (pristatus == 0) {
                                                conn[i].dataReady = FALSE; // Indicate all data has been read from this connection
                                        }

                                        //
                                        // Check for close/cleanup request
                                        //
                                        if ((pristatus < 0) || (secstatus < 0)) {
                                                init_conn(i, TRUE);
                                        }
                                        if (sig_exit)
                                                break;
                                }
                                fdpass++;
                                if (sig_exit)
                                        break;
                        } while (var2 > 0 && sig_alrm == 0); // Do another pass if any data was read AND alarm hasn't fired
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
                                                if (repo.isServer) {
                                                        if (conf.maxBandwidth > 0) {
                                                                // Adjust current upstream/downstream bandwidth
                                                                if (conn[i].testType == TEST_TYPE_US) {
                                                                        if ((repo.usBandwidth -= conn[i].maxBandwidth) < 0)
                                                                                repo.usBandwidth = 0;
                                                                } else {
                                                                        if ((repo.dsBandwidth -= conn[i].maxBandwidth) < 0)
                                                                                repo.dsBandwidth = 0;
                                                                }
                                                                if (conf.verbose) {
                                                                        var = sprintf(scratch, ENDTEXT_NEWBW, i, repo.usBandwidth,
                                                                                      repo.dsBandwidth);
                                                                        send_proc(monConn, scratch, var);
                                                                }
                                                        } else if (conf.verbose) {
                                                                var = sprintf(scratch, ENDTEXT_BASIC "\n", i);
                                                                send_proc(monConn, scratch, var);
                                                        }
                                                        if (conf.oneTest) { // Shutdown server after one test
                                                                appstatus = repo.endTimeStatus;
                                                                sig_exit  = 1;
                                                        }
                                                } else {
                                                        if (i == aggConn) {
                                                                if (conf.jsonOutput) {
                                                                        appstatus = json_finish(); // Finalize JSON processing
                                                                } else {
                                                                        appstatus = repo.endTimeStatus;
                                                                }
                                                                sig_exit = 1;
                                                        } else if (conn[i].testAction == TEST_ACT_TEST) {
                                                                // Decrement active test count if closed while active
                                                                if (--repo.actConnCount < 0)
                                                                        repo.actConnCount = 0;
                                                        }
                                                        if (conf.verbose) {
                                                                var = sprintf(scratch, ENDTEXT_BASIC "\n", i);
                                                                send_proc(monConn, scratch, var);
                                                        }
                                                }
                                                init_conn(i, TRUE);
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
                                var2 = 0;
                                if (tspecisset(&conn[i].timer1Thresh)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].timer1Thresh, >)) {
                                                (conn[i].timer1Action)(i);
                                                var2++;
                                        }
                                }
                                if (tspecisset(&conn[i].timer2Thresh)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].timer2Thresh, >)) {
                                                (conn[i].timer2Action)(i);
                                                var2++;
                                        }
                                }
                                if (tspecisset(&conn[i].timer3Thresh)) {
                                        if (tspeccmp(&repo.systemClock, &conn[i].timer3Thresh, >)) {
                                                (conn[i].timer3Action)(i);
                                                var2++;
                                        }
                                }
                                if (var2 > 0) { // Update local copy of system time clock if work was done
                                        clock_gettime(CLOCK_REALTIME, &repo.systemClock);
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
        if (repo.intfFD >= 0)
                close(repo.intfFD);

        //
        // Cleanup and free memory
        //
        free(repo.sendingRates);
        free(repo.sndBuffer);
        free(repo.defBuffer);
        free(repo.randData);
        free(repo.sndBufRand);
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
        int i, j, var, value;
        char *lbuf, *optstring = "ud46C:x1evsf:jTDXSB:ri:oRa:m:I:t:P:p:A:b:L:U:F:c:h:q:E:Ml:k:?";

        //
        // Clear configuration and global repository data
        //
        memset(&conf, 0, sizeof(conf));
        memset(&repo, 0, sizeof(repo));

        //
        // Parse direction and port number parameters
        //
        value            = opterr;
        opterr           = 0;
        conf.controlPort = DEF_CONTROL_PORT;
        while ((i = getopt(argc, argv, optstring)) != -1) {
                switch (i) {
                case 'u':
                        conf.usTesting = TRUE;
                        break;
                case 'd':
                        conf.dsTesting = TRUE;
                        break;
                case 'p':
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_CONTROL_PORT, MAX_CONTROL_PORT)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.controlPort = value;
                        break;
                }
        }

        //
        // Save hostname/IP of servers or IP of local interface on server (allow port number suffix)
        //
        repo.server[0].name  = NULL;
        repo.server[0].ip[0] = '\0';
        repo.server[0].port  = conf.controlPort;
        for (i = 0, j = optind; j < argc; i++, j++) {
                if (repo.serverCount >= MAX_MC_COUNT) {
                        var = sprintf(scratch, "ERROR: Server count exceeds maximum (%d)\n", MAX_MC_COUNT);
                        var = write(fd, scratch, var);
                        return -1;
                }
                //
                // Parse IP address/port as: <IPv4>, <IPv4>:<port>, <IPv6>, or [<IPv6>]:<port> (see RFC5952)
                //
                var = 0; // A value of one indicates IP address has port suffix (anything else means no port suffix)
                if (*argv[j] == '[') {
                        argv[j]++; // Adjust base pointer past '['
                        if ((lbuf = strchr(argv[j], ']')) != NULL)
                                *lbuf++ = '\0'; // Remove trailing ']' and step to expected ':'
                        if (lbuf == NULL || *lbuf != ':') {
                                var = sprintf(scratch, "ERROR: Invalid format for IPv6 address with port number\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        var++;
                } else if ((lbuf = strchr(argv[j], ':')) != NULL) {
                        var++;
                        if (strchr(lbuf + 1, ':') != NULL) // Check for second ':'
                                var++;
                }
                value = conf.controlPort;
                if (var == 1) { // Port suffix present
                        if (*lbuf == ':') {
                                *lbuf++ = '\0';       // Remove ':' and step to port number
                                value   = atoi(lbuf); // Convert to numeric
                                if ((var = param_error(value, MIN_CONTROL_PORT, MAX_CONTROL_PORT)) > 0) {
                                        var = write(fd, scratch, var);
                                        return -1;
                                }
                        }
                }
                repo.server[i].name = argv[j];
                repo.server[i].port = value;
                repo.serverCount++;
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
                if (repo.serverCount > 1) {
                        var = sprintf(scratch, "ERROR: Server only allows one local bind address or hostname\n");
                        var = write(fd, scratch, var);
                        return -1;
                }
        } else if (repo.serverCount == 0) {
                var = sprintf(scratch, "ERROR: Server hostname or IP address required when client\n");
                var = write(fd, scratch, var);
                return -1;
        }

        //
        // Continue to initialize non-zero configuration data
        //
        if (!repo.isServer) {
                conf.maxConnections = MAX_CLIENT_CONN;
        } else {
                conf.maxConnections = MAX_SERVER_CONN;
        }
        conf.addrFamily   = AF_UNSPEC;
        conf.minConnCount = DEF_MC_COUNT;
        conf.maxConnCount = DEF_MC_COUNT;
        conf.errSuppress  = TRUE;
        conf.jumboStatus  = DEF_JUMBO_STATUS;
        conf.rateAdjAlgo  = DEF_RA_ALGO;
        conf.useOwDelVar  = DEF_USE_OWDELVAR;
        conf.ignoreOooDup = DEF_IGNORE_OOODUP;
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
        repo.epollFD       = -1;           // No file descriptor
        repo.maxConnIndex  = -1;           // No connections allocated
        repo.endTimeStatus = STATUS_ERROR; // Default to hard error, require explicit success
        repo.intfFD        = -1;           // No file descriptor

        //
        // Parse remaining parameters
        //
        optind = 0;
        opterr = value;
        while ((i = getopt(argc, argv, optstring)) != -1) {
                switch (i) {
                case '4':
                        conf.addrFamily = AF_INET;
                        conf.ipv4Only   = TRUE;
                        break;
                case '6':
                        conf.addrFamily = AF_INET6;
                        conf.ipv6Only   = TRUE;
                        break;
                case 'C':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Multi-connection count only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        if ((lbuf = strchr(optarg, '-')) != NULL)
                                *lbuf = '\0';
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_MC_COUNT, MAX_MC_COUNT)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.minConnCount = value;
                        if (lbuf != NULL) {
                                value = atoi(++lbuf);
                                if ((var = param_error(value, MIN_MC_COUNT, MAX_MC_COUNT)) > 0) {
                                        var = write(fd, scratch, var);
                                        return -1;
                                }
                        } else { // No max specified
                                if (value < repo.serverCount)
                                        value = repo.serverCount;
                        }
                        if (value < repo.serverCount) {
                                var = sprintf(scratch, "ERROR: Maximum multi-connection count must be >= server count\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.maxConnCount = value;
                        break;
                case 'x':
                        if (!repo.isServer) {
                                var = sprintf(scratch, "ERROR: Execution as daemon only valid when server\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.isDaemon = TRUE;
                        break;
                case '1':
                        if (!repo.isServer) {
                                var = sprintf(scratch, "ERROR: One test execution only valid when server\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.oneTest = TRUE;
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
                case 'f':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Ouput format options only available to client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        if (strcasecmp(optarg, "json") == 0) {
                                conf.jsonOutput = TRUE;
                        } else if (strcasecmp(optarg, "jsonb") == 0) {
                                conf.jsonOutput = TRUE;
                                conf.jsonBrief  = TRUE;
                        } else if (strcasecmp(optarg, "jsonf") == 0) {
                                conf.jsonOutput    = TRUE;
                                conf.jsonFormatted = TRUE;
                        } else {
                                var = sprintf(scratch, "ERROR: '%s' is not a valid output format\n", optarg);
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        break;
                case 'j':
                        conf.jumboStatus = !DEF_JUMBO_STATUS; // Not the default
                        break;
                case 'T':
                        conf.traditionalMTU = TRUE;
                        break;
                case 'D':
                        conf.debug = TRUE;
                        break;
                case 'X':
                        conf.randPayload = TRUE;
                        break;
                case 'S':
                        conf.showSendingRates = TRUE;
                        break;
                case 'B':
                        if (repo.isServer) {
                                var = MAX_SERVER_BW;
                        } else {
                                var = MAX_CLIENT_BW;
                        }
                        value = atoi(optarg);
                        if ((var = param_error(value, MIN_REQUIRED_BW, var)) > 0) {
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.maxBandwidth = value;
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
                case 'R':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Option to ignore Out-of-Order/Duplicates only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.ignoreOooDup = !DEF_IGNORE_OOODUP; // Not the default
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
                        lbuf = optarg;
                        if (*lbuf == SRIDX_ISSTART_PREFIX) {
                                lbuf++;
                                conf.srIndexIsStart = TRUE; // Use SR index as starting point instead of static value
                        }
                        value = atoi(lbuf);
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
                case 'A':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Rate adjustment algorithm only set by client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        value = 0;
                        for (var = CHTA_RA_ALGO_MIN; var <= CHTA_RA_ALGO_MAX; var++) {
                                if (strcasecmp(optarg, rateAdjAlgo[var]) == 0) {
                                        value = var;
                                        break;
                                }
                        }
                        if (var <= CHTA_RA_ALGO_MAX) {
                                conf.rateAdjAlgo = value;
                        } else {
                                var = sprintf(scratch, "ERROR: '%s' is not a valid rate adjustment algorithm\n", optarg);
                                var = write(fd, scratch, var);
                                return -1;
                        }
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
                case 'E':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Local interface option only available to client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        strncpy(conf.intfName, optarg, IFNAMSIZ + 1);
                        conf.intfName[IFNAMSIZ] = '\0';
                        break;
                case 'M':
                        if (repo.isServer) {
                                var = sprintf(scratch, "ERROR: Maximum from local interface only available to client\n");
                                var = write(fd, scratch, var);
                                return -1;
                        }
                        conf.intfForMax = TRUE;
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
                                      "%s\nUsage: %s [option]... [server[:<port>]]...\n\n"
                                      "Specify '-u' or '-d' to test as a client (server parameter(s) required), else\n"
                                      "run as a server and await client test requests (server parameter optional).\n\n"
                                      "Options:\n"
                                      "(c)    -u|-d        Test %s OR %s as client\n"
                                      "       -4           Use only IPv4 address family (AF_INET)\n"
                                      "       -6           Use only IPv6 address family (AF_INET6)\n"
                                      "(c)    -C cnt[-max] Multi-connection count [Default %d per server]\n"
                                      "(s)    -x           Execute server as background (daemon) process\n"
                                      "(s)    -1           Server exits after one test execution\n"
                                      "(e)    -e           Disable suppression of socket (send/receive) errors\n"
                                      "       -v           Enable verbose output messaging\n"
                                      "       -s           Summary/Max output only (no sub-interval output)\n"
                                      "       -f format    JSON output (json, jsonb [brief], jsonf [formatted])\n"
                                      "(j)    -j           Disable jumbo datagram sizes above 1 Gbps\n",
                                      SOFTWARE_TITLE, argv[0], USTEST_TEXT, DSTEST_TEXT, DEF_MC_COUNT);
                        var = write(fd, scratch, var);
                        var = sprintf(scratch,
                                      "       -T           Use datagram sizes for traditional (1500 byte) MTU\n"
                                      "       -D           Enable debug output messaging (requires '-v')\n"
                                      "(m)    -X           Randomize datagram payload (else zeroes)\n"
                                      "       -S           Show server sending rate table and exit\n"
                                      "       -B mbps      Max bandwidth required by client OR available to server\n"
                                      "       -r           Display loss ratio instead of delivered percentage\n"
                                      "       -i count     Display bimodal maxima (specify initial sub-intervals)\n"
                                      "(c)    -o           Use One-Way Delay instead of RTT for delay variation\n"
                                      "(c)    -R           Include Out-of-Order/Duplicate datagrams\n"
                                      "       -a key       Authentication key (%d characters max)\n"
                                      "(m,v)  -m value     Packet marking octet (IP_TOS/IPV6_TCLASS) [Default %d]\n"
                                      "(m,i)  -I [%c]index  Index of sending rate (see '-S') [Default %c0 = <Auto>]\n"
                                      "(m)    -t time      Test interval time in seconds [Default %d, Max %d]\n",
                                      AUTH_KEY_SIZE, DEF_IPTOS_BYTE, SRIDX_ISSTART_PREFIX, SRIDX_ISSTART_PREFIX, DEF_TESTINT_TIME,
                                      MAX_TESTINT_TIME);
                        var = write(fd, scratch, var);
                        var = sprintf(scratch,
                                      "(c)    -P period    Sub-interval period in seconds [Default %d]\n"
                                      "       -p port      Default port number used for control [Default %d]\n"
                                      "(c)    -A algo      Rate adjustment algorithm (%s - %s) [Default %s]\n"
                                      "       -b buffer    Socket buffer request size (SO_SNDBUF/SO_RCVBUF)\n"
                                      "(c)    -L delvar    Low delay variation threshold in ms [Default %d]\n"
                                      "(c)    -U delvar    Upper delay variation threshold in ms [Default %d]\n"
                                      "(c)    -F interval  Status feedback/trial interval in ms [Default %d]\n"
                                      "(c)    -c thresh    Congestion slow adjustment threshold [Default %d]\n"
                                      "(c)    -h delta     High-speed (row adjustment) delta [Default %d]\n"
                                      "(c)    -q seqerr    Sequence error threshold [Default %d]\n"
                                      "(c)    -E intf      Show local interface traffic rate (ex. eth0)\n",
                                      DEF_SUBINT_PERIOD, DEF_CONTROL_PORT, rateAdjAlgo[CHTA_RA_ALGO_MIN],
                                      rateAdjAlgo[CHTA_RA_ALGO_MAX], rateAdjAlgo[DEF_RA_ALGO], DEF_LOW_THRESH, DEF_UPPER_THRESH,
                                      DEF_TRIAL_INT, DEF_SLOW_ADJ_TH, DEF_HS_DELTA, DEF_SEQ_ERR_TH);
                        var = write(fd, scratch, var);
                        var = sprintf(scratch,
                                      "(c)    -M           Use local interface rate to determine maximum\n"
                                      "(s)    -l logfile   Log file name when executing as daemon\n"
                                      "(s)    -k logsize   Log file maximum size in KBytes [Default %d]\n\n"
                                      "Parameters:\n"
                                      "   server[:<port>]  Hostname/IP of server OR local interface IP if server\n"
                                      "                    - Optional port number overrides configured control port\n"
                                      "                    - Format for IPv6 address w/port number = '[<IPv6>]:<port>'\n"
                                      "Notes:\n"
                                      "(c) = Used only by client.\n"
                                      "(s) = Used only by server.\n"
                                      "(e) = Suppressed due to expected errors with overloaded network interfaces.\n"
                                      "(j) = Datagram sizes that would result in jumbo frames if available.\n"
                                      "(m) = Used as a request by the client or a maximum by the server. Client\n"
                                      "      requests that exceed server maximum are automatically coerced down.\n"
                                      "(v) = Values can be specified as decimal (0 - 255) or hex (0x00 - 0xff).\n"
                                      "(i) = Static OR starting (with '%c' prefix) sending rate index.\n",
                                      DEF_LOGFILE_MAX, SRIDX_ISSTART_PREFIX);
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
        if (conf.verbose && conf.jsonOutput) {
                var = sprintf(scratch, "ERROR: Verbose not available with JSON output format option\n");
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
        if (conf.bimodalCount >= conf.testIntTime / conf.subIntPeriod) {
                var = sprintf(scratch, "ERROR: Bimodal count must be less than total sub-intervals\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if (conf.intfForMax && *conf.intfName == '\0') {
                var = sprintf(scratch, "ERROR: Maximum from local interface requires local interface option\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if (conf.minConnCount > conf.maxConnCount) {
                var = sprintf(scratch, "ERROR: Minimum connection count > maximum connection count\n");
                var = write(fd, scratch, var);
                return -1;
        }
        if (conf.minConnCount == DEF_MC_COUNT && conf.maxConnCount == DEF_MC_COUNT) {
                // If connection counts not specified bump default to cover provided servers
                if (repo.serverCount > DEF_MC_COUNT)
                        conf.minConnCount = conf.maxConnCount = repo.serverCount;
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
//
// Finish JSON processing and output
//
int json_finish() {
        int var;
        char *json_string = NULL;

        //
        // Add final items to output object and add it to top-level object
        //
        // Override a successful test completion if there were any warnings or soft errors
        //
        if (repo.endTimeStatus == STATUS_SUCCESS && *json_errbuf != '\0')
                repo.endTimeStatus = STATUS_WARNING;
        if (json_output) {
                create_timestamp(&repo.systemClock);
                cJSON_AddStringToObject(json_output, "EOMTime", scratch);
                //
                if (repo.endTimeStatus == STATUS_SUCCESS) {
                        cJSON_AddStringToObject(json_output, "Status", "Complete");
                } else {
                        cJSON_AddStringToObject(json_output, "Status", "Error_Other");
                }
                cJSON_AddItemToObject(json_top, "Output", json_output);
        }

        //
        // Add error information to top-level object
        //
        cJSON_AddNumberToObject(json_top, "ErrorStatus", repo.endTimeStatus);
        cJSON_AddStringToObject(json_top, "ErrorMessage", json_errbuf);

        //
        // Convert JSON Object to string and output
        //
        // NOTE: When stdout is not redirected to a file, JSON may appear clipped due to non-blocking console writes
        //
        json_string     = cJSON_PrintBuffered(json_top, 32768, conf.jsonFormatted); // Size covers likely default test options
        var             = strlen(json_string);
        conf.jsonOutput = FALSE; // IMPORTANT: Disable JSON formatting prior to final send_proc() call
        send_proc(errConn, json_string, var);
        send_proc(errConn, "\n", 1);
        //
        free(json_string);
        cJSON_Delete(json_top);

        return repo.endTimeStatus;
}
//----------------------------------------------------------------------------
