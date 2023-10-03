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
 * Len Ciavattone          04/21/2022    Increase sending rates to 40 Gbps
 * Len Ciavattone          12/26/2022    Add random payload size support
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
                                sr->txInterval2 = 50000;
                                sr->udpAddon2   = payload;       // Set maximum value
                                sr->udpAddon2 |= SRATE_RAND_BIT; // Set randomization bit
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
                // To better support the use of jumbo sizes with a non-jumbo MTU, do not use any payload sizes
                // that would mix datagrams requiring fragmentation with datagrams not requiring fragmentation.
                // Because this has been shown to produce reordering, all payload sizes will be greater than
                // what can be accommodated in IPv6 with a 1500 byte MTU.
                //
                for (i = MAX_L3_PACKET + 125; i <= MAX_JL3_PACKET; i += 125) {
                        sr              = &repo.sendingRates[repo.maxSendingRates++];
                        sr->txInterval1 = BASE_SEND_TIMER1;
                        sr->udpPayload1 = i - L3DG_OVERHEAD;
                        sr->burstSize1  = 10;
                }
                jmax    = 11;
                payload = MAX_JPAYLOAD_SIZE;

        } else if (conf.traditionalMTU) {
                jmax    = 9;
                payload = MAX_TPAYLOAD_SIZE;
        } else {
                jmax    = 11;
                payload = MAX_PAYLOAD_SIZE;
        }
        for (j = jmax; repo.maxSendingRates < MAX_SENDING_RATES; j++) {
                sr              = &repo.sendingRates[repo.maxSendingRates++];
                sr->txInterval1 = BASE_SEND_TIMER1;
                sr->udpPayload1 = payload;
                if (j < MAX_BURST_SIZE)
                        sr->burstSize1 = j;
                else
                        sr->burstSize1 = MAX_BURST_SIZE;
                sr->txInterval2 = 0;
                sr->udpPayload2 = 0;
                sr->burstSize2  = 0;
                sr->udpAddon2   = 0;
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
        int i, j, var, payload, ipv6add, min, avg;
        BOOL bvar, randpayload;
        char buf[16];
        double dvar, dvar2;
        struct sendingRate *sr;

        //
        // Output header
        //
        if (conf.ipv6Only) {
                ipv6add = IPV6_ADDSIZE;
                strcpy(buf, "IPv6");
        } else {
                ipv6add = 0;
                strcpy(buf, "IPv4");
        }
        var = sprintf(scratch, "Sending Rate Table for %s (Dual Transmitters, Referenced by Index)...\n", buf);
        var = write(fd, scratch, var);
        var = sprintf(scratch, "%s) %s %s %s  + %s %s %s   %s  = %s\n", "Index", "TxInt(us)", "Payload", "Burst", "TxInt(us)",
                      "Payload", "Burst", "Add-On", "Mbps(L3/IP)");
        var = write(fd, scratch, var);

        //
        // Output each row
        //
        min = (int) MIN_PAYLOAD_SIZE - ipv6add; // Minimum payload size
        for (i = 0, sr = repo.sendingRates; i < repo.maxSendingRates; i++, sr++) {
                dvar = 0;
                bvar = FALSE; // Is randomization in use
                //
                // Process all three send options
                //
                for (j = 0; j < 3; j++) {
                        dvar2       = 0;
                        payload     = 0;
                        randpayload = FALSE;
                        if (j == 0) { // Transmitter 1
                                if (sr->burstSize1 > 0) {
                                        dvar2   = (double) ((USECINSEC / sr->txInterval1) * sr->burstSize1);
                                        payload = (int) (sr->udpPayload1 & ~SRATE_RAND_BIT) - ipv6add;
                                        if (sr->udpPayload1 & SRATE_RAND_BIT)
                                                randpayload = TRUE;
                                }
                        } else if (j == 1) { // Transmitter 2
                                if (sr->burstSize2 > 0) {
                                        dvar2   = (double) ((USECINSEC / sr->txInterval2) * sr->burstSize2);
                                        payload = (int) (sr->udpPayload2 & ~SRATE_RAND_BIT) - ipv6add;
                                        if (sr->udpPayload2 & SRATE_RAND_BIT)
                                                randpayload = TRUE;
                                }
                        } else { // Add-on (used with transmitter 2)
                                if (sr->udpAddon2 > 0) {
                                        dvar2   = (double) (USECINSEC / sr->txInterval2);
                                        payload = (int) (sr->udpAddon2 & ~SRATE_RAND_BIT) - ipv6add;
                                        if (sr->udpAddon2 & SRATE_RAND_BIT)
                                                randpayload = TRUE;
                                }
                        }
                        //
                        // Calculate bytes/sec for this send option and add to total
                        //
                        avg = payload; // Init average to static value or maximum if SRATE_RAND_BIT set
                        if (randpayload) {
                                bvar = TRUE;
                                avg  = (min + payload) / 2; // Calculate average
                        }
                        dvar += dvar2 * (double) (avg + L3DG_OVERHEAD + ipv6add);
                        //
                        // Accummulate row text
                        //
                        if (randpayload) {
                                sprintf(buf, "%d-%d", min, payload);
                        } else {
                                sprintf(buf, "%d", payload);
                        }
                        if (j == 0) {
                                var = sprintf(scratch, "%5d) %8u  %7s %5u  ", i, sr->txInterval1, buf, sr->burstSize1);
                        } else if (j == 1) {
                                var += sprintf(&scratch[var], "+ %8u  %7s %5u  ", sr->txInterval2, buf, sr->burstSize2);
                        } else {
                                var += sprintf(&scratch[var], "%7s ", buf);
                        }
                }

                //
                // Aggregate sending rate
                //
                dvar *= 8;       // Convert to bits/sec
                dvar /= 1000000; // Convert to Mbps
                if (bvar)
                        scratch[var++] = '~';
                else
                        scratch[var++] = ' ';
                var += sprintf(&scratch[var], "= %10.2f\n", dvar); // Finalize row text
                var = write(fd, scratch, var);
        }
        if (!conf.jumboStatus) {
                var = sprintf(scratch, "NOTE: Disabling jumbo datagram sizes may impede rates above 1 Gbps\n");
                var = write(fd, scratch, var);
        }
        return;
}
//----------------------------------------------------------------------------
