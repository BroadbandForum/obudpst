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
 * UDP Speed Test - udpst_protocol.h
 *
 * This file contains the constants and data structures needed for the protocol
 * exchange between client and server.
 *
 */

//----------------------------------------------------------------------------
//
// Protocol version
//
#define PROTOCOL_VER 7 // Protocol version between client and server

//----------------------------------------------------------------------------
//
// Sending rate structure for a single index (row) of transmission parameters
//
#define MAX_BURST_SIZE    100 // Max datagram burst size
#define MIN_INTERVAL_USEC 100 // Min interval/timer granularity (us)
struct sendingRate {
        uint32_t txInterval1; // Transmit interval (us)
        uint32_t udpPayload1; // UDP payload (bytes)
        uint32_t burstSize1;  // UDP burst size per interval
        uint32_t txInterval2; // Transmit interval (us)
        uint32_t udpPayload2; // UDP payload (bytes)
        uint32_t burstSize2;  // UDP burst size per interval
        uint32_t udpAddon2;   // UDP add-on (bytes)
};
//----------------------------------------------------------------------------
//
// Sub-interval statistics structure for received traffic information
//
#define INITIAL_MIN_DELAY UINT32_MAX // Initial minimum delay (no data/value)
struct subIntStats {
        uint32_t rxDatagrams; // Received datagrams
        uint32_t rxBytes;     // Received bytes
        uint32_t deltaTime;   // Time delta
        uint32_t seqErrLoss;  // Loss sum
        uint32_t seqErrOoo;   // Out-of-Order sum
        uint32_t delayVarMin; // Delay variation minimum
        uint32_t delayVarMax; // Delay variation maximum
        uint32_t delayVarSum; // Delay variation sum
        uint32_t delayVarCnt; // Delay variation count
        uint32_t rttMinimum;  // Minimum round-trip time
        uint32_t rttMaximum;  // Maximum round-trip time
        uint32_t accumTime;   // Accumulated time
};
//----------------------------------------------------------------------------
//
// Control header for UDP payload of Setup Request/Response PDUs
//
struct controlHdrSR {
#define CHSR_ID 0xACE1
        uint16_t controlId;   // Control ID
        uint16_t protocolVer; // Protocol version
#define CHSR_CREQ_NONE     0
#define CHSR_CREQ_SETUPREQ 1
#define CHSR_CREQ_SETUPRSP 2
        uint8_t cmdRequest; // Command request
#define CHSR_CRSP_NONE     0
#define CHSR_CRSP_ACKOK    1
#define CHSR_CRSP_BADVER   2
#define CHSR_CRSP_BADJS    3
#define CHSR_CRSP_AUTHNC   4
#define CHSR_CRSP_AUTHREQ  5
#define CHSR_CRSP_AUTHINV  6
#define CHSR_CRSP_AUTHFAIL 7
#define CHSR_CRSP_AUTHTIME 8
        uint8_t cmdResponse; // Command response
        uint16_t reserved1;  // (alignment)
        uint16_t testPort;   // Test port on server
        uint8_t jumboStatus; // Jumbo datagram support status (BOOL)
#define AUTHMODE_NONE   0
#define AUTHMODE_SHA256 1
        uint8_t authMode;      // Authentication mode
        uint32_t authUnixTime; // Authentication time stamp
#ifdef SHA256_DIGEST_LENGTH
#define AUTH_DIGEST_LENGTH SHA256_DIGEST_LENGTH
#else
#define AUTH_DIGEST_LENGTH 32 // Use SHA256 length equivalent
#endif
        unsigned char authDigest[AUTH_DIGEST_LENGTH];
};
//----------------------------------------------------------------------------
//
// Control header for UDP payload of Test Activation PDUs
//
struct controlHdrTA {
#define CHTA_ID 0xACE2
        uint16_t controlId;   // Control ID
        uint16_t protocolVer; // Protocol version
#define CHTA_CREQ_NONE      0
#define CHTA_CREQ_TESTACTUS 1
#define CHTA_CREQ_TESTACTDS 2
        uint8_t cmdRequest; // Command request
#define CHTA_CRSP_NONE     0
#define CHTA_CRSP_ACKOK    1
#define CHTA_CRSP_BADPARAM 2
        uint8_t cmdResponse;         // Command response
        uint16_t lowThresh;          // Low delay variation threshold
        uint16_t upperThresh;        // Upper delay variation threshold
        uint16_t trialInt;           // Status feedback/trial interval (ms)
        uint16_t testIntTime;        // Test interval time (sec)
        uint8_t subIntPeriod;        // Sub-interval period (sec)
        uint8_t ipTosByte;           // IP ToS byte for testing
        uint16_t srIndexConf;        // Configured sending rate index
        uint8_t useOwDelVar;         // Use one-way delay instead of RTT
        uint8_t highSpeedDelta;      // High-speed row adjustment delta
        uint16_t slowAdjThresh;      // Slow rate adjustment threshold
        uint16_t seqErrThresh;       // Sequence error threshold
        struct sendingRate srStruct; // Sending rate structure
};
//----------------------------------------------------------------------------
//
// Load header for UDP payload of load PDUs
//
struct loadHdr {
#define LOAD_ID 0xBEEF
        uint16_t loadId; // Load ID
#define TEST_ACT_TEST  0
#define TEST_ACT_STOP1 1
#define TEST_ACT_STOP2 2
        uint8_t testAction;  // Test action
        uint8_t rxStopped;   // Receive traffic stopped indicator (BOOL)
        uint32_t lpduSeqNo;  // Load PDU sequence number
        uint16_t udpPayload; // UDP payload (bytes)
        uint16_t spduSeqErr; // Status PDU sequence error count
        //
        uint32_t spduTime_sec;  // Send time in last received status PDU
        uint32_t spduTime_nsec; // Send time in last received status PDU
        uint32_t lpduTime_sec;  // Send time of this load PDU
        uint32_t lpduTime_nsec; // Send time of this load PDU
};
//----------------------------------------------------------------------------
//
// Status feedback header for UDP payload of status PDUs
//
struct statusHdr {
#define STATUS_ID 0xFEED
        uint16_t statusId;  // Status ID
        uint8_t testAction; // Test action
        uint8_t rxStopped;  // Receive traffic stopped indicator (BOOL)
        uint32_t spduSeqNo; // Status PDU sequence number
        //
        struct sendingRate srStruct; // Sending rate structure
        //
        uint32_t subIntSeqNo;      // Sub-interval sequence number
        struct subIntStats sisSav; // Sub-interval saved stats
        //
        uint32_t seqErrLoss; // Loss sum
        uint32_t seqErrOoo;  // Out-of-Order sum
        //
        uint32_t clockDeltaMin; // Clock delta minimum
        uint32_t delayVarMin;   // Delay variation minimum
        uint32_t delayVarMax;   // Delay variation maximum
        uint32_t delayVarSum;   // Delay variation sum
        uint32_t delayVarCnt;   // Delay variation count
        uint32_t rttMinimum;    // Minimum round-trip time sampled
        uint32_t rttSample;     // Last round-trip time sample
        uint8_t delayMinUpd;    // Delay minimum(s) updated
        uint8_t reserved2;      // (alignment)
        uint16_t reserved3;     // (alignment)
        //
        uint32_t tiDeltaTime;   // Trial interval delta time
        uint32_t tiRxDatagrams; // Trial interval receive datagrams
        uint32_t tiRxBytes;     // Trial interval receive bytes
        //
        uint32_t spduTime_sec;  // Send time of this status PDU
        uint32_t spduTime_nsec; // Send time of this status PDU
};
//----------------------------------------------------------------------------
