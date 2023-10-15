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
 * UDP Speed Test - udpst.h
 *
 * This file contains all the primary software-specific constants and structure
 * definitions.
 *
 */

#ifndef UDPST_H
#define UDPST_H

#ifdef __linux__
#include "config.h"
#endif /* __linux__ */
#include "udpst_common.h"

//----------------------------------------------------------------------------
//
// General
//
#define SOFTWARE_TITLE    "UDP Speed Test"
#define USTEST_TEXT       "Upstream"
#define DSTEST_TEXT       "Downstream"
#define TIME_FORMAT       "%Y-%m-%d %H:%M:%S"
#define STRING_SIZE       1024               // String buffer size
#define AUTH_KEY_SIZE     32                 // Authentication key size
#define HS_DELTA_BACKUP   3                  // High-speed delta backup multiplier
#define MAX_SERVER_CONN   256                // Max server connections
#define MAX_CLIENT_CONN   (MAX_MC_COUNT + 1) // Max client connections (plus aggregate)
#define MAX_EPOLL_EVENTS  MAX_SERVER_CONN    // Max epoll events handled at one time
#define AGG_QUERY_TIME    10                 // Query timer for aggregate connection (ms)
#define MIN_RANDOM_START  5                  // Minimum used for random I/O start (ms)
#define MAX_RANDOM_START  50                 // Maximum used for random I/O start (ms)
#define AUTH_TIME_WINDOW  150                // Authentication +/- time windows (sec)
#define AUTH_ENFORCE_TIME TRUE               // Enforce authentication time window
#define WARNING_MSG_LIMIT 10                 // Warning message limit (per connection)
#define WARNING_NOTRAFFIC 1                  // Receive traffic stopped warning threshold (sec)
#define TIMEOUT_NOTRAFFIC (WARNING_NOTRAFFIC + 2)
//
// General status and status base values for warning and error ranges (ErrorStatus)
//   See udpst_protocol.h for CHSR_CRSP_XXXX and CHTA_CRSP_XXXX values
//
#define STATUS_SUCCESS      0   // Success (test completed without incident)
#define STATUS_WARNBASE     1   // Warnings and soft errors
#define STATUS_WARNMAX      49  // Warning and soft error maximum
#define STATUS_CONF_ERRBASE 50  // Configuration errors
#define STATUS_INIT_ERRBASE 75  // Initialization errors
#define CHSR_CRSP_ERRBASE   100 // Setup request errors (offset by CHSR_CRSP_XXXX)
#define CHTA_CRSP_ERRBASE   150 // Test activation errors (offset by CHTA_CRSP_XXXX)
#define STATUS_CONN_ERRBASE 200 // Connection errors
#define STATUS_ERROR        255 // General or unspecified error
// Warnings (offset of STATUS_WARNBASE)
#define WARN_SRV_TIMEOUT 0 // Server response timeout
#define WARN_LOC_STATUS  1 // Locally received status messages lost
#define WARN_REM_STATUS  2 // Remotely received status messages lost
#define WARN_LOC_STOPPED 3 // Locally received traffic has stopped
#define WARN_REM_STOPPED 4 // Remotely received traffic has stopped
// Configuration errors (offset of STATUS_CONF_ERRBASE)
#define ERROR_CONF_GENERIC 0 // Generic configuration issue
// Initialization errors (offset of STATUS_INIT_ERRBASE)
#define ERROR_INIT_GENERIC 0 // Generic configuration issue
// Connection errors (offset of STATUS_CONN_ERRBASE)
#define ERROR_CONN_MIN 0 // Minimum connections unavailable
//
// Alternative for INET6_ADDRSTRLEN (allows for '%' and textual Zone ID)
//
#define INET6_ADDR_STRLEN (INET6_ADDRSTRLEN + 1 + IFNAMSIZ)
//
// DISABLE_INT_TIMER disables the interval timer when compiling for client
// devices that are unable to support the required clock resolution. Because
// this results in high CPU utilization, it is not recommended for standard
// server operation.
//
#ifndef DISABLE_INT_TIMER
#define SEND_TIMER_ADJ 75 // Data send timer adjustment (us)
#else
#define SEND_TIMER_ADJ 0 // Set to zero when interval timer is disabled
#endif

//----------------------------------------------------------------------------
//
// Default and min/max parameter values
//
#define DEF_JUMBO_STATUS     TRUE       // Enable/disable jumbo datagram sizes
#define DEF_USE_OWDELVAR     FALSE      // Use one-way delay instead of RTT
#define DEF_IGNORE_OOODUP    TRUE       // Ignore Out-of-Order/Duplicate datagrams
#define DEF_MC_COUNT         1          // Multi-connection test count
#define MIN_MC_COUNT         1          //
#define MAX_MC_COUNT         24         //
#define DEF_IPTOS_BYTE       0          // IP ToS byte for testing
#define MIN_IPTOS_BYTE       0          //
#define MAX_IPTOS_BYTE       UINT8_MAX  //
#define DEF_SRINDEX_CONF     UINT16_MAX // Sending rate index, <Auto> = UINT16_MAX
#define MIN_SRINDEX_CONF     0          //
#define MAX_SRINDEX_CONF     (MAX_SENDING_RATES - 1)
#define SRIDX_ISSTART_PREFIX '@'        // Prefix char for sending rate starting point
#define DEF_TESTINT_TIME     10         // Test interval time (sec)
#define MIN_TESTINT_TIME     5          //
#define MAX_TESTINT_TIME     3600       //
#define DEF_SUBINT_PERIOD    1          // Sub-interval period (sec)
#define MIN_SUBINT_PERIOD    1          //
#define MAX_SUBINT_PERIOD    10         //
#define DEF_CONTROL_PORT     25000      // Control port
#define MIN_CONTROL_PORT     1          //
#define MAX_CONTROL_PORT     UINT16_MAX //
#define DEF_BIMODAL_COUNT    0          // Bimodal initial sub-interval count
#define MIN_BIMODAL_COUNT    1          //
#define MAX_BIMODAL_COUNT    (MAX_TESTINT_TIME / MIN_SUBINT_PERIOD)
#define DEF_SOCKET_BUF       1024000        // Socket buffer to request
#define MIN_SOCKET_BUF       0              // (0 = System default/minimum)
#define MAX_SOCKET_BUF       16777216       //
#define DEF_LOW_THRESH       30             // Low delay variation threshold (ms)
#define MIN_LOW_THRESH       1              //
#define MAX_LOW_THRESH       10000          //
#define DEF_UPPER_THRESH     90             // Upper delay variation threshold (ms)
#define MIN_UPPER_THRESH     1              //
#define MAX_UPPER_THRESH     10000          //
#define DEF_TRIAL_INT        50             // Status feedback/trial interval (ms)
#define MIN_TRIAL_INT        5              //
#define MAX_TRIAL_INT        250            //
#define DEF_SLOW_ADJ_TH      3              // Slow adjustment threshold
#define MIN_SLOW_ADJ_TH      1              //
#define MAX_SLOW_ADJ_TH      UINT16_MAX     //
#define DEF_HS_DELTA         10             // High-speed delta (rows)
#define MIN_HS_DELTA         1              //
#define MAX_HS_DELTA         UINT8_MAX      //
#define DEF_SEQ_ERR_TH       10             // Sequence error threshold
#define MIN_SEQ_ERR_TH       0              //
#define MAX_SEQ_ERR_TH       UINT16_MAX     //
#define DEF_LOGFILE_MAX      1000           // Log file max size (KBytes)
#define MIN_LOGFILE_MAX      10             //
#define MAX_LOGFILE_MAX      1000000        //
#define MIN_REQUIRED_BW      1              // Required OR available bandwidth (Mbps)
#define MAX_CLIENT_BW        10000          //
#define MAX_SERVER_BW        100000         //
#define DEF_RA_ALGO          CHTA_RA_ALGO_B // Default rate adjustment algorithm

//----------------------------------------------------------------------------
//
// Sending rate payload, protocol, and buffer sizes
//
#define MAX_SENDING_RATES 1153              // Max rows in sending rate table
#define BASE_SEND_TIMER1  MIN_INTERVAL_USEC // Base send timer, transmitter 1 (us)
#define BASE_SEND_TIMER2  1000              // Base send timer, transmitter 2 (us)
#define MAX_L3_PACKET     1250              // Max desired L3 packet size
#define MAX_JL3_PACKET    9000              // Max desired jumbo L3 packet
#define MAX_TL3_PACKET    1500              // Max desired traditional L3 packet
#define L3DG_OVERHEAD     (8 + 20)          // UDP + IPv4
#define L2DG_OVERHEAD     (8 + 20 + 14)     // UDP + IPv4 + Eth(NoFCS)
#define L1DG_OVERHEAD     (8 + 20 + 18)     // UDP + IPv4 + Eth(w/FCS)
#define L0DG_OVERHEAD     (8 + 20 + 18 + 4) // UDP + IPv4 + Eth(w/FCS) + VLAN
#define IPV6_ADDSIZE      20                // IPv6 additional size (over IPv4)
#define MIN_PAYLOAD_SIZE  (sizeof(struct loadHdr) + IPV6_ADDSIZE)
#define MAX_PAYLOAD_SIZE  (MAX_L3_PACKET - L3DG_OVERHEAD)
#define MAX_JPAYLOAD_SIZE (MAX_JL3_PACKET - L3DG_OVERHEAD)
#define MAX_TPAYLOAD_SIZE (MAX_TL3_PACKET - L3DG_OVERHEAD)
//
// Send buffer needs to contain all datagram payloads when not using GSO (or all segment buffers with GSO)
//
#define DEF_BUFFER_SIZE 65536 // Larger than IP_MAXPACKET (with even boundary)
#define MMSG_SEGMENTS   ((MAX_BURST_SIZE / (DEF_BUFFER_SIZE / MAX_JPAYLOAD_SIZE) + 1))
#define SND_BUFFER_SIZE (DEF_BUFFER_SIZE * MMSG_SEGMENTS)
#define GSO_CMSG_LEN    (CMSG_LEN(sizeof(uint16_t)))
#define GSO_CMSG_SIZE   (CMSG_SPACE(sizeof(uint16_t)))
#ifndef UDP_MAX_SEGMENTS
#define UDP_MAX_SEGMENTS (1 << 6UL)
#endif
//
// Receive buffer is used to read RECVMMSG_SIZE messages (of size RCV_HEADER_SIZE) when using recvmmsg()
//   Limit: RECVMMSG_SIZE <= DEF_BUFFER_SIZE / RCV_HEADER_SIZE
//   Suggested: RECVMMSG_SIZE >= (DEF_SOCKET_BUF * 2) / MAX_JPAYLOAD_SIZE
//
#define RCV_BUFFER_SIZE DEF_BUFFER_SIZE
#define RCV_HEADER_SIZE ((((sizeof(struct loadHdr) - 1) / 4) + 1) * 4) // Enforce 32-bit boundary
#define RECVMMSG_SIZE   256

//----------------------------------------------------------------------------
//
// File descriptor flags and file modes
//
#define LOGFILE_FLAGS (O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK)
#define LOGFILE_MODE  (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

//----------------------------------------------------------------------------
//
// Socket management actions
//
#define SMA_LOOKUP 0
#define SMA_BIND   1
#define SMA_UPDATE 2

//----------------------------------------------------------------------------
//
// Rate Adjustment Algorithms
//
#define RETRY_THRESH_ALGOC 5 // AlgoC: Initial retry threshold

//----------------------------------------------------------------------------
// Data structures
//----------------------------------------------------------------------------
//
// Configuration structure for settings and parameters
//
struct configuration {
        int maxConnections;              // Maximum simultaneous connections
        BOOL usTesting;                  // Upstream testing requested
        BOOL dsTesting;                  // Downstream testing requested
        int addrFamily;                  // Address family
        BOOL ipv4Only;                   // Only allow IPv4 testing
        BOOL ipv6Only;                   // Only allow IPv6 testing
        int minConnCount;                // Minimum multi-connection count
        int maxConnCount;                // Maximum multi-connection count
        BOOL isDaemon;                   // Execute as daemon
        BOOL oneTest;                    // Exit after one test (server only)
        BOOL errSuppress;                // Suppress send/receive errors
        BOOL verbose;                    // Enable verbose messaging
        BOOL summaryOnly;                // Do not show sub-interval stats
        BOOL jsonOutput;                 // JSON Output format
        BOOL jsonBrief;                  // JSON Output should be minimized
        BOOL jsonFormatted;              // JSON Output should be formatted
        BOOL jumboStatus;                // Enable/disable jumbo datagram sizes
        BOOL traditionalMTU;             // Traditional (1500 byte) MTU
        BOOL debug;                      // Enable debug messaging
        BOOL randPayload;                // Payload randomization
        int rateAdjAlgo;                 // Rate adjustment algorithm
        BOOL showSendingRates;           // Display sending rate table parameters
        BOOL showLossRatio;              // Display loss ratio
        int bimodalCount;                // Bimodal initial sub-interval count
        BOOL useOwDelVar;                // Use one-way delay instead of RTT
        BOOL ignoreOooDup;               // Ignore Out-of-Order/Duplicate datagrams
        char authKey[AUTH_KEY_SIZE + 1]; // Authentication key
        int ipTosByte;                   // IP ToS byte for testing
        int srIndexConf;                 // Configured sending rate index
        BOOL srIndexIsStart;             // Configured SR index is starting point
        int testIntTime;                 // Test interval time (sec)
        int subIntPeriod;                // Sub-interval period (sec)
        int controlPort;                 // Control port number for setup requests
        int sockSndBuf;                  // Socket send buffer size
        int sockRcvBuf;                  // Socket receive buffer size
        int lowThresh;                   // Low delay variation threshold
        int upperThresh;                 // Upper delay variation threshold
        int trialInt;                    // Status feedback/trial interval (ms)
        int slowAdjThresh;               // Slow rate adjustment threshold
        int highSpeedDelta;              // High-speed row adjustment delta
        int seqErrThresh;                // Sequence error threshold
        int maxBandwidth;                // Required OR available bandwidth
        BOOL intfForMax;                 // Local interface used for maximum
        char intfName[IFNAMSIZ + 1];     // Local interface for supplemental stats
        int logFileMax;                  // Maximum log file size
        char *logFile;                   // Name of log file
        char *outputFile;                // Name of output (export) file
};
//----------------------------------------------------------------------------
//
// Repository of global variables and structures
//
struct serverId {
        char *name;                 // Server hostname or IP address
        char ip[INET6_ADDR_STRLEN]; // Server IP address
        int port;                   // Server control port number
};
struct testSummary {
        unsigned int rxDatagrams; // Total rx datagrams
        unsigned int seqErrLoss;  // Loss sum
        unsigned int seqErrOoo;   // Out-of-Order sum
        unsigned int seqErrDup;   // Duplicate sum
        unsigned int delayVarMin; // Delay variation minimum
        unsigned int delayVarMax; // Delay variation maximum
        unsigned int delayVarSum; // Delay variation sum
        unsigned int rttMinimum;  // Minimum round-trip time
        unsigned int rttMaximum;  // Maximum round-trip time
        double rateSumL3;         // Rate sum at L3
        double rateSumIntf;       // Rate sum of local interface
        unsigned int sampleCount; // Sample count
};
struct repository {
        struct timespec systemClock;          // Clock reference (CLOCK_REALTIME)
        int epollFD;                          // Epoll file descriptor
        int maxConnIndex;                     // Largest (current) connection index
        int mcIdent;                          // Multi-connection identifier
        struct sendingRate *sendingRates;     // Sending rate table (array)
        int maxSendingRates;                  // Size (rows) of sending rate table
        char *sndBuffer;                      // Send buffer for load PDUs
        char *defBuffer;                      // Default buffer for general I/O
        char *randData;                       // Randomized seed data
        char *sndBufRand;                     // Send buffer for randomized load PDUs
        char *rcvDataPtr;                     // Received data pointer for load PDUs
        int rcvDataSize;                      // Received data size in default buffer
        struct sockaddr_storage remSas;       // Remote IP sockaddr storage
        socklen_t remSasLen;                  // Remote IP sockaddr storage length
        BOOL isServer;                        // Execute as server
        struct serverId server[MAX_MC_COUNT]; // Array of server ID structures
        int serverCount;                      // Size of server structure array
        int hSpeedThresh;                     // Index of high-speed threshold
        int logFileSize;                      // Current log file size
        int usBandwidth;                      // Current upstream bandwidth
        int dsBandwidth;                      // Current downstream bandwidth
        int endTimeStatus;                    // Exit status when end time expires
        int actConnCount;                     // Active testing connection count
        int sisConnCount;                     // Sub-interval stats connection count
        BOOL testHdrDone;                     // Test header creation complete
        double siAggRateL3;                   // Sub-interval L3 aggregate rate
        double siAggRateL2;                   // Sub-interval L2 aggregate rate
        double siAggRateL1;                   // Sub-interval L1 aggregate rate
        double siAggRateL0;                   // Sub-interval L1+VLAN aggregate rate
        struct testSummary testSum;           // Test summary statistics
        int intfFD;                           // File descriptor to read interface stats
        unsigned long intfBytes;              // Last byte counter of interface stats
        struct timespec intfTime;             // Sample time of interface stats
        struct timespec timeOfMax[2];         // Time of maximums (bimodal)
        int actConnections[2];                // Active testing connections (bimodal)
        struct subIntStats sisMax[2];         // Sub-interval maximum stats (bimodal)
        double rateMaxL3[2];                  // L3 rate maximums (bimodal)
        double rateMaxL2[2];                  // L2 rate maximums (bimodal)
        double rateMaxL1[2];                  // L1 rate maximums (bimodal)
        double rateMaxL0[2];                  // L1+VLAN rate maximums (bimodal)
        double intfMax[2];                    // Interface maximums (bimodal)
        double intfMbps;                      // Last interface rate obtained
};
//----------------------------------------------------------------------------
//
// Data structure representing a connection to a device, file, socket, etc.
//
struct connection {
        int fd; // File descriptor
#define T_UNKNOWN  0
#define T_UDP      1
#define T_CONSOLE  2
#define T_LOG      3
#define T_NULL     4
#define T_MAXTYPES 5
        int type;       // Connection type
        int subType;    // Connection subtype
        BOOL connected; // Socket was connected
#define S_FREE      0
#define S_CREATED   1
#define S_BOUND     2
#define S_LISTEN    3
#define S_CONNPEN   4
#define S_DATA      5
#define S_MAXSTATES 6
        int state; // Current state
#define TEST_TYPE_UNK 0
#define TEST_TYPE_US  1
#define TEST_TYPE_DS  2
        int testType;                    // Test type being executed
        int testAction;                  // Test action (see load header)
        BOOL dataReady;                  // Data ready indicator
        int serverIndex;                 // Index of server ID
        int ipProtocol;                  // IPPROTO_IP or IPPROTO_IPV6
        int ipTosByte;                   // IP ToS byte for testing
        char locAddr[INET6_ADDR_STRLEN]; // Local IP address as string
        int locPort;                     // Local port
        char remAddr[INET6_ADDR_STRLEN]; // Remote IP address as string
        int remPort;                     // Remote port
        FILE *outputFPtr;                // Output file pointer
        //
        int srIndex;                 // Sending rate index
        struct sendingRate srStruct; // Sending rate structure
        unsigned int lpduSeqNo;      // Load PDU sequence number
        unsigned int spduSeqNo;      // Status PDU sequence number
        int spduSeqErr;              // Status PDU sequence error count
        //
        int protocolVer; // Protocol version
        int mcIndex;     // Multi-connection index
        int mcCount;     // Multi-connection count
        int mcIdent;     // Multi-connection identifier
        //
        int maxBandwidth;    // Required bandwidth
        int lowThresh;       // Low delay variation threshold
        int upperThresh;     // Upper delay variation threshold
        int slowAdjThresh;   // Slow rate adjustment threshold
        int slowAdjCount;    // Slow rate adjustment counter
        int trialInt;        // Status feedback/trial interval (ms)
        int testIntTime;     // Test interval time (sec)
        int subIntPeriod;    // Sub-interval period (sec)
        int srIndexConf;     // Configured sending rate index
        BOOL srIndexIsStart; // Configured SR index is starting point
        int highSpeedDelta;  // High-speed row adjustment delta
        int seqErrThresh;    // Sequence error threshold
        BOOL randPayload;    // Payload randomization
        int rateAdjAlgo;     // Rate adjustment algorithm
        //
        int algoCRetryCount;  // AlgoC: Waiting timer till next multiplicative retry
        int algoCRetryThresh; // AlgoC: Threshold for multiplicative retry
        BOOL algoCUpdate;     // AlgoC: Indicates when max send rate was updated
        //
        struct timespec endTime;      // Connection end time
        int (*priAction)(int);        // Primary action upon IO
        int (*secAction)(int);        // Secondary action upon IO
        struct timespec timer1Thresh; // First timer threshold
        int (*timer1Action)(int);     // First action upon expiry
        struct timespec timer2Thresh; // Second timer threshold
        int (*timer2Action)(int);     // Second action upon expiry
        struct timespec timer3Thresh; // Third timer threshold
        int (*timer3Action)(int);     // Third action upon expiry
        //
        struct timespec subIntClock; // Sub-interval clock
        unsigned int accumTime;      // Accumulated time
        unsigned int subIntSeqNo;    // Sub-interval sequence number
        struct subIntStats sisAct;   // Sub-interval active stats
        struct subIntStats sisSav;   // Sub-interval saved stats
        int subIntCount;             // Sub-interval count
        //
#define LPDU_HISTORY_SIZE 32 // Size must be power of 2
#define LPDU_HISTORY_MASK (LPDU_HISTORY_SIZE - 1)
        unsigned int lpduHistBuf[LPDU_HISTORY_SIZE]; // History buffer of last seq numbers
        unsigned int lpduHistIdx;                    // History buffer index of next seq number
        BOOL ignoreOooDup;                           // Ignore Out-of-Order/Duplicate datagrams
        unsigned int seqErrLoss;                     // Loss sum
        unsigned int seqErrOoo;                      // Out-of-Order sum
        unsigned int seqErrDup;                      // Duplicate sum
        //
        BOOL useOwDelVar;         // Use one-way delay instead of RTT
        int clockDeltaMin;        // Clock delta minimum
        unsigned int delayVarMin; // Delay variation minimum
        unsigned int delayVarMax; // Delay variation maximum
        unsigned int delayVarSum; // Delay variation sum
        unsigned int delayVarCnt; // Delay variation count
        unsigned int rttMinimum;  // Minimum round-trip time
        unsigned int rttSample;   // Last round-trip time (sampled)
        BOOL delayMinUpd;         // Delay minimum(s) updated
        //
        struct timespec trialIntClock; // Trial interval clock
        unsigned int tiDeltaTime;      // Trial interval delta time
        unsigned int tiRxDatagrams;    // Trial interval receive datagrams
        unsigned int tiRxBytes;        // Trial interval receive bytes
        //
        int warningCount;          // Warning message count
        BOOL rxStoppedLoc;         // Local receive traffic stopped indicator
        BOOL rxStoppedRem;         // Remote receive traffic stopped indicator
        struct timespec pduRxTime; // Receive time of last load or status PDU
        struct timespec spduTime;  // Send time in last received status PDU
};
//----------------------------------------------------------------------------

#endif /* UDPST_H */
