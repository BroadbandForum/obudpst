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
 * Len Ciavattone          01/01/2023    Add timer to prevent client from
 *                                       processing rogue load PDUs forever
 * Len Ciavattone          01/14/2023    Add multi-connection support
 * Len Ciavattone          02/07/2023    Randomize start of send intervals
 * Len Ciavattone          02/14/2023    Add per-server port selection
 * Len Ciavattone          03/05/2023    Fix server setup error messages
 * Len Ciavattone          03/22/2023    Add GSO and GRO optimizations
 * Len Ciavattone          03/25/2023    GRO replaced w/recvmmsg+truncation
 * Len Ciavattone          05/24/2023    Add data output (export) capability
 * Len Ciavattone          10/01/2023    Updated ErrorStatus values
 * Len Ciavattone          12/18/2023    Add server msg for invalid setup req
 * Len Ciavattone          02/23/2024    Add status feedback loss to export
 * Len Ciavattone          03/03/2024    Add multi-key support
 * Len Ciavattone          04/12/2024    Enhanced control PDU integrity checks
 * Len Ciavattone          06/24/2024    Add interface Mbps to export
 * Len Ciavattone          07/02/2024    Preset dir for bw dealloc on timeout
 * Len Ciavattone          09/15/2025    Add RFC compatibility, performance
 *                                       statistics, and improved idling
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
#include <time.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/file.h>
#ifdef AUTH_KEY_ENABLE
#include <openssl/hmac.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h> // Added for OSSL_KDF_PARAM_*
#include <openssl/params.h>     // Added for OSSL_PARAM
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
int open_outputfile(int);
void insert_auth(int, unsigned char *, unsigned char *, unsigned char *, size_t);
int validate_auth(int, unsigned char *, unsigned char *, unsigned char *, unsigned char *, size_t);
BOOL verify_ctrlpdu(int, struct controlHdrSR *, struct controlHdrTA *, char *, char *);
#ifdef __linux__
int kdf_hmac_sha256(char *, uint32_t, unsigned char *, unsigned char *);
#endif

//----------------------------------------------------------------------------
//
// External data
//
extern int errConn, monConn, aggConn;
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
#define TESTHDR_LINE                                                                                                     \
        "%s%s Test Int(sec): %d, DelayVar Thresh(ms): %d-%d [%s], Trial Int(ms): %d, Ignore OoO/Dup: %s, Payload: %s,\n" \
        "  ID: %d, SR Index: %s, Cong. Thresh: %d, HS Delta: %d, SeqErr Thresh: %d, Algo: %s, Conn: %d, DSCP+ECN: %d%s\n"

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
                if (c->outputFPtr != NULL)
                        fclose(c->outputFPtr);
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
int send_setupreq(int connindex, int mcIndex, int serverIndex) {
        register struct connection *c = &conn[connindex], *a;
        int i, fd, var;
        struct timespec tspecvar;
        char addrstr[INET6_ADDR_STRLEN], portstr[8], intfpath[IFNAMSIZ + 64];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
#ifdef AUTH_KEY_ENABLE
        char *key;
#endif

        //
        // Additional initialization on first setup request
        //
        if (c->mcIndex == 0) {
                //
                // Open local sysfs interface statistics
                //
                if (*conf.intfName) {
                        for (i = 0; i < 2; i++) {
                                var = sprintf(intfpath, "/sys/class/net/%s/statistics/", conf.intfName);
                                if ((conf.usTesting && i == 0) || (conf.dsTesting && i == 1))
                                        strcat(&intfpath[var], "tx_bytes");
                                else
                                        strcat(&intfpath[var], "rx_bytes");
                                if ((fd = open(intfpath, O_RDONLY)) < 0) {
                                        var = sprintf(scratch, "OPEN ERROR: %s (%s)\n", strerror(errno), intfpath);
                                        send_proc(errConn, scratch, var);
                                        return -1;
                                }
                                if (i == 0)
                                        repo.intfFD = fd;
                                else
                                        repo.intfFDAlt = fd;
                        }
                }
                //
                // Init aggregate connection and aggregate query timer
                //
                a = &conn[aggConn]; // Aggregate connection pointer
                if (conf.usTesting) {
                        a->testType = TEST_TYPE_US;
                } else {
                        a->testType = TEST_TYPE_DS;
                }
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = AGG_QUERY_TIME * NSECINMSEC;
                tspecplus(&repo.systemClock, &tspecvar, &a->timer1Thresh);
                a->timer1Action = &agg_query_proc;
                a->state        = S_DATA; // Allow for data timer processing
        }
        repo.actConnCount++; // Increment active test connection count

        //
        // Build setup request PDU
        //
        memset(cHdrSR, 0, CHSR_SIZE_CVER);
        cHdrSR->pduId       = htons(CHSR_ID);
        c->protocolVer      = PROTOCOL_VER; // Client always uses current version
        cHdrSR->protocolVer = htons((uint16_t) c->protocolVer);
        c->mcIndex          = mcIndex; // Multi-connection index of this connection
        cHdrSR->mcIndex     = (uint8_t) c->mcIndex;
        c->mcCount          = conf.maxConnCount; // Configured maximum multi-connection count
        cHdrSR->mcCount     = (uint8_t) c->mcCount;
        if (repo.mcIdent == 0) {
                repo.mcIdent = getuniform(1, UINT16_MAX); // Random (non-zero) multi-connection identifier
        }
        c->mcIdent          = repo.mcIdent;
        cHdrSR->mcIdent     = htons((uint16_t) c->mcIdent);
        cHdrSR->cmdRequest  = CHSR_CREQ_SETUPREQ;
        cHdrSR->cmdResponse = CHSR_CRSP_NONE;
        if (conf.maxBandwidth > 0) {
                // Each connection requests 1/Nth the total bandwidth
                if ((c->maxBandwidth = conf.maxBandwidth / conf.maxConnCount) < 1)
                        c->maxBandwidth = 1;
                var = c->maxBandwidth;
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
        //
        // Insert authentication if configured
        //
#ifdef AUTH_KEY_ENABLE
        if (*conf.authKey != '\0' || conf.keyFile != NULL) {
                c->authMode      = AUTHMODE_1;
                cHdrSR->authMode = (uint8_t) c->authMode;
                if (*conf.authKey != '\0') {
                        key = conf.authKey;
                } else {
                        key = repo.key[repo.keyIndex].key;
                }
                // Create KDF keys via shared key and timestamp (identical authUnixTime values
                // must be used for KDF and initial PDU to server)
                kdf_hmac_sha256(key, repo.systemClock.tv_sec, c->clientKey, c->serverKey);
                insert_auth(conf.keyId, c->clientKey, (unsigned char *) &cHdrSR->authMode, (unsigned char *) cHdrSR,
                            CHSR_SIZE_CVER);
        }
#endif
#ifdef ADD_HEADER_CSUM
        cHdrSR->checkSum = checksum(cHdrSR, CHSR_SIZE_CVER);
#endif

        //
        // Update global address info for subsequent send
        //
        c->serverIndex = serverIndex;
        if ((var = sock_mgmt(connindex, repo.server[serverIndex].ip, repo.server[serverIndex].port, NULL, SMA_UPDATE)) != 0) {
                send_proc(errConn, scratch, var);
                return -1;
        }

        //
        // Send setup request PDU (socket not yet connected)
        //
        if (send_proc(connindex, (char *) cHdrSR, CHSR_SIZE_CVER) != CHSR_SIZE_CVER)
                return -1;
        if (conf.verbose) {
                getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                var = sprintf(scratch, "[%d]Setup request (%d.%d) sent from %s:%d to %s:%s\n", connindex, c->mcIndex, c->mcIdent,
                              c->locAddr, c->locPort, addrstr, portstr);
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
        var = sprintf(scratch, "WARNING: Timeout awaiting response from server %s:%d\n", repo.server[c->serverIndex].ip,
                      repo.server[c->serverIndex].port);
        send_proc(errConn, scratch, var);
        repo.endTimeStatus = STATUS_WARNBASE + WARN_SRV_TIMEOUT; // ErrorStatus
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
        int i = -1, var, pver, mbw = 0, currbw = repo.dsBandwidth, errmsg;
        BOOL usbw = FALSE;
        struct timespec tspecvar;
        char addrstr[INET6_ADDR_STRLEN], portstr[8];
        struct controlHdrSR *cHdrSR        = (struct controlHdrSR *) repo.defBuffer;
        struct controlHdrNR *cHdrNR        = (struct controlHdrNR *) repo.defBuffer;
        struct perfStatsCounters *psC      = &repo.psCounters;
        unsigned char ckey[SHA256_KEY_LEN] = {0}, skey[SHA256_KEY_LEN] = {0}; // Must be initialized to zero

        //
        // Verify PDU
        //
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (!verify_ctrlpdu(connindex, cHdrSR, NULL, addrstr, portstr)) {
                return 0; // Ignore bad PDU
        }
        psC->setupRequestCnt++;

        //
        // Validate authentication if included and configured
        //
        errmsg = 0;
        pver   = (int) ntohs(cHdrSR->protocolVer);
        if (cHdrSR->authMode == AUTHMODE_1 && (*conf.authKey != '\0' || conf.keyFile != NULL)) {
                i = validate_auth(pver, ckey, skey, (unsigned char *) &cHdrSR->authMode, (unsigned char *) cHdrSR,
                                  (size_t) repo.rcvDataSize);
                if (i > 0) {
                        errmsg              = sprintf(scratch, "ERROR: Authentication failure of setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_AUTHFAIL;
                        psC->ctrlAuthFailure++;
                } else if (i < 0) {
                        errmsg              = sprintf(scratch, "ERROR: Authentication time invalid in setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_AUTHTIME;
                        psC->ctrlBadAuthTime++;
                }
        }

        //
        // Check specifics of setup request from client
        //
        mbw = (int) (ntohs(cHdrSR->maxBandwidth) & ~CHSR_USDIR_BIT); // Obtain max bandwidth while ignoring upstream bit
        if (errmsg == 0) {
                if (ntohs(cHdrSR->maxBandwidth) & CHSR_USDIR_BIT) {
                        usbw   = TRUE; // Max bandwidth is for upstream
                        currbw = repo.usBandwidth;
                }
                if (pver < PROTOCOL_MIN || pver > PROTOCOL_VER) {
                        errmsg              = sprintf(scratch, "ERROR: Invalid version (%d) in setup request from", pver);
                        cHdrSR->protocolVer = htons(PROTOCOL_VER); // Send back expected version
                        cHdrSR->cmdResponse = CHSR_CRSP_BADVER;
                        psC->invalidProtocolVer++;

                } else if (cHdrSR->mcCount == 0 || cHdrSR->mcCount > MAX_MC_COUNT || cHdrSR->mcIndex >= cHdrSR->mcCount) {
                        errmsg = sprintf(scratch, "ERROR: Invalid multi-connection parameters (%d,%d) in setup request from",
                                         cHdrSR->mcIndex, cHdrSR->mcCount);
                        cHdrSR->cmdResponse = CHSR_CRSP_MCINVPAR;
                        psC->invalidSetupOption++;

                } else if (((cHdrSR->modifierBitmap & CHSR_JUMBO_STATUS) && !conf.jumboStatus) ||
                           (!(cHdrSR->modifierBitmap & CHSR_JUMBO_STATUS) && conf.jumboStatus)) {
                        errmsg              = sprintf(scratch, "ERROR: Invalid jumbo datagram option in setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_BADJS;
                        psC->invalidSetupOption++;

                } else if (((cHdrSR->modifierBitmap & CHSR_TRADITIONAL_MTU) && !conf.traditionalMTU) ||
                           (!(cHdrSR->modifierBitmap & CHSR_TRADITIONAL_MTU) && conf.traditionalMTU)) {
                        errmsg              = sprintf(scratch, "ERROR: Invalid traditional MTU option in setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_BADTMTU;
                        psC->invalidSetupOption++;

                } else if (conf.maxBandwidth > 0 && mbw == 0) {
                        errmsg              = sprintf(scratch, "ERROR: Required bandwidth not specified in setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_NOMAXBW;
                        psC->invalidSetupOption++;

                } else if (conf.maxBandwidth > 0 && currbw + mbw > conf.maxBandwidth) {
                        errmsg =
                            sprintf(scratch, "ERROR: Capacity exceeded (%d.%d) by required bandwidth (%d) in setup request from",
                                    cHdrSR->mcIndex, (int) ntohs(cHdrSR->mcIdent), mbw);
                        cHdrSR->cmdResponse = CHSR_CRSP_CAPEXC;
                        psC->bandwidthExceeded++;

                } else if (cHdrSR->authMode != AUTHMODE_0 && *conf.authKey == '\0' && conf.keyFile == NULL) {
                        errmsg              = sprintf(scratch, "ERROR: Unexpected authentication in setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_AUTHNC;
                        psC->invalidSetupOption++;
#ifndef AUTH_IS_OPTIONAL
                } else if (cHdrSR->authMode == AUTHMODE_0 && (*conf.authKey != '\0' || conf.keyFile != NULL)) {
                        errmsg              = sprintf(scratch, "ERROR: Authentication missing in setup request from");
                        cHdrSR->cmdResponse = CHSR_CRSP_AUTHREQ;
                        psC->invalidSetupOption++;
#endif
                } else if (cHdrSR->authMode > AUTHMODE_1) {
                        errmsg =
                            sprintf(scratch, "ERROR: Invalid authentication mode (%u) in setup request from", cHdrSR->authMode);
                        cHdrSR->cmdResponse = CHSR_CRSP_AUTHINV;
                        psC->invalidSetupOption++;
                }
        }
        if (cHdrSR->cmdResponse == CHSR_CRSP_NONE) {
                if (conf.verbose) {
                        var = sprintf(scratch, "[%d]Setup request (%d.%d, Ver: %d, MaxBW: %d, KeyID: %d) received from %s:%s\n",
                                      connindex, (int) cHdrSR->mcIndex, (int) ntohs(cHdrSR->mcIdent), pver, mbw,
                                      (int) cHdrSR->keyId, addrstr, portstr);
                        send_proc(monConn, scratch, var);
                }
                //
                // Obtain new test connection for this client
                //
                if ((i = new_conn(-1, repo.server[0].ip, 0, T_UDP, &recv_proc, &service_actreq)) < 0) {
                        errmsg              = 0; // Error message already output as part of allocation failure
                        cHdrSR->cmdResponse = CHSR_CRSP_CONNFAIL;
                        psC->connCreateFail++;
                }
        }
        cHdrSR->cmdRequest = CHSR_CREQ_SETUPRSP; // Convert setup request to setup response
        if (cHdrSR->cmdResponse != CHSR_CRSP_NONE) {
                //
                // Output error message if needed (append source info), send back setup response, and exit
                //
                if (errmsg > 0) {
                        errmsg += sprintf(&scratch[errmsg], " %s:%s\n", addrstr, portstr);
                        send_proc(errConn, scratch, errmsg);
                }
                if (pver >= EXTAUTH_PVER) {
                        insert_auth((int) cHdrSR->keyId, skey, (unsigned char *) &cHdrSR->authMode, (unsigned char *) cHdrSR,
                                    (size_t) repo.rcvDataSize);
                }
                cHdrSR->checkSum = 0;
#ifdef ADD_HEADER_CSUM
                cHdrSR->checkSum = checksum(cHdrSR, repo.rcvDataSize);
#endif
                psC->setupRejectCnt++;
                send_proc(connindex, (char *) cHdrSR, repo.rcvDataSize);
                return 0;
        }

        //
        // Initialize new test connection obtained above as 'i'
        //
        if (pver < PROTOCOL_VER)
                psC->legacyProtocolVer++;
        conn[i].protocolVer = pver;
        conn[i].mcIndex     = (int) cHdrSR->mcIndex;
        conn[i].mcCount     = (int) cHdrSR->mcCount;
        conn[i].mcIdent     = (int) ntohs(cHdrSR->mcIdent);
        if (conf.maxBandwidth > 0) {
                conn[i].maxBandwidth = mbw; // Save bandwidth for adjustment at end of test
                if (usbw) {
                        conn[i].testType = TEST_TYPE_US; // Preset direction to allow for bandwidth deallocation on timeout
                        repo.usBandwidth += mbw;         // Update current upstream bandwidth
                } else {
                        conn[i].testType = TEST_TYPE_DS; // Preset direction to allow for bandwidth deallocation on timeout
                        repo.dsBandwidth += mbw;         // Update current downstream bandwidth
                }
                if (conf.verbose && mbw > 0) {
                        var = sprintf(scratch, "[%d]Bandwidth of %d allocated (New USBW: %d, DSBW: %d)\n", i, mbw, repo.usBandwidth,
                                      repo.dsBandwidth);
                        send_proc(monConn, scratch, var);
                }
        }
        conn[i].authMode = (int) cHdrSR->authMode;
        memcpy(conn[i].clientKey, ckey, SHA256_KEY_LEN);
        memcpy(conn[i].serverKey, skey, SHA256_KEY_LEN);

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
        if (pver >= EXTAUTH_PVER) {
                insert_auth((int) cHdrSR->keyId, skey, (unsigned char *) &cHdrSR->authMode, (unsigned char *) cHdrSR,
                            (size_t) repo.rcvDataSize);
        }
        cHdrSR->checkSum = 0;
#ifdef ADD_HEADER_CSUM
        cHdrSR->checkSum = checksum(cHdrSR, repo.rcvDataSize);
#endif
        psC->setupAcceptCnt++;
        if (send_proc(connindex, (char *) cHdrSR, repo.rcvDataSize) != repo.rcvDataSize)
                return 0;
        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Setup response (%d.%d) sent from %s:%d to %s:%s\n", connindex, conn[i].mcIndex,
                              conn[i].mcIdent, c->locAddr, c->locPort, addrstr, portstr);
                send_proc(monConn, scratch, var);
        }

        //
        // Send null request to client from new test connection (to potentially open firewall for server)
        // NOTE: The protocol version check can be commented out so that a null request will also be sent to
        //       legacy clients. Although this may result in a "ALERT: Received invalid test activation response..."
        //       error message on them, it would allow a newer server to no longer require that all its ephemeral ports
        //       be configured as reachable through its firewall.
        //
        if (pver >= EXTAUTH_PVER) {
                cHdrNR->pduId       = htons(CHNR_ID);
                cHdrNR->protocolVer = htons((uint16_t) pver);
                cHdrNR->cmdRequest  = CHNR_CREQ_NULLREQ;
                cHdrNR->cmdResponse = CHNR_CRSP_NONE;
                cHdrNR->reserved1   = 0;
                //
                cHdrNR->authMode = (uint8_t) conn[i].authMode;
                insert_auth((int) cHdrSR->keyId, skey, (unsigned char *) &cHdrNR->authMode, (unsigned char *) cHdrNR,
                            CHNR_SIZE_CVER);
                cHdrNR->checkSum = 0;
#ifdef ADD_HEADER_CSUM
                cHdrNR->checkSum = checksum(cHdrNR, CHNR_SIZE_CVER);
#endif
                if (send_proc(i, (char *) cHdrNR, CHNR_SIZE_CVER) != CHNR_SIZE_CVER)
                        return 0;
                if (conf.verbose) {
                        var = sprintf(scratch, "[%d]Null request (%d.%d) sent from %s:%d to %s:%s\n", i, conn[i].mcIndex,
                                      conn[i].mcIdent, conn[i].locAddr, conn[i].locPort, addrstr, portstr);
                        send_proc(monConn, scratch, var);
                }
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
        int i, var;
        char addrstr[INET6_ADDR_STRLEN], portstr[8];
        struct controlHdrSR *cHdrSR = (struct controlHdrSR *) repo.defBuffer;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (!verify_ctrlpdu(connindex, cHdrSR, NULL, NULL, NULL)) {
                return 0; // Ignore bad PDU (or null request)
        }

        //
        // Validate authentication if configured
        //
        if (*conf.authKey != '\0' || conf.keyFile != NULL) {
                var = 0;
                i   = validate_auth(c->protocolVer, c->clientKey, c->serverKey, (unsigned char *) &cHdrSR->authMode,
                                    (unsigned char *) cHdrSR, CHSR_SIZE_CVER);
                if (i > 0) {
                        var                = sprintf(scratch, "ERROR: Authentication failure of setup response from server");
                        repo.endTimeStatus = CHSR_CRSP_ERRBASE + CHSR_CRSP_AUTHFAIL; // ErrorStatus
                } else if (i < 0) {
                        var                = sprintf(scratch, "ERROR: Authentication time invalid in setup response from server");
                        repo.endTimeStatus = CHSR_CRSP_ERRBASE + CHSR_CRSP_AUTHTIME; // ErrorStatus
                }
                if (var > 0) {
                        var += sprintf(&scratch[var], " %s:%d\n", repo.server[c->serverIndex].ip, repo.server[c->serverIndex].port);
                        send_proc(errConn, scratch, var);
                        tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                        return 0;
                }
        }

        //
        // Process any setup response errors
        //
        if (cHdrSR->cmdResponse != CHSR_CRSP_ACKOK) {
                var                = 0;
                repo.endTimeStatus = CHSR_CRSP_ERRBASE + cHdrSR->cmdResponse; // ErrorStatus
                switch (cHdrSR->cmdResponse) {
                case CHSR_CRSP_BADVER:
                        var = sprintf(scratch, "ERROR: Client protocol version (%u) not accepted by server (%u)", PROTOCOL_VER,
                                      ntohs(cHdrSR->protocolVer));
                        break;
                case CHSR_CRSP_BADJS:
                        var = sprintf(scratch, "ERROR: Client jumbo datagram size option does not match server");
                        break;
                case CHSR_CRSP_AUTHNC:
                        var = sprintf(scratch, "ERROR: Authentication not configured on server");
                        break;
                case CHSR_CRSP_AUTHREQ:
                        var = sprintf(scratch, "ERROR: Authentication required by server");
                        break;
                case CHSR_CRSP_AUTHINV:
                        var = sprintf(scratch, "ERROR: Authentication method does not match server");
                        break;
                case CHSR_CRSP_AUTHFAIL:
                        var = sprintf(scratch, "ERROR: Authentication verification failed at server");
                        break;
                case CHSR_CRSP_AUTHTIME:
                        var = sprintf(scratch, "ERROR: Authentication time outside time window of server");
                        break;
                case CHSR_CRSP_NOMAXBW:
                        var = sprintf(scratch, "ERROR: Max bandwidth option required by server");
                        break;
                case CHSR_CRSP_CAPEXC:
                        var = sprintf(scratch, "ERROR: Required max bandwidth exceeds available capacity on server");
                        break;
                case CHSR_CRSP_BADTMTU:
                        var = sprintf(scratch, "ERROR: Client traditional MTU option does not match server");
                        break;
                case CHSR_CRSP_MCINVPAR:
                        var = sprintf(scratch, "ERROR: Multi-connection parameters rejected by server");
                        break;
                case CHSR_CRSP_CONNFAIL:
                        var = sprintf(scratch, "ERROR: Connection allocation failure on server");
                        break;
                default:
                        repo.endTimeStatus = CHSR_CRSP_ERRBASE; // Unexpected values use only error base for ErrorStatus
                        var = sprintf(scratch, "ERROR: Unexpected CRSP (%u) in setup response from server", cHdrSR->cmdResponse);
                }
                if (var > 0) {
                        var += sprintf(&scratch[var], " %s:%d\n", repo.server[c->serverIndex].ip, repo.server[c->serverIndex].port);
                        send_proc(errConn, scratch, var);
                }
                tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                return 0;
        }

        //
        // Obtain IP address and port number of sender
        //
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Setup response (%d.%d) received from %s:%s\n", connindex, c->mcIndex, c->mcIdent,
                              addrstr, portstr);
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
        cHdrTA->pduId       = htons(CHTA_ID);
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
        cHdrTA->subIntPeriod   = htons((uint16_t) c->subIntPeriod);
        c->dscpEcn             = conf.dscpEcn;
        cHdrTA->dscpEcn        = (uint8_t) c->dscpEcn;
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
        // Send test activation request to server
        //
        c->secAction = &service_actresp; // Set service handler for response
#ifdef AUTH_KEY_ENABLE
        if (*conf.authKey != '\0' || conf.keyFile != NULL) {
                cHdrTA->authMode = (uint8_t) c->authMode;
                insert_auth(conf.keyId, c->clientKey, (unsigned char *) &cHdrTA->authMode, (unsigned char *) cHdrTA,
                            CHTA_SIZE_CVER);
        }
#endif
#ifdef ADD_HEADER_CSUM
        cHdrTA->checkSum = checksum(cHdrTA, CHTA_SIZE_CVER);
#endif
        if (send_proc(connindex, (char *) cHdrTA, CHTA_SIZE_CVER) != CHTA_SIZE_CVER)
                return 0;
        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Test activation request (%d.%d) sent from %s:%d to %s:%d\n", connindex, c->mcIndex,
                              c->mcIdent, c->locAddr, c->locPort, c->remAddr, c->remPort);
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
        int i, var;
        char addrstr[INET6_ADDR_STRLEN], portstr[8];
        struct sendingRate *sr = repo.sendingRates; // Set to first row of table
        struct timespec tspecvar;
        struct controlHdrTA *cHdrTA   = (struct controlHdrTA *) repo.defBuffer;
        struct perfStatsCounters *psC = &repo.psCounters;

        //
        // Verify PDU
        //
        getnameinfo((struct sockaddr *) &repo.remSas, repo.remSasLen, addrstr, INET6_ADDR_STRLEN, portstr, sizeof(portstr),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (!verify_ctrlpdu(connindex, NULL, cHdrTA, addrstr, portstr)) {
                return 0; // Ignore bad PDU
        }
        psC->actRequestCnt++;

        //
        // Validate authentication based on mode of original setup request
        //
        if (c->authMode == AUTHMODE_1 && c->protocolVer >= EXTAUTH_PVER) {
                var = 0;
                i   = validate_auth(c->protocolVer, c->clientKey, c->serverKey, (unsigned char *) &cHdrTA->authMode,
                                    (unsigned char *) cHdrTA, (size_t) repo.rcvDataSize);
                if (i > 0) {
                        var = sprintf(scratch, "ERROR: Authentication failure of test activation request from");
                        psC->ctrlAuthFailure++;
                } else if (i < 0) {
                        var = sprintf(scratch, "ERROR: Authentication time invalid in test activation request from");
                        psC->ctrlBadAuthTime++;
                }
                if (var > 0) {
                        var += sprintf(&scratch[var], " %s:%s\n", addrstr, portstr);
                        send_proc(errConn, scratch, var);
                        return 0;
                }
        }

        //
        // Update global address info with client address/port number and connect socket
        //
        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Test activation request (%d.%d) received from %s:%s\n", connindex, c->mcIndex,
                              c->mcIdent, addrstr, portstr);
                send_proc(monConn, scratch, var);
        }
        var = atoi(portstr);
        if ((var = sock_mgmt(connindex, addrstr, var, NULL, SMA_UPDATE)) != 0) {
                send_proc(errConn, scratch, var);
                return 0;
        }
        if (sock_connect(connindex) < 0)
                return 0;

        // ===================================================================
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
        if (c->protocolVer >= MSSUBINT_PVER) {
                c->subIntPeriod = (int) ntohs(cHdrTA->subIntPeriod);
        } else {
                c->subIntPeriod = (int) cHdrTA->reserved1; // Location within previous PDU version
                c->subIntPeriod *= MSECINSEC;              // Convert seconds to ms
        }
        if (c->subIntPeriod < MIN_SUBINT_PERIOD || c->subIntPeriod > MAX_SUBINT_PERIOD) {
                c->subIntPeriod = DEF_SUBINT_PERIOD;
                if (c->protocolVer >= MSSUBINT_PVER) {
                        cHdrTA->subIntPeriod = htons((uint16_t) c->subIntPeriod);
                } else {
                        cHdrTA->reserved1 = (uint8_t) (c->subIntPeriod / MSECINSEC); // Send back as seconds
                }
        }
        if (c->subIntPeriod > (c->testIntTime * MSECINSEC)) { // Check for invalid relationship
                c->testIntTime      = DEF_TESTINT_TIME;
                cHdrTA->testIntTime = htons((uint16_t) c->testIntTime);
                c->subIntPeriod     = DEF_SUBINT_PERIOD;
                if (c->protocolVer >= MSSUBINT_PVER) {
                        cHdrTA->subIntPeriod = htons((uint16_t) c->subIntPeriod);
                } else {
                        cHdrTA->reserved1 = (uint8_t) (c->subIntPeriod / MSECINSEC); // Send back as seconds
                }
        }
        //
        // DSCP+ECN byte (also set socket option)
        //
        c->dscpEcn = (int) cHdrTA->dscpEcn;
        if (c->dscpEcn < MIN_DSCPECN_BYTE || c->dscpEcn > MAX_DSCPECN_BYTE) {
                c->dscpEcn      = DEF_DSCPECN_BYTE;
                cHdrTA->dscpEcn = (uint8_t) c->dscpEcn;
        } else if (c->dscpEcn > conf.dscpEcn) { // Enforce server maximum
                c->dscpEcn      = conf.dscpEcn;
                cHdrTA->dscpEcn = (uint8_t) c->dscpEcn;
        }
        if (c->dscpEcn != 0) {
                if (c->ipProtocol == IPPROTO_IPV6)
                        var = IPV6_TCLASS;
                else
                        var = IP_TOS;
                if (setsockopt(c->fd, c->ipProtocol, var, (const void *) &c->dscpEcn, sizeof(c->dscpEcn)) < 0) {
                        c->dscpEcn      = 0;
                        cHdrTA->dscpEcn = (uint8_t) c->dscpEcn;
                }
        }
        //
        // Static or starting sending rate index (special case <Auto>, which is the default but greater than max)
        //
        c->srIndexConf = (int) ntohs(cHdrTA->srIndexConf);
        if (c->srIndexConf != CHTA_SRIDX_DEF) {
                if (c->srIndexConf < MIN_SRINDEX_CONF || c->srIndexConf > MAX_SRINDEX_CONF) {
                        c->srIndexConf      = CHTA_SRIDX_DEF;
                        cHdrTA->srIndexConf = htons((uint16_t) c->srIndexConf);
                } else if (c->srIndexConf > conf.srIndexConf) { // Enforce server maximum
                        c->srIndexConf      = conf.srIndexConf;
                        cHdrTA->srIndexConf = htons((uint16_t) c->srIndexConf);
                }
                if (cHdrTA->modifierBitmap & CHTA_SRIDX_ISSTART) {
                        c->srIndexIsStart = TRUE;           // Designate configured value as starting point
                        c->srIndex        = c->srIndexConf; // Set starting point from configured value
                }
                if (c->srIndexConf != CHTA_SRIDX_DEF)
                        sr = &repo.sendingRates[c->srIndexConf]; // Select starting SR table row
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
        // If upstream test, send back initial sending rate transmission parameters
        //
        if (cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                sr_copy(sr, &cHdrTA->srStruct, TRUE);
        } else {
                memset(&cHdrTA->srStruct, 0, sizeof(struct sendingRate));
        }
        //
        // <<<<< If request is being rejected update counter >>>>>
        //
        if (cHdrTA->cmdResponse != CHTA_CRSP_ACKOK)
                psC->badActParameter++;
        // ===================================================================

        //
        // Continue updating connection if test activation is NOT being rejected
        //
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
                        c->testType     = TEST_TYPE_US;
                        c->rttMinimum   = STATUS_NODEL;
                        c->rttVarSample = STATUS_NODEL;
#ifdef HAVE_RECVMMSG
                        c->secAction = &service_recvmmsg;
#else
                        c->secAction = &service_loadpdu;
#endif
                        c->delayVarMin = STATUS_NODEL;
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
                        c->secAction = &service_statuspdu;
                        //
                        if (sr->txInterval1 > 0) {
                                var              = getuniform(MIN_RANDOM_START * USECINMSEC, MAX_RANDOM_START * USECINMSEC);
                                tspecvar.tv_sec  = 0;
                                tspecvar.tv_nsec = (long) (var * NSECINUSEC);
                                tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                        }
                        c->timer1Action = &send1_loadpdu;
                        if (sr->txInterval2 > 0) {
                                var              = getuniform(MIN_RANDOM_START * USECINMSEC, MAX_RANDOM_START * USECINMSEC);
                                tspecvar.tv_sec  = 0;
                                tspecvar.tv_nsec = (long) (var * NSECINUSEC);
                                tspecplus(&repo.systemClock, &tspecvar, &c->timer2Thresh);
                        }
                        c->timer2Action = &send2_loadpdu;
                }
                psC->actAcceptCnt++;
        } else {
                psC->actRejectCnt++;
        }

        //
        // Send test activation response to client
        //
        if (c->protocolVer >= EXTAUTH_PVER) {
                insert_auth((int) cHdrTA->keyId, c->serverKey, (unsigned char *) &cHdrTA->authMode, (unsigned char *) cHdrTA,
                            (size_t) repo.rcvDataSize);
                cHdrTA->checkSum = 0;
#ifdef ADD_HEADER_CSUM
                cHdrTA->checkSum = checksum(cHdrTA, repo.rcvDataSize);
#endif
        } else {
                cHdrTA->reserved3 = 0;
#ifdef ADD_HEADER_CSUM
                cHdrTA->reserved3 = checksum(cHdrTA, repo.rcvDataSize); // Location within previous PDU version
#endif
        }
        if (send_proc(connindex, (char *) cHdrTA, repo.rcvDataSize) != repo.rcvDataSize)
                return 0;
        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Test activation response (%d.%d) sent from %s:%d to %s:%d\n", connindex, c->mcIndex,
                              c->mcIdent, c->locAddr, c->locPort, c->remAddr, c->remPort);
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
        // Open output file if specified for upstream tests
        //
        if (conf.outputFile != NULL && cHdrTA->cmdRequest == CHTA_CREQ_TESTACTUS) {
                if ((var = open_outputfile(connindex)) > 0) {
                        send_proc(errConn, scratch, var); // Output error message
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
        // NOTE: This timer triggers the normal/graceful test stop initiated by the server
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
        int i, var, ipv6add;
        char *testtype, connid[8], delusage[8], sritext[8], payload[8];
        char intflabel[IFNAMSIZ + 8];
        struct sendingRate *sr = &c->srStruct; // Set to connection structure
        struct timespec tspecvar;
        struct controlHdrTA *cHdrTA = (struct controlHdrTA *) repo.defBuffer;

        //
        // Verify PDU
        //
        if (!verify_ctrlpdu(connindex, NULL, cHdrTA, NULL, NULL)) {
                return 0; // Ignore bad PDU (or null request)
        }

        //
        // Validate authentication if configured
        //
        if (*conf.authKey != '\0' || conf.keyFile != NULL) {
                var = 0;
                i   = validate_auth(c->protocolVer, c->clientKey, c->serverKey, (unsigned char *) &cHdrTA->authMode,
                                    (unsigned char *) cHdrTA, CHTA_SIZE_CVER);
                if (i > 0) {
                        var = sprintf(scratch, "ERROR: Authentication failure of test activation response from server");
                        repo.endTimeStatus = CHTA_CRSP_ERRBASE + CHSR_CRSP_AUTHFAIL; // Reuse CHSR offset for ErrorStatus
                } else if (i < 0) {
                        var = sprintf(scratch, "ERROR: Authentication time invalid in test activation response from server");
                        repo.endTimeStatus = CHTA_CRSP_ERRBASE + CHSR_CRSP_AUTHTIME; // Reuse CHSR offset for ErrorStatus
                }
                if (var > 0) {
                        var += sprintf(&scratch[var], " %s:%d\n", repo.server[c->serverIndex].ip, repo.server[c->serverIndex].port);
                        send_proc(errConn, scratch, var);
                        tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                        return 0;
                }
        }

        //
        // Process any test activation response errors
        //
        if (cHdrTA->cmdResponse != CHTA_CRSP_ACKOK) {
                repo.endTimeStatus = CHTA_CRSP_ERRBASE + cHdrTA->cmdResponse; // ErrorStatus
                if (cHdrTA->cmdResponse == CHTA_CRSP_BADPARAM) {
                        var = sprintf(scratch, "ERROR: Requested test parameter(s) rejected by server %s:%d\n",
                                      repo.server[c->serverIndex].ip, repo.server[c->serverIndex].port);
                } else {
                        repo.endTimeStatus = CHTA_CRSP_ERRBASE; // Unexpected values use only error base for ErrorStatus
                        var = sprintf(scratch, "ERROR: Unexpected CRSP (%u) in test activation response from server %s:%d\n",
                                      cHdrTA->cmdResponse, repo.server[c->serverIndex].ip, repo.server[c->serverIndex].port);
                }
                send_proc(errConn, scratch, var);
                tspeccpy(&c->endTime, &repo.systemClock); // Set for immediate close/exit
                return 0;
        }
        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Test activation response (%d.%d) received from %s:%d\n", connindex, c->mcIndex,
                              c->mcIdent, c->remAddr, c->remPort);
                send_proc(monConn, scratch, var);
        }

        //
        // Update test parameters (and set socket option) that may have been modified by server
        //
        c->lowThresh    = (int) ntohs(cHdrTA->lowThresh);
        c->upperThresh  = (int) ntohs(cHdrTA->upperThresh);
        c->trialInt     = (int) ntohs(cHdrTA->trialInt);
        c->testIntTime  = (int) ntohs(cHdrTA->testIntTime);
        c->subIntPeriod = (int) ntohs(cHdrTA->subIntPeriod);
        c->dscpEcn      = (int) cHdrTA->dscpEcn;
        if (c->dscpEcn != 0) {
                if (c->ipProtocol == IPPROTO_IPV6)
                        var = IPV6_TCLASS;
                else
                        var = IP_TOS;
                if (setsockopt(c->fd, c->ipProtocol, var, (const void *) &c->dscpEcn, sizeof(c->dscpEcn)) < 0) {
                        var = sprintf(scratch, "ERROR: Failure setting IP_TOS/IPV6_TCLASS (%d) %s\n", c->dscpEcn, strerror(errno));
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
                        var              = getuniform(MIN_RANDOM_START * USECINMSEC, MAX_RANDOM_START * USECINMSEC);
                        tspecvar.tv_sec  = 0;
                        tspecvar.tv_nsec = (long) (var * NSECINUSEC);
                        tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                }
                c->timer1Action = &send1_loadpdu;
                if (sr->txInterval2 > 0) {
                        var              = getuniform(MIN_RANDOM_START * USECINMSEC, MAX_RANDOM_START * USECINMSEC);
                        tspecvar.tv_sec  = 0;
                        tspecvar.tv_nsec = (long) (var * NSECINUSEC);
                        tspecplus(&repo.systemClock, &tspecvar, &c->timer2Thresh);
                }
                c->timer2Action = &send2_loadpdu;
        } else {
                //
                // Downstream
                // Setup to receive load PDUs and send status PDUs
                //
                testtype        = DSTEST_TEXT;
                c->rttMinimum   = STATUS_NODEL;
                c->rttVarSample = STATUS_NODEL;
#ifdef HAVE_RECVMMSG
                c->secAction = &service_recvmmsg;
#else
                c->secAction = &service_loadpdu;
#endif
                c->delayVarMin = STATUS_NODEL;
                tspeccpy(&c->trialIntClock, &repo.systemClock);
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = (long) (c->trialInt * NSECINMSEC);
                tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);
                c->timer1Action = &send_statuspdu;
        }

        //
        // Display test settings and general info of first completed connection
        //
        if (!repo.testHdrDone) {
                repo.testHdrDone = TRUE;

                *connid = '\0';
                if (conf.verbose)
                        sprintf(connid, "[%d]", connindex);

                if (c->useOwDelVar)
                        strcpy(delusage, OWD_TEXT);
                else
                        strcpy(delusage, RTT_TEXT);
                if (c->randPayload)
                        strcpy(payload, RAND_TEXT);
                else
                        strcpy(payload, ZERO_TEXT);
                if (c->srIndexConf == CHTA_SRIDX_DEF) {
                        strcpy(sritext, SRAUTO_TEXT);
                } else if (c->srIndexIsStart) {
                        sprintf(sritext, "%c%d", SRIDX_ISSTART_PREFIX, c->srIndexConf);
                } else {
                        sprintf(sritext, "%d", c->srIndexConf);
                }
                *intflabel = '\0';
                if (repo.intfFD >= 0) { // Append interface label
                        snprintf(intflabel, sizeof(intflabel), ", [%s]", conf.intfName);
                }
                if (!conf.jsonOutput) {
                        var = sprintf(scratch, TESTHDR_LINE, connid, testtype, c->testIntTime, c->lowThresh, c->upperThresh,
                                      delusage, c->trialInt, boolText[c->ignoreOooDup], payload, c->mcIdent, sritext,
                                      c->slowAdjThresh, c->highSpeedDelta, c->seqErrThresh, rateAdjAlgo[c->rateAdjAlgo], c->mcCount,
                                      c->dscpEcn, intflabel);
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
                                cJSON_AddNumberToObject(json_input, "ID", c->mcIdent);
                                cJSON_AddStringToObject(json_input, "Host", repo.server[0].name);
                                cJSON_AddStringToObject(json_input, "HostIPAddress", repo.server[0].ip);
                                cJSON_AddNumberToObject(json_input, "Port", c->remPort);
                                cJSON_AddNumberToObject(json_input, "NumberOfHosts", repo.serverCount);
                                cJSON *json_hostArray = cJSON_CreateArray();
                                for (i = 0; i < repo.serverCount; i++) {
                                        cJSON *json_host = cJSON_CreateObject();
                                        cJSON_AddStringToObject(json_host, "Host", repo.server[i].name);
                                        cJSON_AddStringToObject(json_host, "HostIPAddress", repo.server[i].ip);
                                        cJSON_AddNumberToObject(json_host, "ControlPort", repo.server[i].port);
                                        cJSON_AddItemToArray(json_hostArray, json_host);
                                }
                                cJSON_AddItemToObject(json_input, "HostList", json_hostArray);
                                cJSON_AddStringToObject(json_input, "ClientIPAddress", c->locAddr);
                                cJSON_AddNumberToObject(json_input, "ClientPort", c->locPort);
                                cJSON_AddNumberToObject(json_input, "JumboFramesPermitted", conf.jumboStatus);
                                cJSON_AddNumberToObject(json_input, "NumberOfConnections", conf.maxConnCount);
                                cJSON_AddNumberToObject(json_input, "MinNumOfConnections", conf.minConnCount);
                                cJSON_AddNumberToObject(json_input, "DSCP", c->dscpEcn >> 2);
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
                                if (c->srIndexConf == CHTA_SRIDX_DEF || c->srIndexIsStart) {
                                        cJSON_AddStringToObject(json_input, "TestType", "Search");
                                } else {
                                        cJSON_AddStringToObject(json_input, "TestType", "Fixed");
                                }
                                cJSON_AddNumberToObject(json_input, "IPDVEnable", c->useOwDelVar);
                                cJSON_AddNumberToObject(json_input, "IPRREnable", 1);
                                cJSON_AddNumberToObject(json_input, "RIPREnable", 1);
                                cJSON_AddNumberToObject(json_input, "PreambleDuration", 0);
                                // Using "[Start]SendingRateIndex" instead of "StartSendingRate" for this implementation
                                if (c->srIndexConf == CHTA_SRIDX_DEF || c->srIndexIsStart) {
                                        var = 0;
                                        if (c->srIndexIsStart)
                                                var = c->srIndexConf;
                                        cJSON_AddNumberToObject(json_input, "StartSendingRateIndex", var);
                                        cJSON_AddNumberToObject(json_input, "SendingRateIndex", -1);
                                } else {
                                        cJSON_AddNumberToObject(json_input, "StartSendingRateIndex", c->srIndexConf);
                                        cJSON_AddNumberToObject(json_input, "SendingRateIndex", c->srIndexConf);
                                }
                                cJSON_AddNumberToObject(json_input, "NumberTestSubIntervals",
                                                        (c->testIntTime * MSECINSEC) / c->subIntPeriod);
                                cJSON_AddNumberToObject(json_input, "NumberFirstModeTestSubIntervals", conf.bimodalCount);
                                cJSON_AddNumberToObject(json_input, "TestSubInterval", c->subIntPeriod);
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
                        create_timestamp(&repo.systemClock, TRUE);
                        cJSON_AddStringToObject(json_output, "BOMTime", scratch);
                        //
                        cJSON_AddNumberToObject(json_output, "TmaxUsed", WARNING_NOTRAFFIC * MSECINSEC);
                        cJSON_AddNumberToObject(json_output, "TestInterval", c->testIntTime);
                        cJSON_AddNumberToObject(json_output, "TmaxRTTUsed", TIMEOUT_NOTRAFFIC * MSECINSEC);
                        cJSON_AddNumberToObject(json_output, "TimestampResolutionUsed", 1);
                }
        }

        //
        // Open output file if specified for downstream test
        //
        if (conf.outputFile != NULL && cHdrTA->cmdRequest == CHTA_CREQ_TESTACTDS) {
                if ((var = open_outputfile(connindex)) > 0) {
                        send_proc(errConn, scratch, var); // Output error message
                }
        }

        //
        // Set end time (used as watchdog) in case server goes quiet
        //
        tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
        tspecvar.tv_nsec = 0;
        tspecplus(&repo.systemClock, &tspecvar, &c->endTime);

        //
        // Set timer to force an eventual shutdown if server never initiates a normal/graceful test stop,
        // but continues sending load PDUs. This timer sets the local test action to STOP to block the
        // end time (watchdog) from updating. This prevents the client from processing load PDUs forever.
        //
        tspecvar.tv_sec  = (time_t) (c->testIntTime + TIMEOUT_NOTRAFFIC);
        tspecvar.tv_nsec = NSECINSEC / 2;
        tspecplus(&repo.systemClock, &tspecvar, &c->timer3Thresh);
        c->timer3Action = &stop_test;

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
                var = sprintf(scratch, "GETADDRINFO ERROR[%s]: %s (%s)\n", host, strerror(errno), (const char *) gai_strerror(i));
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
                } else if (conf.verbose) {
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
                        if (repo.isServer && repo.server[0].name == NULL && ai->ai_next != NULL) {
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
                        if (ai->ai_family == AF_INET6) {
                                i = 0;
                                if (conf.ipv6Only) // Explicitly enable OR disable (some non-Linux systems default to enabled)
                                        i = 1;
                                if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const void *) &i, sizeof(i)) == -1) {
                                        var = sprintf(scratch, "IPV6_V6ONLY ERROR: %s (%d)\n", strerror(errno), i);
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
        for (i = 0; i < conf.maxConnections; i++) {
                if (conn[i].fd == -1) {
                        conn[i].fd        = fd;        // Save initial descriptor
                        conn[i].type      = type;      // Set connection type
                        conn[i].state     = S_CREATED; // Set connection state
                        conn[i].priAction = priaction; // Set primary action routine
                        conn[i].secAction = secaction; // Set secondary action routine
                        break;
                }
        }
        if (i == conf.maxConnections) {
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
        sndbuf = rcvbuf = 0;
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
                if (conf.verbose) {
                        var = sizeof(sndbuf);
                        if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *) &sndbuf, (socklen_t *) &var) < 0) {
                                var = sprintf(scratch, "[%d]GET SO_SNDBUF ERROR: %s\n", i, strerror(errno));
                                send_proc(errConn, scratch, var);
                                init_conn(i, TRUE);
                                return -1;
                        }
                        var = sizeof(rcvbuf);
                        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &rcvbuf, (socklen_t *) &var) < 0) {
                                var = sprintf(scratch, "[%d]GET SO_RCVBUF ERROR: %s\n", i, strerror(errno));
                                send_proc(errConn, scratch, var);
                                init_conn(i, TRUE);
                                return -1;
                        }
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

        if (conf.verbose) {
                var = sprintf(scratch, "[%d]Connection created (SNDBUF/RCVBUF: %d/%d) and assigned %s:%d\n", i, sndbuf, rcvbuf,
                              conn[i].locAddr, conn[i].locPort);
                send_proc(monConn, scratch, var);
        }
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
//
// Open output (export) data file
//
int open_outputfile(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        char *lbuffer, *chr, fname[STRING_SIZE];

        if (*conf.outputFile == '\0') {
                return sprintf(scratch, "ERROR: Output file not defined\n");
        }

        //
        // Replace special conversion specification options (#i,#c,...)
        //
        var     = 0;
        lbuffer = conf.outputFile;
        while (*lbuffer) {
                if (*lbuffer == '#') {
                        chr = lbuffer + 1;
                        if (*chr == 'i') { // Multi-Connection index
                                var += sprintf(&scratch[var], "%d", c->mcIndex);
                                lbuffer++;
                        } else if (*chr == 'c') { // Multi-Connection count
                                var += sprintf(&scratch[var], "%d", c->mcCount);
                                lbuffer++;
                        } else if (*chr == 'I') { // Multi-Connection identifier
                                var += sprintf(&scratch[var], "%d", c->mcIdent);
                                lbuffer++;
                        } else if (*chr == 'l') { // Local IP
                                var += sprintf(&scratch[var], "%s", c->locAddr);
                                lbuffer++;
                        } else if (*chr == 'r') { // Remote IP
                                var += sprintf(&scratch[var], "%s", c->remAddr);
                                lbuffer++;
                        } else if (*chr == 's') { // Source port
                                var += sprintf(&scratch[var], "%d", c->remPort);
                                lbuffer++;
                        } else if (*chr == 'd') { // Destination port
                                var += sprintf(&scratch[var], "%d", c->locPort);
                                lbuffer++;
                        } else if (*chr == 'M') { // Mode
                                if (repo.isServer)
                                        scratch[var++] = 'S'; // Server
                                else
                                        scratch[var++] = 'C'; // Client
                                lbuffer++;
                        } else if (*chr == 'D') { // Direction
                                if (repo.isServer)
                                        scratch[var++] = 'U'; // Upstream
                                else
                                        scratch[var++] = 'D'; // Downstream
                                lbuffer++;
                        } else if (*chr == 'H') { // Host name (or IP) specified for server
                                if (repo.isServer) {
                                        if (repo.server[0].name == NULL)
                                                var += sprintf(&scratch[var], "%s", "InAddrAny");
                                        else
                                                var += sprintf(&scratch[var], "%s", repo.server[0].name);
                                } else {
                                        var += sprintf(&scratch[var], "%s", repo.server[c->serverIndex].name);
                                }
                                lbuffer++;
                        } else if (*chr == 'p') { // Control port
                                if (repo.isServer)
                                        var += sprintf(&scratch[var], "%d", repo.server[0].port);
                                else
                                        var += sprintf(&scratch[var], "%d", repo.server[c->serverIndex].port);
                                lbuffer++;
                        } else if (*chr == 'E') { // Interface name
                                var += sprintf(&scratch[var], "%s", conf.intfName);
                                lbuffer++;
                        } else {
                                scratch[var++] = *lbuffer; // Copy character if conversion option not found
                        }
                } else {
                        scratch[var++] = *lbuffer; // Copy non-conversion character
                }
                lbuffer++;
        }
        scratch[var] = '\0';

        //
        // Replace date/time conversion specifications with current values
        //
        if (strftime(fname, sizeof(fname), scratch, localtime(&repo.systemClock.tv_sec)) == 0) {
                return sprintf(scratch, "ERROR: Output file name length exceeds maximum\n");
        }

        //
        // Open output file
        //
        if ((c->outputFPtr = fopen(fname, "w")) == NULL) {
                return sprintf(scratch, "FOPEN ERROR: <%.*s> %s\n", NAME_MAX, fname, strerror(errno));
        }

        //
        // Initialize with header
        //
        fputs("SeqNo,PayLoad,SrcTxTime,DstRxTime,OWD,IntfMbps,IntfMbpsAlt,RTTTxTime,RTTRxTime,RTTRespDelay,RTT,StatusLoss\n",
              c->outputFPtr);

        return 0;
}
//----------------------------------------------------------------------------
//
// Insert authentication
//
void insert_auth(int keyid, unsigned char *key, unsigned char *modeptr, unsigned char *data, size_t data_len) {
        struct authOverlay *ao = (struct authOverlay *) (modeptr - AO_MODE_OFFSET); // Align with overlay structure
#ifdef AUTH_KEY_ENABLE
        unsigned int uvar;
#endif
        if (keyid < 0 || key == NULL || data == NULL || data_len == 0)
                return;

        //
        // Clear and fill authentication overlay
        //
        ao->authUnixTime = 0;
        memset(ao->authDigest, 0, AUTH_DIGEST_LENGTH);
        ao->keyId         = 0;
        ao->reservedAuth1 = 0;
#ifdef AUTH_KEY_ENABLE
        if (ao->authMode == AUTHMODE_1) {
                ao->authUnixTime = htonl((uint32_t) repo.systemClock.tv_sec);
                ao->keyId        = (uint8_t) keyid;
                ao->checkSum     = 0; // Must be cleared before HMAC calculation
                HMAC(EVP_sha256(), key, SHA256_KEY_LEN, data, data_len, ao->authDigest, &uvar);
        }
#endif
        return;
}
//----------------------------------------------------------------------------
//
// Validate authentication
// Return Values: 0 = Success, 1 = Auth. Failure, -1 = Outside Auth. Time Window
//
int validate_auth(int pver, unsigned char *ckey, unsigned char *skey, unsigned char *modeptr, unsigned char *data,
                  size_t data_len) {
        struct authOverlay *ao = (struct authOverlay *) (modeptr - AO_MODE_OFFSET); // Align with overlay structure
        int i, var, authfail = 1;
        char *key;
        BOOL kdf = FALSE; // KDF keys available/provided
        unsigned char digest1[AUTH_DIGEST_LENGTH];
        struct timespec tspecvar;
#ifdef AUTH_KEY_ENABLE
        unsigned int uvar;
        unsigned char digest2[AUTH_DIGEST_LENGTH];
#endif
        if (data == NULL || data_len == 0)
                return authfail;

        //
        // Check authentication mode
        //
        if (ao->authMode != AUTHMODE_1)
                return authfail;

        //
        // Save off received digest and zero it in header
        //
        memcpy(digest1, ao->authDigest, AUTH_DIGEST_LENGTH);
        memset(ao->authDigest, 0, AUTH_DIGEST_LENGTH);
        ao->checkSum = 0; // Must be cleared before HMAC calculation

        //
        // Use KDF keys if already available
        //
        key = NULL;
        if (pver >= EXTAUTH_PVER) {
                if (memcmp(ckey, skey, SHA256_KEY_LEN) != 0) { // Check if not identical (not initialized to zero)
                        kdf = TRUE;
                        if (repo.isServer)
                                key = (char *) ckey;
                        else
                                key = (char *) skey;
                }
        }

        //
        // Attempt initial validation via key file entry if available
        //
        if (key == NULL && conf.keyFile != NULL) {
                var = (int) ao->keyId; // Obtain key ID
                for (i = 0; i < repo.keyCount; i++) {
                        if (repo.key[i].id == var) { // Find key with matching key ID
                                key = repo.key[i].key;
                                break;
                        }
                }
        }
        for (i = 0; i < 2; i++) {
                if (key != NULL) {
                        //
                        // Create and use KDF keys if needed, else use shared key directly
                        //
                        if (pver >= EXTAUTH_PVER) {
                                if (!kdf) {
#ifdef AUTH_KEY_ENABLE
                                        // If creating KDF keys via shared key and timestamp, identical authUnixTime values
                                        // must be used for KDF and initial PDU from client
                                        kdf_hmac_sha256(key, ntohl(ao->authUnixTime), ckey, skey);
#endif
                                }
                                if (repo.isServer)
                                        key = (char *) ckey;
                                else
                                        key = (char *) skey;
                                var = SHA256_KEY_LEN;
                        } else {
                                var = strlen(key);
                        }
#ifdef AUTH_KEY_ENABLE
                        HMAC(EVP_sha256(), key, var, data, data_len, digest2, &uvar);
                        if (memcmp(digest1, digest2, AUTH_DIGEST_LENGTH) == 0) {
                                authfail = 0;
                                break;
                        }
#endif
                }
                if (kdf) {
                        break; // Pre-existing KDF key must succeed on first pass
                }
                //
                // Attempt backup validation via command-line key if key file entry was unavailable or unsuccessful
                //
                if (i == 0 && *conf.authKey != '\0') {
                        key = conf.authKey;
                }
        }

        //
        // Check authentication time window if validation was successful
        //
        if (authfail == 0 && AUTH_ENFORCE_TIME) {
                tspecvar.tv_sec = (time_t) ntohl(ao->authUnixTime);
                if (tspecvar.tv_sec < repo.systemClock.tv_sec - AUTH_TIME_WINDOW ||
                    tspecvar.tv_sec > repo.systemClock.tv_sec + AUTH_TIME_WINDOW) {
                        authfail = -1;
                }
        }
        return authfail;
}
//----------------------------------------------------------------------------
//
// Verify control PDU integrity
//
BOOL verify_ctrlpdu(int connindex, struct controlHdrSR *cHdrSR, struct controlHdrTA *cHdrTA, char *addrstr, char *portstr) {
        register struct connection *c = &conn[connindex];
        BOOL bvar;
        int var, pver, minsize, maxsize, csum;
        static int alertCount         = 0; // Static
        struct controlHdrNR *cHdrNR   = NULL;
        struct perfStatsCounters *psC = &repo.psCounters;

        //
        // Initialize based on role and PDU type
        //
        if (repo.isServer) {
                if (cHdrSR) {
                        // Setup request/response
                        minsize = CHSR_SIZE_MVER;
                        maxsize = CHSR_SIZE_CVER;
                        pver    = (int) ntohs(cHdrSR->protocolVer);
                } else {
                        // Test activation request/response
                        minsize = CHTA_SIZE_MVER;
                        maxsize = CHTA_SIZE_CVER;
                        pver    = c->protocolVer;
                }
        } else {
                if (cHdrSR) {
                        // Setup request/response
                        minsize = maxsize = CHSR_SIZE_CVER;
                        cHdrNR            = (struct controlHdrNR *) cHdrSR;
                } else {
                        // Test activation request/response
                        minsize = maxsize = CHTA_SIZE_CVER;
                        cHdrNR            = (struct controlHdrNR *) cHdrTA;
                }
                pver = c->protocolVer;

                //
                // Check for possible null request from server
                //
                if (ntohs(cHdrNR->pduId) == CHNR_ID) {
                        if (conf.verbose) {
                                var = sprintf(scratch, "[%d]Null request (%d.%d) received", connindex, c->mcIndex, c->mcIdent);
                                if (*c->remAddr != '\0') {
                                        var += sprintf(&scratch[var], " from %s:%d", c->remAddr, c->remPort);
                                } else {
                                        // Possible scenario if setup response is lost or arrives after null request
                                        var += sprintf(&scratch[var], " before socket connect");
                                }
                                scratch[var++] = '\n';
                                send_proc(monConn, scratch, var);
                        }
                        return FALSE; // No processing required
                }
        }

        //
        // Perform PDU verification
        //
        bvar = FALSE;
        if (cHdrSR) {
                if (repo.rcvDataSize < minsize || repo.rcvDataSize > maxsize) {
                        bvar = TRUE;
                        psC->ctrlInvalidSize++;

                } else if (ntohs(cHdrSR->pduId) != CHSR_ID) {
                        bvar = TRUE;
                        psC->ctrlInvalidFormat++;

                } else if (cHdrSR->cmdRequest != CHSR_CREQ_SETUPREQ && cHdrSR->cmdRequest != CHSR_CREQ_SETUPRSP) {
                        bvar = TRUE;
                        psC->ctrlInvalidFormat++;

                } else if (cHdrSR->checkSum != 0) {
                        if (checksum(cHdrSR, repo.rcvDataSize)) {
                                bvar = TRUE;
                                psC->ctrlInvalidChksum++;
                        }
                }
        } else {
                if (repo.rcvDataSize < minsize || repo.rcvDataSize > maxsize) {
                        bvar = TRUE;
                        psC->ctrlInvalidSize++;

                } else if (ntohs(cHdrTA->pduId) != CHTA_ID) {
                        bvar = TRUE;
                        psC->ctrlInvalidFormat++;

                } else if (cHdrTA->cmdRequest != CHTA_CREQ_TESTACTUS && cHdrTA->cmdRequest != CHTA_CREQ_TESTACTDS) {
                        bvar = TRUE;
                        psC->ctrlInvalidFormat++;

                } else if ((pver >= EXTAUTH_PVER) && (cHdrTA->checkSum != 0)) {
                        if (checksum(cHdrTA, repo.rcvDataSize)) {
                                bvar = TRUE;
                                psC->ctrlInvalidChksum++;
                        }
                } else if ((pver < EXTAUTH_PVER) && (cHdrTA->reserved3 != 0)) { // Location within previous PDU version
                        if (checksum(cHdrTA, repo.rcvDataSize)) {
                                bvar = TRUE;
                                psC->ctrlInvalidChksum++;
                        }
                }
        }

        //
        // Process verification failure and generate alert if not suppressed
        //
        if (bvar) {
#ifdef SUPP_INVPDU_ALERT
                bvar = FALSE; // Flip boolean to suppress output
#endif
                if (bvar && alertCount < ALERT_MSG_LIMIT && (!repo.isServer || conf.verbose)) {
                        if (!repo.isServer && cHdrTA) {
                                // A lost test activation response for a downstream test will have load PDUs right behind it.
                                // Excessive alerts are controlled via a local counter and the alert message limit.
                                alertCount++;
                        }

                        var = sprintf(scratch, "ALERT: Received invalid");
                        if (cHdrSR) {
                                var += sprintf(&scratch[var], " setup");
                        } else {
                                var += sprintf(&scratch[var], " test activation");
                        }
                        if (repo.isServer) {
                                var += sprintf(&scratch[var], " request");
                        } else {
                                var += sprintf(&scratch[var], " response");
                        }
                        if (cHdrSR) {
                                var += sprintf(&scratch[var], " (%d,0x%04X:0x%04X,0x%04X)", repo.rcvDataSize, ntohs(cHdrSR->pduId),
                                               ntohs(cHdrSR->protocolVer), cHdrSR->checkSum);
                        } else {
                                if (pver >= EXTAUTH_PVER)
                                        csum = cHdrTA->checkSum;
                                else
                                        csum = cHdrTA->reserved3; // Location within previous PDU version
                                var += sprintf(&scratch[var], " (%d,0x%04X:0x%04X,0x%04X)", repo.rcvDataSize, ntohs(cHdrTA->pduId),
                                               ntohs(cHdrTA->protocolVer), csum);
                        }
                        if (repo.isServer) {
                                var += sprintf(&scratch[var], " from %s:%s\n", addrstr, portstr);
                        } else {
                                var += sprintf(&scratch[var], " [Server %s:%d]\n", repo.server[c->serverIndex].ip,
                                               repo.server[c->serverIndex].port);
                        }
                        send_proc(errConn, scratch, var);
                }
                return FALSE;
        }
        return TRUE;
}
//----------------------------------------------------------------------------
#ifdef __linux__
#ifdef AUTH_KEY_ENABLE
//
// Output individual authentication keys of length SHA256_KEY_LEN
// from derived key material.
//
// Return Values: 0 = Failure, 1 = Success
//
int kdf_hmac_sha256(char *Kin, uint32_t authUnixTime,
                    unsigned char *cAuthKey,   // Client key
                    unsigned char *sAuthKey) { // Server key

        int var, keylen = SHA256_KEY_LEN * 2;
        char context[16];
        unsigned char *keyptr, keybuf[keylen];
        EVP_KDF *kdf      = NULL;
        EVP_KDF_CTX *kctx = NULL;
        OSSL_PARAM params[16], *p = params;

        //
        // Fetch KDF algorithm and create context
        //
        if ((kdf = EVP_KDF_fetch(NULL, "KBKDF", NULL)) == NULL) {
                return 0;
        }
        if ((kctx = EVP_KDF_CTX_new(kdf)) == NULL) {
                EVP_KDF_free(kdf);
                return 0;
        }

        //
        // Set parameters for KBKDF
        // ---------------------------------------------------------
        *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_MODE, "COUNTER", 0);
        *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_MAC, "HMAC", 0);
        *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, "SHA256", 0);
        *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, Kin, strlen(Kin));
        *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, "UDPSTP", 6);
        var  = snprintf(context, sizeof(context), "%u", authUnixTime);
        *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, context, var);
        //
        // Confirm the following are enabled
        //
        var  = 1;
        *p++ = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_KBKDF_USE_L, &var);
        *p++ = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_KBKDF_USE_SEPARATOR, &var);
        //
        // Set counter length in bits (available as of OpenSSL 3.1)
        //
        // var = 32; // Length of 32 is backward compatible with OpenSSL 3.0
        //*p++ = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_KBKDF_R, &var);
        *p++ = OSSL_PARAM_construct_end();
        // ---------------------------------------------------------

        //
        // Derive key material
        //
        if (EVP_KDF_derive(kctx, keybuf, keylen, params) < 1) {
                EVP_KDF_CTX_free(kctx);
                EVP_KDF_free(kdf);
                return 0;
        }

        //
        // Output individual keys
        //
        keyptr = keybuf;
        memcpy(cAuthKey, keyptr, SHA256_KEY_LEN);
        keyptr += SHA256_KEY_LEN;
        memcpy(sAuthKey, keyptr, SHA256_KEY_LEN);

        //
        // Cleanup
        //
        EVP_KDF_CTX_free(kctx);
        EVP_KDF_free(kdf);
        return 1;
}
#endif // AUTH_KEY_ENABLE
#endif
//----------------------------------------------------------------------------
