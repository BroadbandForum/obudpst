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
 * UDP Speed Test - udpst_common.h
 *
 * This file contains the common and time-related constants and macros.
 *
 */

#ifndef UDPST_COMMON_H
#define UDPST_COMMON_H

//----------------------------------------------------------------------------
//
// Common
//
typedef int BOOL;
#define TRUE  1 // Boolean true
#define FALSE 0 // Boolean false
// Macros for 64 bit variables to switch to and from network
#ifndef ntohll
#define ntohll(x) (((uint64_t) (ntohl((int) ((x << 32) >> 32))) << 32) | (unsigned int) ntohl(((int) (x >> 32))))
#endif
#ifndef htonll
#define htonll(x) ntohll(x)
#endif

//----------------------------------------------------------------------------
//
// Time-related
//
#define SECINDAY     (60 * 60 * 24)         // sec in a day
#define MSECINSEC    1000                   // msec in a second
#define MSECINMIN    (MSECINSEC * 60)       // msec in a minute
#define MSECINDAY    (SECINDAY * MSECINSEC) // msec in a day
#define USECINSEC    1000000                // usec in a second
#define USECINMSEC   1000                   // usec in a msec
#define USECADJ      (USECINMSEC / 2)       // usec adjustment for rounding
#define NSECINSEC    1000000000             // nsec in a sec
#define NSECINMSEC   1000000                // nsec in a msec
#define NSECINUSEC   1000                   // nsec in a usec
#define NSECADJ      (NSECINUSEC / 2)       // nsec adjustment for rounding
#define NSECADJ_MSEC (NSECINMSEC / 2)       // nsec adjustment for rounding

//----------------------------------------------------------------------------
// Macros for timespec operations
//----------------------------------------------------------------------------
//
// Add timespec (keep nsec within bounds)
//
#define tspecplus(a, b, result)                                  \
        do {                                                     \
                (result)->tv_sec  = (a)->tv_sec + (b)->tv_sec;   \
                (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec; \
                while ((result)->tv_nsec >= NSECINSEC) {         \
                        ++(result)->tv_sec;                      \
                        (result)->tv_nsec -= NSECINSEC;          \
                }                                                \
        } while (0)
//
// Subtract timespec (keep nsec within bounds)
//
#define tspecminus(a, b, result)                                 \
        do {                                                     \
                (result)->tv_sec  = (a)->tv_sec - (b)->tv_sec;   \
                (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
                while ((result)->tv_nsec < 0) {                  \
                        --(result)->tv_sec;                      \
                        (result)->tv_nsec += NSECINSEC;          \
                }                                                \
        } while (0)
//
// Copy timespec
//
#define tspeccpy(d, s)                       \
        do {                                 \
                (d)->tv_sec  = (s)->tv_sec;  \
                (d)->tv_nsec = (s)->tv_nsec; \
        } while (0)
//
// Compare timespec values
//
#define tspeccmp(a, b, CMP) (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_nsec CMP(b)->tv_nsec) : ((a)->tv_sec CMP(b)->tv_sec))
//
// Convert timespec to us
//
#define tspecusec(a) (((a)->tv_sec * USECINSEC) + (((a)->tv_nsec + NSECADJ) / NSECINUSEC))
//
// Convert timespec to ms
//
#define tspecmsec(a) (((a)->tv_sec * MSECINSEC) + (((a)->tv_nsec + NSECADJ_MSEC) / NSECINMSEC))
//
// Test and clear timespec
//
#define tspecisset(tsp) ((tsp)->tv_sec || (tsp)->tv_nsec)
#define tspecclear(tsp) ((tsp)->tv_sec = (tsp)->tv_nsec = 0)
//----------------------------------------------------------------------------

#endif /* UDPST_COMMON_H */
