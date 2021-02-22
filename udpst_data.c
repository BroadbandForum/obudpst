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
 * UDP Speed Test - udpst_data.c
 *
 * This file manages the sending and servicing of both load and status PDUs. It
 * handles traffic data collection, sending rate adjustments, and any output
 * messaging of test status and results.
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
 * Len Ciavattone          09/05/2020    Allow socket_error() & receive_trunc()
 *                                       to be redefined externally
 * Len Ciavattone          09/18/2020    Use usec instead of ms for delta time
 *                                       values (new protocol version required)
 * Len Ciavattone          10/09/2020    Add support for bimodal maxima (and
 *                                       include sub-interval count in output)
 * Len Ciavattone          11/10/2020    Add option to ignore OoO/Dup
 * Daniel Egger            02/22/2021    Add sendmsg support
 *
 */

#include "config.h"

#define UDPST_DATA
#ifdef __linux__
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#ifdef AUTH_KEY_ENABLE
#include <openssl/hmac.h>
#include <openssl/x509.h>
#endif
#else
#include "../udpst_data_alt1.h"
#endif
//
#include "udpst_common.h"
#include "udpst_protocol.h"
#include "udpst.h"
#include "udpst_data.h"
#ifndef __linux__
#include "../udpst_data_alt2.h"
#endif

//----------------------------------------------------------------------------
//
// Internal function prototypes
//
int send_loadpdu(int, int);
int adjust_sending_rate(int);
int proc_subinterval(int, BOOL);
int output_currate(int);
int output_maxrate(int);
double get_rate(int, struct subIntStats *, int);
#ifdef __linux__
int socket_error(int, int, char *);
int receive_trunc(int, int, int);
#endif
void sis_copy(struct subIntStats *, struct subIntStats *, BOOL);
void output_warning(int, int);

//----------------------------------------------------------------------------
//
// External data
//
extern int errConn, monConn;
extern char scratch[STRING_SIZE];
extern struct configuration conf;
extern struct repository repo;
extern struct connection *conn;

//----------------------------------------------------------------------------
//
// Global data
//
#define WARN_LOC_STATUS  0 // Locally received status messages lost
#define WARN_REM_STATUS  1 // Remotely received status messages lost
#define WARN_LOC_STOPPED 2 // Locally received traffic has stopped
#define WARN_REM_STOPPED 3 // Remotely received traffic has stopped
#define LOSSRATIO_TEXT   "LossRatio: %.2E, "
#define DELIVERED_TEXT   "Delivered(%%): %6.2f, "
#define SUMMARY_TEXT     "Loss/OoO/Dup: %u/%u/%u, OWDVar(ms): %u/%u/%u, RTTVar(ms): %u-%u, Mbps(L3/IP): %.2f\n"
#define MINIMUM_TEXT     "One-Way Delay(ms): %d [w/clock difference], Round-Trip Time(ms): %u\n"
#define DEBUG_STATS      "[Loss/OoO/Dup: %u/%u/%u, OWDVar(ms): %u/%u/%u, RTTVar(ms): %d]"
#define CLIENT_DEBUG     "[%d]DEBUG Status Feedback " DEBUG_STATS " Mbps(L3/IP): %.2f\n"
#define SERVER_DEBUG     "[%d]DEBUG Rate Adjustment " DEBUG_STATS " SRIndex: %d\n"
#define MINIMUM_DEBUG    "[%d]DEBUG Minimum " MINIMUM_TEXT
static char scratch2[STRING_SIZE + 32]; // Allow for log file timestamp prefix

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------
#if defined (HAVE_SENDMMSG)
//
// Send a burst of messages using the Linux 3.0+ only sendmmsg syscall
//
static void _sendmmsg_burst(int connindex, int totalburst, int burstsize, unsigned int payload, unsigned int addon) {
        static struct mmsghdr mmsg[MAX_BURST_SIZE]; // Static array
        static struct iovec iov[MAX_BURST_SIZE];    // Static array
        struct connection *c = &conn[connindex];
        unsigned int uvar;
        char *nextsndbuf;
        int i, var;

        memset(mmsg, 0, totalburst * sizeof(struct mmsghdr));
        nextsndbuf = repo.sndBuffer;
        for (i = 0; i < totalburst; i++) {
                struct loadHdr *lHdr = (struct loadHdr *) nextsndbuf;
                if (i == 0) {
                        lHdr->loadId     = htons(LOAD_ID);
                        lHdr->testAction = (uint8_t) c->testAction;
                        lHdr->rxStopped  = (uint8_t) c->rxStoppedLoc;
                        // lpduSeqNo set below
                        // udpPayload set below
                        lHdr->spduSeqErr = htons((uint16_t) c->spduSeqErr);
                        //
                        lHdr->spduTime_sec  = htonl((uint32_t) c->spduTime.tv_sec);
                        lHdr->spduTime_nsec = htonl((uint32_t) c->spduTime.tv_nsec);
                        lHdr->lpduTime_sec  = htonl((uint32_t) repo.systemClock.tv_sec);
                        lHdr->lpduTime_nsec = htonl((uint32_t) repo.systemClock.tv_nsec);
                } else {
                        //
                        // Replicate static fields of first datagram
                        //
                        memcpy((void *) lHdr, repo.sndBuffer, sizeof(struct loadHdr));
                }
                lHdr->lpduSeqNo = htonl((uint32_t) ++c->lpduSeqNo);
                if (i < burstsize)
                        uvar = payload;
                else
                        uvar = addon;
                lHdr->udpPayload = htons((uint16_t) uvar);

                //
                // Setup corresponding message structure
                //
                iov[i].iov_base            = (void *) lHdr;
                iov[i].iov_len             = (size_t) uvar;
                mmsg[i].msg_hdr.msg_iov    = &iov[i];
                mmsg[i].msg_hdr.msg_iovlen = 1;
                nextsndbuf += payload;
        }

        //
        // Send complete burst with single system call
        //
        // NOTE: Certain error conditions are expected when overloading an interface
        //
        var = sendmmsg(c->fd, mmsg, totalburst, 0);
        if (!conf.errSuppress) {
                if (var < 0) {
                        //
                        // An error of EAGAIN (Resource temporarily unavailable) indicates the send buffer is full
                        //
                        if ((var = socket_error(connindex, errno, "SENDMMSG")) > 0)
                                send_proc(errConn, scratch, var);
                } else if (var < totalburst) {
                        //
                        // Not all messages sent indicates the send buffer is full
                        //
                        var = sprintf(scratch, "[%d]SENDMMSG INCOMPLETE: Only %d out of %d sent\n", connindex, var, totalburst);
                        send_proc(errConn, scratch, var);
                }
        }
}
#endif /* HAVE_SENDMMSG */

//
// Send a burst of messages using the slower but more widely available sendmsg syscall
//
static void _sendmsg_burst(int connindex, int totalburst, int burstsize, unsigned int payload, unsigned int addon) {
        static struct msghdr msg; // Static array
        static struct iovec iov;    // Static array
        struct connection *c = &conn[connindex];
        unsigned int uvar;
        char *nextsndbuf;
        int i, var;

        memset((void *)&msg, 0, sizeof(struct msghdr));
        nextsndbuf = repo.sndBuffer;
        for (i = 0; i < totalburst; i++) {
                struct loadHdr *lHdr = (struct loadHdr *) nextsndbuf;
                if (i == 0) {
                        lHdr->loadId     = htons(LOAD_ID);
                        lHdr->testAction = (uint8_t) c->testAction;
                        lHdr->rxStopped  = (uint8_t) c->rxStoppedLoc;
                        // lpduSeqNo set below
                        // udpPayload set below
                        lHdr->spduSeqErr = htons((uint16_t) c->spduSeqErr);
                        //
                        lHdr->spduTime_sec  = htonl((uint32_t) c->spduTime.tv_sec);
                        lHdr->spduTime_nsec = htonl((uint32_t) c->spduTime.tv_nsec);
                        lHdr->lpduTime_sec  = htonl((uint32_t) repo.systemClock.tv_sec);
                        lHdr->lpduTime_nsec = htonl((uint32_t) repo.systemClock.tv_nsec);
                }
                lHdr->lpduSeqNo = htonl((uint32_t) ++c->lpduSeqNo);
                if (i < burstsize)
                        uvar = payload;
                else
                        uvar = addon;
                lHdr->udpPayload = htons((uint16_t) uvar);

                //
                // Setup corresponding message structure
                //
                iov.iov_base            = (void *) lHdr;
                iov.iov_len             = (size_t) uvar;
                msg.msg_iov    = &iov;
                msg.msg_iovlen = 1;

                //
                // Send a single message of our burst with a system call
                //
                // NOTE: Certain error conditions are expected when overloading an interface
                //
                var = sendmsg(c->fd, &msg, 0);
                if (!conf.errSuppress) {
                        if (var < 0) {
                                //
                                // An error of EAGAIN (Resource temporarily unavailable) indicates the send buffer is full
                                //
                                if ((var = socket_error(connindex, errno, "SENDMMSG")) > 0)
                                        send_proc(errConn, scratch, var);
                        }
                }
        }
}

// Send load PDUs via periodic timers for transmitters 1 & 2
//
int send1_loadpdu(int connindex) {
        return send_loadpdu(connindex, 1);
}
int send2_loadpdu(int connindex) {
        return send_loadpdu(connindex, 2);
}
int send_loadpdu(int connindex, int transmitter) {
        register struct connection *c = &conn[connindex];
        int burstsize, totalburst, txintpri, txintalt;
        unsigned int payload, addon;
        struct timespec tspecvar, *tspecpri, *tspecalt;
        struct sendingRate *sr;

        //
        // Select sending rate source
        //
        if (repo.isServer) {
                sr = &repo.sendingRates[c->srIndex]; // Local table if server
        } else {
                sr = &c->srStruct; // Server specified values if client
        }

        //
        // Select transmitter specifics
        //
        if (transmitter == 1) {
                payload   = (unsigned int) sr->udpPayload1;
                burstsize = (int) sr->burstSize1;
                addon     = 0;
        } else {
                payload   = (unsigned int) sr->udpPayload2;
                burstsize = (int) sr->burstSize2;
                addon     = (unsigned int) sr->udpAddon2;
        }
        if (c->ipProtocol == IPPROTO_IPV6) {
                //
                // Truncate payload if it would cause oversized IPv6 jumbo packet
                //
                if (payload > MAX_JPAYLOAD_SIZE - IPV6_ADDSIZE)
                        payload = MAX_JPAYLOAD_SIZE - IPV6_ADDSIZE;
        }

        //
        // Handle test stop in progress
        //
        if (c->testAction != TEST_ACT_TEST) {
                burstsize = 1; // Reduce load w/min burst size
                if (repo.isServer) {
                        if (monConn >= 0 && c->testAction == TEST_ACT_STOP1) {
                                int var = sprintf(scratch, "[%d]Sending test stop\n", connindex);
                                send_proc(monConn, scratch, var);
                        }
                        c->testAction = TEST_ACT_STOP2; // Second phase of test stop
                } else {
                        //
                        // The PDU sent in this pass will confirm the test stop back to the server,
                        // schedule an immediate/subsequent test end
                        //
                        repo.endTimeStatus = 0;
                        tspeccpy(&c->endTime, &repo.systemClock);
                }
        }

        //
        // Process timers 1 & 2 as primary or alternate
        //
        if (transmitter == 1) {
                txintpri = (int) sr->txInterval1;
                txintalt = (int) sr->txInterval2;
                tspecpri = &c->timer1Thresh;
                tspecalt = &c->timer2Thresh;
        } else {
                txintpri = (int) sr->txInterval2;
                txintalt = (int) sr->txInterval1;
                tspecpri = &c->timer2Thresh;
                tspecalt = &c->timer1Thresh;
        }
        //
        // Reset or clear primary timer (this one)
        //
        if (txintpri > 0) {
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = (long) ((txintpri - SEND_TIMER_ADJ) * NSECINUSEC);
                tspecplus(&repo.systemClock, &tspecvar, tspecpri);
        } else {
                tspecclear(tspecpri);
        }
        //
        // Set or clear alternate timer (the other one)
        //
        if (!tspecisset(tspecalt) && txintalt > 0) {
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = (long) ((txintalt - SEND_TIMER_ADJ) * NSECINUSEC);
                tspecplus(&repo.systemClock, &tspecvar, tspecalt);
        } else if (tspecisset(tspecalt) && txintalt == 0) {
                tspecclear(tspecalt);
        }

        //
        // Check for burst size of zero
        //
        if (burstsize == 0 && addon == 0) {
                return 0; // Nothing to do until next call
        }

        //
        // If receive traffic stopped, set indicator to inform peer and generate warning (else clear indicator)
        //
        if (tspecisset(&c->pduRxTime)) {
                tspecminus(&repo.systemClock, &c->pduRxTime, &tspecvar);
                if (tspecvar.tv_sec >= WARNING_NOTRAFFIC) {
                        c->rxStoppedLoc = TRUE;
                        tspecclear(&c->pduRxTime); // Clear PDU receive time to maintain indicator until traffic resumes
                        if (c->warningCount < WARNING_MSG_LIMIT) {
                                c->warningCount++;
                                output_warning(connindex, WARN_LOC_STOPPED);
                        }
                } else {
                        c->rxStoppedLoc = FALSE;
                }
        }

        //
        // Build complete burst of datagrams and message structures
        //
        totalburst = burstsize;
        if (addon > 0)
                totalburst++;

#if defined (HAVE_SENDMMSG)
        _sendmmsg_burst(connindex, totalburst, burstsize, payload, addon);
#else
        _sendmsg_burst(connindex, totalburst, burstsize, payload, addon);
#endif /* HAVE_SENDMMSG */

        return 0;
}
//----------------------------------------------------------------------------
//
// Service incoming load PDUs
//
int service_loadpdu(int connindex) {
        register struct connection *c = &conn[connindex];
        int i, delta, var;
        BOOL bvar, firstpdu = FALSE;
        unsigned int uvar, seqno;
        struct loadHdr *lHdr = (struct loadHdr *) repo.defBuffer;
        struct timespec tspecvar, tspecdelta;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) sizeof(struct loadHdr) || ntohs(lHdr->loadId) != LOAD_ID) {
                return 0; // Ignore bad PDU
        }

        //
        // Handle test stop in progress, else extend test (reset watchdog)
        //
        if (c->testAction != TEST_ACT_TEST || lHdr->testAction != TEST_ACT_TEST) {
                if (repo.isServer) {
                        //
                        // If client is confirming stop, end test
                        //
                        if (lHdr->testAction != TEST_ACT_TEST) {
                                if (conf.verbose && c->endTime.tv_sec != repo.systemClock.tv_sec &&
                                    c->endTime.tv_nsec != repo.systemClock.tv_nsec) {
                                        output_maxrate(connindex);
                                }
                                tspeccpy(&c->endTime, &repo.systemClock);
                                return 0;
                        }
                } else {
                        //
                        // On first pass, finalize testing
                        //
                        if (c->testAction == TEST_ACT_TEST) {
                                output_maxrate(connindex);
                                if (monConn >= 0) {
                                        var = sprintf(scratch, "[%d]Test stop received\n", connindex);
                                        send_proc(monConn, scratch, var);
                                }
                                c->testAction = (int) lHdr->testAction;
                        }
                        return 0;
                }
        } else {
                tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
                tspecvar.tv_nsec = 0;
                tspecplus(&repo.systemClock, &tspecvar, &c->endTime);
        }

        //
        // Save receive time for this PDU
        //
        tspeccpy(&c->pduRxTime, &repo.systemClock);

        //
        // Generate warning if peer indicates receive traffic has stopped
        //
        if ((bvar = (BOOL) lHdr->rxStopped) != c->rxStoppedRem) {
                c->rxStoppedRem = bvar; // Save value if changed
                if (c->rxStoppedRem) {  // Only warn if state indicates true
                        if (c->warningCount < WARNING_MSG_LIMIT) {
                                c->warningCount++;
                                output_warning(connindex, WARN_REM_STOPPED);
                        }
                }
        }

        //
        // Generate warning if peer indicates status message sequence errors
        //
        if ((var = (int) ntohs(lHdr->spduSeqErr)) != c->spduSeqErr) {
                c->spduSeqErr = var;     // Save value if changed
                if (c->spduSeqErr > 0) { // Only warn if count indicates loss
                        if (c->warningCount < WARNING_MSG_LIMIT) {
                                c->warningCount++;
                                output_warning(connindex, WARN_REM_STATUS);
                        }
                }
        }

        //
        // Update traffic stats (use size specified in PDU, actual receive was truncated)
        //
        uvar = (unsigned int) ntohs(lHdr->udpPayload);
        c->sisAct.rxDatagrams++;
        c->sisAct.rxBytes += (uint32_t) uvar;
        c->tiRxDatagrams++;
        c->tiRxBytes += uvar;

        //
        // Check sequence number for loss, also reordering/duplication (end processing if so)
        //
        if (c->lpduSeqNo == 0)
                firstpdu = TRUE;
        var   = 0; // Used below for history buffer processing
        seqno = (unsigned int) ntohl(lHdr->lpduSeqNo);
        if (seqno >= c->lpduSeqNo + 1) {
                //
                // Sequence number greater than or equal to expected
                //
                if (seqno > c->lpduSeqNo + 1) {
                        uvar = seqno - c->lpduSeqNo - 1; // Calculate loss
                        c->seqErrLoss += uvar;
                        c->sisAct.seqErrLoss += (uint32_t) uvar;
                }
                c->lpduSeqNo = seqno; // Update for next expected
        } else {
                //
                // Sequence number less than expected, check history buffer
                //
                for (i = 0; i < LPDU_HISTORY_SIZE; i++) {
                        if (seqno == c->lpduHistBuf[i])
                                break;
                }
                if (i < LPDU_HISTORY_SIZE) {
                        //
                        // Sequence number in history buffer, increment duplicate count
                        //
                        c->seqErrDup++;
                        c->sisAct.seqErrDup++;
                        var = 2; // Skip history buffer insertion as well as subsequent processing
                } else {
                        //
                        // Sequence number NOT in history buffer, increment out-of-order count
                        //
                        c->seqErrOoo++;
                        c->sisAct.seqErrOoo++;
                        var = 1; // Skip subsequent processing

                        //
                        // Correct previous loss count that resulted from this "late" datagram
                        //
                        // NOTE: If this datagram arrives after either of these have been cleared (because they were just sent
                        // in a status feedback message), the previous trial or sub-interval will still show the loss.
                        //
                        if (c->seqErrLoss > 0)
                                c->seqErrLoss--;
                        if (c->sisAct.seqErrLoss > 0)
                                c->sisAct.seqErrLoss--;
                }
        }
        if (var < 2) {
                c->lpduHistBuf[c->lpduHistIdx] = seqno;                                // Save sequence number in history buffer
                c->lpduHistIdx                 = ++c->lpduHistIdx & LPDU_HISTORY_MASK; // Update history buffer index
        }
        if (var > 0) {
                return 0; // No further processing for non-increasing sequence numbers
        }

        //
        // If an updated value is detected (because another status PDU was sent),
        // calculate round-trip time from the last status PDU sent until this load PDU
        //
        tspecvar.tv_sec  = (time_t) ntohl(lHdr->spduTime_sec);
        tspecvar.tv_nsec = (long) ntohl(lHdr->spduTime_nsec);
        if (tspecvar.tv_nsec != c->spduTime.tv_nsec || tspecvar.tv_sec != c->spduTime.tv_sec) {
                tspecminus(&repo.systemClock, &tspecvar, &tspecdelta);
                uvar = (unsigned int) tspecmsec(&tspecdelta);
                //
                // Check for new minimum
                //
                if (uvar < c->rttMinimum) {
                        c->rttMinimum  = uvar;
                        c->delayMinUpd = TRUE;
                }
                //
                // Update RTT variation for trial interval and RTT variation range for sub-interval
                //
                c->rttSample = uvar - c->rttMinimum;
                if (c->rttSample < (unsigned int) c->sisAct.rttMinimum)
                        c->sisAct.rttMinimum = (uint32_t) c->rttSample;
                if (c->rttSample > (unsigned int) c->sisAct.rttMaximum)
                        c->sisAct.rttMaximum = (uint32_t) c->rttSample;
                tspeccpy(&c->spduTime, &tspecvar); // Save to detect updated value
        }

        //
        // Calculate one-way clock delta and delay variation for this load PDU
        //
        tspecvar.tv_sec  = (time_t) ntohl(lHdr->lpduTime_sec);
        tspecvar.tv_nsec = (long) ntohl(lHdr->lpduTime_nsec);
        tspecminus(&repo.systemClock, &tspecvar, &tspecdelta);
        delta = (int) tspecmsec(&tspecdelta);
        if (firstpdu) {
                c->clockDeltaMin = delta;
                c->delayMinUpd   = TRUE;
        } else {
                //
                // Check for new minimum
                //
                if (delta < c->clockDeltaMin) {
                        c->clockDeltaMin = delta;
                        c->delayMinUpd   = TRUE;
                }
                uvar = (unsigned int) (delta - c->clockDeltaMin);
                //
                // Update one-way delay variation stats for trial interval
                //
                if (uvar < c->delayVarMin)
                        c->delayVarMin = uvar;
                if (uvar > c->delayVarMax)
                        c->delayVarMax = uvar;
                c->delayVarSum += uvar;
                c->delayVarCnt++;
                //
                // Update one-way delay variation stats for sub-interval
                //
                if (uvar < (unsigned int) c->sisAct.delayVarMin)
                        c->sisAct.delayVarMin = (uint32_t) uvar;
                if (uvar > (unsigned int) c->sisAct.delayVarMax)
                        c->sisAct.delayVarMax = (uint32_t) uvar;
                c->sisAct.delayVarSum += (uint32_t) uvar;
                c->sisAct.delayVarCnt++;
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Send status PDUs via periodic timer
//
int send_statuspdu(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        unsigned int dvmin, dvavg;
        struct timespec tspecvar;
        struct sendingRate *sr;
        struct statusHdr *sHdr = (struct statusHdr *) repo.defBuffer;

        //
        // Check for test stop in progress, else reset status send timer
        //
        if (c->testAction != TEST_ACT_TEST) {
                tspecclear(&c->timer1Thresh); // Stop subsequent status messages
                if (repo.isServer) {
                        if (monConn >= 0 && c->testAction == TEST_ACT_STOP1) {
                                var = sprintf(scratch, "[%d]Sending test stop\n", connindex);
                                send_proc(monConn, scratch, var);
                        }
                        c->testAction = TEST_ACT_STOP2; // Second phase of test stop
                } else {
                        //
                        // The PDU sent in this pass will confirm the test stop back to the server,
                        // schedule an immediate/subsequent test end
                        //
                        repo.endTimeStatus = 0;
                        tspeccpy(&c->endTime, &repo.systemClock);
                }
        } else {
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = (long) (c->trialInt * NSECINMSEC);
                tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);

                //
                // Only continue if some data has been received (initial load PDUs could still be in transit)
                //
                if (c->lpduSeqNo == 0) {
                        if (monConn >= 0) {
                                var = sprintf(scratch, "[%d]Skipping status transmission, awaiting initial load PDUs...\n",
                                              connindex);
                                send_proc(monConn, scratch, var);
                        }
                        return 0;
                }

                //
                // If server, adjust sending rate based on our receive traffic conditions
                //
                if (repo.isServer) {
                        adjust_sending_rate(connindex);
                }
        }

        //
        // If receive traffic stopped, set indicator to inform peer and generate warning (else clear indicator)
        //
        if (tspecisset(&c->pduRxTime)) {
                tspecminus(&repo.systemClock, &c->pduRxTime, &tspecvar);
                if (tspecvar.tv_sec >= WARNING_NOTRAFFIC) {
                        c->rxStoppedLoc = TRUE;
                        tspecclear(&c->pduRxTime); // Clear PDU receive time to maintain indicator until traffic resumes
                        if (c->warningCount < WARNING_MSG_LIMIT) {
                                c->warningCount++;
                                output_warning(connindex, WARN_LOC_STOPPED);
                        }
                } else {
                        c->rxStoppedLoc = FALSE;
                }
        }

        //
        // Build status header
        //
        sHdr->statusId   = htons(STATUS_ID);
        sHdr->testAction = (uint8_t) c->testAction;
        sHdr->rxStopped  = (uint8_t) c->rxStoppedLoc;
        sHdr->spduSeqNo  = htonl((uint32_t) ++c->spduSeqNo);

        //
        // Copy sending rate parameters or clear structure
        //
        if (repo.isServer) {
                sr = &repo.sendingRates[c->srIndex];
                sr_copy(sr, &sHdr->srStruct, TRUE);
        } else {
                memset(&sHdr->srStruct, 0, sizeof(struct sendingRate));
        }

        //
        // Include last saved sub-interval statistics
        //
        sHdr->subIntSeqNo = htonl((uint32_t) c->subIntSeqNo);
        sis_copy(&c->sisSav, &sHdr->sisSav, TRUE);

        //
        // Include sequence error loss, out-of-order, and duplicate stats
        //
        sHdr->seqErrLoss = htonl((uint32_t) c->seqErrLoss);
        sHdr->seqErrOoo  = htonl((uint32_t) c->seqErrOoo);
        sHdr->seqErrDup  = htonl((uint32_t) c->seqErrDup);

        //
        // Include delay info
        //
        sHdr->clockDeltaMin = htonl((uint32_t) c->clockDeltaMin);
        sHdr->delayVarMin   = htonl(c->delayVarMin);
        sHdr->delayVarMax   = htonl(c->delayVarMax);
        sHdr->delayVarSum   = htonl(c->delayVarSum);
        sHdr->delayVarCnt   = htonl(c->delayVarCnt);
        sHdr->rttMinimum    = htonl(c->rttMinimum);
        sHdr->rttSample     = htonl(c->rttSample);
        sHdr->delayMinUpd   = (uint8_t) c->delayMinUpd;
        sHdr->reserved2     = 0;
        sHdr->reserved3     = 0;

        //
        // Include trial interval info
        //
        tspecminus(&repo.systemClock, &c->trialIntClock, &tspecvar);
        c->tiDeltaTime      = (unsigned int) tspecusec(&tspecvar);
        sHdr->tiDeltaTime   = htonl((uint32_t) c->tiDeltaTime);
        sHdr->tiRxDatagrams = htonl((uint32_t) c->tiRxDatagrams);
        sHdr->tiRxBytes     = htonl((uint32_t) c->tiRxBytes);

        //
        // Include time reference for this status PDU
        //
        sHdr->spduTime_sec  = htonl((uint32_t) repo.systemClock.tv_sec);
        sHdr->spduTime_nsec = htonl((uint32_t) repo.systemClock.tv_nsec);
        if (!repo.isServer) {
                //
                // Output debug messages if configured
                //
                if (monConn >= 0 && conf.debug && c->testAction == TEST_ACT_TEST) {
                        if (c->delayMinUpd && c->rttMinimum != INITIAL_MIN_DELAY) {
                                var = sprintf(scratch, MINIMUM_DEBUG, connindex, c->clockDeltaMin, c->rttMinimum);
                                send_proc(monConn, scratch, var);
                        }
                        dvmin = dvavg = 0;
                        if (c->delayVarCnt > 0) {
                                dvmin = c->delayVarMin;
                                dvavg = c->delayVarSum / c->delayVarCnt;
                        }
                        var = -1;
                        if (c->rttSample != INITIAL_MIN_DELAY)
                                var = (int) c->rttSample;
                        var = sprintf(scratch, CLIENT_DEBUG, connindex, c->seqErrLoss, c->seqErrOoo, c->seqErrDup, dvmin, dvavg,
                                      c->delayVarMax, var, get_rate(connindex, NULL, L3DG_OVERHEAD));
                        send_proc(monConn, scratch, var);
                }
        }
        //
        // Initialize values after copying to status message
        //
        c->seqErrLoss = 0;
        c->seqErrOoo  = 0;
        c->seqErrDup  = 0;
        // Do not clear clock delta minimum
        c->delayVarMin = INITIAL_MIN_DELAY;
        c->delayVarMax = 0;
        c->delayVarSum = 0;
        c->delayVarCnt = 0;
        // Do not clear global RTT minimum
        c->rttSample   = INITIAL_MIN_DELAY;
        c->delayMinUpd = FALSE;
        tspeccpy(&c->trialIntClock, &repo.systemClock);
        c->tiDeltaTime   = 0;
        c->tiRxDatagrams = 0;
        c->tiRxBytes     = 0;

        //
        // Send status message
        //
        var = sizeof(struct statusHdr);
        send_proc(connindex, (char *) sHdr, var);

        //
        // Initialize or process sub-interval statistics. Because it is checked with each
        // status message, the sub-interval time has the granularity of the trial interval.
        //
        if (!tspecisset(&c->subIntClock)) { // If clock never set
                //
                // Initialize stats and sub-interval clock on first status message
                //
                proc_subinterval(connindex, TRUE);
        } else {
                //
                // Check sub-interval clock for expiration
                //
                tspecminus(&repo.systemClock, &c->subIntClock, &tspecvar);
                var = (c->subIntPeriod * MSECINSEC) - (c->trialInt / 2);
                if ((int) tspecmsec(&tspecvar) > var)
                        proc_subinterval(connindex, FALSE);
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Service incoming status PDUs
//
int service_statuspdu(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        BOOL bvar;
        unsigned int uvar, seqno, dvmin, dvavg;
        struct statusHdr *sHdr = (struct statusHdr *) repo.defBuffer;
        struct timespec tspecvar;

        //
        // Verify PDU
        //
        if (repo.rcvDataSize < (int) sizeof(struct statusHdr) || ntohs(sHdr->statusId) != STATUS_ID) {
                return 0; // Ignore bad PDU
        }

        //
        // Handle test stop in progress, else extend test (reset watchdog)
        //
        if (c->testAction != TEST_ACT_TEST || sHdr->testAction != TEST_ACT_TEST) {
                if (repo.isServer) {
                        //
                        // If client is confirming stop, end test
                        //
                        if (sHdr->testAction != TEST_ACT_TEST) {
                                if (conf.verbose) {
                                        output_maxrate(connindex);
                                }
                                tspeccpy(&c->endTime, &repo.systemClock);
                                return 0;
                        }
                } else {
                        //
                        // On first pass, finalize testing
                        //
                        if (c->testAction == TEST_ACT_TEST) {
                                output_maxrate(connindex);
                                if (monConn >= 0) {
                                        var = sprintf(scratch, "[%d]Test stop received\n", connindex);
                                        send_proc(monConn, scratch, var);
                                }
                                c->testAction = (unsigned int) sHdr->testAction;
                        }
                        return 0;
                }
        } else {
                tspecvar.tv_sec  = TIMEOUT_NOTRAFFIC;
                tspecvar.tv_nsec = 0;
                tspecplus(&repo.systemClock, &tspecvar, &c->endTime);
        }

        //
        // Save receive time for this PDU
        //
        tspeccpy(&c->pduRxTime, &repo.systemClock);

        //
        // Generate warning if peer indicates receive traffic has stopped
        //
        if ((bvar = (BOOL) sHdr->rxStopped) != c->rxStoppedRem) {
                c->rxStoppedRem = bvar; // Save value if changed
                if (c->rxStoppedRem) {  // Only warn if state indicates true
                        if (c->warningCount < WARNING_MSG_LIMIT) {
                                c->warningCount++;
                                output_warning(connindex, WARN_REM_STOPPED);
                        }
                }
        }

        //
        // Check for status message sequence errors, update error count, and generate warning
        // (count is included in subsequent load PDUs to inform peer)
        //
        c->spduSeqErr = 0;
        seqno         = (unsigned int) ntohl(sHdr->spduSeqNo);
        if (seqno >= c->spduSeqNo + 1) {
                if (seqno > c->spduSeqNo + 1) {
                        c->spduSeqErr = (int) (seqno - c->spduSeqNo - 1);
                }
                c->spduSeqNo = seqno;
        } else {
                c->spduSeqErr = UINT16_MAX; // Signal reordered with special value
        }
        if (c->spduSeqErr > 0) { // Only warn if count indicates loss
                if (c->warningCount < WARNING_MSG_LIMIT) {
                        c->warningCount++;
                        output_warning(connindex, WARN_LOC_STATUS);
                }
        }

        //
        // Save sequence error loss, out-of-order, and duplicate stats
        //
        c->seqErrLoss = (unsigned int) ntohl(sHdr->seqErrLoss);
        c->seqErrOoo  = (unsigned int) ntohl(sHdr->seqErrOoo);
        c->seqErrDup  = (unsigned int) ntohl(sHdr->seqErrDup);

        //
        // Save delay info
        //
        c->clockDeltaMin = (int) ntohl(sHdr->clockDeltaMin);
        c->delayVarMin   = ntohl(sHdr->delayVarMin);
        c->delayVarMax   = ntohl(sHdr->delayVarMax);
        c->delayVarSum   = ntohl(sHdr->delayVarSum);
        c->delayVarCnt   = ntohl(sHdr->delayVarCnt);
        c->rttMinimum    = ntohl(sHdr->rttMinimum);
        c->rttSample     = ntohl(sHdr->rttSample);
        c->delayMinUpd   = (BOOL) sHdr->delayMinUpd;

        //
        // Save trial interval info
        //
        c->tiDeltaTime   = (unsigned int) ntohl(sHdr->tiDeltaTime);
        c->tiRxDatagrams = (unsigned int) ntohl(sHdr->tiRxDatagrams);
        c->tiRxBytes     = (unsigned int) ntohl(sHdr->tiRxBytes);

        //
        // Save time reference for this status PDU
        //
        c->spduTime.tv_sec  = (time_t) ntohl(sHdr->spduTime_sec);
        c->spduTime.tv_nsec = (long) ntohl(sHdr->spduTime_nsec);
        if (!repo.isServer) {
                //
                // If not server, use (copy) sending rate parameters specified by server in this status message
                //
                sr_copy(&c->srStruct, &sHdr->srStruct, FALSE);

                //
                // Output debug messages if configured
                //
                if (monConn >= 0 && conf.debug && c->testAction == TEST_ACT_TEST) {
                        if (c->delayMinUpd && c->rttMinimum != INITIAL_MIN_DELAY) {
                                var = sprintf(scratch, MINIMUM_DEBUG, connindex, c->clockDeltaMin, c->rttMinimum);
                                send_proc(monConn, scratch, var);
                        }
                        dvmin = dvavg = 0;
                        if (c->delayVarCnt > 0) {
                                dvmin = c->delayVarMin;
                                dvavg = c->delayVarSum / c->delayVarCnt;
                        }
                        var = -1;
                        if (c->rttSample != INITIAL_MIN_DELAY)
                                var = (int) c->rttSample;
                        var = sprintf(scratch, CLIENT_DEBUG, connindex, c->seqErrLoss, c->seqErrOoo, c->seqErrDup, dvmin, dvavg,
                                      c->delayVarMax, var, get_rate(connindex, NULL, L3DG_OVERHEAD));
                        send_proc(monConn, scratch, var);
                }
        } else {
                //
                // If server, adjust our sending rate based on client traffic info in this status message
                //
                adjust_sending_rate(connindex);
        }

        //
        // Copy last saved sub-interval statistics (as measured by peer receiver) if it is new, which
        // is detected by an updated sequence number
        //
        uvar = (unsigned int) ntohl(sHdr->subIntSeqNo);
        if (uvar != c->subIntSeqNo) {
                c->subIntSeqNo = uvar; // Save it to detect updated stats
                sis_copy(&c->sisSav, &sHdr->sisSav, FALSE);
                if (c->testAction == TEST_ACT_TEST && (!repo.isServer || conf.verbose)) {
                        //
                        // Process and output the latest rate info indicated by receiver
                        //
                        output_currate(connindex);
                }
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Server function to perform sending rate adjustment calculation
//
int adjust_sending_rate(int connindex) {
        register struct connection *c = &conn[connindex];
        unsigned int dvmin, dvavg;
        int var, delay, seqerr;

        //
        // Select algorithm parameters
        //
        seqerr = (int) c->seqErrLoss;
        if (!c->ignoreOooDup) {
                seqerr += (int) (c->seqErrOoo + c->seqErrDup);
        }
        delay = c->lowThresh; // Default to 'no change' if data not available
        dvmin = dvavg = 0;
        if (c->delayVarCnt > 0) {
                dvmin = c->delayVarMin;
                dvavg = c->delayVarSum / c->delayVarCnt;
        }
        if (c->useOwDelVar) {
                // Use average one-way delay variation
                if (c->delayVarCnt > 0) {
                        delay = (int) dvavg;
                }
        } else {
                // Use last sampled round-trip time variation
                if (c->rttSample != INITIAL_MIN_DELAY) {
                        delay = (int) c->rttSample;
                }
        }

        //
        // Adjust sending rate as needed
        // This section of code corresponds to the flowchart in TR-471 section 5.2.1,
        // Sending Rate Search Algorithm, and ITU-T Recommendation Y.1540, Annex B
        //
        if (c->srIndexConf != DEF_SRINDEX_CONF) {
                c->srIndex = c->srIndexConf; // Use static sending rate if specified
        } else {
                if (seqerr <= c->seqErrThresh && delay < c->lowThresh) {
                        if (c->srIndex < repo.hSpeedThresh && c->slowAdjCount < c->slowAdjThresh) {
                                if (c->srIndex + c->highSpeedDelta > repo.hSpeedThresh)
                                        c->srIndex = repo.hSpeedThresh;
                                else
                                        c->srIndex += c->highSpeedDelta;
                                c->slowAdjCount = 0;
                        } else {
                                if (c->srIndex < repo.maxSendingRates - 1)
                                        c->srIndex++;
                        }
                } else if (seqerr > c->seqErrThresh || delay > c->upperThresh) {
                        c->slowAdjCount++;
                        if (c->srIndex < repo.hSpeedThresh && c->slowAdjCount == c->slowAdjThresh) {
                                if (c->srIndex > c->highSpeedDelta * HS_DELTA_BACKUP)
                                        c->srIndex -= c->highSpeedDelta * HS_DELTA_BACKUP;
                                else
                                        c->srIndex = 0;
                        } else {
                                if (c->srIndex > 0)
                                        c->srIndex--;
                        }
                }
        }

        //
        // Display debug info if needed
        //
        if (monConn >= 0 && conf.debug && c->testAction == TEST_ACT_TEST) {
                var = -1;
                if (c->rttSample != INITIAL_MIN_DELAY)
                        var = (int) c->rttSample;
                var = sprintf(scratch, SERVER_DEBUG, connindex, c->seqErrLoss, c->seqErrOoo, c->seqErrDup, dvmin, dvavg,
                              c->delayVarMax, var, c->srIndex);
                send_proc(monConn, scratch, var);
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Process the accumulated sub-interval statistics
//
int proc_subinterval(int connindex, BOOL initialize) {
        register struct connection *c = &conn[connindex];
        struct timespec tspecvar;
        int var;

        //
        // If requested, initialize active sub-interval statistics and exit
        //
        if (initialize) {
                memset(&c->sisAct, 0, sizeof(struct subIntStats));
                c->sisAct.delayVarMin = INITIAL_MIN_DELAY;
                c->sisAct.rttMinimum  = INITIAL_MIN_DELAY;
                tspeccpy(&c->subIntClock, &repo.systemClock);
                c->accumTime = 0;
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Sub-Interval statistics initialized...\n", connindex);
                        send_proc(monConn, scratch, var);
                }
                return 0;
        }

        //
        // Finalize active statistics for this sub-interval and save them
        //
        c->subIntSeqNo++; // Indicate updated stats
        tspecminus(&repo.systemClock, &c->subIntClock, &tspecvar);
        c->sisAct.deltaTime = (uint32_t) tspecusec(&tspecvar); // Measured sub-interval time
        c->accumTime += (unsigned int) tspecmsec(&tspecvar);
        c->sisAct.accumTime = (uint32_t) c->accumTime;
        memcpy(&c->sisSav, &c->sisAct, sizeof(struct subIntStats));

        //
        // Output the current rate if test is still active
        //
        if (c->testAction == TEST_ACT_TEST && (!repo.isServer || conf.verbose)) {
                output_currate(connindex);
        }

        //
        // Re-initialize active sub-interval statistics after saving
        //
        memset(&c->sisAct, 0, sizeof(struct subIntStats));
        c->sisAct.delayVarMin = INITIAL_MIN_DELAY;
        c->sisAct.rttMinimum  = INITIAL_MIN_DELAY;
        tspeccpy(&c->subIntClock, &repo.systemClock);

        return 0;
}
//----------------------------------------------------------------------------
//
// Output sampled data rate and summary statistics
//
int output_currate(int connindex) {
        register struct connection *c = &conn[connindex];
        int i, var;
        unsigned int dvmin, dvavg, rttmin;
        double mbps, delivered = 0;
        char connid[8];
        struct testSummary *ts = &c->testSum;

        //
        // Perform rate calculation and check if max rate so far
        //
        i = 0; // Initialize to single maximum or first bimodal maximum
        c->subIntCount++;
        if (conf.bimodalCount > 0 && c->subIntCount > conf.bimodalCount)
                i++; // Adjust to save as second bimodal maximum
        mbps = get_rate(connindex, &c->sisSav, L3DG_OVERHEAD);
        if (mbps > c->rateMaxL3) {
                memcpy(&c->sisMax[i], &c->sisSav, sizeof(struct subIntStats));
                c->rateMaxL3 = mbps;
        }
        if (conf.bimodalCount > 0 && c->subIntCount == conf.bimodalCount)
                c->rateMaxL3 = 0.0; // Reset for second bimodal maximum

        //
        // Output sampled rate info
        //
        *connid = '\0';
        if (conf.verbose)
                sprintf(connid, "[%d]", connindex);
        if (c->sisSav.rxDatagrams + c->sisSav.seqErrLoss > 0) {
                if (conf.showLossRatio)
                        delivered = (double) c->sisSav.seqErrLoss;
                else
                        delivered = (double) c->sisSav.rxDatagrams * 100.0;
                delivered /= (double) ((double) c->sisSav.rxDatagrams + (double) c->sisSav.seqErrLoss);
        }
        dvmin = dvavg = 0;
        if (c->sisSav.delayVarCnt > 0) {
                dvmin = (unsigned int) c->sisSav.delayVarMin;
                dvavg = (unsigned int) (c->sisSav.delayVarSum / c->sisSav.delayVarCnt);
        }
        rttmin = 0;
        if (c->sisSav.rttMinimum != INITIAL_MIN_DELAY) {
                rttmin = (unsigned int) c->sisSav.rttMinimum;
        }
        if (!conf.summaryOnly) {
                i = 1; // Determine required width of accumulated time field
                if (c->testIntTime > 999)
                        i = 7;
                else if (c->testIntTime > 99)
                        i = 5;
                else if (c->testIntTime > 9)
                        i = 3;
                if (c->subIntCount > 999)
                        i -= 3;
                else if (c->subIntCount > 99)
                        i -= 2;
                else if (c->subIntCount > 9)
                        i -= 1;
                strcpy(scratch2, "%sSub-Interval[%d](sec): %*d, "); // Use variable-width accumulated time for text alignment
                if (!conf.showLossRatio) {
                        strcat(scratch2, DELIVERED_TEXT SUMMARY_TEXT);
                } else {
                        strcat(scratch2, LOSSRATIO_TEXT SUMMARY_TEXT);
                }
                var = (int) ((c->sisSav.accumTime / 100) + 5) / 10;
                var =
                    sprintf(scratch, scratch2, connid, c->subIntCount, i, var, delivered, c->sisSav.seqErrLoss, c->sisSav.seqErrOoo,
                            c->sisSav.seqErrDup, dvmin, dvavg, c->sisSav.delayVarMax, rttmin, c->sisSav.rttMaximum, mbps);
                send_proc(errConn, scratch, var);
        }

        //
        // Accumulate overall test summary statistics
        //
        if (ts->sampleCount == 0) {
                ts->delayVarMin = dvmin;
                ts->delayVarMax = (unsigned int) c->sisSav.delayVarMax;
                ts->delayVarSum = dvavg;
                //
                ts->rttMinimum = rttmin;
                ts->rttMaximum = (unsigned int) c->sisSav.rttMaximum;
        } else {
                if (dvmin < ts->delayVarMin)
                        ts->delayVarMin = dvmin;
                if (c->sisSav.delayVarMax > (uint32_t) ts->delayVarMax)
                        ts->delayVarMax = (unsigned int) c->sisSav.delayVarMax;
                ts->delayVarSum += dvavg;
                //
                if (rttmin < ts->rttMinimum)
                        ts->rttMinimum = rttmin;
                if (c->sisSav.rttMaximum > (uint32_t) ts->rttMaximum)
                        ts->rttMaximum = (unsigned int) c->sisSav.rttMaximum;
        }
        ts->deliveredSum += delivered;
        ts->seqErrLoss += (unsigned int) c->sisSav.seqErrLoss;
        ts->seqErrOoo += (unsigned int) c->sisSav.seqErrOoo;
        ts->seqErrDup += (unsigned int) c->sisSav.seqErrDup;
        ts->rateSumL3 += (double) mbps;
        ts->sampleCount++;

        return 0;
}
//----------------------------------------------------------------------------
//
// Output maximum data rate and overall test summary statistics
//
int output_maxrate(int connindex) {
        register struct connection *c = &conn[connindex];
        char *testtype, connid[8], maxtext[24];
        int i, sibegin, siend, var;
        unsigned int uvar;
        struct testSummary *ts = &c->testSum;

        //
        // Setup header fields
        //
        *connid = '\0';
        if (conf.verbose)
                sprintf(connid, "[%d]", connindex);
        if (c->testType == TEST_TYPE_US) {
                testtype = USTEST_TEXT;
        } else {
                testtype = DSTEST_TEXT;
        }

        //
        // Output summary info
        //
        if (ts->sampleCount > 0) {
                ts->deliveredSum /= (double) ts->sampleCount;
                ts->delayVarSum = (((ts->delayVarSum * 10) / ts->sampleCount) + 5) / 10;
                ts->rateSumL3 /= (double) ts->sampleCount;
        }
        strcpy(scratch2, "%s%s Summary ");
        if (!conf.showLossRatio) {
                strcat(scratch2, DELIVERED_TEXT SUMMARY_TEXT);
        } else {
                strcat(scratch2, LOSSRATIO_TEXT SUMMARY_TEXT);
        }
        var = sprintf(scratch, scratch2, connid, testtype, ts->deliveredSum, ts->seqErrLoss, ts->seqErrOoo, ts->seqErrDup,
                      ts->delayVarMin, ts->delayVarSum, ts->delayVarMax, ts->rttMinimum, ts->rttMaximum, ts->rateSumL3);
        send_proc(errConn, scratch, var);

        //
        // Output delay info
        //
        uvar = 0;
        strcpy(scratch2, "%s%s Minimum " MINIMUM_TEXT);
        if (c->rttMinimum != INITIAL_MIN_DELAY)
                uvar = c->rttMinimum;
        var = sprintf(scratch, scratch2, connid, testtype, c->clockDeltaMin, uvar);
        send_proc(errConn, scratch, var);

        //
        // Output rate info for either single maximum or both bimodal maxima
        //
        sibegin = 1;
        if (conf.bimodalCount >= c->subIntCount) {
                siend = c->subIntCount;
        } else {
                siend = conf.bimodalCount;
        }
        for (i = 0; i < 2; i++) {
                if (conf.bimodalCount == 0) {
                        strcpy(maxtext, "Maximum");
                } else {
                        sprintf(maxtext, "Max[%d-%d]", sibegin, siend);
                }
                strcpy(scratch2, "%s%s %s Mbps(L3/IP): %.2f, Mbps(L2/Eth): %.2f, Mbps(L1/Eth): %.2f, Mbps(L1/Eth+VLAN): %.2f\n");
                var = sprintf(scratch, scratch2, connid, testtype, maxtext, get_rate(connindex, &c->sisMax[i], L3DG_OVERHEAD),
                              get_rate(connindex, &c->sisMax[i], L2DG_OVERHEAD), get_rate(connindex, &c->sisMax[i], L1DG_OVERHEAD),
                              get_rate(connindex, &c->sisMax[i], L0DG_OVERHEAD));
                send_proc(errConn, scratch, var);
                if (conf.bimodalCount == 0 || conf.bimodalCount >= c->subIntCount)
                        break; // Either a single maximum or bimodal count exceeds sub-interval count

                sibegin = conf.bimodalCount + 1;
                siend   = c->subIntCount;
        }

        return 0;
}
//----------------------------------------------------------------------------
//
// Calculate data rate from trial interval statistics OR sub-interval statistics
//
double get_rate(int connindex, struct subIntStats *sis, int overhead) {
        register struct connection *c = &conn[connindex];
        unsigned int delta, dgrams, bytes;
        double mbps = 0;

        if (c->ipProtocol == IPPROTO_IPV6)
                overhead += IPV6_ADDSIZE;

        if (sis == NULL) {
                delta  = c->tiDeltaTime;
                dgrams = c->tiRxDatagrams;
                bytes  = c->tiRxBytes;
        } else {
                delta  = (unsigned int) sis->deltaTime;
                dgrams = (unsigned int) sis->rxDatagrams;
                bytes  = (unsigned int) sis->rxBytes;
        }
        if (delta > 0) {
                mbps = (double) dgrams;
                mbps *= (double) overhead;
                mbps += (double) bytes;
                mbps *= 8.0;
                mbps /= (double) delta;
        }
        return mbps;
}
//----------------------------------------------------------------------------
//
// Stop test at the end of test interval time
//
int stop_test(int connindex) {
        register struct connection *c = &conn[connindex];

        //
        // Clear timer
        //
        tspecclear(&c->timer3Thresh);

        //
        // Signal stop
        //
        c->testAction = TEST_ACT_STOP1; // First phase of test stop

        return 0;
}
//----------------------------------------------------------------------------
//
// Generic connection receive processor
//
int recv_proc(int connindex) {
        register struct connection *c = &conn[connindex];
        int var, recvsize;

        //
        // Specify receive buffer size (minimize for load PDUs to reduce overhead of memory copy)
        //
        if (c->secAction == &service_loadpdu) {
                recvsize = sizeof(struct loadHdr);
        } else {
                recvsize = DEF_BUFFER_SIZE;
        }

        //
        // Issue read
        //
        if (c->subType == SOCK_STREAM || c->connected) {
                repo.rcvDataSize = recv(c->fd, repo.defBuffer, recvsize, 0);

        } else if (c->subType == SOCK_DGRAM) {
                repo.remSasLen   = sizeof(repo.remSas);
                repo.rcvDataSize = recvfrom(c->fd, repo.defBuffer, recvsize, 0, (struct sockaddr *) &repo.remSas, &repo.remSasLen);
        } else {
                repo.rcvDataSize = read(c->fd, repo.defBuffer, recvsize);
        }

        //
        // Validate status
        //
        if (repo.rcvDataSize < 0) {
                repo.rcvDataSize = 0;
                if ((var = receive_trunc(errno, recvsize, sizeof(struct loadHdr))) > 0) {
                        repo.rcvDataSize = var;
                } else if ((var = socket_error(connindex, errno, "RECV/RECVFROM")) > 0) {
                        if (!conf.errSuppress) {
                                send_proc(errConn, scratch, var);
                        }
                }
        } else if ((repo.rcvDataSize == 0) && (c->subType == SOCK_STREAM)) {
                if (monConn >= 0) {
                        var = sprintf(scratch, "[%d]Connection was closed\n", connindex);
                        send_proc(monConn, scratch, var);
                }
                return -1;
        }
        return repo.rcvDataSize;
}
//----------------------------------------------------------------------------
//
// Generic connection send processor
//
int send_proc(int connindex, char *sendbuffer, int sendsize) {
        register struct connection *c = &conn[connindex];
        int var, actual = 0;

        //
        // Prefix send buffer with timestamp for log file write
        //
        if (c->type == T_LOG) {
                var = strftime(scratch2, sizeof(scratch2), TIME_FORMAT, localtime(&repo.systemClock.tv_sec));
                var += sprintf(&scratch2[var], " %s", sendbuffer);
                sendbuffer = scratch2;
                sendsize   = var;
        } else if (sendsize == 0) {
                sendsize = strlen(sendbuffer);
        }

        //
        // Issue send
        //
        if (c->subType == SOCK_STREAM || c->connected) {
                actual = send(c->fd, sendbuffer, sendsize, 0);

        } else if (c->subType == SOCK_DGRAM) {
                actual = sendto(c->fd, sendbuffer, sendsize, 0, (struct sockaddr *) &repo.remSas, repo.remSasLen);
        } else {
                var = c->fd;
                if ((c->type == T_CONSOLE) || (c->type == T_NULL)) {
                        var = STDOUT_FILENO;
                }
                actual = write(var, sendbuffer, sendsize);
        }

        //
        // Validate status
        //
        if (actual < 0) {
                if ((var = socket_error(connindex, errno, "SEND/SENDTO")) > 0) {
                        if (!conf.errSuppress) {
                                send_proc(errConn, scratch, var);
                        }
                }
                return 0;
        }

        //
        // Recycle log file if growth size exceeded
        //
        // NOTE: If file operations fail there is no device to send error messages to
        //
        if (c->type == T_LOG) {
                repo.logFileSize += actual;
                if (repo.logFileSize > conf.logFileMax) {
                        close(c->fd);
                        strcpy(scratch2, conf.logFile);
                        strcat(scratch2, ".old");
                        var              = rename(conf.logFile, scratch2);
                        c->fd            = open(conf.logFile, LOGFILE_FLAGS, LOGFILE_MODE);
                        repo.logFileSize = 0;
                }
        }
        return actual;
}
#ifdef __linux__
//----------------------------------------------------------------------------
//
// Check for send or receive socket error (exclude operation would block indications)
//
// Populate scratch buffer and return length on error
//
int socket_error(int connindex, int error, char *optext) {
        int var = 0;

        if (error != EWOULDBLOCK && error != EAGAIN) {
                var = sprintf(scratch, "[%d]%s ERROR: %s\n", connindex, optext, strerror(error));
        }
        return var;
}
//----------------------------------------------------------------------------
//
// Check for receive truncation (included for completeness, linux truncates silently)
//
int receive_trunc(int error, int requested, int expected) {
        int var = 0;

        if (error == EMSGSIZE && requested == expected) {
                var = requested;
        }
        return var;
}
#endif
//----------------------------------------------------------------------------
//
// Copy sending rate structure with proper network byte order
//
void sr_copy(struct sendingRate *srhost, struct sendingRate *srnet, BOOL hton) {
        //
        // Copy based on direction
        //
        if (hton) {
                srnet->txInterval1 = htonl(srhost->txInterval1);
                srnet->udpPayload1 = htonl(srhost->udpPayload1);
                srnet->burstSize1  = htonl(srhost->burstSize1);
                srnet->txInterval2 = htonl(srhost->txInterval2);
                srnet->udpPayload2 = htonl(srhost->udpPayload2);
                srnet->burstSize2  = htonl(srhost->burstSize2);
                srnet->udpAddon2   = htonl(srhost->udpAddon2);
        } else {
                srhost->txInterval1 = ntohl(srnet->txInterval1);
                srhost->udpPayload1 = ntohl(srnet->udpPayload1);
                srhost->burstSize1  = ntohl(srnet->burstSize1);
                srhost->txInterval2 = ntohl(srnet->txInterval2);
                srhost->udpPayload2 = ntohl(srnet->udpPayload2);
                srhost->burstSize2  = ntohl(srnet->burstSize2);
                srhost->udpAddon2   = ntohl(srnet->udpAddon2);
        }
        return;
}
//----------------------------------------------------------------------------
//
// Copy sub-interval statistics structure with proper network byte order
//
void sis_copy(struct subIntStats *sishost, struct subIntStats *sisnet, BOOL hton) {
        //
        // Copy based on direction
        //
        if (hton) {
                sisnet->rxDatagrams = htonl(sishost->rxDatagrams);
                sisnet->rxBytes     = htonl(sishost->rxBytes);
                sisnet->deltaTime   = htonl(sishost->deltaTime);
                sisnet->seqErrLoss  = htonl(sishost->seqErrLoss);
                sisnet->seqErrOoo   = htonl(sishost->seqErrOoo);
                sisnet->seqErrDup   = htonl(sishost->seqErrDup);
                sisnet->delayVarMin = htonl(sishost->delayVarMin);
                sisnet->delayVarMax = htonl(sishost->delayVarMax);
                sisnet->delayVarSum = htonl(sishost->delayVarSum);
                sisnet->delayVarCnt = htonl(sishost->delayVarCnt);
                sisnet->rttMinimum  = htonl(sishost->rttMinimum);
                sisnet->rttMaximum  = htonl(sishost->rttMaximum);
                sisnet->accumTime   = htonl(sishost->accumTime);
        } else {
                sishost->rxDatagrams = ntohl(sisnet->rxDatagrams);
                sishost->rxBytes     = ntohl(sisnet->rxBytes);
                sishost->deltaTime   = ntohl(sisnet->deltaTime);
                sishost->seqErrLoss  = ntohl(sisnet->seqErrLoss);
                sishost->seqErrOoo   = ntohl(sisnet->seqErrOoo);
                sishost->seqErrDup   = ntohl(sisnet->seqErrDup);
                sishost->delayVarMin = ntohl(sisnet->delayVarMin);
                sishost->delayVarMax = ntohl(sisnet->delayVarMax);
                sishost->delayVarSum = ntohl(sisnet->delayVarSum);
                sishost->delayVarCnt = ntohl(sisnet->delayVarCnt);
                sishost->rttMinimum  = ntohl(sisnet->rttMinimum);
                sishost->rttMaximum  = ntohl(sisnet->rttMaximum);
                sishost->accumTime   = ntohl(sisnet->accumTime);
        }
        return;
}
//----------------------------------------------------------------------------
//
// Output warning message for test anomaly or error condition
//
void output_warning(int connindex, int type) {
        register struct connection *c = &conn[connindex];
        int var;
        char connid[8], location[16];

        if (c->testAction == TEST_ACT_TEST && (!repo.isServer || conf.verbose)) {
                *connid = '\0';
                if (conf.verbose)
                        sprintf(connid, "[%d]", connindex);

                var = 0;
                if (type == WARN_LOC_STATUS || type == WARN_LOC_STOPPED) {
                        strcpy(location, "LOCAL");
                } else {
                        strcpy(location, "REMOTE");
                }
                switch (type) {
                case WARN_LOC_STATUS:
                case WARN_REM_STATUS:
                        var = sprintf(scratch, "%s%s WARNING: Incoming status feedback messages lost (%d)\n", connid, location,
                                      c->spduSeqErr);
                        break;
                case WARN_LOC_STOPPED:
                case WARN_REM_STOPPED:
                        var = sprintf(scratch, "%s%s WARNING: Incoming traffic has completely stopped\n", connid, location);
                        break;
                }
                if (var > 0)
                        send_proc(errConn, scratch, var);
        }
        return;
}
//----------------------------------------------------------------------------
