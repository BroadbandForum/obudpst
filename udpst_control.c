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

//----------------------------------------------------------------------------
//
// Global data
//
#define SRAUTO_TEXT "<Auto>"
#define TESTHDR_LINE1 \
        "%s%s Test Interval(sec): %d, DelayVar Thresholds(ms): %d-%d [%s], Trial Interval(ms): %d, Ignore OoO/Dup: %s,\n"
#define TESTHDR_LINE2 "    SendingRate Index: %s, Congestion Threshold: %d, High-Speed Delta: %d, SeqError Threshold: %d, "
static char *testHdrV4 = TESTHDR_LINE1 TESTHDR_LINE2 "IPv4 ToS: %d\n";
static char *testHdrV6 = TESTHDR_LINE1 TESTHDR_LINE2 "IPv6 TClass: %d\n";

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
        char addrstr[INET6_ADDRSTRLEN], portstr[8];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
#ifdef AUTH_KEY_ENABLE
        unsigned int uvar;
#endif

        //
        // Build setup request PDU
        //
        memset(cHdrSR, 0, sizeof(struct controlHdrSR));
        cHdrSR->controlId   = htons(CHSR_ID);
        cHdrSR->protocolVer = htons(PROTOCOL_VER);
        cHdrSR->cmdRequest  = CHSR_CREQ_SETUPREQ;
        cHdrSR->cmdResponse = CHSR_CRSP_NONE;
        cHdrSR->jumboStatus = (uint8_t) conf.jumboStatus;
        if (*conf.authKey == '\0') {
                cHdrSR->authMode     = AUTHMODE_NONE;
                cHdrSR->authUnixTime = 0;
#ifdef AUTH_KEY_ENABLE
        } else {
                cHdrSR->authMode     = AUTHMODE_SHA256;
                cHdrSR->authUnixTime = htonl((uint32_t) repo.systemClock.tv_sec);
                HMAC(EVP_sha256(), conf.authKey, strlen(conf.authKey), (const unsigned char *) cHdrSR, sizeof(struct controlHdrSR),
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
        var = sizeof(struct controlHdrSR);
        if (send_proc(connindex, (char *) cHdrSR, var) != var)
                return -1;
        if (monConn >= 0) {
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
        int i, var;
        struct timespec tspecvar;
        char addrstr[INET6_ADDRSTRLEN], portstr[8];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
#ifdef AUTH_KEY_ENABLE
        unsigned int uvar;
        unsigned char digest1[AUTH_DIGEST_LENGTH], digest2[AUTH_DIGEST_LENGTH];
#endif
        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) sizeof(struct controlHdrSR) || ntohs(cHdrSR->controlId) != CHSR_ID) {
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
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
                            NI_NUMERICHOST | NI_NUMERICSERV);
        }
        if ((i = (int) ntohs(cHdrSR->protocolVer)) != PROTOCOL_VER) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Invalid version (%d) in setup request from %s:%s\n", connindex, i, addrstr,
                                      portstr);
                }
                cHdrSR->protocolVer = htons(PROTOCOL_VER); // Send back expected version
                cHdrSR->cmdResponse = CHSR_CRSP_BADVER;

        } else if ((cHdrSR->jumboStatus != TRUE && cHdrSR->jumboStatus != FALSE) || // Enforce C boolean
                   cHdrSR->jumboStatus != (uint8_t) conf.jumboStatus) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Invalid jumbo datagram option in setup request from %s:%s\n", connindex,
                                      addrstr, portstr);
                }
                cHdrSR->cmdResponse = CHSR_CRSP_BADJS;

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
                HMAC(EVP_sha256(), conf.authKey, strlen(conf.authKey), (const unsigned char *) cHdrSR, sizeof(struct controlHdrSR),
                     digest2, &uvar);
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
                var = sizeof(struct controlHdrSR);
                send_proc(connindex, (char *) cHdrSR, var);
                return 0;
        }
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Setup request received from %s:%s\n", connindex, addrstr, portstr);
                send_proc(monConn, scratch, var);
        }

        //
        // Obtain new test connection for this client
        //
        if ((i = new_conn(-1, repo.serverIp, 0, T_UDP, &recv_proc, &service_actreq)) < 0)
                return 0;
        if (monConn >= 0) {
                var = sprintf(scratch, "[%d]Connection %d allocated and assigned %s:%d\n", connindex, i, conn[i].locAddr,
                              conn[i].locPort);
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
        var                 = sizeof(struct controlHdrSR);
        if (send_proc(connindex, (char *) cHdrSR, var) != var)
                return 0;
        if (monConn >= 0) {
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
        char addrstr[INET6_ADDRSTRLEN], portstr[8];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) sizeof(struct controlHdrSR) || ntohs(cHdrSR->controlId) != CHSR_ID) {
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
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
        memset(cHdrTA, 0, sizeof(struct controlHdrTA));
        cHdrTA->controlId   = htons(CHTA_ID);
        cHdrTA->protocolVer = htons(PROTOCOL_VER);
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

        //
        // Send test activation request
        //
        c->secAction = &service_actresp; // Set service handler for response
        var          = sizeof(struct controlHdrTA);
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
        char *testhdr, *testtype, connid[8], delusage[8], sritext[8];
        char addrstr[INET6_ADDRSTRLEN], portstr[8];
        struct sendingRate *sr = repo.sendingRates; // Set to first row of table
        struct timespec tspecvar;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) sizeof(struct controlHdrTA) || ntohs(cHdrTA->controlId) != CHTA_ID ||
            ntohs(cHdrTA->protocolVer) != PROTOCOL_VER) {
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
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
        // Static sending rate index (special case <Auto>, which is the default but greater than max)
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
        // If upstream test, send back sending rate parameters from first row of table
        //
        if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                sr_copy(sr, &cHdrTA->srStruct, TRUE);
        } else {
                memset(&cHdrTA->srStruct, 0, sizeof(struct sendingRate));
        }
        cHdrTA->cmdResponse = CHTA_CRSP_ACKOK; // Indicate request accepted
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
        var = sizeof(struct controlHdrTA);
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
        if (!conf.JSONsummary) {
                if (conf.verbose)
                        sprintf(connid, "[%d]", connindex);
                if (!repo.isServer || conf.verbose) {
                        if (c->ipProtocol == IPPROTO_IPV6)
                                testhdr = testHdrV6;
                        else
                                testhdr = testHdrV4;
                        if (c->useOwDelVar)
                                strcpy(delusage, "OWD");
                        else
                                strcpy(delusage, "RTT");
                        if (c->srIndexConf == DEF_SRINDEX_CONF)
                                strcpy(sritext, SRAUTO_TEXT);
                        else
                                sprintf(sritext, "%d", c->srIndexConf);
                        var =
                        sprintf(scratch, testhdr, connid, testtype, c->testIntTime, c->lowThresh, c->upperThresh, delusage, c->trialInt,
                                boolText[c->ignoreOooDup], sritext, c->slowAdjThresh, c->highSpeedDelta, c->seqErrThresh, c->ipTosByte);
                        send_proc(errConn, scratch, var);
                }
        } else {
                // RODT - Think about adding test config to JSON object 
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
        int var;
        char *testhdr, *testtype, connid[8], delusage[8], sritext[8];
        struct sendingRate *sr = &c->srStruct; // Set to connection structure
        struct timespec tspecvar;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) sizeof(struct controlHdrTA) || ntohs(cHdrTA->controlId) != CHTA_ID ||
            ntohs(cHdrTA->protocolVer) != PROTOCOL_VER) {
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
        if (!conf.JSONsummary) {
                if (conf.verbose)
                        sprintf(connid, "[%d]", connindex);
                if (!repo.isServer || conf.verbose) {
                        if (c->ipProtocol == IPPROTO_IPV6)
                                testhdr = testHdrV6;
                        else
                                testhdr = testHdrV4;
                        if (c->useOwDelVar)
                                strcpy(delusage, "OWD");
                        else
                                strcpy(delusage, "RTT");
                        if (c->srIndexConf == DEF_SRINDEX_CONF)
                                strcpy(sritext, SRAUTO_TEXT);
                        else
                                sprintf(sritext, "%d", c->srIndexConf);
                        var =
                        sprintf(scratch, testhdr, connid, testtype, c->testIntTime, c->lowThresh, c->upperThresh, delusage, c->trialInt,
                                boolText[c->ignoreOooDup], sritext, c->slowAdjThresh, c->highSpeedDelta, c->seqErrThresh, c->ipTosByte);
                        send_proc(errConn, scratch, var);
                }
        } else {
                // RODT - Write the config to JSON object?
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
        char addrstr[INET6_ADDRSTRLEN], portstr[8];
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
                                getnameinfo(ai->ai_addr, ai->ai_addrlen, addrstr, INET6_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST);
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
                        getnameinfo((struct sockaddr *) ai->ai_addr, ai->ai_addrlen, ip, INET6_ADDRSTRLEN, NULL, 0, NI_NUMERICHOST);
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
        getnameinfo((struct sockaddr *) &sas, var, conn[i].locAddr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
        getnameinfo((struct sockaddr *) &sas, var, c->locAddr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
        getnameinfo((struct sockaddr *) &sas, var, c->remAddr, INET6_ADDRSTRLEN, portstr, sizeof(portstr),
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
