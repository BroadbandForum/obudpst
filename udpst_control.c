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
 * UDP Speed Test - udpst_control.c
 *
 * This file handles the control message processing needed to setup and
 * activate test sessions. This includes allocating connections and managing
 * the associated sockets.
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
 * Len Ciavattone          11/10/2020    Add option to ignore OoO/Dup
 * Len Ciavattone          10/13/2021    Refresh with clang-format
 *                                       Add TR-181 fields in JSON
 *                                       Add interface traffic rate support
 * Len Ciavattone          11/18/2021    Add backward compat. protocol version
 *                                       Add bandwidth management support
 * Len Ciavattone          12/08/2021    Add starting sending rate
 * Len Ciavattone          12/17/2021    Add payload randomization
 * Len Ciavattone          02/02/2022    Add rate adj. algo. selection
 *
 */

#define UDPST_CONTROL
#if __linux__
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/file.h>
#ifdef AUTH_KEY_ENABLE
#include <openssl/hmac.h>
#include <openssl/x509.h>
#endif
#else
#include "../udpst_control_alt1.h"
#endif
//
#include "cJSON.h"
#include "udpst_common.h"
#include "udpst_protocol.h"
#include "udpst.h"
#include "udpst_control.h"
#include "udpst_data.h"
#ifndef __linux__
#include "../udpst_control_alt2.h"
#endif

//----------------------------------------------------------------------------
//
// Internal function prototypes
//
int timeout_testinit(int);
int service_actreq(int);
int service_actresp(int);
int sock_connect(int);
int connected(int);

//----------------------------------------------------------------------------
//
// External data
//
extern int errConn, monConn;
extern char scratch[STRING_SIZE];
extern struct configuration conf;
extern struct repository repo;
extern struct connection *conn;
extern char *boolText[];
extern char *rateAdjAlgo[];
//
extern cJSON *json_top, *json_output;

//----------------------------------------------------------------------------
//
// Global data
//
#define SRAUTO_TEXT "<Auto>"
#define OWD_TEXT    "OWD"
#define RTT_TEXT    "RTT"
#define ZERO_TEXT   "zeroes"
#define RAND_TEXT   "random"
#define TESTHDR_LINE1 \
        "%s%s Test Int(sec): %d, DelayVar Thresh(ms): %d-%d [%s], Trial Int(ms): %d, Ignore OoO/Dup: %s, Payload: %s,\n"
#define TESTHDR_LINE2 "    SendRate Index: %s, Cong. Thresh: %d, High-Speed Delta: %d, SeqError Thresh: %d, Algo: %s, "
static char *testHdrV4 = TESTHDR_LINE1 TESTHDR_LINE2 "IPv4 ToS: %d%s\n";
static char *testHdrV6 = TESTHDR_LINE1 TESTHDR_LINE2 "IPv6 TClass: %d%s\n";

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------
//
// Initialize a connection structure
//
void init_conn(int connindex, BOOL cleanup) {
        register struct connection *c = &conn[connindex];
        int i;

        //
        // Cleanup prior to clear and init
        //
        if (cleanup) {
                if (connindex == repo.maxConnIndex) {
                        for (i = connindex - 1; i >= 0; i--) {
                                if (conn[i].fd == -1)
                                        continue;
                                repo.maxConnIndex = i;
                                break;
                        }
                }
                if (c->fd >= 0) {
#ifdef __linux__
                        // Event needed to be non-null before kernel version 2.6.9
                        epoll_ctl(repo.epollFD, EPOLL_CTL_DEL, c->fd, NULL);
#endif
                        close(c->fd);
                }
                if (c->intfFD >= 0)
                        close(c->intfFD);
        }

        //
        // Clear structure
        //
        memset(&conn[connindex], 0, sizeof(struct connection));

        //
        // Initialize non-zero values
        //
        c->fd           = -1;
        c->priAction    = &null_action;
        c->secAction    = &null_action;
        c->timer1Action = &null_action;
        c->timer2Action = &null_action;
        c->timer3Action = &null_action;
        c->intfFD       = -1;

        return;
}
//----------------------------------------------------------------------------
//
// Null action routine
//
int null_action(int connindex) {
        (void) (connindex);

        return 0;
}
//----------------------------------------------------------------------------
//
// Client function to send setup request to server's control port
//
// A setup response is expected back from the server
//
int send_setupreq(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        struct timespec tspecvar;
        char addrstr[INET6_ADDR_STRLEN], portstr[8], intfpath[IFNAMSIZ + 64];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
#ifdef AUTH_KEY_ENABLE
        unsigned int uvar;
#endif
        //
        // Open local sysfs interface statistics
        //
        if (*conf.intfName) {
                var = sprintf(intfpath, "/sys/class/net/%s/statistics/", conf.intfName);
                if (conf.usTesting)
                        strcat(&intfpath[var], "tx_bytes");
                else
                        strcat(&intfpath[var], "rx_bytes");
                if ((c->intfFD = open(intfpath, O_RDONLY)) < 0) {
                        var = sprintf(scratch, "OPEN ERROR: %s (%s)\n", strerror(errno), intfpath);
                        send_proc(errConn, scratch, var);
                        return -1;
                }
        }

        //
        // Build setup request PDU
        //
        memset(cHdrSR, 0, CHSR_SIZE_CVER);
        cHdrSR->controlId   = htons(CHSR_ID);
        c->protocolVer      = PROTOCOL_VER; // Client always uses current version
        cHdrSR->protocolVer = htons((uint16_t) c->protocolVer);
        cHdrSR->cmdRequest  = CHSR_CREQ_SETUPREQ;
        cHdrSR->cmdResponse = CHSR_CRSP_NONE;
        if (conf.maxBandwidth > 0) {
                c->maxBandwidth = conf.maxBandwidth;
                var             = c->maxBandwidth;
                if (conf.usTesting)
                        var |= CHSR_USDIR_BIT; // Set upstream bit of max bandwidth being transmitted
                cHdrSR->maxBandwidth = htons((uint16_t) var);
        }
        if (conf.jumboStatus) {
                cHdrSR->modifierBitmap |= CHSR_JUMBO_STATUS;
        }
        if (conf.traditionalMTU) {
                cHdrSR->modifierBitmap |= CHSR_TRADITIONAL_MTU;
        }
        if (*conf.authKey == '\0') {
                cHdrSR->authMode     = AUTHMODE_NONE;
                cHdrSR->authUnixTime = 0;
#ifdef AUTH_KEY_ENABLE
        } else {
                cHdrSR->authMode     = AUTHMODE_SHA256;
                cHdrSR->authUnixTime = htonl((uint32_t) repo.systemClock.tv_sec);
                HMAC(EVP_sha256(), conf.authKey, strlen(conf.authKey), (const unsigned char *) cHdrSR, CHSR_SIZE_CVER,
                     cHdrSR->authDigest, &uvar);
#endif
        }

        //
        // Update global address info for subsequent send
        //
        if ((var = sock_mgmt(connindex, repo.serverIp, conf.controlPort, NULL, SMA_UPDATE)) != 0) {
                send_proc(errConn, scratch, var);
                return -1;
        }

        //
        // Send setup request PDU (socket not yet connected)
        //
        var = CHSR_SIZE_CVER;
        if (send_proc(connindex, (char *) cHdrSR, var) != var)
                return -1;
        if (monConn >= 0) {
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                var = sprintf(scratch, "[%d]Setup request sent from %s:%d to %s:%s\n", connindex, c->locAddr, c->locPort, addrstr,
                              portstr);
                send_proc(monConn, scratch, var);
        }

        //
        // Set timeout timer awaiting test initiation
        //
        tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
        tspecvar.tv_nsec = 0;
        tspecplus(&repo.systemClock, &tspecvar, &c->timer3Thresh);
        c->timer3Action = &timeout_testinit;

        return 0;
}
//----------------------------------------------------------------------------
//
// Client function to process timeout awaiting test initiation
//
int timeout_testinit(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;

        //
        // Clear timeout timer
        //
        tspecclear(&c->timer3Thresh);
        c->timer3Action = &null_action;

        //
        // Notify user and set immediate end time
        //
        var = sprintf(scratch, "Timeout awaiting server response, exiting!\n");
        send_proc(errConn, scratch, var);
        tspeccpy(&c->endTime, &repo.systemClock);

        return 0;
}
//----------------------------------------------------------------------------
//
// Server function to service client setup request received on control port
//
// A new test connection is allocated and a setup response is sent back
//
int service_setupreq(int connindex) {
        register struct connection *c = &conn[connindex];
        int i, var, pver, mbw = 0, currbw = repo.dsBandwidth;
        BOOL usbw = FALSE;
        struct timespec tspecvar;
        char addrstr[INET6_ADDR_STRLEN], portstr[8];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
#ifdef AUTH_KEY_ENABLE
        unsigned int uvar;
        unsigned char digest1[AUTH_DIGEST_LENGTH], digest2[AUTH_DIGEST_LENGTH];
#endif
        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) CHSR_SIZE_MVER || repo.rcvDataSize > (int) CHSR_SIZE_CVER ||
            ntohs(cHdrSR->controlId) != CHSR_ID) {
                return 0; // Ignore bad PDU
        }
        if (cHdrSR->cmdRequest != CHSR_CREQ_SETUPREQ) {
                return 0;
        }
        if (cHdrSR->cmdResponse != CHSR_CRSP_NONE) {
                return 0;
        }

        //
        // Check specifics of setup request from client
        //
        var = 0;
        if (monConn >= 0) {
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                            NI_NUMERICHOST | NI_NUMERICSERV);
        }
        pver = (int) ntohs(cHdrSR->protocolVer);
        if (pver >= BWMGMT_PVER) {
                mbw = (int) (ntohs(cHdrSR->maxBandwidth) & ~CHSR_USDIR_BIT); // Obtain max bandwidth while ignoring upstream bit
                if (ntohs(cHdrSR->maxBandwidth) & CHSR_USDIR_BIT) {
                        usbw   = TRUE; // Max bandwidth is for upstream
                        currbw = repo.usBandwidth;
                }
        }
        if (pver < PROTOCOL_MIN || pver > PROTOCOL_VER) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Invalid version (%d) in setup request from %s:%s\n", connindex, pver, addrstr,
                                      portstr);
                }
                cHdrSR->protocolVer = htons(PROTOCOL_VER); // Send back expected version
                cHdrSR->cmdResponse = CHSR_CRSP_BADVER;

        } else if (((cHdrSR->modifierBitmap & CHSR_JUMBO_STATUS) && !conf.jumboStatus) ||
                   !(cHdrSR->modifierBitmap & CHSR_JUMBO_STATUS) && conf.jumboStatus) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Invalid jumbo datagram option in setup request from %s:%s\n", connindex,
                                      addrstr, portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_BADJS;

        } else if (((cHdrSR->modifierBitmap & CHSR_TRADITIONAL_MTU) && !conf.traditionalMTU) ||
                   (!(cHdrSR->modifierBitmap & CHSR_TRADITIONAL_MTU) && conf.traditionalMTU)) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Invalid traditional MTU option in setup request from %s:%s\n", connindex,
                                      addrstr, portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_BADTMTU;

        } else if (pver >= BWMGMT_PVER && conf.maxBandwidth > 0 && mbw == 0) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Required bandwidth not specified in setup request from %s:%s\n", connindex,
                                      addrstr, portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_NOMAXBW;

        } else if (pver >= BWMGMT_PVER && conf.maxBandwidth > 0 && currbw + mbw > conf.maxBandwidth) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Capacity exceeded by required bandwidth (%d) in setup request from %s:%s\n",
                                      connindex, mbw, addrstr, portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_CAPEXC;

        } else if (cHdrSR->authMode != AUTHMODE_NONE && *conf.authKey == '\0') {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Unexpected authentication in setup request from %s:%s\n", connindex, addrstr,
                                      portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_AUTHNC;
#ifdef AUTH_KEY_ENABLE
        } else if (cHdrSR->authMode == AUTHMODE_NONE && *conf.authKey != '\0') {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Authentication missing in setup request from %s:%s\n", connindex, addrstr,
                                      portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_AUTHREQ;

        } else if (cHdrSR->authMode != AUTHMODE_SHA256 && *conf.authKey != '\0') {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Invalid authentication method in setup request from %s:%s\n", connindex,
                                      addrstr, portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_AUTHINV;

        } else if (cHdrSR->authMode == AUTHMODE_SHA256 && *conf.authKey != '\0') {
                //
                // Validate authentication digest (leave zeroed for response) and check time window if enforced
                //
                memcpy(digest1, cHdrSR->authDigest, AUTH_DIGEST_LENGTH);
                memset(cHdrSR->authDigest, 0, AUTH_DIGEST_LENGTH);
                HMAC(EVP_sha256(), conf.authKey, strlen(conf.authKey), (const unsigned char *) cHdrSR, CHSR_SIZE_CVER, digest2,
                     &uvar);
                if (memcmp(digest1, digest2, AUTH_DIGEST_LENGTH)) {
                        if (monConn >= 0) {
                                var = sprintf(scratch, "[%d]Authentication failure of setup request from %s:%s\n", connindex,
                                              addrstr, portstr);
                        }
                        cHdrSR->cmdResponse = CHSR_CRSP_AUTHFAIL;

                } else if (AUTH_ENFORCE_TIME) {
                        tspecvar.tv_sec = (time_t) ntohl(cHdrSR->authUnixTime);
                        if (tspecvar.tv_sec < repo.systemClock.tv_sec - AUTH_TIME_WINDOW ||
                            tspecvar.tv_sec > repo.systemClock.tv_sec + AUTH_TIME_WINDOW) {
                                if (monConn >= 0) {
                                        var = sprintf(scratch, "[%d]Authentication time invalid in setup request from %s:%s\n",
                                                      connindex, addrstr, portstr);
                                }
                                cHdrSR->cmdResponse = CHSR_CRSP_AUTHTIME;
                        }
                }
#endif
        }
        cHdrSR->cmdRequest = CHSR_CREQ_SETUPRSP; // Convert to setup response
        if (cHdrSR->cmdResponse != CHSR_CRSP_NONE) {
                //
                // If error, send back appropriate setup response and exit
                //
                if (monConn >= 0)
                        send_proc(monConn, scratch, var);
                var = CHSR_SIZE_CVER;
                send_proc(connindex, (char *) cHdrSR, var);
                return 0;
        }
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Setup request (Ver: %d, MaxBW: %d) received from %s:%s\n", connindex, pver, mbw,
                              addrstr, portstr);
                send_proc(monConn, scratch, var);
        }

        //
        // Obtain new test connection for this client
        //
        if ((i = new_conn(-1, repo.serverIp, 0, T_UDP, &recv_proc, &service_actreq)) < 0)
                return 0;
        conn[i].protocolVer = pver;
        if (conf.maxBandwidth > 0) {
                conn[i].maxBandwidth = mbw; // Save bandwidth for adjustment at end of test
                if (usbw)
                        repo.usBandwidth += mbw; // Update current upstream bandwidth
                else
                        repo.dsBandwidth += mbw; // Update current downstream bandwidth
        }
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Connection %d allocated and assigned %s:%d (New USBW: %d, DSBW: %d)\n", connindex, i,
                              conn[i].locAddr, conn[i].locPort, repo.usBandwidth, repo.dsBandwidth);
                send_proc(monConn, scratch, var);
        }

        //
        // Set end time (used as watchdog) in case client goes quiet
        //
        tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
        tspecvar.tv_nsec = 0;
        tspecplus(&repo.systemClock, &tspecvar, &conn[i].endTime);

        //
        // Send setup response to client with port number of new test connection
        //
        cHdrSR->cmdResponse = CHSR_CRSP_ACKOK;
        cHdrSR->testPort    = htons((uint16_t) conn[i].locPort);
        var                 = CHSR_SIZE_CVER;
        if (send_proc(connindex, (char *) cHdrSR, var) != var)
                return 0;
        if (monConn >= 0) {
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                var = sprintf(scratch, "[%d]Setup response sent from %s:%d to %s:%s\n", connindex, c->locAddr, c->locPort, addrstr,
                              portstr);
                send_proc(monConn, scratch, var);
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Client function to service setup response received from server
//
// Send test activation request to server for the new test connection
//
int service_setupresp(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        char addrstr[INET6_ADDR_STRLEN], portstr[8];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) CHSR_SIZE_CVER || ntohs(cHdrSR->controlId) != CHSR_ID) {
                return 0; // Ignore bad PDU
        }
        if (cHdrSR->cmdRequest != CHSR_CREQ_SETUPRSP) {
                return 0;
        }

        //
        // Process any setup response errors
        //
        if (cHdrSR->cmdResponse != CHSR_CRSP_ACKOK) {
                if (cHdrSR->cmdResponse == CHSR_CRSP_BADVER) {
                        var = sprintf(scratch, "ERROR: Client version (%u) does not match server (%u)\n", PROTOCOL_VER,
                                      ntohs(cHdrSR->protocolVer));
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_BADJS) {
                        var = sprintf(scratch, "ERROR: Client jumbo datagram size option does not match server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_BADTMTU) {
                        var = sprintf(scratch, "ERROR: Client traditional MTU option does not match server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_AUTHNC) {
                        var = sprintf(scratch, "ERROR: Authentication not configured on server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_AUTHREQ) {
                        var = sprintf(scratch, "ERROR: Authentication required by server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_AUTHINV) {
                        var = sprintf(scratch, "ERROR: Authentication method does not match server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_AUTHFAIL) {
                        var = sprintf(scratch, "ERROR: Authentication verification failed at server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_AUTHTIME) {
                        var = sprintf(scratch, "ERROR: Authentication time outside server time window\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_NOMAXBW) {
                        var = sprintf(scratch, "ERROR: Max bandwidth option required by server\n");
                } else if (cHdrSR->cmdResponse == CHSR_CRSP_CAPEXC) {
                        var = sprintf(scratch, "ERROR: Required max bandwidth exceeds available server capacity\n");
                } else {
                        var = sprintf(scratch, "ERROR: Unexpected CRSP (%u) in setup response from server\n", cHdrSR->cmdResponse);
                }
                send_proc(errConn, scratch, var);
                tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                return 0;
        }

        //
        // Obtain IP address and port number of sender
        //
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Setup response received from %s:%s\n", connindex, addrstr, portstr);
                send_proc(monConn, scratch, var);
        }

        //
        // Update global address info with new server specified address/port number and connect socket
        //
        var = (int) ntohs(cHdrSR->testPort);
        if ((var = sock_mgmt(connindex, addrstr, var, NULL, SMA_UPDATE)) != 0) {
                send_proc(errConn, scratch, var);
                return 0;
        }
        if (sock_connect(connindex) < 0)
                return 0;

        //
        // Build test activation PDU
        //
        memset(cHdrTA, 0, CHTA_SIZE_CVER);
        cHdrTA->controlId   = htons(CHTA_ID);
        cHdrTA->protocolVer = htons((uint16_t) c->protocolVer);
        if (conf.usTesting) {
                c->testType        = TEST_TYPE_US;
                cHdrTA->cmdRequest = CHTA_CREQ_TESTACTUS;
        } else {
                c->testType        = TEST_TYPE_DS;
                cHdrTA->cmdRequest = CHTA_CREQ_TESTACTDS;
        }
        cHdrTA->cmdResponse = CHTA_CRSP_NONE;

        //
        // Save configured parameters in connection and copy to test activation request
        //
        c->lowThresh           = conf.lowThresh;
        cHdrTA->lowThresh      = htons((uint16_t) c->lowThresh);
        c->upperThresh         = conf.upperThresh;
        cHdrTA->upperThresh    = htons((uint16_t) c->upperThresh);
        c->trialInt            = conf.trialInt;
        cHdrTA->trialInt       = htons((uint16_t) c->trialInt);
        c->testIntTime         = conf.testIntTime;
        cHdrTA->testIntTime    = htons((uint16_t) c->testIntTime);
        c->subIntPeriod        = conf.subIntPeriod;
        cHdrTA->subIntPeriod   = (uint8_t) c->subIntPeriod;
        c->ipTosByte           = conf.ipTosByte;
        cHdrTA->ipTosByte      = (uint8_t) c->ipTosByte;
        c->srIndexConf         = conf.srIndexConf;
        cHdrTA->srIndexConf    = htons((uint16_t) c->srIndexConf);
        c->useOwDelVar         = (BOOL) conf.useOwDelVar;
        cHdrTA->useOwDelVar    = (uint8_t) c->useOwDelVar;
        c->highSpeedDelta      = conf.highSpeedDelta;
        cHdrTA->highSpeedDelta = (uint8_t) c->highSpeedDelta;
        c->slowAdjThresh       = conf.slowAdjThresh;
        cHdrTA->slowAdjThresh  = htons((uint16_t) c->slowAdjThresh);
        c->seqErrThresh        = conf.seqErrThresh;
        cHdrTA->seqErrThresh   = htons((uint16_t) c->seqErrThresh);
        c->ignoreOooDup        = (BOOL) conf.ignoreOooDup;
        cHdrTA->ignoreOooDup   = (uint8_t) c->ignoreOooDup;
        if (conf.srIndexIsStart) {
                c->srIndexIsStart = TRUE; // Designate configured value as starting point
                cHdrTA->modifierBitmap |= CHTA_SRIDX_ISSTART;
        }
        if (conf.randPayload) {
                c->randPayload = TRUE;
                cHdrTA->modifierBitmap |= CHTA_RAND_PAYLOAD;
        }
        c->rateAdjAlgo      = conf.rateAdjAlgo;
        cHdrTA->rateAdjAlgo = (uint8_t) c->rateAdjAlgo;

        //
        // Send test activation request
        //
        c->secAction = &service_actresp; // Set service handler for response
        var          = CHTA_SIZE_CVER;
        if (send_proc(connindex, (char *) cHdrTA, var) != var)
                return 0;
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Test activation request sent from %s:%d to %s:%d\n", connindex, c->locAddr, c->locPort,
                              c->remAddr, c->remPort);
                send_proc(monConn, scratch, var);
        }

        return 0;
}
//----------------------------------------------------------------------------
//
// Server function to service test activation request received on new test connection
//
// Send test activation response back to client, connection is ready for testing
//
int service_actreq(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        char *testhdr, *testtype, connid[8], delusage[8], sritext[8], payload[8];
        char addrstr[INET6_ADDR_STRLEN], portstr[8], intflabel[IFNAMSIZ + 8];
        struct sendingRate *sr = repo.sendingRates; // Set to first row of table
        struct timespec tspecvar;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        var = (int) ntohs(cHdrTA->protocolVer);
        if (repo.rcvDataSize < (int) CHTA_SIZE_MVER || repo.rcvDataSize > (int) CHTA_SIZE_CVER ||
            ntohs(cHdrTA->controlId) != CHTA_ID || var < PROTOCOL_MIN || var > PROTOCOL_VER) {
                return 0; // Ignore bad PDU
        }
        if ((cHdrTA->cmdRequest != CHTA_CREQ_TESTACTUS) && (cHdrTA->cmdRequest != CHTA_CREQ_TESTACTDS)) {
                return 0;
        }
        if (cHdrTA->cmdResponse != CHTA_CRSP_NONE) {
                return 0;
        }

        //
        // Obtain IP address and port number of sender
        //
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Test activation request received from %s:%s\n", connindex, addrstr, portstr);
                send_proc(monConn, scratch, var);
        }

        //
        // Update global address info with client address/port number and connect socket
        //
        var = atoi(portstr);
        if ((var = sock_mgmt(connindex, addrstr, var, NULL, SMA_UPDATE)) != 0) {
                send_proc(errConn, scratch, var);
                return 0;
        }
        if (sock_connect(connindex) < 0)
                return 0;

        //====================================================================================
        // Accept (but police) most test parameters as is and enforce server configured
        // maximums where applicable. Update modified values for communication back to client.
        // If the request needs to be rejected use command response value CHTA_CRSP_BADPARAM.
        //
        cHdrTA->cmdResponse = CHTA_CRSP_ACKOK; // Initialize to request accepted
        //
        // Low and upper delay variation thresholds
        //
        c->lowThresh = (int) ntohs(cHdrTA->lowThresh);
        if (c->lowThresh < MIN_LOW_THRESH || c->lowThresh > MAX_LOW_THRESH) {
                c->lowThresh      = DEF_LOW_THRESH;
                cHdrTA->lowThresh = htons((uint16_t) c->lowThresh);
        }
        c->upperThresh = (int) ntohs(cHdrTA->upperThresh);
        if (c->upperThresh < MIN_UPPER_THRESH || c->upperThresh > MAX_UPPER_THRESH) {
                c->upperThresh      = DEF_UPPER_THRESH;
                cHdrTA->upperThresh = htons((uint16_t) c->upperThresh);
        }
        if (c->lowThresh > c->upperThresh) { // Check for invalid relationship
                c->lowThresh        = DEF_LOW_THRESH;
                cHdrTA->lowThresh   = htons((uint16_t) c->lowThresh);
                c->upperThresh      = DEF_UPPER_THRESH;
                cHdrTA->upperThresh = htons((uint16_t) c->upperThresh);
        }
        //
        // Trial interval
        //
        c->trialInt = (int) ntohs(cHdrTA->trialInt);
        if (c->trialInt < MIN_TRIAL_INT || c->trialInt > MAX_TRIAL_INT) {
                c->trialInt      = DEF_TRIAL_INT;
                cHdrTA->trialInt = htons((uint16_t) c->trialInt);
        }
        //
        // Test interval time and sub-interval period
        //
        c->testIntTime = (int) ntohs(cHdrTA->testIntTime);
        if (c->testIntTime < MIN_TESTINT_TIME || c->testIntTime > MAX_TESTINT_TIME) {
                c->testIntTime      = DEF_TESTINT_TIME;
                cHdrTA->testIntTime = htons((uint16_t) c->testIntTime);
        } else if (c->testIntTime > conf.testIntTime) { // Enforce server maximum
                c->testIntTime      = conf.testIntTime;
                cHdrTA->testIntTime = htons((uint16_t) c->testIntTime);
        }
        c->subIntPeriod = (int) cHdrTA->subIntPeriod;
        if (c->subIntPeriod < MIN_SUBINT_PERIOD || c->subIntPeriod > MAX_SUBINT_PERIOD) {
                c->subIntPeriod      = DEF_SUBINT_PERIOD;
                cHdrTA->subIntPeriod = (uint8_t) c->subIntPeriod;
        }
        if (c->subIntPeriod > c->testIntTime) { // Check for invalid relationship
                c->testIntTime       = DEF_TESTINT_TIME;
                cHdrTA->testIntTime  = htons((uint16_t) c->testIntTime);
                c->subIntPeriod      = DEF_SUBINT_PERIOD;
                cHdrTA->subIntPeriod = (uint8_t) c->subIntPeriod;
        }
        //
        // IP ToS/TClass byte (also set socket option)
        //
        c->ipTosByte = (int) cHdrTA->ipTosByte;
        if (c->ipTosByte < MIN_IPTOS_BYTE || c->ipTosByte > MAX_IPTOS_BYTE) {
                c->ipTosByte      = DEF_IPTOS_BYTE;
                cHdrTA->ipTosByte = (uint8_t) c->ipTosByte;
        } else if (c->ipTosByte > conf.ipTosByte) { // Enforce server maximum
                c->ipTosByte      = conf.ipTosByte;
                cHdrTA->ipTosByte = (uint8_t) c->ipTosByte;
        }
        if (c->ipTosByte != 0) {
                if (c->ipProtocol == IPPROTO_IPV6)
                        var = IPV6_TCLASS;
                else
                        var = IP_TOS;
                if (setsockopt(c->fd, c->ipProtocol, var, (const void *) &c->ipTosByte, sizeof(c->ipTosByte)) < 0) {
                        c->ipTosByte      = 0;
                        cHdrTA->ipTosByte = (uint8_t) c->ipTosByte;
                }
        }
        //
        // Static or starting sending rate index (special case <Auto>, which is the default but greater than max)
        //
        c->srIndexConf = (int) ntohs(cHdrTA->srIndexConf);
        if (c->srIndexConf != DEF_SRINDEX_CONF) {
                if (c->srIndexConf < MIN_SRINDEX_CONF || c->srIndexConf > MAX_SRINDEX_CONF) {
                        c->srIndexConf      = DEF_SRINDEX_CONF;
                        cHdrTA->srIndexConf = htons((uint16_t) c->srIndexConf);
                } else if (c->srIndexConf > conf.srIndexConf) { // Enforce server maximum
                        c->srIndexConf      = conf.srIndexConf;
                        cHdrTA->srIndexConf = htons((uint16_t) c->srIndexConf);
                }
                if (cHdrTA->modifierBitmap & CHTA_SRIDX_ISSTART) {
                        c->srIndexIsStart = TRUE;                               // Designate configured value as starting point
                        c->srIndex        = c->srIndexConf;                     // Set starting point from configured value
                        sr                = &repo.sendingRates[c->srIndexConf]; // Select starting SR table row
                }
        }
        //
        // Use one-way delay flag
        //
        c->useOwDelVar = (BOOL) cHdrTA->useOwDelVar;
        if (c->useOwDelVar != TRUE && c->useOwDelVar != FALSE) { // Enforce C boolean
                c->useOwDelVar      = DEF_USE_OWDELVAR;
                cHdrTA->useOwDelVar = (uint8_t) c->useOwDelVar;
        }
        //
        // High-speed delta
        //
        c->highSpeedDelta = (int) cHdrTA->highSpeedDelta;
        if (c->highSpeedDelta < MIN_HS_DELTA || c->highSpeedDelta > MAX_HS_DELTA) {
                c->highSpeedDelta      = DEF_HS_DELTA;
                cHdrTA->highSpeedDelta = (uint8_t) c->highSpeedDelta;
        }
        //
        // Slow rate adjustment threshold
        //
        c->slowAdjThresh = (int) ntohs(cHdrTA->slowAdjThresh);
        if (c->slowAdjThresh < MIN_SLOW_ADJ_TH || c->slowAdjThresh > MAX_SLOW_ADJ_TH) {
                c->slowAdjThresh      = DEF_SLOW_ADJ_TH;
                cHdrTA->slowAdjThresh = htons((uint16_t) c->slowAdjThresh);
        }
        //
        // Sequence error threshold
        //
        c->seqErrThresh = (int) ntohs(cHdrTA->seqErrThresh);
        if (c->seqErrThresh < MIN_SEQ_ERR_TH || c->seqErrThresh > MAX_SEQ_ERR_TH) {
                c->seqErrThresh      = DEF_SEQ_ERR_TH;
                cHdrTA->seqErrThresh = htons((uint16_t) c->seqErrThresh);
        }
        //
        // Ignore Out-of-Order/Duplicate flag
        //
        c->ignoreOooDup = (BOOL) cHdrTA->ignoreOooDup;
        if (c->ignoreOooDup != TRUE && c->ignoreOooDup != FALSE) { // Enforce C boolean
                c->ignoreOooDup      = DEF_IGNORE_OOODUP;
                cHdrTA->ignoreOooDup = (uint8_t) c->ignoreOooDup;
        }
        //
        // Payload randomization (only allow if also configured on server)
        //
        if (cHdrTA->modifierBitmap & CHTA_RAND_PAYLOAD) {
                if (conf.randPayload) {
                        c->randPayload = TRUE;
                } else {
                        cHdrTA->modifierBitmap &= ~CHTA_RAND_PAYLOAD; // Reset bit for return
                }
        }
        //
        // Rate adjustment algorithm
        //
        c->rateAdjAlgo = (int) cHdrTA->rateAdjAlgo;
        if (c->rateAdjAlgo < CHTA_RA_ALGO_MIN || c->rateAdjAlgo > CHTA_RA_ALGO_MAX) {
                c->rateAdjAlgo      = DEF_RA_ALGO;
                cHdrTA->rateAdjAlgo = (uint8_t) c->rateAdjAlgo;
        }
        //
        // If upstream test, send back sending rate parameters from first row of table
        //
        if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                sr_copy(sr, &cHdrTA->srStruct, TRUE);
        } else {
                memset(&cHdrTA->srStruct, 0, sizeof(struct sendingRate));
        }
        //====================================================================================

        //
        // Continue updating connection if test activation is NOT being rejected
        //
        testtype = NULL;
        if (cHdrTA->cmdResponse == CHTA_CRSP_ACKOK) {
                //
                // Set connection test action as testing and initialize PDU received time
                //
                c->testAction = TEST_ACT_TEST;
                tspeccpy(&c->pduRxTime, &repo.systemClock);

                //
                // Finalize connection for testing based on test type
                //
                if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                        //
                        // Upstream
                        // Setup to receive load PDUs and send status PDUs
                        //
                        c->testType   = TEST_TYPE_US;
                        testtype      = USTEST_TEXT;
                        c->rttMinimum = INITIAL_MIN_DELAY;
                        c->rttSample  = INITIAL_MIN_DELAY;
                        c->secAction  = &service_loadpdu;
                        //
                        c->delayVarMin = INITIAL_MIN_DELAY;
                        tspeccpy(&c->trialIntClock, &repo.systemClock);
                        tspecvar.tv_sec  = 0;
                        tspecvar.tv_nsec = (long) (c->trialInt * NSECINMSEC);
                        tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                        c->timer1Action = &send_statuspdu;
                } else {
                        //
                        // Downstream
                        // Setup to receive status PDUs and send load PDUs
                        //
                        c->testType  = TEST_TYPE_DS;
                        testtype     = DSTEST_TEXT;
                        c->secAction = &service_statuspdu;
                        //
                        if (sr->txInterval1 > 0) {
                                tspecvar.tv_sec  = 0;
                                tspecvar.tv_nsec = (long) ((sr->txInterval1 - SEND_TIMER_ADJ) * NSECINUSEC);
                                tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                        }
                        c->timer1Action = &send1_loadpdu;
                        if (sr->txInterval2 > 0) {
                                tspecvar.tv_sec  = 0;
                                tspecvar.tv_nsec = (long) ((sr->txInterval2 - SEND_TIMER_ADJ) * NSECINUSEC);
                                tspecplus(&repo.systemClock, &tspecvar, &c->timer2Thresh);
                        }
                        c->timer2Action = &send2_loadpdu;
                }
        }

        //
        // Send test activation response to client
        //
        var = CHTA_SIZE_CVER;
        if (send_proc(connindex, (char *) cHdrTA, var) != var)
                return 0;
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Test activation response sent from %s:%d to %s:%d\n", connindex, c->locAddr, c->locPort,
                              c->remAddr, c->remPort);
                send_proc(monConn, scratch, var);
        }

        //
        // Do not continue if test activation request is being rejected
        //
        if (cHdrTA->cmdResponse != CHTA_CRSP_ACKOK) {
                tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                return 0;
        }

        //
        // Display test settings and general info if needed
        //
        *connid = '\0';
        if (!conf.jsonOutput) {
                if (conf.verbose)
                        sprintf(connid, "[%d]", connindex);
                if (!repo.isServer || conf.verbose) {
                        if (c->ipProtocol == IPPROTO_IPV6)
                                testhdr = testHdrV6;
                        else
                                testhdr = testHdrV4;
                        if (c->useOwDelVar)
                                strcpy(delusage, OWD_TEXT);
                        else
                                strcpy(delusage, RTT_TEXT);
                        if (c->randPayload)
                                strcpy(payload, RAND_TEXT);
                        else
                                strcpy(payload, ZERO_TEXT);
                        if (c->srIndexConf == DEF_SRINDEX_CONF) {
                                strcpy(sritext, SRAUTO_TEXT);
                        } else if (c->srIndexIsStart) {
                                sprintf(sritext, "%c%d", SRIDX_ISSTART_PREFIX, c->srIndexConf);
                        } else {
                                sprintf(sritext, "%d", c->srIndexConf);
                        }
                        *intflabel = '\0';
                        if (c->intfFD >= 0) { // Append interface label
                                snprintf(intflabel, sizeof(intflabel), ", [%s]", conf.intfName);
                        }
                        var = sprintf(scratch, testhdr, connid, testtype, c->testIntTime, c->lowThresh, c->upperThresh, delusage,
                                      c->trialInt, boolText[c->ignoreOooDup], payload, sritext, c->slowAdjThresh, c->highSpeedDelta,
                                      c->seqErrThresh, rateAdjAlgo[c->rateAdjAlgo], c->ipTosByte, intflabel);
                        send_proc(errConn, scratch, var);
                }
        }

        //
        // Update end time (used as watchdog) in case client goes quiet
        //
        tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
        tspecvar.tv_nsec = 0;
        tspecplus(&repo.systemClock, &tspecvar, &c->endTime);

        //
        // Set timer to stop test after desired test interval time
        //
        tspecvar.tv_sec  = (time_t) c->testIntTime;
        tspecvar.tv_nsec = NSECINSEC / 2;
        tspecplus(&repo.systemClock, &tspecvar, &c->timer3Thresh);
        c->timer3Action = &stop_test;

        return 0;
}
//----------------------------------------------------------------------------
//
// Client function to service test activation response from server
//
// Connection is ready for testing
//
int service_actresp(int connindex) {
        register struct connection *c = &conn[connindex];
        int var, ipv6add;
        char *testhdr, *testtype, connid[8], delusage[8], sritext[8], payload[8];
        char intflabel[IFNAMSIZ + 8];
        struct sendingRate *sr = &c->srStruct; // Set to connection structure
        struct timespec tspecvar;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) CHTA_SIZE_CVER || ntohs(cHdrTA->controlId) != CHTA_ID) {
                return 0; // Ignore bad PDU
        }
        if ((cHdrTA->cmdRequest != CHTA_CREQ_TESTACTUS) && (cHdrTA->cmdRequest != CHTA_CREQ_TESTACTDS)) {
                return 0;
        }

        //
        // Process any test activation response errors
        //
        if (cHdrTA->cmdResponse != CHTA_CRSP_ACKOK) {
                if (cHdrTA->cmdResponse == CHTA_CRSP_BADPARAM) {
                        var = sprintf(scratch, "ERROR: Requested test parameter(s) rejected by server\n");
                } else {
                        var = sprintf(scratch, "ERROR: Unexpected CRSP (%u) in test activation response from server\n",
                                      cHdrTA->cmdResponse);
                }
                send_proc(errConn, scratch, var);
                tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                return 0;
        }
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Test activation response received from %s:%d\n", connindex, c->remAddr, c->remPort);
                send_proc(monConn, scratch, var);
        }

        //
        // Update test parameters (and set socket option) that may have been modified by server
        //
        c->lowThresh    = (int) ntohs(cHdrTA->lowThresh);
        c->upperThresh  = (int) ntohs(cHdrTA->upperThresh);
        c->trialInt     = (int) ntohs(cHdrTA->trialInt);
        c->testIntTime  = (int) ntohs(cHdrTA->testIntTime);
        c->subIntPeriod = (int) cHdrTA->subIntPeriod;
        c->ipTosByte    = (int) cHdrTA->ipTosByte;
        if (c->ipTosByte != 0) {
                if (c->ipProtocol == IPPROTO_IPV6)
                        var = IPV6_TCLASS;
                else
                        var = IP_TOS;
                if (setsockopt(c->fd, c->ipProtocol, var, (const void *) &c->ipTosByte, sizeof(c->ipTosByte)) < 0) {
                        var = sprintf(scratch, "ERROR: Failure setting IP ToS/TClass (%d) %s\n", c->ipTosByte, strerror(errno));
                        send_proc(errConn, scratch, var);
                        tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                        return 0;
                }
        }
        c->srIndexConf    = (int) ntohs(cHdrTA->srIndexConf);
        c->useOwDelVar    = (BOOL) cHdrTA->useOwDelVar;
        c->highSpeedDelta = (int) cHdrTA->highSpeedDelta;
        c->slowAdjThresh  = (int) ntohs(cHdrTA->slowAdjThresh);
        c->seqErrThresh   = (int) ntohs(cHdrTA->seqErrThresh);
        c->ignoreOooDup   = (BOOL) cHdrTA->ignoreOooDup;
        if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                // If upstream test, save sending rate parameters sent by server
                sr_copy(sr, &cHdrTA->srStruct, FALSE);
        }
        if (!(cHdrTA->modifierBitmap & CHTA_RAND_PAYLOAD)) {
                c->randPayload = FALSE; // Payload randomization rejected by server
        }
        c->rateAdjAlgo = (int) cHdrTA->rateAdjAlgo;

        //
        // Set connection test action as testing and initialize PDU received time
        //
        c->testAction = TEST_ACT_TEST;
        tspeccpy(&c->pduRxTime, &repo.systemClock);

        //
        // Finalize connection for testing based on test type
        //
        if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                //
                // Upstream
                // Setup to receive status PDUs and send load PDUs
                //
                testtype     = USTEST_TEXT;
                c->secAction = &service_statuspdu;
                //
                if (sr->txInterval1 > 0) {
                        tspecvar.tv_sec  = 0;
                        tspecvar.tv_nsec = (long) ((sr->txInterval1 - SEND_TIMER_ADJ) * NSECINUSEC);
                        tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                }
                c->timer1Action = &send1_loadpdu;
                if (sr->txInterval2 > 0) {
                        tspecvar.tv_sec  = 0;
                        tspecvar.tv_nsec = (long) ((sr->txInterval2 - SEND_TIMER_ADJ) * NSECINUSEC);
                        tspecplus(&repo.systemClock, &tspecvar, &c->timer2Thresh);
                }
                c->timer2Action = &send2_loadpdu;
        } else {
                //
                // Downstream
                // Setup to receive load PDUs and send status PDUs
                //
                testtype      = DSTEST_TEXT;
                c->rttMinimum = INITIAL_MIN_DELAY;
                c->rttSample  = INITIAL_MIN_DELAY;
                c->secAction  = &service_loadpdu;
                //
                c->delayVarMin = INITIAL_MIN_DELAY;
                tspeccpy(&c->trialIntClock, &repo.systemClock);
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = (long) (c->trialInt * NSECINMSEC);
                tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                c->timer1Action = &send_statuspdu;
        }

        //
        // Display test settings and general info if needed
        //
        *connid = '\0';
        if (conf.verbose)
                sprintf(connid, "[%d]", connindex);
        if (!repo.isServer || conf.verbose) {
                if (c->ipProtocol == IPPROTO_IPV6)
                        testhdr = testHdrV6;
                else
                        testhdr = testHdrV4;
                if (c->useOwDelVar)
                        strcpy(delusage, OWD_TEXT);
                else
                        strcpy(delusage, RTT_TEXT);
                if (c->randPayload)
                        strcpy(payload, RAND_TEXT);
                else
                        strcpy(payload, ZERO_TEXT);
                if (c->srIndexConf == DEF_SRINDEX_CONF) {
                        strcpy(sritext, SRAUTO_TEXT);
                } else if (c->srIndexIsStart) {
                        sprintf(sritext, "%c%d", SRIDX_ISSTART_PREFIX, c->srIndexConf);
                } else {
                        sprintf(sritext, "%d", c->srIndexConf);
                }
                *intflabel = '\0';
                if (c->intfFD >= 0) { // Append interface label
                        snprintf(intflabel, sizeof(intflabel), ", [%s]", conf.intfName);
                }
                if (!conf.jsonOutput) {
                        var = sprintf(scratch, testhdr, connid, testtype, c->testIntTime, c->lowThresh, c->upperThresh, delusage,
                                      c->trialInt, boolText[c->ignoreOooDup], payload, sritext, c->slowAdjThresh, c->highSpeedDelta,
                                      c->seqErrThresh, rateAdjAlgo[c->rateAdjAlgo], c->ipTosByte, intflabel);
                        send_proc(errConn, scratch, var);
                } else {
                        if (!conf.jsonBrief) {
                                //
                                // Create JSON input object
                                //
                                cJSON *json_input = cJSON_CreateObject();
                                //
                                // Add items to input object
                                //
                                cJSON_AddStringToObject(json_input, "Interface", conf.intfName);
                                if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                                        cJSON_AddStringToObject(json_input, "Role", "Sender");
                                } else {
                                        cJSON_AddStringToObject(json_input, "Role", "Receiver");
                                }
                                cJSON_AddStringToObject(json_input, "Host", repo.serverName);
                                cJSON_AddNumberToObject(json_input, "Port", c->remPort);
                                cJSON_AddStringToObject(json_input, "HostIPAddress", c->remAddr);
                                cJSON_AddStringToObject(json_input, "ClientIPAddress", c->locAddr);
                                cJSON_AddNumberToObject(json_input, "ClientPort", c->locPort);
                                cJSON_AddNumberToObject(json_input, "JumboFramesPermitted", conf.jumboStatus);
                                cJSON_AddNumberToObject(json_input, "NumberOfConnections", 1);
                                cJSON_AddNumberToObject(json_input, "DSCP", c->ipTosByte >> 2);
                                if (conf.ipv4Only) {
                                        cJSON_AddStringToObject(json_input, "ProtocolVersion", "IPv4");
                                } else if (conf.ipv6Only) {
                                        cJSON_AddStringToObject(json_input, "ProtocolVersion", "IPv6");
                                } else {
                                        cJSON_AddStringToObject(json_input, "ProtocolVersion", "Any");
                                }
                                ipv6add = 0;
                                if (c->ipProtocol == IPPROTO_IPV6)
                                        ipv6add = IPV6_ADDSIZE;
                                cJSON_AddNumberToObject(json_input, "UDPPayloadMin", MIN_PAYLOAD_SIZE - ipv6add);
                                if (conf.jumboStatus)
                                        var = MAX_JPAYLOAD_SIZE;
                                else if (conf.traditionalMTU)
                                        var = MAX_TPAYLOAD_SIZE;
                                else
                                        var = MAX_PAYLOAD_SIZE;
                                cJSON_AddNumberToObject(json_input, "UDPPayloadMax", var - ipv6add);
                                if (conf.traditionalMTU)
                                        var = MAX_TPAYLOAD_SIZE;
                                else
                                        var = MAX_PAYLOAD_SIZE;
                                cJSON_AddNumberToObject(json_input, "UDPPayloadDefault", var - ipv6add);
                                if (c->randPayload) {
                                        cJSON_AddStringToObject(json_input, "UDPPayloadContent", RAND_TEXT);
                                } else {
                                        cJSON_AddStringToObject(json_input, "UDPPayloadContent", ZERO_TEXT);
                                }
                                if (c->srIndexConf == DEF_SRINDEX_CONF || c->srIndexIsStart) {
                                        cJSON_AddStringToObject(json_input, "TestType", "Search");
                                } else {
                                        cJSON_AddStringToObject(json_input, "TestType", "Fixed");
                                }
                                cJSON_AddNumberToObject(json_input, "IPDVEnable", c->useOwDelVar);
                                cJSON_AddNumberToObject(json_input, "IPRREnable", 1);
                                cJSON_AddNumberToObject(json_input, "RIPREnable", 1);
                                cJSON_AddNumberToObject(json_input, "PreambleDuration", 0);
                                // Using "[Start]SendingRateIndex" instead of "StartSendingRate" for this implementation
                                if (c->srIndexConf == DEF_SRINDEX_CONF || c->srIndexIsStart) {
                                        var = 0;
                                        if (c->srIndexIsStart)
                                                var = c->srIndexConf;
                                        cJSON_AddNumberToObject(json_input, "StartSendingRateIndex", var);
                                        cJSON_AddNumberToObject(json_input, "SendingRateIndex", -1);
                                } else {
                                        cJSON_AddNumberToObject(json_input, "StartSendingRateIndex", c->srIndexConf);
                                        cJSON_AddNumberToObject(json_input, "SendingRateIndex", c->srIndexConf);
                                }
                                cJSON_AddNumberToObject(json_input, "NumberTestSubIntervals", c->testIntTime / c->subIntPeriod);
                                cJSON_AddNumberToObject(json_input, "NumberFirstModeTestSubIntervals", conf.bimodalCount);
                                cJSON_AddNumberToObject(json_input, "TestSubInterval", c->subIntPeriod * MSECINSEC);
                                cJSON_AddNumberToObject(json_input, "StatusFeedbackInterval", c->trialInt);
                                cJSON_AddNumberToObject(json_input, "TimeoutNoTestTraffic", WARNING_NOTRAFFIC * MSECINSEC);
                                cJSON_AddNumberToObject(json_input, "TimeoutNoStatusMessage", WARNING_NOTRAFFIC * MSECINSEC);
                                cJSON_AddNumberToObject(json_input, "Tmax", WARNING_NOTRAFFIC * MSECINSEC);
                                cJSON_AddNumberToObject(json_input, "TmaxRTT", TIMEOUT_NOTRAFFIC * MSECINSEC);
                                cJSON_AddNumberToObject(json_input, "TimestampResolution", 1);
                                cJSON_AddNumberToObject(json_input, "SeqErrThresh", c->seqErrThresh);
                                cJSON_AddNumberToObject(json_input, "ReordDupIgnoreEnable", c->ignoreOooDup);
                                cJSON_AddNumberToObject(json_input, "LowerThresh", c->lowThresh);
                                cJSON_AddNumberToObject(json_input, "UpperThresh", c->upperThresh);
                                cJSON_AddNumberToObject(json_input, "HighSpeedDelta", c->highSpeedDelta);
                                cJSON_AddNumberToObject(json_input, "SlowAdjThresh", c->slowAdjThresh);
                                cJSON_AddNumberToObject(json_input, "HSpeedThresh", repo.hSpeedThresh * 1000000);
                                cJSON_AddStringToObject(json_input, "RateAdjAlgorithm", rateAdjAlgo[c->rateAdjAlgo]);
                                //
                                // Add input object to top-level object
                                //
                                cJSON_AddItemToObject(json_top, "Input", json_input);
                        }

                        //
                        // Create output object and add initial items
                        //
                        if (json_output == NULL) {
                                json_output = cJSON_CreateObject();
                        }
                        create_timestamp(&repo.systemClock);
                        cJSON_AddStringToObject(json_output, "BOMTime", scratch);
                        //
                        cJSON_AddNumberToObject(json_output, "TmaxUsed", WARNING_NOTRAFFIC * MSECINSEC);
                        cJSON_AddNumberToObject(json_output, "TestInterval", c->testIntTime);
                        cJSON_AddNumberToObject(json_output, "TmaxRTTUsed", TIMEOUT_NOTRAFFIC * MSECINSEC);
                        cJSON_AddNumberToObject(json_output, "TimestampResolutionUsed", 1);
                }
        }

        //
        // Clear timeout timer
        //
        tspecclear(&c->timer3Thresh);
        c->timer3Action = &null_action;

        //
        // Set end time (used as watchdog) in case server goes quiet
        //
        tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
        tspecvar.tv_nsec = 0;
        tspecplus(&repo.systemClock, &tspecvar, &c->endTime);

        return 0;
}
//----------------------------------------------------------------------------
//
// Socket mgmt function for socket-based connections
//
// Populate scratch buffer and return length on error
//
int sock_mgmt(int connindex, char *host, int port, char *ip, int action) {
        register struct connection *c = &conn[connindex];
        int i, var, fd;
        BOOL hostisaddr = FALSE;
        char addrstr[INET6_ADDR_STRLEN], portstr[8];
        struct addrinfo hints, *res = NULL, *ai;
        struct sockaddr_storage sas;

        //
        // Process/resolve address parameter
        //
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags    = AI_NUMERICSERV;
        hints.ai_family   = conf.addrFamily;
        hints.ai_socktype = SOCK_DGRAM;
        if (host == NULL) {
                hints.ai_flags |= AI_PASSIVE;
        } else if (*host == '\0') {
                host = NULL;
                hints.ai_flags |= AI_PASSIVE;
        } else {
                //
                // Use inet_pton() to prevent possibly unnecessary name lookup by getaddrinfo()
                //
                if (inet_pton(AF_INET, host, &sas) == 1) {
                        hostisaddr      = TRUE;
                        hints.ai_family = AF_INET;
                        hints.ai_flags |= AI_NUMERICHOST;
                } else if (inet_pton(AF_INET6, host, &sas) == 1) {
                        // IPv6 link-local addresses may require a Zone/Scope ID suffix ('%<interface>')
                        hostisaddr      = TRUE;
                        hints.ai_family = AF_INET6;
                        hints.ai_flags |= AI_NUMERICHOST;
                }
        }
        snprintf(portstr, sizeof(portstr), "%d", port);

        //
        // Obtain address info/resolve name if needed
        //
        if ((i = getaddrinfo(host, portstr, &hints, &res)) != 0) {
                var = sprintf(scratch, "GETADDRINFO ERROR: %s (%s)\n", strerror(errno), (const char *) gai_strerror(i));
                return var;
        }

        //
        // Check specified address against address family (if also specified), else output name resolution details
        //
        if (action == SMA_LOOKUP && host != NULL) {
                if (hostisaddr) {
                        if (conf.addrFamily != AF_UNSPEC && conf.addrFamily != hints.ai_family) {
                                var = sprintf(scratch, "ERROR: Specified IP address does not match address family\n");
                                return var;
                        }
                } else if (monConn >= 0) {
                        var = sprintf(scratch, "%s =", host);
                        for (ai = res; ai != NULL; ai = ai->ai_next) {
                                getnameinfo(ai->ai_addr, ai->ai_addrlen, addrstr, INET6_ADDR_STRLEN, NULL, 0, NI_NUMERICHOST);
                                var += sprintf(&scratch[var], " %s", addrstr);
                        }
                        var += sprintf(&scratch[var], "\n");
                        send_proc(monConn, scratch, var);
                }
        }

        //
        // Process address info based on action (prefer returned order of addresses)
        //
        if (host == NULL)
                host = "<any>";
        var = sprintf(scratch, "ERROR: Socket mgmt, action %d failure for %s:%d\n", action, host, port);
        for (ai = res; ai != NULL; ai = ai->ai_next) {
                if (action == SMA_LOOKUP) {
                        //
                        // If address family not specified, set it to match for subsequent calls
                        //
                        if (conf.addrFamily == AF_UNSPEC)
                                conf.addrFamily = ai->ai_family;
                        //
                        // Save IP address to designated location
                        //
                        getnameinfo((struct sockaddr *) ai->ai_addr, ai->ai_addrlen, ip, INET6_ADDR_STRLEN, NULL, 0,
                                    NI_NUMERICHOST);
                        var = 0;
                        break;
                } else if (action == SMA_BIND) {
                        //
                        // Special case for server when no bind address (or address family) is specified. Continue
                        // to next ai if it is INET6 but this one isn't, so server supports both by default.
                        //
                        if (repo.isServer && repo.serverName == NULL && ai->ai_next != NULL) {
                                if (ai->ai_family != AF_INET6 && ai->ai_next->ai_family == AF_INET6)
                                        continue;
                        }
                        //
                        // Obtain socket, restrict to INET6 if needed, and bind
                        //
                        if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
                                var = sprintf(scratch, "SOCKET ERROR: %s (%s:%d)\n", strerror(errno), host, port);
                                continue;
                        }
                        if (conf.ipv6Only) {
                                i = 1;
                                if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const void *) &i, sizeof(i)) == -1) {
                                        var = sprintf(scratch, "IPV6_V6ONLY ERROR: %s\n", strerror(errno));
                                        close(fd);
                                        continue;
                                }
                        }
                        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
                                var = sprintf(scratch, "BIND ERROR: %s (%s:%d)\n", strerror(errno), host, port);
                                if (errno == EINVAL && ai->ai_family == AF_INET6) {
                                        var += sprintf(&scratch[var], "%s\n",
                                                       "HINT: Address may require a Zone/Scope ID suffix (e.g., '%eth1')");
                                }
                                close(fd);
                                continue;
                        }
                        c->fd      = fd;
                        c->subType = SOCK_DGRAM;
                        c->state   = S_BOUND;
                        var        = 0;
                        break;
                } else if (action == SMA_UPDATE) {
                        //
                        // Update address info for subsequent operations
                        //
                        memcpy(&repo.remSas, ai->ai_addr, ai->ai_addrlen);
                        repo.remSasLen = ai->ai_addrlen;
                        var            = 0;
                        break;
                }
        }
        if (res != NULL)
                freeaddrinfo(res);

        return var;
}
//----------------------------------------------------------------------------
//
// Obtain and initialize a new connection structure
//
int new_conn(int activefd, char *host, int port, int type, int (*priaction)(int), int (*secaction)(int)) {
        int i, var, fd, sndbuf, rcvbuf;
        struct sockaddr_storage sas;
        char portstr[8];
#ifdef __linux__
        struct epoll_event epevent;
#endif

        //
        // Find available connection within connection array
        //
        fd = activefd;
        for (i = 0; i < MAX_CONNECTIONS; i++) {
                if (conn[i].fd == -1) {
                        conn[i].fd        = fd;        // Save initial descriptor
                        conn[i].type      = type;      // Set connection type
                        conn[i].state     = S_CREATED; // Set connection state
                        conn[i].priAction = priaction; // Set primary action routine
                        conn[i].secAction = secaction; // Set secondary action routine
                        break;
                }
        }
        if (i == MAX_CONNECTIONS) {
                var = sprintf(scratch, "ERROR: Max connections exceeded\n");
                send_proc(errConn, scratch, var);
                return -1;
        }
        if (i > repo.maxConnIndex)
                repo.maxConnIndex = i;

        //
        // Perform socket creation and bind
        //
        if (type == T_UDP) {
                if ((var = sock_mgmt(i, host, port, NULL, SMA_BIND)) != 0) {
                        send_proc(errConn, scratch, var);
                        init_conn(i, TRUE);
                        return -1;
                }
                fd = conn[i].fd; // Update local descriptor
        }

        //
        // Set FD as non-blocking
        // Console FD (i.e., stdin) gets setup in main()
        //
        if (type != T_CONSOLE) {
                var = fcntl(fd, F_GETFL, 0);
                if (fcntl(fd, F_SETFL, var | O_NONBLOCK) != 0) {
                        var = sprintf(scratch, "[%d]F_SETFL ERROR: %s\n", i, strerror(errno));
                        send_proc(errConn, scratch, var);
                        init_conn(i, TRUE);
                        return -1;
                }
        }
#ifdef __linux__
        //
        // Add fd for epoll read operations (exclude console when command line not supported)
        //
        if ((type != T_LOG) && (type != T_NULL) && (type != T_CONSOLE)) {
                epevent.events   = EPOLLIN;
                epevent.data.u32 = (uint32_t) i;
                if (epoll_ctl(repo.epollFD, EPOLL_CTL_ADD, fd, &epevent) != 0) {
                        var = sprintf(scratch, "[%d]EPOLL_CTL ERROR: %s\n", i, strerror(errno));
                        send_proc(errConn, scratch, var);
                        init_conn(i, TRUE);
                        return -1;
                }
        }
#endif
        //
        // Return if FD already existed
        //
        if (activefd != -1)
                return i;

        //
        // Set address reuse
        //
        if (type == T_UDP) {
                var = 1;
                if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *) &var, sizeof(var)) < 0) {
                        var = sprintf(scratch, "[%d]SET SO_REUSEADDR ERROR: %s\n", i, strerror(errno));
                        send_proc(errConn, scratch, var);
                        init_conn(i, TRUE);
                        return -1;
                }
        }

        //
        // Change buffering if specified
        //
        if (type == T_UDP) {
                //
                // Set socket buffers
                //
                if (conf.sockSndBuf != 0 && conf.sockRcvBuf != 0) {
                        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const void *) &conf.sockSndBuf, sizeof(conf.sockSndBuf)) < 0) {
                                var = sprintf(scratch, "[%d]SET SO_SNDBUF ERROR: %s\n", i, strerror(errno));
                                send_proc(errConn, scratch, var);
                                init_conn(i, TRUE);
                                return -1;
                        }
                        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const void *) &conf.sockRcvBuf, sizeof(conf.sockRcvBuf)) < 0) {
                                var = sprintf(scratch, "[%d]SET SO_RCVBUF ERROR: %s\n", i, strerror(errno));
                                send_proc(errConn, scratch, var);
                                init_conn(i, TRUE);
                                return -1;
                        }
                }
                //
                // Get buffer values
                //
                if (monConn >= 0) {
                        sndbuf = 0;
                        var    = sizeof(sndbuf);
                        if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *) &sndbuf, (socklen_t *) &var) < 0) {
                                var = sprintf(scratch, "[%d]GET SO_SNDBUF ERROR: %s\n", i, strerror(errno));
                                send_proc(errConn, scratch, var);
                                init_conn(i, TRUE);
                                return -1;
                        }
                        rcvbuf = 0;
                        var    = sizeof(rcvbuf);
                        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &rcvbuf, (socklen_t *) &var) < 0) {
                                var = sprintf(scratch, "[%d]GET SO_RCVBUF ERROR: %s\n", i, strerror(errno));
                                send_proc(errConn, scratch, var);
                                init_conn(i, TRUE);
                                return -1;
                        }
                        var = sprintf(scratch, "[%d]Socket created with SO_SNDBUF/SO_RCVBUF of %d/%d\n", i, sndbuf, rcvbuf);
                        send_proc(monConn, scratch, var);
                }
        }

        //
        // Obtain local IP address and port number
        //
        var = sizeof(sas);
        if (getsockname(fd, (struct sockaddr *) &sas, (socklen_t *) &var) < 0) {
                var = sprintf(scratch, "[%d]GETSOCKNAME ERROR: %s\n", i, strerror(errno));
                send_proc(errConn, scratch, var);
                init_conn(i, TRUE);
                return -1;
        }
        getnameinfo((struct sockaddr *) &sas, var, conn[i].locAddr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        conn[i].locPort = atoi(portstr);

        //
        // Finish processing by setting to data state
        //
        conn[i].state = S_DATA;

        return i;
}
//----------------------------------------------------------------------------
//
// Initiate a socket connect
//
int sock_connect(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;

        //
        // Issue connect
        //
        if (connect(c->fd, (struct sockaddr *) &repo.remSas, repo.remSasLen) == -1) {
                //
                // Connect error (immediate completion expected with SOCK_DGRAM)
                //
                var = sprintf(scratch, "[%d]CONNECT ERROR: %s\n", connindex, strerror(errno));
                send_proc(errConn, scratch, var);
                return -1;
        }
        c->state     = S_DATA;
        c->connected = TRUE;

        //
        // Call connect completion handler directly
        //
        return connected(connindex);
}
//----------------------------------------------------------------------------
//
// Socket connect completion handler
//
int connected(int connindex) {
        register struct connection *c = &conn[connindex];
        char *p;
        int var;
        char portstr[8];
        struct sockaddr_storage sas;

        //
        // Initialize post-connect action routines
        //
        c->priAction = &recv_proc;
        c->secAction = &null_action;

        //
        // Update local IP address and port number
        //
        var = sizeof(sas);
        if (getsockname(c->fd, (struct sockaddr *) &sas, (socklen_t *) &var) < 0) {
                var = sprintf(scratch, "[%d]GETSOCKNAME ERROR: %s\n", connindex, strerror(errno));
                send_proc(errConn, scratch, var);
                return -1;
        }
        getnameinfo((struct sockaddr *) &sas, var, c->locAddr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        c->locPort = atoi(portstr);

        //
        // Obtain remote IP address and port number
        //
        var = sizeof(sas);
        if (getpeername(c->fd, (struct sockaddr *) &sas, (socklen_t *) &var) < 0) {
                var = sprintf(scratch, "[%d]GETPEERNAME ERROR: %s\n", connindex, strerror(errno));
                send_proc(errConn, scratch, var);
                return -1;
        }
        getnameinfo((struct sockaddr *) &sas, var, c->remAddr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        c->remPort = atoi(portstr);

        //
        // Check if peer is IPv6 (i.e., not an IPv4 [x.x.x.x] or IPv4-mapped address [::ffff:x.x.x.x])
        //
        var = 0;
        for (p = c->remAddr; *p; p++) {
                if (*p == '.')
                        var++;
        }
        if (var != 3) {
                c->ipProtocol = IPPROTO_IPV6;
        } else {
                c->ipProtocol = IPPROTO_IP;
        }
        return 0;
}
//----------------------------------------------------------------------------
