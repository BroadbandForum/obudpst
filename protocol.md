# Test Protocol Description ( version 8 )

(An Internet-Draft, 
https://datatracker.ietf.org/doc/html/draft-morton-ippm-capacity-metric-protocol-02
describes Protocol Version 8 in even more detail.)

## Introduction 
The udpst utility uses a unique Control protocol and test packet format. 
All control message PDUs and test PDUs use UDP transport, with limited reliability
provided at the control application layer. 

The minimum test architecture requires two hosts, one in the role of server 
usually located within the network, and another host in the role of client 
residing at the subscriber end of the access path under test.

On the server, the process is run in either the foreground mode or
background  mode (as a daemon) where it awaits Setup requests on its UDP control
port.

The client, whose process is always run in the foreground, requires 
specification of a test direction parameter (upstream or downstream test)
as well as the hostname or IP address of the server 
(these are the minimum input parameters). 


## Setup Request and Response Exchange
The client SHALL begin the Control protocol 
connection by sending a Setup Request message to the server's control port. 

The client SHALL simultaneously start
a test initiation timer so that if the control protocol fails to
complete all exchanges in the allocated time, the client software SHALL exit 
(close the UDP socket and indicate an error message to the user).

(Note: in version 8, the watchdog time is configured, in udpst.h, as 
#define WARNING_NOTRAFFIC 1    // Receive traffic stopped warning threshold (sec)
#define TIMEOUT_NOTRAFFIC (WARNING_NOTRAFFIC + 4)  or 5 seconds)

The Setup Request message PDU SHALL be organized as follows:
```
        uint16_t controlId;   // Control ID = 0xACE1
        uint16_t protocolVer; // Protocol version = 0x08
        uint8_t cmdRequest;   // Command request = 1 (request)
        uint8_t cmdResponse;  // Command response = 0
        uint16_t reserved1;   // Reserved (alignment)
        uint16_t testPort;    // Test port on server  (=0 for Request)
        uint8_t jumboStatus;  // Jumbo datagram support status (BOOL)
        uint8_t authMode;     // Authentication mode
        uint32_t authUnixTime;// Authentication time stamp
        unsigned char authDigest[AUTH_DIGEST_LENGTH] // SHA256_DIGEST_LENGTH = 32 oct
```
The UDP PDU format layout SHALL be as follows (big-endian AB):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |          controlId            |          protocolVer          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  cmdRequest   | cmdResponse   |           reserved1           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           testPort            |  jumboStatus  |   authMode    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         authUnixTime                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                                                               |
   |                                                               |
   |                                                               |
   |                                                               |
   |          authDigest[AUTH_DIGEST_LENGTH](256 bits)             |
   |                                                               |
   |                                                               |
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```


When the server receives the Setup Request it SHALL validate the request by
checking the protocol version, the jumbo datagram support indicator, and the
authentication data if utilized. If the client has selected options for
 Jumbo datagram support status (BOOL), 
 Authentication mode, and 
 Authentication time stamp
that do not match the server configuration, 
the server MUST reject the Setup Request.

(Note: in version 8, the watchdog time is configured, in udpst.h, as 
#define WARNING_NOTRAFFIC 1    // Receive traffic stopped warning threshold (sec)
#define TIMEOUT_NOTRAFFIC (WARNING_NOTRAFFIC + 4)  or 5 seconds)

If the Setup Request must be rejected 
(due to any of the reasons in the Command response codes listed below), a Setup
Response SHALL be sent back to the client with a corresponding command response
value indicating the reason for the rejection. 
```
        uint16_t controlId;   // Control ID = 0xACE1
        uint16_t protocolVer; // Protocol version = 0x08
        uint8_t cmdRequest;   // Command request = 2 (reply)
        uint8_t cmdResponse;  // Command response = <see table below>
        uint16_t reserved1;   // Reserved (alignment)
        uint16_t testPort;    // Test port on server  (available port in Response)
        uint8_t jumboStatus;  // Jumbo datagram support status (BOOL)
        uint8_t authMode;     // Authentication mode
        uint32_t authUnixTime;// Authentication time stamp
        unsigned char authDigest[AUTH_DIGEST_LENGTH] // 32 octets, MBZ

Command Response Codes
Control Header Setup Request Code CHSR_CRSP_NONE     0 = None
Control Header Setup Request Code CHSR_CRSP_ACKOK    1 = Acknowledgement
Control Header Setup Request Code CHSR_CRSP_BADVER   2 = Bad Protocol Version
Control Header Setup Request Code CHSR_CRSP_BADJS    3 = Invalid Jumbo datagram option
Control Header Setup Request Code CHSR_CRSP_AUTHNC   4 = Unexpected Authentication in Setup Request
Control Header Setup Request Code CHSR_CRSP_AUTHREQ  5 = Authentication missing in Setup Request
Control Header Setup Request Code CHSR_CRSP_AUTHINV  6 = Invalid authentication method
Control Header Setup Request Code CHSR_CRSP_AUTHFAIL 7 = Authentication failure
Control Header Setup Request Code CHSR_CRSP_AUTHTIME 8 = Authentication time is invalid in Setup Request
````

If the server finds that the Setup Request matches its configuration and 
is otherwise acceptable, the server SHALL initiate a new connection 
for the client, using a new UDP socket allocated from the UDP
ephemeral port range. Then, the server SHALL start a watchdog timer 
(to terminate the connection in case the client goes silent), 
and sends the Setup Response back to the client (see below for composition).

If the Setup Request is accepted by the server, a Setup
Response SHALL be sent back to the client with a corresponding command
response value indicating 1 = Acknowledgement.

```
        uint16_t controlId;   // Control ID = 0xACE1
        uint16_t protocolVer; // Protocol version = 0x08
        uint8_t cmdRequest;   // Command request = 2 (reply)
        uint8_t cmdResponse;  // Command response = 1 (Acknowledgement)
        uint16_t reserved1;   // Reserved (alignment)
        uint16_t testPort;    // Test port on server  (available port in Response)
        uint8_t jumboStatus;  // Jumbo datagram support status (BOOL)
        uint8_t authMode;     // Authentication mode
        uint32_t authUnixTime;// Authentication time stamp
        unsigned char authDigest[AUTH_DIGEST_LENGTH] // 32 octets, MBZ
        ...
```

The new connection is associated with a new UDP socket allocated from 
the UDP ephemeral port range at the server.
The server SHALL set a timer for the new connection as a watchdog
(in case the client goes quiet) and send the Setup response back to the
client. 

(Note: in version 8, the watchdog time-out is configured at 5 seconds)

The Setup Response SHALL include the port number at the server for the 
new socket, and this UDP port-pair SHALL be used for all subsequent 
communication. The server SHALL also include the values of 
 Jumbo datagram support status (BOOL), 
 Authentication mode, and 
 Authentication time stamp
for the client's use on the new connection in its Setup Response,
and the remaining 32 octets MUST Be Zero (MBZ).

Finally, the new UDP connection associated with the new socket and port number is opened, and the server awaits communication there.

If a Test Activation request is not subsequently received
from the client on this new port number before the watchdog timer expires, 
the server SHALL close the socket and deallocate the port.


### Setup Response Processing at the Client

When the client receives the Setup response from the server it first 
checks the cmdResponse value.

If this value indicates an error the client SHALL display/report
a relevant message to the user or management process and exit.

If the client receives a Command Response code (CRSP) 
that is not equal to one of the codes defined above,
then the client MUST terminate the connection and terminate operation
of the current Setup Request.
 
If the Command Response code (CRSP) value indicates success 
the client SHALL compose a Test Activation
Request with all the test parameters it desires such as the test direction, the
test duration, etc. 

## Test Activation - the 0xACE2 exchange

### Test Activation Request - client

Upon a successful setup, the client SHALL then send the 
Test Activation Request to the UDP port
number the server communicated in the Setup Response.

The client SHALL compose Test Activation Request as follows:
```
        uint16_t controlId;   // Control ID
        uint16_t protocolVer; // Protocol version
        uint8_t cmdRequest; // Command request, 1 = upstream, 2 = downstream
        uint8_t cmdResponse;         // Command response (set to 0)
        uint16_t lowThresh;          // Low delay variation threshold
        uint16_t upperThresh;        // Upper delay variation threshold
        uint16_t trialInt;           // Status feedback/trial interval (ms)
        uint16_t testIntTime;        // Test interval time (sec)
        uint8_t subIntPeriod;        // Sub-interval period (sec)
        uint8_t ipTosByte;           // IP ToS byte for testing
        uint16_t srIndexConf;        // Configured sending rate index (see Note below)
        uint8_t useOwDelVar;         // Use one-way delay instead of RTT
        uint8_t highSpeedDelta;      // High-speed row adjustment delta
        uint16_t slowAdjThresh;      // Slow rate adjustment threshold
        uint16_t seqErrThresh;       // Sequence error threshold
        uint8_t ignoreOooDup;        // Ignore Out-of-Order/Duplicate datagrams
        uint8_t reserved1;           // (Alignment)
        uint16_t reserved2;          // (Alignment)

Control Header Test Activation Command Request Values:
CHTA_CREQ_NONE      0 = No Request
CHTA_CREQ_TESTACTUS 1 = Request test in Upstream direction (client to server, client takes the role of sending test packets)
CHTA_CREQ_TESTACTDS 2 = Request test in Downstream direction (server to client, client takes the role of receiving test packets)

Control Header Test Activation Command Response Values:
CHTA_CRSP_NONE     0 = Used by client when making a Request
CHTA_CRSP_ACKOK    1 = Used by Server in affirmative Response
CHTA_CRSP_BADPARAM 2 = Used by Server to indicate an error; bad parameter; reject;

```
Note: uint16_t srIndexConf is the table index of the configured *fixed* 
sending rate index to use.
The client can request the specified rate, or the server can use this field 
to coerce a maximum rate in its response. 
If the server sets to 0 in its response, client SHALL not use fixed rate.

The UDP PDU format of the Test Activation Request is as follows 
(big-endian AB):
```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |          controlId            |          protocolVer          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  cmdRequest   | cmdResponse   |           lowThresh           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |         upperThresh           |           trialInt            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |         testIntTime           | subIntPeriod  |  ipTosByte    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |         srIndexConf           |  useOwDelVar  |highSpeedDelta |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |         slowAdjThresh         |         seqErrThresh          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   | ignoreOooDup  | reserved1     |           reserved2           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
Note: This is only 28 octets of the 56 octet PDU sent, the rest are MBZ
for a Test Activation Request.


```

The client SHALL use the configuration for 

 Jumbo datagram support status (BOOL), 
 Authentication mode, and 
 Authentication time stamp 

requested and confirmed by the server.


### Test Activation Request - server response

After the server receives the Test Activation request on the 
new connection, it
MUST choose to accept, ignore or modify any of the test parameters. 

When the server sends the 
Test Activation response back, 
it SHALL set the cmd Response field to 

```
        uint8_t cmdResponse;// Command response (set to 1, ACK, or 2 error)
```

The server SHALL include all the test parameters
again to make the client aware of any changes. 

If the client has requested an upstream test, 
the server SHALL include
the transmission parameters from the first row of the sending rate
table. 

The remaining 28 octets of the Test Activation Request (normally read from the 
first row of the sending rate table) are called the Sending Rate Structure,
and SHALL be organized as follows:

```
        uint32_t txInterval1; // Transmit interval (us)
        uint32_t udpPayload1; // UDP payload (bytes)
        uint32_t burstSize1;  // UDP burst size per interval
        uint32_t txInterval2; // Transmit interval (us)
        uint32_t udpPayload2; // UDP payload (bytes)
        uint32_t burstSize2;  // UDP burst size per interval
        uint32_t udpAddon2;   // UDP add-on (bytes)
```

```
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          txInterval1                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          udpPayload1                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          burstSize1                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          txInterval2                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          udpPayload2                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          burstSize2                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          udpAdddon2                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```


Note that the server additionally has the option of
completely rejecting the request and sending back an appropriate command
response value:
```
        uint8_t cmdResponse;         // Command response (set to 2, error)
```

If activation continues, the new connection is prepared for an
upstream OR downstream test.

In the case of a downstream test, the server prepares to send with either 
a single timer to send status PDUs at the specified interval 
OR dual timers to send load PDUs based on the first row
of the sending rate table. 

The server SHALL then 
send a Test Activation response back to the client, 
update the watchdog timer with a new time-out value, and 
set a test duration timer to eventually stop the test. 

The new connection is now ready for testing.

### Test Activation Response action at the client

When the client receives the Test Activation response, 
it first checks the command response value. 

If the client receives a Test Activation command response value 
that indicates an error, the client SHALL
display/report a relevant message to the user or management process 
and exit. 

If the client receives a Test Activation command response value 
that is not equal to one of the codes defined above,
then the client MUST terminate the connection and terminate operation
of the current Setup Request.

If the client receives a Test Activation command response value 
that indicates success (CHTA_CRSP_ACKOK) 
the client SHALL update its configuration to use
any test parameters modified by the server. 

Next, the client SHALL prepare its connection for either
an upstream test with dual timers set to send load PDUs
(based on the starting transmission parameters sent by the server),
OR a downstream test with a single
timer to send status PDUs at the specified interval. 

Then, the client SHALL 
stop the test initiation timer,
set a new time-out value for the watchdog timer,
and start the timer (in case the server goes quiet). 

The connection is now ready for testing.

## Testing

The client and server take-on the roles of test packet sender or receiver
when testing begins, consistent with the direction of testing. 

Testing proceeds with one end point sending load PDUs, based on transmission
parameters from the sending rate table, 
and the other end point receiving the load PDUs and sending status
messages to communicate the traffic conditions at the receiver. 

### Load PDUs

The watchdog timer at the receiver SHALL be reset each time 
a test PDU is received. 

When the server is sending Load PDUs in the role of sender,
it SHALL use the transmission parameters directly from the sending rate table
via the index that is currently selected (which was based on the feedback in
its received status messages). 

However, when the client is sending load PDUs in the role of sender, 
it SHALL use the discreet
transmission parameters that were communicated by the server in its periodic
status messages (and not referencing a sending rate table). 
This approach allows the server to control the
individual sending rates as well as the algorithm used to decide when and how
to adjust the rate.

The test protocol is designed to locate the Load adjustment algorithm
at the server only, to allow for updates on fewer and more accessible 
hosts.

The server uses a load adjustment algorithm which evaluates measurements, either
it's own or the contents of received feedback messages. 
This algorithm is unique to udpst; it provides the ability to search for the
Maximum IP Capacity that is absent from other testing tools.
Although the algorithm depends on the protocol, 
it is not part of the protocol per se.

The current algorithm has three paths to its decision on the next sending rate:

1: When there are no impairments present (no sequence errors, low delay variation),
resulting in sending rate increase. 

2: When there are low impairments present (no sequence errors but higher levels
of delay variation), so the same sending rate is retained.

3: When the impairment levels are above the thresholds set for this purpose 
and "congestion" is inferred,  resulting in sending rate decrease.

The algorithm also has two modes for increasing/decreasing the sending rate:

1: A high-speed mode to achieve high sending rates quickly, 
but also back-off quickly when "congestion" is inferred from the measurements.
Any two consecutive feedback intervals that have a sequence number anomaly 
and/or contain an upper delay variation threshold exception 
in both of the two consecutive intervals,
count as the two consecutive feedback measurements required to declare
"congestion" within a test. 

2: A single-step mode where all rate adjustments use the minimum 
increase or decrease of one step in the sending rate table. 
The single step mode continues after the first inference of 
"congestion" from measured impairments.

On the other hand, the test configuration MAY use a fixed sending rate 
requested by the client, using the field below:
```
        uint16_t srIndexConf;        // Configured sending rate index
```
The client MAY communicate the desired fixed rate in it's activation request. 


The Load PDU SHALL have the following format and field definitions:

```

        uint16_t loadId; // Load ID (=0xBEEF for the LOad PDU)
        uint8_t testAction;  // Test action (= 0x00 normally, until test stop)
        uint8_t rxStopped;   // Receive traffic stopped indicator (BOOL)
        uint32_t lpduSeqNo;  // Load PDU sequence number (starts at 1)
        uint16_t udpPayload; // UDP payload LENGTH(bytes)
        uint16_t spduSeqErr; // Status PDU sequence error count
        //
        uint32_t spduTime_sec;  // Send time in last received status PDU
        uint32_t spduTime_nsec; // Send time in last received status PDU
        uint32_t lpduTime_sec;  // Send time of this load PDU
        uint32_t lpduTime_nsec; // Send time of this load PDU

Test Action Codes
TEST_ACT_TEST  0  // normal
TEST_ACT_STOP1 1  // normal stop at end of test: server sends in STATUS or Test PDU
TEST_ACT_STOP2 2  // ACK of STOP1: sent by client in STATUS or Test PDU

```
The Test Load UDP PDU format is as follows (big-endian AB):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           loadId              |   testAction  | rxStopped     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           lpduSeqNo                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           udpPayload          |           spduSeqErr          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          spduTime_sec                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         spduTime_nsec                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          lpduTime_sec                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         lpduTime_nsec                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .                    MBZ = udpPayload - 28 octets               .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .                                                               .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .                                                               .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .                                                               .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .                                                               .
```

## Status PDU

The receiver SHALL send a Status PDU to the sender during a test at the
configured feedback interval.

The watchdog timer at the Load PDU sender SHALL be reset each time a status
PDU is received.

The Status Header PDU SHALL have the following format and field definitions:

```
// Status feedback header for UDP payload of status PDUs
//

        uint16_t statusId;  // Status ID = 0xFEED
        uint8_t testAction; // Test action
        uint8_t rxStopped;  // Receive traffic stopped indicator (BOOL)
        uint32_t spduSeqNo; // Status PDU sequence number (starts at 1)
        //
        struct sendingRate srStruct; // Sending Rate Structure (28 octets)
        //
        uint32_t subIntSeqNo;      // Sub-interval sequence number
        struct subIntStats sisSav; // Sub-interval Saved Stats Structure  (52 octets)
        //
        uint32_t seqErrLoss; // Loss sum
        uint32_t seqErrOoo;  // Out-of-Order sum
        uint32_t seqErrDup;  // Duplicate sum
        //
        uint32_t clockDeltaMin; // Clock delta minimum (either RTT or 1-way delay)
        uint32_t delayVarMin;   // Delay variation minimum
        uint32_t delayVarMax;   // Delay variation maximum
        uint32_t delayVarSum;   // Delay variation sum
        uint32_t delayVarCnt;   // Delay variation count
        uint32_t rttMinimum;    // Minimum round-trip time sampled
        uint32_t rttSample;     // Last round-trip time sample
        uint8_t delayMinUpd;    // Delay minimum(s) updated observed, communicated in both directions.
        uint8_t reserved2;      // (alignment)
        uint16_t reserved3;     // (alignment)
        //
        uint32_t tiDeltaTime;   // Trial interval delta time
        uint32_t tiRxDatagrams; // Trial interval receive datagrams
        uint32_t tiRxBytes;     // Trial interval receive bytes
        //
        uint32_t spduTime_sec;  // Send time of this status PDU
        uint32_t spduTime_nsec; // Send time of this status PDU

```

The Status feedback UDP payload PDUs format is as follows (big-endian AB):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |          statusId             |   testAction  | rxStopped     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           spduSeqNo                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .               Sending Rate Structure (28 octets)              .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          subIntSeqNo                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .        Sub-interval Saved Stats Structure  (52 octets)        .
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           seqErrLoss                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           seqErrOoo                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           seqErrDup                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         clockDeltaMin                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarMin                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarMax                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarSum                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarCnt                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          rttMinimum                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           rttSample                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |  delayMinUpd  |   reserved2   |           reserved3           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          tiDeltaTime                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         tiRxDatagrams                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           tiRxBytes                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         spduTime_sec                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                         spduTime_nsec                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

```
Note that the Sending Rate Structure (28 octets) is defined in the Test Activation section.

Also note that the Sub-interval Saved Stats Structure (52 octets) 
SHALL be included (and populated as required when the 
server is in the receiver role) as defined below.

The Sub-interval saved statistics structure for received traffic measurements
SHALL be organized and formatted as follows:

```

        uint32_t rxDatagrams; // Received datagrams
        uint32_t rxBytes;     // Received bytes
        uint32_t deltaTime;   // Time delta
        uint32_t seqErrLoss;  // Loss sum
        uint32_t seqErrOoo;   // Out-of-Order sum
        uint32_t seqErrDup;   // Duplicate sum
        uint32_t delayVarMin; // Delay variation minimum
        uint32_t delayVarMax; // Delay variation maximum
        uint32_t delayVarSum; // Delay variation sum
        uint32_t delayVarCnt; // Delay variation count
        uint32_t rttMinimum;  // Minimum round-trip time
        uint32_t rttMaximum;  // Maximum round-trip time
        uint32_t accumTime;   // Accumulated time
----------------------------------------------------------------------------

   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          rxDatagrams                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                            rxBytes                            |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           deltaTime                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           seqErrLoss                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           seqErrOoo                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           seqErrDup                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarMin                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarMax                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarSum                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          delayVarCnt                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          rttMinimum                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          rttMaximum                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           accumTime                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

```
Note that the 52 octet saved statistics structure above has slight differences from
the 40 octets that follow in the status feedback PDU, particularly the
time-related fields.

Upon receiving the Status Feedback PDU or expiration of the feedback interval,
the server SHALL perform calculations 
required by the Load adjustment algorithm and adjust its sending rate, 
or signal that the client do so in its role as as sender. 


## Test Stop

When the test duration timer on the server expires, 
it SHALL set the connection test
action to STOP and also starts marking all outgoing load or status PDUs with a
test action of STOP1. 

```
        uint8_t testAction; // Test action (server sets STOP1)
```
This is simply a non-reversible state for all future messages sent from the server.

When the client receives a load or status PDU with the STOP1 indication,
it SHALL finalize testing, display the test results, and also mark its
connection with a test action of STOP (so that any PDUs received subsequent 
to the STOP1 are ignored). 

With the test action of the client's connection set to STOP, the very next
expiry of a send timer for either a load or status PDU 
SHALL cause the client to
schedule an immediate end time to exit. 

The client SHALL then send all subsequent load or status PDUs with a test
action of STOP2
 
```
        uint8_t testAction; // Test action (client sets STOP2)
```
as confirmation to the server, and a graceful termination of the test can begin.

When the server receives the STOP2
confirmation in the load or status PDU, 
the server SHALL schedule an immediate end time for
the connection which closes the socket and deallocates it.

In a non-graceful test stop, the watchdog/quiet timers at each end-point will 
expire, notifications SHALL be made and the test action of each end-point's 
connection SHALL be set to STOP.

