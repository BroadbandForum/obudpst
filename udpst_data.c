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
 * Len Ciavattone          10/13/2021    Refresh with clang-format
 *                                       Add TR-181 fields & sub-int. in JSON
 *                                       Add JSON bimodal output
 *                                       Add JSON error support to send_proc()
 *                                       Add interface traffic rate support
 * Len Ciavattone          12/08/2021    Add starting sending rate
 * Len Ciavattone          12/17/2021    Add payload randomization
 * Len Ciavattone          12/24/2021    Handle interface byte counter wrap
 * Len Ciavattone          01/08/2022    Check burstsize >1 if forcing to 1
 * Len Ciavattone          02/02/2022    Add rate adj. algo. selection
 * Al Morton               04/12/2022    Type C algoithm, Multiply and Retry
 * Len Ciavattone          12/26/2022    Add random payload size support
 * Len Ciavattone          12/29/2022    Add single test option on server
 * Len Ciavattone          01/14/2023    Add multi-connection support
 * Len Ciavattone          03/22/2023    Add GSO and GRO optimizations
 * Len Ciavattone          03/25/2023    GRO replaced w/recvmmsg+truncation
 * Len Ciavattone          04/04/2023    Add optional rate limiting
 * Len Ciavattone          05/24/2023    Add data output (export) capability
 * Len Ciavattone          10/01/2023    Updated ErrorStatus values
 * Len Ciavattone          12/08/2023    Always handle intf counters as 64-bit
 *
 */

#define UDPST_DATA
#ifdef __linux__
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/ip.h>  // For GSO support
#include <netinet/udp.h> // For GSO support
#ifdef AUTH_KEY_ENABLE
#include <openssl/hmac.h>
#include <openssl/x509.h>
#endif
#else
#include "../udpst_data_alt1.h"
#endif
//
#include "cJSON.h"
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
int output_currate(int);
int output_maxrate(int);
double get_rate(int, struct subIntStats *, int);
#ifdef __linux__
int socket_error(int, int, char *);
int receive_trunc(int, int, int);
#endif
void sis_copy(struct subIntStats *, struct subIntStats *, BOOL);
void output_warning(int, int);
double upd_intf_stats(BOOL);
void output_minimum(int);
void output_debug(int);

//----------------------------------------------------------------------------
//
// External data
//
extern int errConn, monConn, aggConn;
extern char scratch[STRING_SIZE];
extern struct configuration conf;
extern struct repository repo;
extern struct connection *conn;
//
extern cJSON *json_top, *json_output, *json_siArray;
extern char json_errbuf[STRING_SIZE], json_errbuf2[STRING_SIZE];

//----------------------------------------------------------------------------
//
// Global data
//
#define LOSSRATIO_TEXT "LossRatio: %.2E, "
#define DELIVERED_TEXT "Delivered(%%): %6.2f, "
#define SUMMARY_TEXT   "Loss/OoO/Dup: %u/%u/%u, OWDVar(ms): %u/%u/%u, RTTVar(ms): %u-%u, Mbps(L3/IP): %.2f%s\n"
#define MINIMUM_TEXT   "Minimum One-Way Delay(ms): %d [w/clock diff], Round-Trip Time(ms): %u"
#define MINIMUM_FINAL  MINIMUM_TEXT ", Active Connections: %d\n"
#define DEBUG_STATS    "[Loss/OoO/Dup: %u/%u/%u, OWDVar(ms): %u/%u/%u, RTTVar(ms): %d]"
#define CLIENT_DEBUG   "[%d]DEBUG Status Feedback " DEBUG_STATS " Mbps(L3/IP): %.2f\n"
#define SERVER_DEBUG   "[%d]DEBUG Rate Adjustment " DEBUG_STATS " SRIndex: %d\n"
static char scratch2[STRING_SIZE + 32]; // Allow for log file timestamp prefix
static int mmsgDataSize[RECVMMSG_SIZE]; // Received data size of each message

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------
//
// Populate the static part of the our message header
//
static void _populate_header(struct loadHdr *lHdr, struct connection *c, unsigned int rttRespDelay) {
        lHdr->loadId     = htons(LOAD_ID);
        lHdr->testAction = (uint8_t) c->testAction;
        lHdr->rxStopped  = (uint8_t) c->rxStoppedLoc;
        // lpduSeqNo populated by the send function
        // udpPayload populated by the send function
        lHdr->spduSeqErr    = htons((uint16_t) c->spduSeqErr);
        lHdr->spduTime_sec  = htonl((uint32_t) c->spduTime.tv_sec);
        lHdr->spduTime_nsec = htonl((uint32_t) c->spduTime.tv_nsec);
        lHdr->lpduTime_sec  = htonl((uint32_t) repo.systemClock.tv_sec);
        lHdr->lpduTime_nsec = htonl((uint32_t) repo.systemClock.tv_nsec);
        lHdr->rttRespDelay  = htons((uint16_t) rttRespDelay);
        lHdr->reserved1     = 0;
}
//
// Randomize payload of datagram (via single call of random())
//
static void _randomize_payload(char *buffer, unsigned int length) {
        register long int rvar = random(); // Obtain random value (0 - RAND_MAX)
        register long int *b, *rd = (long int *) repo.randData;
        register unsigned int len = length, i;

#if LONG_MAX > 2147483647L
        rvar |= rvar << 32; // Copy value to upper half when using 64 bits
#endif
        //
        // Randomize initial bytes while aligning buffer on long int boundary
        //
        i = 0;
        while (len && (unsigned long int) buffer % sizeof(long int)) {
                *buffer++ = (char) (rvar >> i++); // Keep it very simple
                len--;
        }

        //
        // Randomize majority as long int values using randomized seed data
        //
        b = (long int *) buffer;
        while (len > sizeof(long int)) {
                *b++ = rvar ^ *rd++; // Also simple (but more efficient)
                len -= sizeof(long int);
        }
        buffer = (char *) b;

        //
        // Randomize any remaining bytes
        //
        i = 8; // Something different than initial bytes
        while (len) {
                *buffer++ = (char) (rvar >> i++); // Back to very simple
                len--;
        }
}

#if defined(HAVE_SENDMMSG)
#if defined(HAVE_GSO)
//
// Send a burst of messages using GSO (Generic Segmentation Offload)
//
static void _sendmmsg_gso(int connindex, int totalburst, int burstsize, unsigned int payload, unsigned int addon) {
        register struct connection *c = &conn[connindex];
        char *sndbuf, *nextsndbuf, cmsgbuf[GSO_CMSG_SIZE * MMSG_SEGMENTS] = {0};
        unsigned int uvar, rttrd = 0, totalsize;
        int i, j, var;
        struct cmsghdr *cmsg;
        struct mmsghdr mmsg[MMSG_SEGMENTS];
        struct iovec iov[MMSG_SEGMENTS];
        struct timespec tspecvar;

        //
        // Calculate RTT response delay
        //
        if (tspecisset(&c->pduRxTime)) {
                tspecminus(&repo.systemClock, &c->pduRxTime, &tspecvar);
                rttrd = (unsigned int) tspecmsec(&tspecvar);
        }

        //
        // Prepare send structures
        //
        memset(mmsg, 0, sizeof(mmsg));
        if (c->randPayload) {
                sndbuf = repo.sndBufRand;
        } else {
                sndbuf = repo.sndBuffer;
        }
        j    = 0; // Message count
        cmsg = (struct cmsghdr *) cmsgbuf;
        while (totalburst > 0) {
                //
                // Fill send buffer until GSO limit or burst completion
                //
                totalsize  = 0;
                nextsndbuf = sndbuf;
                for (i = 0; i < totalburst; i++) {
                        if (i < burstsize)
                                uvar = payload;
                        else
                                uvar = addon;
                        //
                        // Check for GSO limits
                        //
                        if (i >= UDP_MAX_SEGMENTS) // Segment limit
                                break;
                        if (totalsize + uvar > IP_MAXPACKET) // Size limit
                                break;
                        //
                        // Build load PDU (including corresponding control message on first one)
                        //
                        struct loadHdr *lHdr = (struct loadHdr *) nextsndbuf;
                        _populate_header(lHdr, c, rttrd);
                        lHdr->lpduSeqNo  = htonl((uint32_t) ++c->lpduSeqNo);
                        lHdr->udpPayload = htons((uint16_t) uvar);
                        if (c->randPayload) {
                                _randomize_payload((char *) lHdr + sizeof(struct loadHdr), uvar - sizeof(struct loadHdr));
                        }
                        if (i == 0) {
                                cmsg->cmsg_len                  = GSO_CMSG_LEN;
                                cmsg->cmsg_level                = SOL_UDP;
                                cmsg->cmsg_type                 = UDP_SEGMENT;
                                *((uint16_t *) CMSG_DATA(cmsg)) = (uint16_t) uvar;
                        }
                        totalsize += uvar;
                        nextsndbuf += payload;
                }
                totalburst -= i;
                if (burstsize > 0)
                        burstsize -= i;

                //
                // Setup message structure for buffer
                //
                iov[j].iov_base                = (void *) sndbuf;
                iov[j].iov_len                 = (size_t) totalsize;
                mmsg[j].msg_hdr.msg_iov        = &iov[j];
                mmsg[j].msg_hdr.msg_iovlen     = 1;
                mmsg[j].msg_hdr.msg_control    = cmsg;
                mmsg[j].msg_hdr.msg_controllen = GSO_CMSG_SIZE;
                j++;

                //
                // Advance to next send buffer
                //
                sndbuf += DEF_BUFFER_SIZE;
                cmsg = (struct cmsghdr *) ((char *) cmsg + GSO_CMSG_SIZE);
        }

        //
        // Send complete burst with single system call
        //
        // NOTE: Certain error conditions are expected when overloading an interface
        //
        var = sendmmsg(c->fd, mmsg, j, 0);
        if (var == -1 && errno == EINVAL) { // Flag GSO incompatibility
                var = sprintf(scratch, "ERROR: GSO incompatible with IP fragmentation (disable jumbo sizes or increase MTU)\n");
                send_proc(errConn, scratch, var);
                tspeccpy(&c->endTime, &repo.systemClock); // End testing
                return;
        }
        if (!conf.errSuppress) {
                if (var < 0) {
                        //
                        // An error of EAGAIN (Resource temporarily unavailable) indicates the send buffer is full
                        //
                        if ((var = socket_error(connindex, errno, "SENDMMSG+GSO")) > 0)
                                send_proc(errConn, scratch, var);

                } else if (var < j) {
                        //
                        // Not all messages sent indicates the send buffer is full
                        //
                        var = sprintf(scratch, "[%d]SENDMMSG+GSO INCOMPLETE: Only %d out of %d sent\n", connindex, var, j);
                        send_proc(errConn, scratch, var);
                }
        }
}
#else

//
// Send a burst of messages using the Linux 3.0+ only sendmmsg syscall
//
static void _sendmmsg_burst(int connindex, int totalburst, int burstsize, unsigned int payload, unsigned int addon) {
        register struct connection *c = &conn[connindex];
        static struct mmsghdr mmsg[MAX_BURST_SIZE]; // Static array
        static struct iovec iov[MAX_BURST_SIZE];    // Static array
        unsigned int uvar, rttrd = 0;
        char *nextsndbuf;
        int i, var;
        struct timespec tspecvar;

        //
        // Calculate RTT response delay
        //
        if (tspecisset(&c->pduRxTime)) {
                tspecminus(&repo.systemClock, &c->pduRxTime, &tspecvar);
                rttrd = (unsigned int) tspecmsec(&tspecvar);
        }

        //
        // Prepare send structures
        //
        memset(mmsg, 0, totalburst * sizeof(struct mmsghdr));
        if (c->randPayload) {
                nextsndbuf = repo.sndBufRand;
        } else {
                nextsndbuf = repo.sndBuffer;
        }
        for (i = 0; i < totalburst; i++) {
                struct loadHdr *lHdr = (struct loadHdr *) nextsndbuf;
                _populate_header(lHdr, c, rttrd);
                lHdr->lpduSeqNo = htonl((uint32_t) ++c->lpduSeqNo);
                if (i < burstsize)
                        uvar = payload;
                else
                        uvar = addon;
                lHdr->udpPayload = htons((uint16_t) uvar);
                if (c->randPayload) {
                        _randomize_payload((char *) lHdr + sizeof(struct loadHdr), uvar - sizeof(struct loadHdr));
                }

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
#endif // HAVE_GSO
#else

//
// Send a burst of messages using the slower but more widely available sendmsg syscall
//
static void _sendmsg_burst(int connindex, int totalburst, int burstsize, unsigned int payload, unsigned int addon) {
        register struct connection *c = &conn[connindex];
        struct msghdr msg;
        struct iovec iov;
        unsigned int uvar, rttrd = 0;
        int i;
        struct loadHdr *lHdr;
        struct timespec tspecvar;

        //
        // Calculate RTT response delay
        //
        if (tspecisset(&c->pduRxTime)) {
                tspecminus(&repo.systemClock, &c->pduRxTime, &tspecvar);
                rttrd = (unsigned int) tspecmsec(&tspecvar);
        }

        //
        // Prepare send structures
        //
        memset((void *) &msg, 0, sizeof(struct msghdr));
        if (c->randPayload) {
                lHdr = (struct loadHdr *) repo.sndBufRand;
        } else {
                lHdr = (struct loadHdr *) repo.sndBuffer;
        }
        _populate_header(lHdr, c, rttrd);

        for (i = 0; i < totalburst; i++) {
                int var;
                lHdr->lpduSeqNo = htonl((uint32_t) ++c->lpduSeqNo);
                if (i < burstsize)
                        uvar = payload;
                else
                        uvar = addon;
                lHdr->udpPayload = htons((uint16_t) uvar);
                if (c->randPayload) {
                        _randomize_payload((char *) lHdr + sizeof(struct loadHdr), uvar - sizeof(struct loadHdr));
                }

                //
                // Setup corresponding message structure
                //
                iov.iov_base   = (void *) lHdr;
                iov.iov_len    = (size_t) uvar;
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
                                if ((var = socket_error(connindex, errno, "SENDMSG")) > 0)
                                        send_proc(errConn, scratch, var);
                        }
                }
        }
}
#endif // HAVE_SENDMMSG

//
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
        int var, burstsize, totalburst, txintpri, txintalt;
        unsigned int payload, addon;
        BOOL randpayload;
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
        randpayload = FALSE;
        if (transmitter == 1) {
                payload = (unsigned int) (sr->udpPayload1 & ~SRATE_RAND_BIT);
                if (sr->udpPayload1 & SRATE_RAND_BIT)
                        randpayload = TRUE;
                burstsize = (int) sr->burstSize1;
                addon     = 0;
        } else {
                payload = (unsigned int) (sr->udpPayload2 & ~SRATE_RAND_BIT);
                if (sr->udpPayload2 & SRATE_RAND_BIT)
                        randpayload = TRUE;
                burstsize = (int) sr->burstSize2;
                addon     = (unsigned int) (sr->udpAddon2 & ~SRATE_RAND_BIT);
        }

        //
        // If IPv6 reduce payload to maintain L3 packet sizes
        //
        if (c->ipProtocol == IPPROTO_IPV6) {
                if (payload >= MIN_PAYLOAD_SIZE)
                        payload -= IPV6_ADDSIZE;
                if (addon >= MIN_PAYLOAD_SIZE)
                        addon -= IPV6_ADDSIZE;
        }

        //
        // If designated as random, use stored size as max when calculating size
        //
        var = MIN_PAYLOAD_SIZE;
        if (c->ipProtocol == IPPROTO_IPV6) {
                var -= IPV6_ADDSIZE;
        }
        if (payload > 0 && randpayload) {
                payload = getuniform(var, payload);
        }
        if (addon > 0 && (sr->udpAddon2 & SRATE_RAND_BIT)) {
                addon = getuniform(var, addon);
        }

        //
        // Handle test stop in progress
        //
        if (c->testAction != TEST_ACT_TEST) {
                if (burstsize > 1)
                        burstsize = 1; // Reduce load w/min burst size
                if (repo.isServer) {
                        if (conf.verbose && c->testAction == TEST_ACT_STOP1) {
                                int var = sprintf(scratch, "[%d]Sending test stop\n", connindex);
                                send_proc(monConn, scratch, var);
                        }
                        c->testAction = TEST_ACT_STOP2; // Second phase of test stop
                } else {
                        //
                        // The PDU sent in this pass will confirm the test stop back to the server,
                        // schedule an immediate/subsequent test end
                        //
                        tspeccpy(&c->endTime, &repo.systemClock);
                }
                if (repo.endTimeStatus > STATUS_WARNMAX)     // Declare success, but retain warnings
                        repo.endTimeStatus = STATUS_SUCCESS; // ErrorStatus
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
        // Initialize interface stats on first PDU if sysfs FD is valid
        //
        if (repo.intfFD >= 0 && !tspecisset(&repo.intfTime)) {
                upd_intf_stats(TRUE);
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

#if defined(HAVE_SENDMMSG)
#if defined(HAVE_GSO)
        _sendmmsg_gso(connindex, totalburst, burstsize, payload, addon);
#else
        _sendmmsg_burst(connindex, totalburst, burstsize, payload, addon);
#endif // HAVE_GSO
#else
        _sendmsg_burst(connindex, totalburst, burstsize, payload, addon);
#endif // HAVE_SENDMMSG

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
        unsigned int uvar, seqno, rttrd, payload;
        struct loadHdr *lHdr = (struct loadHdr *) repo.rcvDataPtr;
        struct timespec tspecvar, tspecdelta;
        char *nulloutput = ",,,,\n";

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
                                tspeccpy(&c->endTime, &repo.systemClock);
                                return 0;
                        }
                } else {
                        //
                        // On first pass, finalize testing
                        //
                        if (c->testAction == TEST_ACT_TEST) {
                                if (conf.verbose) {
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
        // Update traffic stats (use size specified in PDU, actual receive may have been truncated)
        //
        payload = (unsigned int) ntohs(lHdr->udpPayload);
        c->sisAct.rxDatagrams++;
        c->sisAct.rxBytes += (uint64_t) payload;
        c->tiRxDatagrams++;
        c->tiRxBytes += payload;

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
                c->lpduHistBuf[c->lpduHistIdx] = seqno; // Save sequence number in history buffer
                ++c->lpduHistIdx;                       // Advance history buffer index
                c->lpduHistIdx &= LPDU_HISTORY_MASK;    // Maintain index limit
        }
        //
        // Calculate one-way clock delta (used again further down)
        //
        tspecvar.tv_sec  = (time_t) ntohl(lHdr->lpduTime_sec);
        tspecvar.tv_nsec = (long) ntohl(lHdr->lpduTime_nsec);
        tspecminus(&repo.systemClock, &tspecvar, &tspecdelta);
        delta = (int) tspecmsec(&tspecdelta);
        if (c->outputFPtr != NULL) { // Start output data with one-way values
                fprintf(c->outputFPtr, "%u,%u,%ld.%06ld,%ld.%06ld,%d", seqno, payload, (long) tspecvar.tv_sec,
                        tspecvar.tv_nsec / NSECINUSEC, (long) repo.systemClock.tv_sec, repo.systemClock.tv_nsec / NSECINUSEC,
                        delta);
        }
        if (var > 0) {
                if (c->outputFPtr != NULL) { // Finalize output data with null entries
                        fputs(nulloutput, c->outputFPtr);
                }
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
                // Adjust RTT based on delay between when status PDU was received and load PDU sent
                //
                if ((rttrd = (unsigned int) ntohs(lHdr->rttRespDelay)) <= uvar) {
                        uvar -= rttrd;
                } else if (rttrd == uvar + 1) { // Allow for rounding adjustment on either end
                        uvar = 0;
                }
                if (c->outputFPtr != NULL) { // Finalize output data with RTT values
                        fprintf(c->outputFPtr, ",%ld.%06ld,%ld.%06ld,%u,%u\n", (long) tspecvar.tv_sec,
                                tspecvar.tv_nsec / NSECINUSEC, (long) repo.systemClock.tv_sec,
                                repo.systemClock.tv_nsec / NSECINUSEC, rttrd, uvar);
                }
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
        } else {
                if (c->outputFPtr != NULL) { // Finalize output data with null entries
                        fputs(nulloutput, c->outputFPtr);
                }
        }

        //
        // Process one-way clock delta (calculated above) and delay variation for this load PDU
        //
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
        struct timespec tspecvar;
        struct sendingRate *sr;
        struct statusHdr *sHdr = (struct statusHdr *) repo.defBuffer;

        //
        // Check for test stop in progress, else reset status send timer
        //
        if (c->testAction != TEST_ACT_TEST) {
                tspecclear(&c->timer1Thresh); // Stop subsequent status messages
                if (repo.isServer) {
                        if (conf.verbose && c->testAction == TEST_ACT_STOP1) {
                                var = sprintf(scratch, "[%d]Sending test stop\n", connindex);
                                send_proc(monConn, scratch, var);
                        }
                        c->testAction = TEST_ACT_STOP2; // Second phase of test stop
                } else {
                        //
                        // The PDU sent in this pass will confirm the test stop back to the server,
                        // schedule an immediate/subsequent test end
                        //
                        tspeccpy(&c->endTime, &repo.systemClock);
                }
                if (repo.endTimeStatus > STATUS_WARNMAX)     // Declare success, but retain warnings
                        repo.endTimeStatus = STATUS_SUCCESS; // ErrorStatus
        } else {
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = (long) (c->trialInt * NSECINMSEC);
                tspecplus(&repo.systemClock, &tspecvar, &c->timer1Thresh);

                //
                // Only continue if some data has been received (initial load PDUs could still be in transit)
                //
                if (c->lpduSeqNo == 0) {
                        if (conf.verbose) {
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
        // Initialize interface stats on first PDU if sysfs FD is valid
        //
        if (repo.intfFD >= 0 && !tspecisset(&repo.intfTime)) {
                upd_intf_stats(TRUE);
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
                // Output verbose/debug messages if configured
                //
                if (conf.verbose && c->testAction == TEST_ACT_TEST) {
                        if (c->delayMinUpd && c->rttMinimum != INITIAL_MIN_DELAY) {
                                output_minimum(connindex);
                        }
                        if (conf.debug)
                                output_debug(connindex);
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
                if ((int) tspecmsec(&tspecvar) > var) {
                        if (!repo.isServer && (c->subIntCount > conn[aggConn].subIntCount)) {
                                //
                                // Process aggregate connection if this one has started the next sub-interval. This can
                                // happen when active connections fail during a test.
                                //
                                output_currate(aggConn);
                        }
                        proc_subinterval(connindex, FALSE);
                }
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
        unsigned int uvar, seqno;
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
                                tspeccpy(&c->endTime, &repo.systemClock);
                                return 0;
                        }
                } else {
                        //
                        // On first pass, finalize testing
                        //
                        if (c->testAction == TEST_ACT_TEST) {
                                if (conf.verbose) {
                                        var = sprintf(scratch, "[%d]Test stop received\n", connindex);
                                        send_proc(monConn, scratch, var);
                                }
                                c->testAction = (int) sHdr->testAction;
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
                // Output verbose/debug messages if configured
                //
                if (conf.verbose && c->testAction == TEST_ACT_TEST) {
                        if (c->delayMinUpd && c->rttMinimum != INITIAL_MIN_DELAY) {
                                output_minimum(connindex);
                        }
                        if (conf.debug)
                                output_debug(connindex);
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
                //
                // Process and output the latest rate info indicated by receiver
                //
                if (c->testAction == TEST_ACT_TEST) {
                        if (!repo.isServer || conf.verbose) {
                                if (!repo.isServer && (c->subIntCount > conn[aggConn].subIntCount)) {
                                        //
                                        // Process aggregate connection if this one has started the next sub-interval. This can
                                        // happen when active connections fail during a test.
                                        //
                                        output_currate(aggConn);
                                }
                                output_currate(connindex);
                        }
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
        //
        if (c->srIndexConf != DEF_SRINDEX_CONF && !c->srIndexIsStart) {
                c->srIndex = c->srIndexConf; // Use static sending rate if not specified as starting point

        } else if (c->rateAdjAlgo == CHTA_RA_ALGO_B) {
                //
                // This section of code corresponds to the flowchart in TR-471 section 5.2.1,
                // Sending Rate Search Algorithm, and ITU-T Recommendation Y.1540, Annex B
                //
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
        } else if (c->rateAdjAlgo == CHTA_RA_ALGO_C) {
                if (c->algoCRetryThresh == 0)
                        c->algoCRetryThresh = RETRY_THRESH_ALGOC; // Keep non-zero initialization local to algorithm
                //
                // Multiplicative adjust sending rate : 1.5x previous rate : with retry after waiting
                // This section of code provides an optional algorithm, with the properties of faster search to the
                // max region, meaning less time when errors might end a fast search, and retry fast if that happens.
                //
                if (seqerr <= c->seqErrThresh && delay < c->lowThresh) {
                        if (c->srIndex < repo.hSpeedThresh && c->slowAdjCount < c->slowAdjThresh) { // Congestion not detected
                                if (c->srIndex * 2 > repo.hSpeedThresh) { // If no room to jump within high-speed threshold
                                        c->srIndex = repo.hSpeedThresh;   // Truncate jump at high-speed threshold
                                } else {
                                        if (c->srIndex == 0)
                                                c->srIndex++; // Pre-increment to deal with zero index

                                        if (c->algoCUpdate == TRUE) { // Halve the multiplicative rate, using algoCUpdate
                                                c->srIndex *= 2;      // Jump forward (while staying below high-speed threshold)
                                                c->algoCUpdate = FALSE;
                                        } else {
                                                c->algoCUpdate = TRUE;
                                        }
                                }
                                c->slowAdjCount = 0; // Reset congestion detection counter
                        } else {
                                if (c->srIndex < repo.maxSendingRates - 1) {
                                        c->srIndex++;         // Increment index (slow path)
                                        c->algoCRetryCount++; // Increment waiting count until retry fast ramp-up
                                }
                                if (c->algoCRetryCount >= c->algoCRetryThresh) {
                                        c->slowAdjCount    = 0; // Retry fast ramp-up again
                                        c->algoCRetryCount = 0; // Clear variables to enable fast ramp-up
                                        c->algoCRetryThresh +=
                                            RETRY_THRESH_ALGOC; // Use higher wait threshold for the next fast ramp-up
                                }
                        }
                } else if (seqerr > c->seqErrThresh || delay > c->upperThresh) {
                        c->slowAdjCount++;
                        if (c->srIndex < repo.hSpeedThresh && c->slowAdjCount == c->slowAdjThresh) { // Congestion detected
                                if (c->srIndex > c->highSpeedDelta * HS_DELTA_BACKUP) {              // If room to jump backward
                                        c->srIndex -=
                                            c->highSpeedDelta * HS_DELTA_BACKUP; // Large jump backward (staying above start)
                                } else {
                                        c->srIndex = 0; // Jump backward to start
                                }
                        } else {
                                if (c->srIndex > 0) {
                                        c->srIndex--;         // Decrement index (slow path)
                                        c->algoCRetryCount++; // Increment waiting count until fast ramp-up retry

                                        if (c->algoCRetryCount >= c->algoCRetryThresh) {
                                                c->slowAdjCount    = 0; // Retry fast ramp-up again
                                                c->algoCRetryCount = 0; // Use the same thresholds in the next fast ramp-up
                                        }
                                }
                        }
                }
        }
#ifdef RATE_LIMITING
        //
        // Perform rate limiting when bandwidth management is used (-B mbps) by limiting the maximum sending rate index
        //
        // NOTE: This is for test purposes only and only needs to be enabled on the server. It is intended for testing
        // speeds below the actual achievable maximum, generally as part of server scale testing. For example, simulating
        // low-speed tests when the client and server are actually connected via high speed.
        //
        if (c->maxBandwidth > 0) {
                //
                // Enforce limit directly when index is equal to bandwidth, else find sending rate index that covers bandwidth
                //
                int i = c->maxBandwidth, bw = c->maxBandwidth;
                if (c->maxBandwidth > 1000) { // If index != bandwidth
                        struct sendingRate *sr;
                        for (i = 1001, sr = &repo.sendingRates[i]; i < repo.maxSendingRates; i++, sr++) {
                                bw = 0; // Simplified bandwidth calculation (random sizes ignored)
                                if (sr->txInterval1 > 0)
                                        bw += ((sr->udpPayload1 + L3DG_OVERHEAD) * sr->burstSize1 * 8) / sr->txInterval1;
                                if (sr->txInterval2 > 0) {
                                        if (sr->udpPayload2 > 0)
                                                bw += ((sr->udpPayload2 + L3DG_OVERHEAD) * sr->burstSize2 * 8) / sr->txInterval2;
                                        if (sr->udpAddon2 > 0)
                                                bw += ((sr->udpAddon2 + L3DG_OVERHEAD) * 8) / sr->txInterval2;
                                }
                                if (bw >= c->maxBandwidth)
                                        break;
                        }
                }
                if (c->srIndex > i) // Enforce limit
                        c->srIndex = i;
                if (conf.verbose && c->spduSeqNo == 1) { // Generate notification once per test
                        var = sprintf(scratch, "[%d]RATE_LIMITING: Rate adjustment limited to sending rate index %d (%d Mbps)\n",
                                      connindex, i, bw);
                        send_proc(errConn, scratch, var);
                }
        }
#endif // RATE_LIMITING

        //
        // Output debug messages if configured
        //
        if (conf.verbose && conf.debug && c->testAction == TEST_ACT_TEST) {
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

        //
        // If not doing initialization
        //
        if (!initialize) {
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
                // Process and output our latest rate info as receiver
                //
                if (c->testAction == TEST_ACT_TEST) {
                        if (!repo.isServer || conf.verbose) {
                                output_currate(connindex);
                        }
                }
        }

        //
        // (Re)initialize active sub-interval statistics after saving
        //
        memset(&c->sisAct, 0, sizeof(struct subIntStats));
        c->sisAct.delayVarMin = INITIAL_MIN_DELAY;
        c->sisAct.rttMinimum  = INITIAL_MIN_DELAY;
        tspeccpy(&c->subIntClock, &repo.systemClock);
        if (initialize)
                c->accumTime = 0;

        return 0;
}
//----------------------------------------------------------------------------
//
// Aggregate query processing
//
int agg_query_proc(int connindex) {
        register struct connection *a = &conn[connindex];
        int var;
        struct timespec tspecvar;

        //
        // Query aggregate connection and overall state of testing
        //
        if (repo.actConnCount < conf.minConnCount) { // Active test count is below minimum
                var = sprintf(scratch, "ERROR: Minimum required connections (%d) unavailable\n", conf.minConnCount);
                send_proc(errConn, scratch, var);
                if (repo.endTimeStatus <= STATUS_WARNMAX)                          // Retain any original error
                        repo.endTimeStatus = STATUS_CONN_ERRBASE + ERROR_CONN_MIN; // ErrorStatus
                tspeccpy(&a->endTime, &repo.systemClock);                          // Trigger process shutdown

        } else if (repo.maxConnIndex == aggConn) { // All test connections finished/failed (only aggregate exists)
                //
                // Process aggregate connection for the final sub-interval if some connections haven't been
                // accounted for. This can happen when active connections fail during the last sub-interval.
                //
                if (repo.sisConnCount > 0) {
                        output_currate(connindex);
                }
                //
                // Output maximums and end testing
                //
                if (repo.testSum.sampleCount > 0) {
                        output_maxrate(connindex);
                }
                tspeccpy(&a->endTime, &repo.systemClock); // Trigger process shutdown
        } else {
                //
                // Reset aggregate query timer
                //
                tspecvar.tv_sec  = 0;
                tspecvar.tv_nsec = AGG_QUERY_TIME * NSECINMSEC;
                tspecplus(&repo.systemClock, &tspecvar, &a->timer1Thresh);

                //
                // Process aggregate sub-interval stats if all active connections have done so individually. This is the
                // normal method by which the aggregate is processed (when no connections have failed).
                //
                if (repo.sisConnCount == repo.actConnCount) {
                        output_currate(connindex);
                }
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Output sampled data rate and summary statistics
//
int output_currate(int connindex) {
        register struct connection *c = &conn[connindex], *a;
        int i, var, sec;
        unsigned int dvmin, dvavg, rttmin;
        double dvar, mbps, sent, delivered = 0.0, intfmbps = 0.0;
        char connid[8], intfrate[16];
        struct testSummary *ts = &repo.testSum;

        //
        // Do not allow sub-interval accumulated time to exceed test time
        //
        sec = (int) ((c->sisSav.accumTime / 100) + 5) / 10;
        if (sec > conf.testIntTime)
                return 0;

        //
        // Increment sub-interval count and obtain sub-interval rate info
        //
        c->subIntCount++;
        if (connindex != aggConn) {
                mbps = get_rate(connindex, &c->sisSav, L3DG_OVERHEAD);
                if (!repo.isServer) {
                        //
                        // Accumulate all rate types into aggregates
                        //
                        repo.siAggRateL3 += mbps;
                        repo.siAggRateL2 += get_rate(connindex, &c->sisSav, L2DG_OVERHEAD);
                        repo.siAggRateL1 += get_rate(connindex, &c->sisSav, L1DG_OVERHEAD);
                        repo.siAggRateL0 += get_rate(connindex, &c->sisSav, L0DG_OVERHEAD);
                }
                //
                // Obtain interface rate at first non-aggregate sub-interval
                //
                if (repo.intfFD >= 0 && repo.sisConnCount == 0) {
                        repo.intfMbps = upd_intf_stats(FALSE); // Save for subsequent use by aggregate connection
                }
                repo.sisConnCount++; // Increment connection count for this sub-interval
        } else {
                mbps              = repo.siAggRateL3; // Previously accumulated aggregate
                intfmbps          = repo.intfMbps;    // Previously obtained interface rate
                repo.sisConnCount = 0;                // Reset counter for next sub-interval
        }

        //
        // Check if aggregate maximum so far
        //
        if (connindex == aggConn) {
                i = 0; // Initialize to single maximum or first bimodal maximum
                if (conf.bimodalCount > 0 && c->subIntCount > conf.bimodalCount) {
                        i++; // Adjust to save as second bimodal maximum
                }
                var = 0;
                if (!conf.intfForMax) {
                        if (mbps > repo.rateMaxL3[i]) // Use test traffic for maximum
                                var = 1;
                } else {
                        if (intfmbps > repo.intfMax[i]) // Use interface traffic for maximum
                                var = 1;
                }
                //
                // If new max save sub-interval time, stats, and rates
                //
                if (var) {
                        tspeccpy(&repo.timeOfMax[i], &repo.systemClock);
                        repo.actConnections[i] = repo.actConnCount;
                        memcpy(&repo.sisMax[i], &c->sisSav, sizeof(struct subIntStats));
                        repo.rateMaxL3[i] = mbps;
                        repo.rateMaxL2[i] = repo.siAggRateL2;
                        repo.rateMaxL1[i] = repo.siAggRateL1;
                        repo.rateMaxL0[i] = repo.siAggRateL0;
                        repo.intfMax[i]   = intfmbps;
                }
        }

        //
        // Merge non-aggregate connection stats into aggregate connection
        //
        if (connindex != aggConn && !repo.isServer) {
                a = &conn[aggConn]; // Aggregate connection pointer
                if (repo.sisConnCount == 1 && a->subIntCount == 0) {
                        // Initialize if first non-aggregate connection AND prior to first aggregate sub-interval
                        a->clockDeltaMin      = c->clockDeltaMin;
                        a->rttMinimum         = c->rttMinimum;
                        a->sisSav.delayVarMin = INITIAL_MIN_DELAY;
                        a->sisSav.rttMinimum  = INITIAL_MIN_DELAY;
                } else {
                        if (c->clockDeltaMin < a->clockDeltaMin)
                                a->clockDeltaMin = c->clockDeltaMin;
                        if (c->rttMinimum < a->rttMinimum)
                                a->rttMinimum = c->rttMinimum;
                }
                a->sisSav.rxDatagrams += c->sisSav.rxDatagrams;
                a->sisSav.rxBytes += c->sisSav.rxBytes;
                a->sisSav.deltaTime += c->sisSav.deltaTime;
                a->sisSav.seqErrLoss += c->sisSav.seqErrLoss;
                a->sisSav.seqErrOoo += c->sisSav.seqErrOoo;
                a->sisSav.seqErrDup += c->sisSav.seqErrDup;
                if (c->sisSav.delayVarMin < a->sisSav.delayVarMin)
                        a->sisSav.delayVarMin = c->sisSav.delayVarMin;
                a->sisSav.delayVarSum += c->sisSav.delayVarSum;
                a->sisSav.delayVarCnt += c->sisSav.delayVarCnt;
                if (c->sisSav.delayVarMax > a->sisSav.delayVarMax)
                        a->sisSav.delayVarMax = c->sisSav.delayVarMax;
                if (c->sisSav.rttMinimum < a->sisSav.rttMinimum)
                        a->sisSav.rttMinimum = c->sisSav.rttMinimum;
                if (c->sisSav.rttMaximum > a->sisSav.rttMaximum)
                        a->sisSav.rttMaximum = c->sisSav.rttMaximum;
                //
                a->sisSav.accumTime = c->sisSav.accumTime; // Use accumulated time of last test connection processed
        }

        //
        // Output sampled rate info
        //
        *connid = '\0';
        if (conf.verbose)
                sprintf(connid, "[%d]", connindex);
        sent = (double) c->sisSav.rxDatagrams + (double) c->sisSav.seqErrLoss;
        if (sent > 0.0) {
                if (conf.showLossRatio)
                        delivered = (double) c->sisSav.seqErrLoss / sent;
                else
                        delivered = ((double) c->sisSav.rxDatagrams * 100.0) / sent;
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
                if (!conf.jsonOutput && (conf.verbose || connindex == aggConn)) {
                        i = 3;
                        if (c->subIntCount > 9)
                                i--;
                        strcpy(scratch2, "%sSub-Interval[%d](sec): %*d, ");
                        if (!conf.showLossRatio) {
                                strcat(scratch2, DELIVERED_TEXT SUMMARY_TEXT);
                        } else {
                                strcat(scratch2, LOSSRATIO_TEXT SUMMARY_TEXT);
                        }
                        *intfrate = '\0';
                        if (repo.intfFD >= 0 && connindex == aggConn) { // Append interface rate to L3/IP rate
                                snprintf(intfrate, sizeof(intfrate), " [%.2f]", intfmbps);
                        }
                        var = sprintf(scratch, scratch2, connid, c->subIntCount, i, sec, delivered, c->sisSav.seqErrLoss,
                                      c->sisSav.seqErrOoo, c->sisSav.seqErrDup, dvmin, dvavg, c->sisSav.delayVarMax, rttmin,
                                      c->sisSav.rttMaximum, mbps, intfrate);
                        send_proc(errConn, scratch, var);
                } else if (conf.jsonOutput && connindex == aggConn) {
                        //
                        // Create JSON sub-interval array if needed
                        //
                        if (json_siArray == NULL) {
                                json_siArray = cJSON_CreateArray();
                        }
                        //
                        // Create sub-interval object and add items to it
                        //
                        cJSON *json_subint = cJSON_CreateObject();
                        cJSON_AddNumberToObject(json_subint, "Interval", c->subIntCount);
                        cJSON_AddNumberToObject(json_subint, "Seconds", sec);
                        //
                        create_timestamp(&repo.systemClock);
                        cJSON_AddStringToObject(json_subint, "TimeOfSubInterval", scratch);
                        cJSON_AddNumberToObject(json_subint, "ActiveConnections", repo.actConnCount);
                        //
                        if (sent > 0.0) {
                                dvar = ((double) c->sisSav.rxDatagrams * 100.0) / sent;
                                cJSON_AddNumberPToObject(json_subint, "DeliveredPercent", dvar, 2);
                                dvar = (double) c->sisSav.seqErrLoss / sent;
                                cJSON_AddNumberPToObject(json_subint, "LossRatio", dvar, 9);
                                dvar = (double) c->sisSav.seqErrOoo / sent;
                                cJSON_AddNumberPToObject(json_subint, "ReorderedRatio", dvar, 9);
                                dvar = (double) c->sisSav.seqErrDup / sent;
                                cJSON_AddNumberPToObject(json_subint, "ReplicatedRatio", dvar, 9);
                        } else {
                                cJSON_AddNumberPToObject(json_subint, "DeliveredPercent", 0.0, 2);
                                cJSON_AddNumberPToObject(json_subint, "LossRatio", 0.0, 9);
                                cJSON_AddNumberPToObject(json_subint, "ReorderedRatio", 0.0, 9);
                                cJSON_AddNumberPToObject(json_subint, "ReplicatedRatio", 0.0, 9);
                        }
                        cJSON_AddNumberToObject(json_subint, "LossCount", c->sisSav.seqErrLoss);
                        cJSON_AddNumberToObject(json_subint, "ReorderedCount", c->sisSav.seqErrOoo);
                        cJSON_AddNumberToObject(json_subint, "ReplicatedCount", c->sisSav.seqErrDup);
                        //
                        dvar = (double) dvmin / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "PDVMin", dvar, -9);
                        dvar = (double) dvavg / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "PDVAvg", dvar, -9);
                        dvar = (double) c->sisSav.delayVarMax / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "PDVMax", dvar, -9);
                        dvar = (double) (c->sisSav.delayVarMax - dvmin) / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "PDVRange", dvar, -9);
                        //
                        dvar = (double) rttmin / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "RTTMin", dvar, -9);
                        dvar = (double) c->sisSav.rttMaximum / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "RTTMax", dvar, -9);
                        dvar = (double) (c->sisSav.rttMaximum - rttmin) / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "RTTRange", dvar, -9);
                        //
                        cJSON_AddNumberPToObject(json_subint, "IPLayerCapacity", mbps, 2);
                        cJSON_AddNumberPToObject(json_subint, "InterfaceEthMbps", intfmbps, 2);
                        //
                        dvar = ((double) c->clockDeltaMin + (double) dvmin) / 1000.0;
                        cJSON_AddNumberPToObject(json_subint, "MinOnewayDelay", dvar, -9);
                        //
                        // Add sub-interval object to sub-interval array
                        //
                        cJSON_AddItemToArray(json_siArray, json_subint);
                }
        }

        //
        // Final processing for aggregate connection
        //
        if (connindex == aggConn) {
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
                ts->rxDatagrams += (unsigned int) c->sisSav.rxDatagrams;
                ts->seqErrLoss += (unsigned int) c->sisSav.seqErrLoss;
                ts->seqErrOoo += (unsigned int) c->sisSav.seqErrOoo;
                ts->seqErrDup += (unsigned int) c->sisSav.seqErrDup;
                ts->rateSumL3 += (double) mbps;
                ts->rateSumIntf += (double) intfmbps;
                ts->sampleCount++;

                //
                // Re-initialize stats for next sub-interval
                //
                memset(&c->sisSav, 0, sizeof(struct subIntStats));
                c->sisSav.delayVarMin = INITIAL_MIN_DELAY;
                c->sisSav.rttMinimum  = INITIAL_MIN_DELAY;
                repo.siAggRateL3      = 0.0;
                repo.siAggRateL2      = 0.0;
                repo.siAggRateL1      = 0.0;
                repo.siAggRateL0      = 0.0;
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Output maximum data rate and overall test summary statistics
//
int output_maxrate(int connindex) {
        register struct connection *c = &conn[connindex];
        char *testtype, connid[8], maxtext[32], intfrate[16];
        int i, sibegin, siend, var;
        unsigned int dvmin, dvavg, rttmin;
        double dvar, sent, delivered = 0.0;
        struct testSummary *ts = &repo.testSum;
        cJSON *json_summary    = NULL;
        cJSON *json_modalArray = NULL;

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
        if (conf.jsonOutput) {
                //
                // If it exists add JSON sub-interval array to output object
                //
                if (json_siArray != NULL) {
                        cJSON_AddItemToObject(json_output, "IncrementalResult", json_siArray);
                }
        }

        //
        // Output summary info
        //
        sent = (double) ts->rxDatagrams + (double) ts->seqErrLoss;
        if (sent > 0.0 && ts->sampleCount > 0) {
                if (conf.showLossRatio)
                        delivered = (double) ts->seqErrLoss / sent;
                else
                        delivered = ((double) ts->rxDatagrams * 100.0) / sent;
                ts->delayVarSum = (((ts->delayVarSum * 10) / ts->sampleCount) + 5) / 10;
                ts->rateSumL3 /= (double) ts->sampleCount;
                ts->rateSumIntf /= (double) ts->sampleCount;
        }
        if (!conf.jsonOutput) {
                strcpy(scratch2, "%s%s Summary ");
                if (!conf.showLossRatio) {
                        strcat(scratch2, DELIVERED_TEXT SUMMARY_TEXT);
                } else {
                        strcat(scratch2, LOSSRATIO_TEXT SUMMARY_TEXT);
                }
                *intfrate = '\0';
                if (repo.intfFD >= 0) { // Append interface rate to L3/IP rate
                        snprintf(intfrate, sizeof(intfrate), " [%.2f]", ts->rateSumIntf);
                }
                var = sprintf(scratch, scratch2, connid, testtype, delivered, ts->seqErrLoss, ts->seqErrOoo, ts->seqErrDup,
                              ts->delayVarMin, ts->delayVarSum, ts->delayVarMax, ts->rttMinimum, ts->rttMaximum, ts->rateSumL3,
                              intfrate);
                send_proc(errConn, scratch, var);
        } else {
                //
                // Create JSON summary object and add items to it
                //
                json_summary = cJSON_CreateObject();
                //
                cJSON_AddNumberToObject(json_summary, "ActiveConnections", repo.actConnCount);
                if (sent > 0.0) {
                        dvar = ((double) ts->rxDatagrams * 100.0) / sent;
                        cJSON_AddNumberPToObject(json_summary, "DeliveredPercent", dvar, 2);
                        dvar = (double) ts->seqErrLoss / sent;
                        cJSON_AddNumberPToObject(json_summary, "LossRatioSummary", dvar, 9);
                        dvar = (double) ts->seqErrOoo / sent;
                        cJSON_AddNumberPToObject(json_summary, "ReorderedRatioSummary", dvar, 9);
                        dvar = (double) ts->seqErrDup / sent;
                        cJSON_AddNumberPToObject(json_summary, "ReplicatedRatioSummary", dvar, 9);
                } else {
                        cJSON_AddNumberPToObject(json_summary, "DeliveredPercent", 0.0, 2);
                        cJSON_AddNumberPToObject(json_summary, "LossRatioSummary", 0.0, 9);
                        cJSON_AddNumberPToObject(json_summary, "ReorderedRatioSummary", 0.0, 9);
                        cJSON_AddNumberPToObject(json_summary, "ReplicatedRatioSummary", 0.0, 9);
                }
                cJSON_AddNumberToObject(json_summary, "LossCount", ts->seqErrLoss);
                cJSON_AddNumberToObject(json_summary, "ReorderedCount", ts->seqErrOoo);
                cJSON_AddNumberToObject(json_summary, "ReplicatedCount", ts->seqErrDup);
                //
                dvar = (double) ts->delayVarMin / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "PDVMin", dvar, -9);
                dvar = (double) ts->delayVarSum / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "PDVAvg", dvar, -9);
                dvar = (double) ts->delayVarMax / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "PDVMax", dvar, -9);
                dvar = (double) (ts->delayVarMax - ts->delayVarMin) / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "PDVRangeSummary", dvar, -9);
                //
                dvar = (double) ts->rttMinimum / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "RTTMin", dvar, -9);
                dvar = (double) ts->rttMaximum / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "RTTMax", dvar, -9);
                dvar = (double) (ts->rttMaximum - ts->rttMinimum) / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "RTTRangeSummary", dvar, -9);
                //
                cJSON_AddNumberPToObject(json_summary, "IPLayerCapacitySummary", ts->rateSumL3, 2);
                cJSON_AddNumberPToObject(json_summary, "InterfaceEthMbps", ts->rateSumIntf, 2);
        }

        //
        // Output delay info
        //
        rttmin = 0;
        if (c->rttMinimum != INITIAL_MIN_DELAY)
                rttmin = c->rttMinimum;
        if (!conf.jsonOutput) {
                strcpy(scratch2, "%s%s " MINIMUM_FINAL);
                var = sprintf(scratch, scratch2, connid, testtype, c->clockDeltaMin, rttmin, repo.actConnCount);
                send_proc(errConn, scratch, var);
        } else {
                //
                // Add final items to summary object and add summary object to output object
                //
                dvar = (double) c->clockDeltaMin / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "MinOnewayDelaySummary", dvar, -9);
                dvar = (double) rttmin / 1000.0;
                cJSON_AddNumberPToObject(json_summary, "MinRTTSummary", dvar, -9);
                //
                cJSON_AddItemToObject(json_output, "Summary", json_summary);
        }

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
                if (!conf.jsonOutput) {
                        if (conf.bimodalCount == 0) {
                                strcpy(maxtext, "Maximum");
                        } else {
                                sprintf(maxtext, "Max[%d-%d]", sibegin, siend);
                        }
                        *intfrate = '\0';
                        if (repo.intfFD >= 0) { // Append interface rate to L3/IP rate
                                snprintf(intfrate, sizeof(intfrate), " [%.2f]", repo.intfMax[i]);
                        }
                        strcpy(scratch2,
                               "%s%s %s Mbps(L3/IP): %.2f%s, Mbps(L2/Eth): %.2f, Mbps(L1/Eth): %.2f, Mbps(L1/Eth+VLAN): %.2f\n");
                        var = sprintf(scratch, scratch2, connid, testtype, maxtext, repo.rateMaxL3[i], intfrate, repo.rateMaxL2[i],
                                      repo.rateMaxL1[i], repo.rateMaxL0[i]);
                        send_proc(errConn, scratch, var);
                } else {
                        if (conf.bimodalCount == 0) {
                                var = c->subIntCount;
                        } else {
                                var = siend - sibegin + 1;
                        }
                        //
                        // Create JSON atmax object and add items to it
                        //
                        cJSON *json_atmax = cJSON_CreateObject();
                        //
                        cJSON_AddNumberToObject(json_atmax, "Mode", i + 1);
                        cJSON_AddNumberToObject(json_atmax, "Intervals", var);
                        //
                        create_timestamp(&repo.timeOfMax[i]);
                        cJSON_AddStringToObject(json_atmax, "TimeOfMax", scratch);
                        cJSON_AddNumberToObject(json_atmax, "ActiveConnections", repo.actConnections[i]);
                        //
                        sent = (double) repo.sisMax[i].rxDatagrams + (double) repo.sisMax[i].seqErrLoss;
                        if (sent > 0.0) {
                                dvar = ((double) repo.sisMax[i].rxDatagrams * 100.0) / sent;
                                cJSON_AddNumberPToObject(json_atmax, "DeliveredPercent", dvar, 2);
                                dvar = (double) repo.sisMax[i].seqErrLoss / sent;
                                cJSON_AddNumberPToObject(json_atmax, "LossRatioAtMax", dvar, 9);
                                dvar = (double) repo.sisMax[i].seqErrOoo / sent;
                                cJSON_AddNumberPToObject(json_atmax, "ReorderedRatioAtMax", dvar, 9);
                                dvar = (double) repo.sisMax[i].seqErrDup / sent;
                                cJSON_AddNumberPToObject(json_atmax, "ReplicatedRatioAtMax", dvar, 9);
                        } else {
                                cJSON_AddNumberPToObject(json_atmax, "DeliveredPercent", 0.0, 2);
                                cJSON_AddNumberPToObject(json_atmax, "LossRatioAtMax", 0.0, 9);
                                cJSON_AddNumberPToObject(json_atmax, "ReorderedRatioAtMax", 0.0, 9);
                                cJSON_AddNumberPToObject(json_atmax, "ReplicatedRatioAtMax", 0.0, 9);
                        }
                        cJSON_AddNumberToObject(json_atmax, "LossCount", repo.sisMax[i].seqErrLoss);
                        cJSON_AddNumberToObject(json_atmax, "ReorderedCount", repo.sisMax[i].seqErrOoo);
                        cJSON_AddNumberToObject(json_atmax, "ReplicatedCount", repo.sisMax[i].seqErrDup);
                        //
                        dvmin = dvavg = 0;
                        if (repo.sisMax[i].delayVarCnt > 0) {
                                dvmin = (unsigned int) repo.sisMax[i].delayVarMin;
                                dvavg = (unsigned int) (repo.sisMax[i].delayVarSum / repo.sisMax[i].delayVarCnt);
                        }
                        dvar = (double) dvmin / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "PDVMin", dvar, -9);
                        dvar = (double) dvavg / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "PDVAvg", dvar, -9);
                        dvar = (double) repo.sisMax[i].delayVarMax / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "PDVMax", dvar, -9);
                        dvar = (double) (repo.sisMax[i].delayVarMax - dvmin) / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "PDVRangeAtMax", dvar, -9);
                        //
                        rttmin = 0;
                        if (repo.sisMax[i].rttMinimum != INITIAL_MIN_DELAY) {
                                rttmin = (unsigned int) repo.sisMax[i].rttMinimum;
                        }
                        dvar = (double) rttmin / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "RTTMin", dvar, -9);
                        dvar = (double) repo.sisMax[i].rttMaximum / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "RTTMax", dvar, -9);
                        dvar = (double) (repo.sisMax[i].rttMaximum - rttmin) / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "RTTRangeAtMax", dvar, -9);
                        //
                        cJSON_AddNumberPToObject(json_atmax, "MaxIPLayerCapacity", repo.rateMaxL3[i], 2);
                        cJSON_AddNumberPToObject(json_atmax, "InterfaceEthMbps", repo.intfMax[i], 2);
                        cJSON_AddNumberPToObject(json_atmax, "MaxETHCapacityNoFCS", repo.rateMaxL2[i], 2);
                        cJSON_AddNumberPToObject(json_atmax, "MaxETHCapacityWithFCS", repo.rateMaxL1[i], 2);
                        cJSON_AddNumberPToObject(json_atmax, "MaxETHCapacityWithFCSVLAN", repo.rateMaxL0[i], 2);
                        //
                        dvar = ((double) c->clockDeltaMin + (double) dvmin) / 1000.0;
                        cJSON_AddNumberPToObject(json_atmax, "MinOnewayDelayAtMax", dvar, -9);

                        //
                        // On first pass add atmax object to output and create modal array, else add to modal array
                        //
                        if (i == 0) {
                                cJSON_AddItemToObject(json_output, "AtMax", json_atmax);
                                json_modalArray = cJSON_CreateArray();
                        } else {
                                cJSON_AddItemToArray(json_modalArray, json_atmax);
                        }

                        //
                        // When complete (modes 1 of 1 <OR> 2 of 2) add modal array to output
                        //
                        if (conf.bimodalCount == 0 || i == 1) {
                                cJSON_AddItemToObject(json_output, "ModalResult", json_modalArray);
                        }
                }
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
        unsigned int delta, dgrams;
        unsigned long long bytes;
        double mbps = 0.0;

        if (c->ipProtocol == IPPROTO_IPV6)
                overhead += IPV6_ADDSIZE;

        if (sis == NULL) {
                delta  = c->tiDeltaTime;
                dgrams = c->tiRxDatagrams;
                bytes  = (unsigned long long) c->tiRxBytes;
        } else {
                delta  = (unsigned int) sis->deltaTime;
                dgrams = (unsigned int) sis->rxDatagrams;
                bytes  = (unsigned long long) sis->rxBytes;
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
// Service recvmmsg() data buffers for load PDUs
//
int service_recvmmsg(int connindex) {
        int i;

        repo.rcvDataPtr = repo.defBuffer;
        for (i = 0; i < RECVMMSG_SIZE; i++) {
                if (mmsgDataSize[i] == 0)
                        break;
                repo.rcvDataSize = mmsgDataSize[i];
                service_loadpdu(connindex);
                repo.rcvDataPtr += RCV_HEADER_SIZE;
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Generic connection receive processor
//
int recv_proc(int connindex) {
        register struct connection *c = &conn[connindex];
        static struct mmsghdr mmsg[RECVMMSG_SIZE]; // Static array
        static struct iovec iov[RECVMMSG_SIZE];    // Static array
        char *rcvbuf;
        int i, var, recvsize;

        //
        // Specify receive buffer size (truncate load PDUs to reduce overhead of memory copy)
        //
        if (c->secAction == &service_recvmmsg || c->secAction == &service_loadpdu) {
                recvsize = RCV_HEADER_SIZE;
        } else {
                recvsize = DEF_BUFFER_SIZE;
        }
        repo.rcvDataPtr = repo.defBuffer; // Default to start of general I/O buffer

        //
        // Issue read
        //
        if (c->subType == SOCK_STREAM || c->connected) {
                //
                // Process based on secondary action routine
                //
                if (c->secAction == &service_recvmmsg) {
                        //
                        // Prepare message structures
                        //
                        memset(mmsg, 0, sizeof(mmsg));
                        rcvbuf = repo.defBuffer;
                        for (i = 0; i < RECVMMSG_SIZE; i++) {
                                iov[i].iov_base            = rcvbuf;
                                iov[i].iov_len             = recvsize;
                                mmsg[i].msg_hdr.msg_iov    = &iov[i];
                                mmsg[i].msg_hdr.msg_iovlen = 1;
                                //
                                rcvbuf += recvsize;  // Next buffer
                                mmsgDataSize[i] = 0; // Initialize as empty
                        }
#ifdef HAVE_RECVMMSG
                        //
                        // Perform read and process messages
                        //
                        repo.rcvDataSize = recvmmsg(c->fd, mmsg, RECVMMSG_SIZE, MSG_TRUNC, NULL); // Returns number of messages
                        for (i = 0; i < repo.rcvDataSize; i++) {
                                mmsgDataSize[i] = (int) mmsg[i].msg_len; // Save actual received length (although truncated)
                        }
#endif
                } else {
                        repo.rcvDataSize = recv(c->fd, repo.defBuffer, recvsize, 0);
                }
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
                if ((var = receive_trunc(errno, recvsize, RCV_HEADER_SIZE)) > 0) {
                        repo.rcvDataSize = var;
                } else if ((var = socket_error(connindex, errno, "RECVMMSG/RECV/RECVFROM")) > 0) {
                        if (!conf.errSuppress) {
                                send_proc(errConn, scratch, var);
                        }
                }
        } else if ((repo.rcvDataSize == 0) && (c->subType == SOCK_STREAM)) {
                if (conf.verbose) {
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
        char *buf;

        //
        // If JSON is configured save error message in JSON error buffer(s)
        //
        if (conf.jsonOutput && c->type == T_CONSOLE && connindex == errConn) {
                if (*json_errbuf != '\0') {
                        strcpy(json_errbuf2, json_errbuf); // Save primary to supplementary
                }
                snprintf(json_errbuf, STRING_SIZE, "%s", sendbuffer);
                //
                buf = json_errbuf;
                while ((buf = strpbrk(buf, "\"\n")) != NULL) {
                        if (*buf == '\"')
                                *buf++ = '\''; // Replace double-quote with single-quote
                        else if (*buf == '\n')
                                *buf = '\0'; // Replace newline with null
                }
                return sendsize;
        }

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
                sisnet->rxBytes     = (uint64_t) htonll(sishost->rxBytes);
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
                sishost->rxBytes     = (uint64_t) ntohll(sisnet->rxBytes);
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

                var       = 0;
                *location = '\0';
                switch (type) {
                case WARN_LOC_STATUS:
                        strcpy(location, "LOCAL");
                case WARN_REM_STATUS:
                        if (*location == '\0')
                                strcpy(location, "REMOTE");
                        var = sprintf(scratch, "%s%s WARNING: Incoming status feedback messages lost (%d)", connid, location,
                                      c->spduSeqErr);
                        break;
                case WARN_LOC_STOPPED:
                        strcpy(location, "LOCAL");
                case WARN_REM_STOPPED:
                        if (*location == '\0')
                                strcpy(location, "REMOTE");
                        var = sprintf(scratch, "%s%s WARNING: Incoming traffic has completely stopped", connid, location);
                        break;
                }
                if (var > 0) {
                        if (!repo.isServer) {
                                var += sprintf(&scratch[var], " [Server %s:%d]", repo.server[c->serverIndex].ip,
                                               repo.server[c->serverIndex].port);
                        }
                        scratch[var++] = '\n';
                        send_proc(errConn, scratch, var);
                        repo.endTimeStatus = STATUS_WARNBASE + type; // ErrorStatus
                }
        }
        return;
}
//----------------------------------------------------------------------------
//
// Create JSON required timestamp with current time
//
// Populate scratch buffer and return length
//
int create_timestamp(struct timespec *tspecvar) {
        int var;

        var = strftime(scratch, STRING_SIZE, "%FT%T", gmtime(&tspecvar->tv_sec));
        var += sprintf(&scratch[var], ".%06ldZ", tspecvar->tv_nsec / NSECINUSEC);

        return var;
}
//----------------------------------------------------------------------------
//
// Update interface statistics via sysfs
//
double upd_intf_stats(BOOL initialize) {
        int var;
        unsigned long long intfbytes; // Always handle counters as 64-bit values
        double mbps = 0.0;
        struct timespec tspecvar;
        char buffer[32];

        if (!initialize) {
                lseek(repo.intfFD, 0, SEEK_SET); // Reset position to read new value
        }
        if ((var = (int) read(repo.intfFD, buffer, sizeof(buffer))) > 0) {
                buffer[var] = '\0';
                if ((intfbytes = strtoull(buffer, NULL, 10)) > 0) {
                        if (!initialize) {
                                if (tspecisset(&repo.intfTime)) {
                                        tspecminus(&repo.systemClock, &repo.intfTime, &tspecvar);
                                        if (intfbytes >= repo.intfBytes) {
                                                mbps = (double) (intfbytes - repo.intfBytes);
                                        } else { // Counter wrapped (allow for 32 or 64-bit wrap threshold)
                                                if (repo.intfBytes <= 4294967295ULL) {
                                                        mbps = (double) ((4294967295ULL - repo.intfBytes) + intfbytes + 1);
                                                } else {
                                                        mbps = (double) ((ULLONG_MAX - repo.intfBytes) + intfbytes + 1);
                                                }
                                        }
                                        mbps *= 8.0;
                                        mbps /= (double) tspecusec(&tspecvar);
                                }
                        }
                        repo.intfBytes = intfbytes;                  // Save current value
                        tspeccpy(&repo.intfTime, &repo.systemClock); // Save current time
                }
        }
        return mbps;
}
//----------------------------------------------------------------------------
//
// Return a uniformly distributed random number between min and max
//
int getuniform(int min, int max) {
        int rvar = (int) random(); // Obtain random value (0 - RAND_MAX)

        return (rvar % (max - min + 1)) + min;
}
//----------------------------------------------------------------------------
//
// Output minimum message
//
void output_minimum(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;

        var = sprintf(scratch, "[%d]" MINIMUM_TEXT "\n", connindex, c->clockDeltaMin, c->rttMinimum);
        send_proc(monConn, scratch, var);

        return;
}
//----------------------------------------------------------------------------
//
// Output debug message
//
void output_debug(int connindex) {
        register struct connection *c = &conn[connindex];
        int var;
        unsigned int dvmin, dvavg;

        dvmin = dvavg = 0;
        if (c->delayVarCnt > 0) {
                dvmin = c->delayVarMin;
                dvavg = c->delayVarSum / c->delayVarCnt;
        }
        var = -1;
        if (c->rttSample != INITIAL_MIN_DELAY)
                var = (int) c->rttSample;
        var = sprintf(scratch, CLIENT_DEBUG, connindex, c->seqErrLoss, c->seqErrOoo, c->seqErrDup, dvmin, dvavg, c->delayVarMax,
                      var, get_rate(connindex, NULL, L3DG_OVERHEAD));
        send_proc(monConn, scratch, var);

        return;
}
//----------------------------------------------------------------------------
