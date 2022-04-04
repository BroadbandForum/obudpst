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
 * UDP Speed Test - udpst_srates.c
 *
 * This file builds or displays the sending rate table containing the send
 * parameters used for testing.
 *
 * Author                  Date          Comments
 * --------------------    ----------    ----------------------------------
 * Len Ciavattone          09/05/2019    Split off from udpst.c
 * Len Ciavattone          09/24/2019    Include max burst when jumbo false
 * Len Ciavattone          12/14/2021    Modified largest jumbo sizes to be
 *                                       even multiple of 1500 byte packets
 *                                       with IPv6. No longer mixing jumbo
 *                                       sizes with non-jumbo sizes.
 * Len Ciavattone          12/21/2021    Add traditional (1500 byte) MTU
 *
 */

#define UDPST_SRATES
#ifdef __linux__
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#ifdef AUTH_KEY_ENABLE
#include <openssl/hmac.h>
#include <openssl/x509.h>
#endif
#else
#include "../udpst_srates_alt1.h"
#endif
//
#include "cJSON.h"
#include "udpst_common.h"
#include "udpst_protocol.h"
#include "udpst.h"
#include "udpst_srates.h"
#ifndef __linux__
#include "../udpst_srates_alt2.h"
#endif

//----------------------------------------------------------------------------
//
// External data
//
extern char scratch[STRING_SIZE];
extern struct configuration conf;
extern struct repository repo;

//----------------------------------------------------------------------------
// Function definitions
//----------------------------------------------------------------------------
//
// Define sending rate table (with or without jumbo payload sizes above 1 Gbps)
//
// Populate scratch buffer and return length on error
//
int def_sending_rates(void) {
        int var, i, j, k, jmax, kmax;
        unsigned int payload;
        BOOL stop;
        struct sendingRate *sr;

        //
        // Define initial sending rates up to 1 Gbps
        //
        if (conf.traditionalMTU) {
                jmax    = 11;
                kmax    = 8;
                payload = MAX_TPAYLOAD_SIZE;
        } else {
                jmax    = 9;
                kmax    = 10;
                payload = MAX_PAYLOAD_SIZE;
        }
        stop = FALSE;
        for (k = 0; k <= kmax; k++) {
                for (i = 0; i < 10; i++) {
                        sr = &repo.sendingRates[repo.maxSendingRates++];
                        if (k > 0) {
                                sr->txInterval1 = BASE_SEND_TIMER1;
                                sr->udpPayload1 = payload;
                                sr->burstSize1  = k;
                        }
                        if (i > 0) {
                                sr->txInterval2 = BASE_SEND_TIMER2;
                                sr->udpPayload2 = payload;
                                sr->burstSize2  = i;
                                sr->udpAddon2   = 0;
                        }
                        if (k == 0 && i == 0) {
                                sr->txInterval2 = BASE_SEND_TIMER2;
                                sr->udpAddon2   = MIN_PAYLOAD_SIZE;
                        } else if (!conf.traditionalMTU && k == kmax) {
                                break;
                        }
                        for (j = 1; j <= jmax; j++) {
                                sr = &repo.sendingRates[repo.maxSendingRates++];
                                if (k > 0) {
                                        sr->txInterval1 = BASE_SEND_TIMER1;
                                        sr->udpPayload1 = payload;
                                        sr->burstSize1  = k;
                                }
                                sr->txInterval2 = BASE_SEND_TIMER2;
                                if (i > 0) {
                                        sr->udpPayload2 = payload;
                                        sr->burstSize2  = i;
                                }
                                sr->udpAddon2 = ((j * 1000) / 8) - L3DG_OVERHEAD;

                                // Explicit stop at 1 Gbps
                                if (repo.maxSendingRates > 1000) {
                                        stop = TRUE;
                                        break;
                                }
                        }
                        if (stop)
                                break;
                }
                if (stop)
                        break;
        }
        repo.hSpeedThresh = repo.maxSendingRates - 1; // Index of high-speed threshold

        //
        // Finish jumbo or non-jumbo payload sizes above 1 Gbps
        //
        // NOTE: Total sending rate rows are the same in either case
        //
        if (conf.jumboStatus) {
                //
                // Increase payload sizes until jumbo limit
                //
                for (i = MAX_L3_PACKET + 125; i <= MAX_JL3_PACKET; i += 125) {
                        sr              = &repo.sendingRates[repo.maxSendingRates++];
                        sr->txInterval1 = BASE_SEND_TIMER1;
                        sr->udpPayload1 = i - L3DG_OVERHEAD;
                        sr->burstSize1  = 10;
                }
                //
                // With jumbo payload size for transmitter 1, add additional payload required for 2
                //
                // To better support the use of jumbo sizes with a non-jumbo MTU, do not use any payload sizes
                // that would mix datagrams requiring fragmentation with datagrams not requiring fragmentation.
                // Because this has been shown to produce reordering, all payload sizes will be greater than
                // what can be accommodated in IPv6 with a 1500 byte MTU.
                //
                for (i = 0; i < 4; i++) {
                        var = MAX_L3_PACKET - (i * 125 * 2);
                        for (j = 0; j < 7; j++) {
                                sr              = &repo.sendingRates[repo.maxSendingRates++];
                                sr->txInterval1 = BASE_SEND_TIMER1;
                                sr->udpPayload1 = MAX_JPAYLOAD_SIZE;
                                sr->burstSize1  = 10 + i;
                                sr->txInterval2 = BASE_SEND_TIMER1;
                                sr->udpPayload2 = (var + (j * MAX_L3_PACKET)) - L3DG_OVERHEAD;
                                // Avoid non-jumbo sizes with less frequent larger payloads
                                if (j == 0) {
                                        sr->txInterval2 *= 4;
                                        sr->udpPayload2 *= 4;
                                } else if (j == 1) {
                                        sr->txInterval2 *= 2;
                                        sr->udpPayload2 *= 2;
                                }
                                sr->burstSize2 = 1;
                                sr->udpAddon2  = 0;
                        }
                }
        } else {
                if (conf.traditionalMTU) {
                        jmax    = 9;
                        payload = MAX_TPAYLOAD_SIZE;
                } else {
                        jmax    = 11;
                        payload = MAX_PAYLOAD_SIZE;
                }
                for (j = jmax; j <= MAX_BURST_SIZE; j++) {
                        sr              = &repo.sendingRates[repo.maxSendingRates++];
                        sr->txInterval1 = BASE_SEND_TIMER1;
                        sr->udpPayload1 = payload;
                        sr->burstSize1  = j;
                        if (conf.traditionalMTU && j < 23) {
                                sr              = &repo.sendingRates[repo.maxSendingRates++];
                                sr->txInterval1 = BASE_SEND_TIMER1;
                                sr->udpPayload1 = payload;
                                sr->burstSize1  = j;
                                sr->txInterval2 = BASE_SEND_TIMER2;
                                sr->udpPayload2 = payload;
                                sr->burstSize2  = 5;
                        }
                        if (repo.maxSendingRates == MAX_SENDING_RATES)
                                break;
                }
        }

        //
        // Sanity check table
        //
        if (repo.maxSendingRates > MAX_SENDING_RATES) {
                var = sprintf(scratch, "ERROR: Sending rate table build failure (overrun)\n");
                return var;
        }
        return 0;
}
//----------------------------------------------------------------------------
//
// Display sending rate table parameters for each index
//
void show_sending_rates(int fd) {
        int i, var, var2, payload1, payload2, addon, ipv6add;
        char ipver[8];
        double dvar;
        struct sendingRate *sr;

        //
        // Output header
        //
        if (conf.ipv6Only) {
                ipv6add = IPV6_ADDSIZE;
                strcpy(ipver, "IPv6");
        } else {
                ipv6add = 0;
                strcpy(ipver, "IPv4");
        }
        var = sprintf(scratch, "Sending Rate Table for %s (Dual Transmitters, Referenced by Index)...\n", ipver);
        var = write(fd, scratch, var);
        var = sprintf(scratch, "%s) %s %s %s  + %s %s %s %s = %s\n", "Index", "TxInt(us)", "Payload", "Burst", "TxInt(us)",
                      "Payload", "Burst", "Add-On", "Mbps(L3/IP)");
        var = write(fd, scratch, var);

        //
        // Output each row
        //
        for (i = 0, sr = repo.sendingRates; i < repo.maxSendingRates; i++, sr++) {
                var = var2 = 0;
                payload1 = payload2 = addon = 0;
                if (sr->burstSize1 > 0) {
                        var      = (USECINSEC / sr->txInterval1) * sr->burstSize1;
                        payload1 = (int) sr->udpPayload1 - ipv6add;
                        var *= payload1 + L3DG_OVERHEAD + ipv6add;
                }
                if (sr->burstSize2 > 0) {
                        var2     = (USECINSEC / sr->txInterval2) * sr->burstSize2;
                        payload2 = (int) sr->udpPayload2 - ipv6add;
                        var2 *= payload2 + L3DG_OVERHEAD + ipv6add;
                }
                if (sr->udpAddon2 > 0) {
                        addon = (int) sr->udpAddon2 - ipv6add;
                        var2 += (USECINSEC / sr->txInterval2) * (addon + L3DG_OVERHEAD + ipv6add);
                }
                dvar = (double) (var + var2);
                dvar *= 8;       // Convert to bits/sec
                dvar /= 1000000; // Convert to Mbps
                var = sprintf(scratch, "%5d) %8u  %7d %5u  + %8u  %7d %5u %5d  = %10.2f\n", i, sr->txInterval1, payload1,
                              sr->burstSize1, sr->txInterval2, payload2, sr->burstSize2, addon, dvar);
                var = write(fd, scratch, var);
        }
        if (!conf.jumboStatus) {
                var = sprintf(scratch, "NOTE: Disabling jumbo datagram sizes may impede rates above 1 Gbps\n");
                var = write(fd, scratch, var);
        }
        return;
}
//----------------------------------------------------------------------------
