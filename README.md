# OB-UDPST
Open Broadband-UDP Speed Test (OB-UDPST) is a client/server software utility to
demonstrate one approach of doing IP capacity measurements as described by:

- Broadband Forum TR-471 (07/2020): _Maximum IP-Layer Capacity Metric, Related Metrics, and Measurements_,
BBF TR-471, https://www.broadband-forum.org/technical/download/TR-471.pdf

- ITU-T Recommendation Y.1540 (12/2019): _Internet protocol data communication service - IP packet transfer and availability performance parameters_, 
ITU-T Y.1540, https://www.itu.int/rec/T-REC-Y.1540-201912-I/en 
   
- ITU-T Y-series Supplement 60 (2020): _Interpreting ITU-T Y.1540 maximum IP-layer capacity measurements_, 
ITU-T Y.Sup60, https://www.itu.int/rec/T-REC-Y.Sup60/en

- ETSI Technical Specification 103 222 Part 2 (08/2019): _Reference benchmarking and KPIs for High speed internet_, 
ETSI TS 103 222-2, V.1.2.1, https://www.etsi.org/deliver/etsi_ts/103200_103299/10322202/01.02.01_60/ts_10322202v010201p.pdf

- IETF IPPM Internet Draft (Approved version 12): _Metrics and Methods for One-way IP Capacity_, 
https://datatracker.ietf.org/doc/draft-ietf-ippm-capacity-metric-method/

- ETSI Technical Report  103 702 (2020-10): _Speech and multimedia Transmission Quality (STQ); 
QoS parameters and test scenarios for assessing network capabilities in 5G performance measurements_,
ETSI TR 103 702, V.0.1.0 https://www.etsi.org/deliver/etsi_tr/103700_103799/103702/01.01.01_60/tr_103702v010101p.pdf


## Overview
Utilizing an adaptive transmission rate, via a pre-built table of discreet
sending rates (from 0.5 Mbps to 10 Gbps), UDP datagrams are sent from client
to server or server to client to determine the maximum available IP-layer
capacity between them. The load traffic is only sent in one direction at a
time, and status feedback messages are sent periodically in the opposite
direction.

For upstream tests, the feedback messages from the server instruct the client
on how it should adjust its transmission rate based on the presence or absence
of sufficient sequence errors or delay variation changes observed by the server.
For downstream tests, the feedback messages simply communicate any sequence
errors or delay variation changes observed by the client. In either case,
the server is the host executing the algorithm that determines the rate
adjustments made during the test. This centralized approach allows the rate
adjustment algorithm to be more easily enhanced and customized to accommodate
diverse network services and protocols. To that end, and to encourage
additional testing and experimentation, the software has been structured so
that virtually all of the settings and thresholds used by the algorithm are
currently available as client-side command-line parameters (allowing modification
beyond the current set of default values updated in Release 7.2.1).

By default, both IPv4 and IPv6 tests can be serviced simultaneously when acting
as a server. When acting as a client, testing is performed in one address
family at a time. Also, the default behavior is that jumbo datagram sizes
(datagrams that would result in jumbo frames) are utilized for sending rates
above 1 Gbps. The expectation is that jumbo frames can be accommodated
end-to-end without fragmentation. For sending rates below 1 Gbps, or all
sending rates when the `-j` option is used, the largest UDP payload size is
1222 bytes. This size was chosen because although it is relatively large it
should still avoid any unexpected fragmentation due to intermediate protocols
or tunneling. It also happens to produce a "convenient" number of bits at Layer
3 when doing IPv4 testing. Note that the `-j` option must match between the
client and server or the server will reject the test request.

All delay variation values, based on One-Way Delay (OWDVar) or Round-Trip Time
(RTTVar), indicate the delays measured above the most recent minimum. Although
OWDVar is measured for each received datagram, RTTVar is only sampled and is
measured using each status feedback message. If specifying the use of OWDVar
instead of RTTVar (via the `-o` option), there is no requirement that the
clocks between the client and server be synchronized. The only expectation is
that the time offset between clocks remains nominally constant during the test.

**Usage examples for server mode:**
```
$ udpst
    Service client requests received on any interface
$ udpst -x
    Service client requests in background (as a daemon) on any interface
$ udpst <IP>
    Only service client requests on interface with the specified IP address
$ udpst -a <key>
    Only service requests that utilize a matching authentication key
```
*Note: The server must be reachable on the UDP control port [default **25000**]
and all UDP ephemeral ports (**32768 - 60999** as of the Linux 2.4 kernel,
available via `cat /proc/sys/net/ipv4/ip_local_port_range`).*

**Usage examples for client mode:**
```
$ udpst -u <server>
    Do upstream test from client to server (as hostname or IP address)
$ udpst -d <server>
    Do downstream test to client from server (as hostname or IP address)
$ udpst -s -u <server>
    Do upstream test and only show the test summary at the end
$ udpst -r -d <server>
    Do downstream test and show loss ratio instead of delivered percentage
$ udpst -f json -d <server>
    Do downstream test and use JSON format for test results 
```
*Note: The client can operate behind a firewall (w/NAT) without any port
forwarding or DMZ designation because all connections with the server are
initiated by the client.*

**To show the sending rate table:**
```
$ udpst -S
```
*The table shows dual transmitters for each index (row) because two separate
sets of transmission parameters are required to achieve the desired granularity
of sending rates. Each of the transmitters has its own interval timer and one
also includes an add-on fragment at the end of each burst (again, for
granularity). By default the table is shown for IPv4 with jumbo datagram sizes
enabled above 1 Gbps. Including `-6` (for IPv6 only) and/or `-j` (to disable
all jumbo sizes) shows the table adjusted for those specific scenarios.*

**For a list of all options:**
```
$ udpst -?
```

**Default values:**

Note that default values have been provided for all essential settings and
thresholds, and these values are recommended for use at the beginning of any
test campaign. The set of default values are subject to re-consideration
with further testing, and may be revised (as were two of the values in 
Release 7.2.1: the sequence error threshold was increased to 10 and the 
consecutive feedback interval to 3; this is equivalent to using -q10 and -c3
in earlier releases, and helps to maintain fast ramp-up when non-congestion
related packet losses are present).

For example, the option to send a fixed rate testing after conducting a search
for the Maximum IP-Layer Capacity will provide a stable measurement of loss,
delay variatiation, etc., using -I index:
```
$ udpst -d <server> -I 967
    Do downstream test to client from server (as hostname or IP address) at
    a fixed rate of 967 Mbps
```

There are circumstances when changes to the defaults are warrented, such as
extremely long paths with unknown cross traffic, high levels of competing traffic,
testing over radio linkswith highly variable loss and delay. In these 
circumstances, the following client-side command-line parameters have been useful:
```
$ udpst -?
  ...
        -i count     Display bimodal maxima (specify initial sub-intervals)
 (c)    -R           Ignore Out-of-Order/Duplicate datagrams
 (m)    -t time      Test interval time in seconds [Default 10, Max 3600]
 (c)    -U delvar    Upper delay variation threshold in ms [Default 90]
 (c)    -c thresh    Congestion slow adjustment threshold [Default 3]
 (c)    -h delta     High-speed (row adjustment) delta [Default 10]
 (c)    -q seqerr    Sequence error threshold [Default 10] 
```
See the following publication (which is updated frequently) for more details
on testing in the circumstances described above:

- ITU-T Y-series Supplement 60 (2020): _Interpreting ITU-T Y.1540 maximum IP-layer capacity measurements_, 
ITU-T Y.Sup60, https://www.itu.int/rec/T-REC-Y.Sup60/en


## Building OB-UDPST

To build OB-UDPST a local installation of CMake is required. Please obtain them
for your particular build system either using the locally available packages or
consult with [https://cmake.org] for other download options.

```
$ make
```
*Note: Authentication functionality uses a command-line key along with the
OpenSSL crypto library to create and validate a HMAC-SHA256 signature (which is
used in the setup request to the server). Although the makefile will build even
if the expected directory is not present, disabling the key and library
dependency, the additional files needed to support authentication should be
relatively easy to obtain (e.g., `sudo apt-get install libssl-dev`).*

## Test Processing Walkthrough (all messaging and PDUs use UDP)
On the server, the software is run in server mode in either the foreground or
background (as a daemon) where it awaits Setup requests on its UDP control
port.

The client, which always runs in the foreground, requires a direction parameter
as well as the hostname or IP address of the server. The client will create a
connection and send a Setup request to the server's control port. It will also
start a test initiation timer so that if the initiation process fails to
complete, the client will display an error message to the user and exit.

**Setup Request**

When the server receives the Setup request it will validate the request by
checking the protocol version, the jumbo datagram support indicator, and the
authentication data if utilized. If the Setup request must be rejected, a Setup
response will be sent back to the client with a corresponding command response
value indicating the reason for the rejection. If the Setup request is
accepted, a new test connection is allocated and initialized for the client.
This new connection is associated with a new UDP socket allocated from the UDP
ephemeral port range. A timer is then set for the new connection as a watchdog
(in case the client goes quiet) and a Setup response is sent back to the
client. The Setup response includes the new port number associated with the new
test connection. Subsequently, if a Test Activation request is not received
from the client on this new port number, the watchdog will close the socket and
deallocate the connection.

**Setup Response**

When the client receives the Setup response from the server it first checks the
command response value. If it indicates an error it will display a message to
the user and exit. If it indicates success it will build a Test Activation
request with all the test parameters it desires such as the direction, the
duration, etc. It will then send the Test Activation request to the UDP port
number the server communicated in the Setup response.

**Test Activation request**

After the server receives the Test Activation request on the new connection, it
can choose to accept, ignore or modify any of the test parameters. When the
Test Activation response is sent back, it will include all the test parameters
again to make the client aware of any changes. If an upstream test is being
requested, the transmission parameters from the first row of the sending rate
table are also included. Note that the server additionally has the option of
completely rejecting the request and sending back an appropriate command
response value. If activation continues, the new connection is prepared for an
upstream OR downstream test with either a single timer to send status PDUs at
the specified interval OR dual timers to send load PDUs based on the first row
of the sending rate table. The server then sends a Test Activation response
back to the client, the watchdog timer is updated and a test duration timer is
set to eventually stop the test. The new connection is now ready for testing.

**Test Activation response**

When the Test Activation response is received back at the client it first
checks the command response value. If it indicates an error it will display a
message to the user and exit. If it indicates success it will update any test
parameters modified by the server. It will then prepare its connection for an
upstream OR downstream test with either dual timers set to send load PDUs
(based on the starting transmission parameters sent by the server) OR a single
timer to send status PDUs at the specified interval. The test initiation timer
is then stopped and a watchdog timer is started (in case the server goes
quiet). The connection is now ready for testing.

**Testing**

Testing proceeds with one end point sending load PDUs, based on transmission
parameters from the sending rate table, and the other end point sending status
messages to communicate the traffic conditions at the receiver. Each time a PDU
is received the watchdog timer is reset. When the server is sending load PDUs
it is using the transmission parameters directly from the sending rate table
via the index that is currently selected (which was based on the feedback in
its received status messages). However, when the client is sending load PDUs it
is not referencing a sending rate table but is instead using the discreet
transmission parameters that were communicated by the server in its periodic
status messages. This approach allows the server to always control the
individual sending rates as well as the algorithm used to decide when and how
to adjust them.

**Test Stop**

When the test duration timer on the server expires it sets the connection test
action to STOP and also starts marking all outgoing load or status PDUs with a
test action of STOP. When received by the client, this is the indication that
it should finalize testing, display the test results, and also mark its
connection with a test action of STOP (so that any subsequently received PDUs
are ignored). With the test action of the connection set to STOP, the very next
expiry of a send timer for either a load or status PDU will cause the client to
schedule an immediate end time to exit. It then sends that PDU with a test
action of STOP as confirmation to the server. When the server receives this
confirmation in the load or status PDU, it schedules an immediate end time for
the connection which closes the socket and deallocates it.

**More Info**

For much more detail on the test protocol, see the ./protocol.md file.

## Linux Socket Buffer Optimization
For very high speeds (typically above 1 Gbps), the socket buffer maximums of
the Linux kernel can be increased to reduce possible datagram loss. As an
example, the following could be added to the /etc/sysctl.conf file:
```
net.core.rmem_max=16777216
net.core.wmem_max=16777216
```
To activate the new values without a reboot:
```
$ sudo sysctl -w net.core.rmem_max=16777216
$ sudo sysctl -w net.core.wmem_max=16777216
```
*The default software settings will automatically take advantage of the
increased socket buffering available. However, the command-line option `-b` can
be used if even higher buffer levels should be explicitly requested for each
socket.*

## Considerations for Older Hardware and Low-End Devices
Systems that are unable to support the required clock resolution will cause an
error message at runtime (e.g., “ERROR: Clock resolution (xxxxxxx ns) out of
range”) and udpst will exit. This is because without the needed interval timer
frequency, sending rates would be skewed and the rate-adaption algorithm would
not function properly.

As a workaround for older devices, a compile-time option (DISABLE_INT_TIMER) is
available that does not rely on an underlying system interval timer. However,
the trade-off for this mode of operation is that it results in high CPU
utilization, but clients running on older or low-capability hosts may be able
to execute tests where they otherwise would not.
```
$ cmake -D DISABLE_INT_TIMER=ON .
```
*Note: Because of the increased CPU utilization, this option is not recommended
for standard server operation.*

## JSON Output
The following code block shows an example of the (formatted) JSON output when
using the `-f json` option for a downstream test on a 1 Gbps PON connection
(the actual/raw JSON output contains no formatting for readability).

*Note: Any warning or error messages will be passed to stdout prior to
the structured JSON output.*
```
{
	"version": "7.2.0",
	"protocol": 8,
	"config": {
		"type": "Downstream",
		"duration": 10,
		"delvat_lower": 30,
		"delvar_upper": 90,
		"delay_usage": "RTT",
		"interval": 50,
		"ignore_ooodup": false,
		"sending_rate": "<Auto>",
		"congest_th": 2,
		"hs_delta": 10,
		"seqerr_th": 0,
		"iptos_byte": 0
	},
	"results": {
		"summary": {
			"avgDeliveredPct": 99.88,
			"seqErrLoss": 1147,
			"seqErrOoo": 0,
			"seqErrDup": 0,
			"owdVarMin": 0,
			"owdVarAvg": 1,
			"owdVarMax": 7,
			"rttVarMin": 0,
			"rttVarMax": 10,
			"avgL3Mbps": 729.73
		},
		"minimum": {
			"owd": 5,
			"rtt": 7
		},
		"maximum": {
			"L3Mbps": 966.91,
			"L2Mbps": 980.91,
			"L1Mbps": 996.46,
			"L0Mbps": 999.57
		}
	}
}
```
