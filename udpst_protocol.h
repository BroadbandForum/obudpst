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

#ifndef UDPST_PROTOCOL_H
#define UDPST_PROTOCOL_H

//----------------------------------------------------------------------------
//
// Protocol version
//
#define PROTOCOL_VER  20 // Current protocol version between client and server
#define PROTOCOL_MIN  11 // Minimum protocol version for backward compatibility
#define MSSUBINT_PVER 20 // Protocol version required for ms sub-interval support
#define EXTAUTH_PVER  20 // Protocol version required for extended auth. support
#define SRASUPP_PVER  20 // Protocol version required for sending rate adj. suppression

//----------------------------------------------------------------------------
//
// Control header for UDP payload of Setup Request/Response PDUs
//
struct controlHdrSR {
#define CHSR_ID 0xACE1
        uint16_t pduId;       // PDU ID
        uint16_t protocolVer; // Protocol version
        uint8_t mcIndex;      // Multi-connection index
        uint8_t mcCount;      // Multi-connection count
        uint16_t mcIdent;     // Multi-connection identifier
#define CHSR_CREQ_NONE     0
#define CHSR_CREQ_SETUPREQ 1   // Setup request
#define CHSR_CREQ_SETUPRSP 2   // Setup response
        uint8_t cmdRequest;    // Command request
#define CHSR_CRSP_NONE     0   // (used with request)
#define CHSR_CRSP_ACKOK    1   // Acknowledgment
#define CHSR_CRSP_BADVER   2   // Bad version
#define CHSR_CRSP_BADJS    3   // Jumbo setting mismatch
#define CHSR_CRSP_AUTHNC   4   // Auth. not configured
#define CHSR_CRSP_AUTHREQ  5   // Auth. required
#define CHSR_CRSP_AUTHINV  6   // Auth. (mode) invalid
#define CHSR_CRSP_AUTHFAIL 7   // Auth. failure
#define CHSR_CRSP_AUTHTIME 8   // Auth. time invalid
#define CHSR_CRSP_NOMAXBW  9   // Max bandwidth required
#define CHSR_CRSP_CAPEXC   10  // Capacity exceeded
#define CHSR_CRSP_BADTMTU  11  // Trad. MTU mismatch
#define CHSR_CRSP_MCINVPAR 12  // Multi-conn. invalid params
#define CHSR_CRSP_CONNFAIL 13  // Conn. allocation failure
        uint8_t cmdResponse;   // Command response
#define CHSR_USDIR_BIT 0x8000  // Upstream direction bit
        uint16_t maxBandwidth; // Required bandwidth in Mbps
        uint16_t testPort;     // Test port on server
#define CHSR_JUMBO_STATUS    0x01
#define CHSR_TRADITIONAL_MTU 0x02
        uint8_t modifierBitmap; // Modifier bitmap
        // ========== Integrity Verification ==========
#define AUTHMODE_0 0           // Mode 0: Unauthenticated (unofficial)
#define AUTHMODE_1 1           // Mode 1: Authenticated Control
#define AUTHMODE_2 2           // Mode 2: Authenticated Control+Status
        uint8_t authMode;      // Authentication mode
        uint32_t authUnixTime; // Authentication timestamp
#define AUTH_DIGEST_LENGTH 32  // SHA-256 digest length
        uint8_t authDigest[AUTH_DIGEST_LENGTH];
        uint8_t keyId;         // Key ID in shared table
        uint8_t reservedAuth1; // (reserved for alignment)
        uint16_t checkSum;     // Header checksum
};
#define SHA256_KEY_LEN 32                          // Authentication key length
#define CHSR_SIZE_CVER sizeof(struct controlHdrSR) // Current protocol version
#define CHSR_SIZE_MVER (CHSR_SIZE_CVER)            // Minimum protocol version
//----------------------------------------------------------------------------
//
// Control header for UDP payload of Null Request PDU
//
struct controlHdrNR {
#define CHNR_ID 0xDEAD
        uint16_t pduId;       // PDU ID
        uint16_t protocolVer; // Protocol version
#define CHNR_CREQ_NONE    0
#define CHNR_CREQ_NULLREQ 1  // Null request
        uint8_t cmdRequest;  // Command request
#define CHNR_CRSP_NONE 0     // (used with request)
        uint8_t cmdResponse; // Command response
        uint8_t reserved1;   // (reserved for alignment)
        // ========== Integrity Verification ==========
        uint8_t authMode;      // Authentication mode
        uint32_t authUnixTime; // Authentication timestamp
        uint8_t authDigest[AUTH_DIGEST_LENGTH];
        uint8_t keyId;         // Key ID in shared table
        uint8_t reservedAuth1; // (reserved for alignment)
        uint16_t checkSum;     // Header checksum
};
#define CHNR_SIZE_CVER sizeof(struct controlHdrNR) // Current protocol version
#define CHNR_SIZE_MVER (CHNR_SIZE_CVER)            // Minimum protocol version
//----------------------------------------------------------------------------
#define MAX_BURST_SIZE    100        // Max datagram burst size
#define MIN_INTERVAL_USEC 100        // Min interval/timer granularity (us)
#define SRATE_RAND_BIT    0x80000000 // Randomization bit (remaining contain max)
//
// Sending rate structure for a single row of transmission parameters
//
struct sendingRate {
        uint32_t txInterval1; // Transmit interval (us)
        uint32_t udpPayload1; // UDP payload (bytes)
        uint32_t burstSize1;  // UDP burst size per interval
        uint32_t txInterval2; // Transmit interval (us)
        uint32_t udpPayload2; // UDP payload (bytes)
        uint32_t burstSize2;  // UDP burst size per interval
        uint32_t udpAddon2;   // UDP add-on (bytes)
};
//
// Control header for UDP payload of Test Act. Request/Response PDUs
//
struct controlHdrTA {
#define CHTA_ID 0xACE2
        uint16_t pduId;       // PDU ID
        uint16_t protocolVer; // Protocol version
#define CHTA_CREQ_NONE      0
#define CHTA_CREQ_TESTACTUS 1        // Test activation upstream
#define CHTA_CREQ_TESTACTDS 2        // Test activation downstream
        uint8_t cmdRequest;          // Command request
#define CHTA_CRSP_NONE     0         // (used with request)
#define CHTA_CRSP_ACKOK    1         // Acknowledgment
#define CHTA_CRSP_BADPARAM 2         // Bad/invalid test params
        uint8_t cmdResponse;         // Command response
        uint16_t lowThresh;          // Low delay variation threshold (ms)
        uint16_t upperThresh;        // Upper delay variation threshold (ms)
        uint16_t trialInt;           // Status Feedback/trial interval (ms)
        uint16_t testIntTime;        // Test interval time (sec)
        uint8_t reserved1;           // (reserved for alignment)
        uint8_t dscpEcn;             // DiffServ and ECN field for testing
#define CHTA_SRIDX_DEF UINT16_MAX    // Request default server search
        uint16_t srIndexConf;        // Configured Sending Rate Table index
        uint8_t useOwDelVar;         // Use one-way delay, not RTT (BOOL)
        uint8_t highSpeedDelta;      // High-speed row adjustment delta
        uint16_t slowAdjThresh;      // Slow rate adjustment threshold
        uint16_t seqErrThresh;       // Sequence error threshold
        uint8_t ignoreOooDup;        // Ignore Out-of-Order/Dup (BOOL)
#define CHTA_SRIDX_ISSTART 0x01      // Use srIndexConf as starting index
#define CHTA_RAND_PAYLOAD  0x02      // Randomize payload
        uint8_t modifierBitmap;      // Modifier bitmap
#define CHTA_RA_ALGO_B 0             // Algorithm B
#define CHTA_RA_ALGO_C 1             // Algorithm C
        uint8_t rateAdjAlgo;         // Rate adjust. algorithm
        uint8_t reserved2;           // (reserved for alignment)
        struct sendingRate srStruct; // Sending rate structure
        uint16_t subIntPeriod;       // Sub-interval period (ms)
        uint16_t reserved3;          // (reserved for alignment)
        uint16_t reserved4;          // (reserved for alignment)
        uint8_t reserved5;           // (reserved for alignment)
        // ========== Integrity Verification ==========
        uint8_t authMode;      // Authentication mode
        uint32_t authUnixTime; // Authentication timestamp
        uint8_t authDigest[AUTH_DIGEST_LENGTH];
        uint8_t keyId;         // Key ID in shared table
        uint8_t reservedAuth1; // (reserved for alignment)
        uint16_t checkSum;     // Header checksum
};
#define CHTA_RA_ALGO_MIN CHTA_RA_ALGO_B
#define CHTA_RA_ALGO_MAX CHTA_RA_ALGO_C
#define CHTA_SIZE_CVER   sizeof(struct controlHdrTA) // Current protocol version
#define CHTA_SIZE_MVER   (CHTA_SIZE_CVER - 44)       // Minimum protocol version
//----------------------------------------------------------------------------
//
// Load header for UDP payload of Load PDUs
//
struct loadHdr {
#define LOAD_ID 0xBEEF
        uint16_t pduId;         // PDU ID
#define TEST_ACT_TEST  0        // Test active
#define TEST_ACT_STOP1 1        // Stop indication used locally by server
#define TEST_ACT_STOP2 2        // Stop indication exchanged with client
        uint8_t testAction;     // Test action
        uint8_t rxStopped;      // Receive traffic stopped (BOOL)
        uint32_t lpduSeqNo;     // Load PDU sequence number
        uint16_t udpPayload;    // UDP payload (bytes)
        uint16_t spduSeqErr;    // Status PDU sequence error count
        uint32_t spduTime_sec;  // Send time in last rx'd status PDU
        uint32_t spduTime_nsec; // Send time in last rx'd status PDU
        uint32_t lpduTime_sec;  // Send time of this load PDU
        uint32_t lpduTime_nsec; // Send time of this load PDU
        uint16_t rttRespDelay;  // Response delay for RTT (ms)
        uint16_t checkSum;      // Header checksum
};
#define TEST_ACT_MAX TEST_ACT_STOP2
//----------------------------------------------------------------------------
//
// Sub-interval statistics structure for received traffic information
//
struct subIntStats {
        uint32_t rxDatagrams; // Received datagrams
        uint64_t rxBytes;     // Received bytes (64 bits)
        uint32_t deltaTime;   // Time delta (us)
        uint32_t seqErrLoss;  // Loss sum
        uint32_t seqErrOoo;   // Out-of-Order sum
        uint32_t seqErrDup;   // Duplicate sum
        uint32_t delayVarMin; // Delay variation minimum (ms)
        uint32_t delayVarMax; // Delay variation maximum (ms)
        uint32_t delayVarSum; // Delay variation sum (ms)
        uint32_t delayVarCnt; // Delay variation count
        uint32_t rttMinimum;  // Minimum round-trip time (ms)
        uint32_t rttMaximum;  // Maximum round-trip time (ms)
        uint32_t accumTime;   // Accumulated time (ms)
};
//
// Status feedback header for UDP payload of status PDUs
//
struct statusHdr {
#define STATUS_ID 0xFEED
        uint16_t pduId;              // PDU ID
        uint8_t testAction;          // Test action
        uint8_t rxStopped;           // Receive traffic stopped (BOOL)
        uint32_t spduSeqNo;          // Status PDU sequence number
        struct sendingRate srStruct; // Sending rate structure
        uint32_t subIntSeqNo;        // Sub-interval sequence number
        struct subIntStats sisSav;   // Sub-interval saved stats
        uint32_t seqErrLoss;         // Loss sum
        uint32_t seqErrOoo;          // Out-of-Order sum
        uint32_t seqErrDup;          // Duplicate sum
        uint32_t clockDeltaMin;      // Clock delta minimum (ms)
        uint32_t delayVarMin;        // Delay variation minimum (ms)
        uint32_t delayVarMax;        // Delay variation maximum (ms)
        uint32_t delayVarSum;        // Delay variation sum (ms)
        uint32_t delayVarCnt;        // Delay variation count
#define STATUS_NODEL UINT32_MAX      // No delay data/value
        uint32_t rttMinimum;         // Min round-trip time sampled (ms)
        uint32_t rttVarSample;       // Last round-trip time sample (ms)
        uint8_t delayMinUpd;         // Delay minimum(s) updated (BOOL)
        uint8_t reserved1;           // (reserved for alignment)
        uint16_t reserved2;          // (reserved for alignment)
        uint32_t tiDeltaTime;        // Trial interval delta time (us)
        uint32_t tiRxDatagrams;      // Trial interval receive datagrams
        uint32_t tiRxBytes;          // Trial interval receive bytes
        uint32_t spduTime_sec;       // Send time of this status PDU
        uint32_t spduTime_nsec;      // Send time of this status PDU
        uint16_t reserved3;          // (reserved for alignment)
        uint8_t reserved4;           // (reserved for alignment)
        // ========== Integrity Verification ==========
        uint8_t authMode;      // Authentication mode
        uint32_t authUnixTime; // Authentication timestamp
        uint8_t authDigest[AUTH_DIGEST_LENGTH];
        uint8_t keyId;         // Key ID in shared table
        uint8_t reservedAuth1; // (reserved for alignment)
        uint16_t checkSum;     // Header checksum
};
#define STATUS_SIZE_CVER sizeof(struct statusHdr) // Current protocol version
#define STATUS_SIZE_MVER (STATUS_SIZE_CVER - 48)  // Minimum protocol version
//----------------------------------------------------------------------------
//
// Authentication overlay structure (for common processing across PDUs)
//
struct authOverlay {           // Start on 32-bit boundary
        uint16_t reserved1;    // (reserved for alignment) - DO NOT OVERWRITE
        uint8_t reserved2;     // (reserved for alignment) - DO NOT OVERWRITE
        uint8_t authMode;      // Authentication mode
        uint32_t authUnixTime; // Authentication timestamp
        uint8_t authDigest[AUTH_DIGEST_LENGTH];
        uint8_t keyId;         // Key ID in shared table
        uint8_t reservedAuth1; // (reserved for alignment)
        uint16_t checkSum;     // Header checksum
};
#define AO_MODE_OFFSET 3                          // Byte offset of authMode (for alignment)
#define AO_SIZE_CVER   sizeof(struct authOverlay) // Current protocol version
#define AO_SIZE_MVER   (AO_SIZE_CVER)             // Minimum protocol version
//----------------------------------------------------------------------------

#endif /* UDPST_PROTOCOL_H */
