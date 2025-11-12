# OB-UDPST
Open Broadband-UDP Speed Test (OB-UDPST) is a client/server software utility to
demonstrate one approach of doing IP capacity measurements as described by:

- Broadband Forum TR-471 Issue 4 (2024/09): _Maximum IP-Layer Capacity Metric,
Related Metrics, and Measurements_, BBF TR-471,
https://www.broadband-forum.org/technical/download/TR-471.pdf

- IETF RFC 9097: _Metrics and Methods for One-way IP Capacity_, 
https://www.rfc-editor.org/rfc/rfc9097.html

- IETF RFC xxx: _UDP Speed Test Protocol for One-way IP Capacity Metric
Measurement_, https://www.rfc-editor.org/rfc/rfcXXX.html

- ITU-T Recommendation Y.1540 (revised 03/2023): _Internet protocol data
communication service - IP packet transfer and availability performance
parameters_, ITU-T Y.1540, https://www.itu.int/rec/T-REC-Y.1540-201912-I/en 
   
- ITU-T Y-series Supplement 60 (2022): _Interpreting ITU-T Y.1540 maximum
IP-layer capacity measurements_, ITU-T Y.Sup60,
https://www.itu.int/rec/T-REC-Y.Sup60/en

- ETSI Technical Specification 103 222 Part 2 (2019-08): _Reference
benchmarking and KPIs for High speed internet_, ETSI TS 103 222-2, V1.2.1,
https://www.etsi.org/deliver/etsi_ts/103200_103299/10322202/01.02.01_60/ts_10322202v010201p.pdf

- ETSI Technical Report 103 702 (2020-11): _Speech and multimedia Transmission
Quality (STQ); QoS parameters and test scenarios for assessing network
capabilities in 5G performance measurements_, ETSI TR 103 702, V1.1.1
https://www.etsi.org/deliver/etsi_tr/103700_103799/103702/01.01.01_60/tr_103702v010101p.pdf

**IMPORTANT: As of release 9.0.0, the default control port has changed from
25000 to 24601. For backward compatibility, 8.2.0 clients will either need to
use the new control port via `-p 24601` or the server can be run with the
legacy control port (via `-p 25000`).**

## Table of Contents

- [Overview](#overview)
- [Fixed-Rate and Low-Rate Testing](#fixed-rate-and-low-rate-testing)
- [Multiple Connections and Distributed Servers](#multiple-connections-and-distributed-servers)
- [Building OB-UDPST](#building-ob-udpst)
- [Test Processing Walkthrough](#test-processing-walkthrough)
- [JSON Output (client test results)](#json-output-client-test-results)
- [Local Interface Traffic Rate](#local-interface-traffic-rate)
- [Server Bandwidth Management](#server-bandwidth-management)
- [Increasing the Starting Sending Rate](#increasing-the-starting-sending-rate)
- [Linux Socket Buffer Optimization](#linux-socket-buffer-optimization)
- [Server Optimization](#server-optimization)
- [Considerations for Older or Low-End Devices](#considerations-for-older-or-low-end-devices)
- [Output (Export) of Received Load Traffic Metadata](#output-export-of-received-load-traffic-metadata)
- [Multi-Key Authentication](#multi-key-authentication)
- [Optional Header Checksum and Integrity Checks](#optional-header-checksum-and-integrity-checks)
- [Local Backpressure](#local-backpressure)
- [Server Performance Statistics](#server-performance-statistics)

## Overview
Utilizing an adaptive transmission rate, via a pre-built table of discreet
sending rates (starting at 0.11 Mbps), UDP datagrams are sent from client
to server(s) or server(s) to client to determine the maximum available IP-layer
capacity between them. The load traffic is only sent in one direction at a
time and status feedback messages are sent periodically in the opposite
direction.

For upstream tests, the feedback messages from the server(s) instruct the client
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
currently available as client-side command-line parameters (allowing
modification beyond the current set of default values).

By default, both IPv4 and IPv6 tests can be serviced simultaneously when acting
as a server. When acting as a client, testing is performed in one address
family at a time. Also, the default behavior is that jumbo datagram sizes
(datagrams that would result in jumbo frames) are utilized for sending rates
above 1 Gbps. The expectation is that jumbo frames can be accommodated
end-to-end without fragmentation. For sending rates below 1 Gbps, or all
sending rates when the `-j` option is used, the default UDP payload size is
1222 bytes. This size was chosen because although it is relatively large it
should still avoid any unexpected fragmentation due to intermediate protocols
or tunneling. Alternatively, the `-T` option is available to allow slightly
larger default datagrams that would create full packets when a traditional
1500 byte MTU is available end-to-end. These larger sizes are optional because
several consumer-grade routers have been shown to handle packet fragmentation,
from a performance perspective, very poorly. Note that both the `-j` and `-T`
options must match between the client and server or the server will reject the
test request.

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
$ udpst -p <port> <Local_IP>
    Service client requests using a non-default UDP control port and only when
    received on the interface with the specified IP address
$ udpst -a <key>
$ udpst -K <file>
    Only service requests that utilize a matching authentication key from the
    command line or from a key file
$ udpst -G <file>
    Write periodic performance statistics (as JSON) to the specified file
```
*Note: The server must be reachable on the UDP control port [default
**24601**]. With release 9.0.0 the server now sends an immediate Null request
control message (after the Setup response) to open access through its firewall
to the new UDP ephemeral port created for the test. However, if the firewall is
using port address translation (PAT), and the Null request is ineffective,
access to all UDP ephemeral ports will need to be preconfigured on the firewall
(**32768 - 60999** as of the Linux 2.4 kernel, available via
`cat /proc/sys/net/ipv4/ip_local_port_range`).*

**Usage examples for client mode:**
```
$ udpst -u <server>
    Do upstream test from client to server using 1 connection (UDP flow)
$ udpst -d <server> <server> <server>
    Do downstream test from 3 server instances using 1 connection each
$ udpst -C 4 -p <port> -d <server>
    Do downstream test from 1 server instance using 4 connections and a
    non-default UDP control port
$ udpst -C 3 -u <server> <server> <server> <server>
    Do upstream test to 4 server instances using 4 connections, but only
    requiring a minimum of 3 (one server could be unreachable or unavailable)
$ udpst -C 6 -d <server>:<port> <server>:<port> <server>:<port>
    Do downstream test from 3 server instances, each using a non-default UDP
    control port, with 6 total connections (setup round robin, 2 each)
$ udpst -C 6-8 -u <server> <server> <server> <server>
    Do upstream test to 4 server instances using 8 connections, but only
    requiring a minimum of 6 (one server could be unreachable or unavailable)
$ udpst -r -d <server>
    Do downstream test and show loss ratio instead of delivered percentage
$ udpst -f json -d <server> >udpst.json
    Do downstream test and redirect JSON output to a file
```
*Note: The client can operate behind a firewall (w/NAT) without any port
forwarding or DMZ designation because all connections with the server(s) are
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
enabled above 1 Gbps. Including `-6` (for IPv6 only), `-j` (to disable
all jumbo sizes), or `-T` (for traditional 1500 byte MTU sizes) shows the table
adjusted for those specific scenarios.*

**For a list of all options:**
```
$ udpst -?
```

**Default values:**

Note that default values have been provided for all essential settings and
thresholds, and these values are recommended for use at the beginning of any
test campaign. The set of default values are subject to re-consideration
with further testing, and may be revised in the future.

There are circumstances when changes to the defaults are warranted, such as
extremely long paths with unknown cross traffic, high levels of competing
traffic, testing over radio links with highly variable loss and delay, and test 
paths that exhibit bi-modal rate behavior in repeated tests.

An option in Release 7.5.0 allows the client to request the algorithm used for
load adjustment when conducting a search for the Maximum IP-Layer Capacity.
The Type C algorithm (a.k.a. Multiply and Retry) will provide a fast rate
increase until congestion, reaching 1 Gbps in ~1 second. The "fast" ramp-up
will be re-tried when conditions warrant, to ensure that the Maximum IP-Layer
Capacity has been reached. This option is activated using `-A C` (with the
more linear Type B algorithm remaining the default).

One change to the default settings was included in Release 7.5.1. All Load
Adjustment search algorithms will now Ignore Reordering (and duplication) as
a component of sequence errors: only packet loss will increase the sequence
error count. The optional use of the `-R` option will now revert the behavior
from "Ignore" back to "Include". This change was justified by recognizing that
both out-of-order and duplicate datagrams are a legitimate part of IP-Layer
Capacity.

See the following publication for more details on testing in the circumstances
described above:
- ITU-T Y-series Supplement 60 (06/22): _Interpreting ITU-T Y.1540 maximum
IP-layer capacity measurements_, ITU-T Y.Sup60,
https://www.itu.int/rec/T-REC-Y.Sup60/en

## Fixed-Rate and Low-Rate Testing
Although the primary function of the software is to "find" the maximum IP-layer
capacity between two endpoints, fixed-rate and low-rate testing can also be
quite valuable. This is because "finding" the maximum capacity involves ramping
up traffic to induce congestion (i.e., loss and/or delay). However, there are
scenarios where simply measuring the traffic conditions is desirable.

Fixed-rate testing via the `-I [@]index` option (used without the '@' prefix)
allows for the sending of load traffic at a static rate, below the actual
maximum. This can be helpful as part of the test and turn-up of new service
or the verification of service after repair. For example, a 1 Gbps service
should theoretically provide an IP capacity of ~972.76 Mbps (1500 MTU, 1 VLAN
Tag). Testing with a sending rate of ~90% of that maximum (via `-I 875`), would
allow confirmation that the connectivity is stable, error-free, and with low
delay variation.

Similarly, for ongoing monitoring, low-rate testing can be used.
Due to the significant amount of traffic typically required to "find" a
maximum, it can be impractical to run frequent capacity tests in
large-scale networks. Therefore, and as an alternative to infrequent testing
alone, low-rate tests (via `-I 0`) can be utilized very regularly between
maximum capacity tests. These tests, with a minimum sending rate, send a
random size datagram every 50 ms. With the size still constrained by the `-j`
and `-T` options, this equates to a traffic rate of only ~0.11 Mbps.
The benefit of this testing is that it can still detect various network
impairments (loss, delay, instability, etc.) while utilizing all of the
existing infrastructure and support systems already in place for maximum
capacity measurements.

## Multiple Connections and Distributed Servers
As of Release 8.0.0, the client can now test using multiple connections (i.e.,
UDP flows) to one or more server instances. Each server instance can itself
service up to 256 independent client connections. When the client wants to
establish more than one connection per server instance OR the client wants to
specify a minimum (and optional maximum) number of connections, the
`-C cnt[-max]` option is used.

For better utilization of hosts, multiple server instances can reside
on one physical machine and service test requests across one or more network
interfaces using unique IP addresses (including IP aliases) or different UDP
control ports. For redundancy and load balancing (within or between locations),
multiple server instances can be utilized across completely separate physical
servers, virtual machines, or containers. And because OB-UDPST is based on UDP,
it is significantly less sensitive to delay than TCP-based measurement tools
(allowing much greater flexibility with geographic server placement).

Some of the benefits of testing with multiple connections to multiple server
instances include:
- More efficient use of server resources (CPU cores, network interfaces, etc.)
- Better utilization of any link aggregation within the network
- Increased use of ECMP (Equal Cost Multi-Path) in the data center or WAN
- The ability to ramp-up test traffic at a compounded rate
- Improved capacity to deal with competing traffic, particularly with
Active Queue Management (AQM) schemes that provide flow queuing/isolation
- The option to utilize smaller servers or lower-speed interfaces instead of a
few large machines with high-speed connectivity
- Support for maintenance and downtime on individual servers without test
interruption
- Opportunities for various levels of redundancy by way of diverse server-side
resources (i.e., NICs, physical machines, LAN networks, WAN links, entire data
centers,...)

**Important Considerations**

There will be different system and network resource impacts for the client
and server given the contrasting downstream vs. upstream (N-to-1 vs. 1-to-N)
traffic dynamic. Very often, more connections will not equal more capacity
(the law of diminishing returns is ever-present). And while low-speed testing
up to 1 Gbps can typically utilize more connections (10+) effectively,
high-speed testing above that threshold generally performs best with fewer
connections (2-4).

With high-speed testing the number of connections can significantly impact
achievable rates due to the default udpst behavior of using jumbo size
datagrams for sending rates above 1 Gbps (i.e., when `-j` is not used). With
fewer connections, where each one will need to drive traffic above 1 Gbps,
they will be able to take advantage of the much higher network efficiency and
reduced I/O rate of larger datagrams. Of course, this assumes that jumbo
frames are supported by the network and IP fragmentation can be avoided.

In another situation observed on a Raspberry Pi 4 (but called out here for
its possible relevance to other devices), the use of multiple flows caused an
undesirable outbound congestion condition on the local interface. The result
was unreliable and inconsistent measurements because some flows would either
monopolize all the bandwidth or be completely starved of it. The cause of the
issue was its multi-queue network interface coupled with its default behavior
of assigning flows to those queues using a 4-tuple hash. Depending on how the
flows were hashed, some were able to completely starve the others. And this
behavior was not unique to udpst. The same multi-flow starvation was observed
with iPerf, for both UDP and TCP.

One simple way to avoid this issue on the Pi4 is to do testing with a single
connection. Alternatively, XPS (Transmit Packet Steering) can be utilized to
make the device behave the same as servers normally do with multi-queue NICs.
That is, having each CPU map onto one queue. Additional details, as well as
the XPS configuration used on the Pi4, are available below under
"Considerations for Older or Low-End Devices".

## Building OB-UDPST
To build OB-UDPST a local installation of CMake is required. Please obtain it
for your particular build system either using the locally available packages or
consult with [https://cmake.org] for other download options.

```
$ cmake .
$ make
```
*Note: Authentication functionality utilizes a key along with the
OpenSSL crypto library to create and validate a HMAC-SHA256 signature (which is
used in control messages between client and server). Although the makefile will
build even if the expected OpenSSL directory is not present (disabling the key
and library dependency), the additional files needed to support authentication
should be relatively easy to obtain (e.g., `sudo apt-get install libssl-dev` or
`sudo yum install openssl-devel`).*

## Test Processing Walkthrough
**All messaging and PDUs use UDP**

On the server, the software is run in server mode in either the foreground or
background (as a daemon) where it awaits Setup requests on its UDP control
port.

The client, which always runs in the foreground, requires a direction parameter
as well as the hostname or IP address of one or more servers. The client will
create a connection and send a Setup request to each server's control port. It
will also start a test initiation timer for each so that if the initiation
process fails to complete, and the required minimum connection count is not
available, the client will display an error message to the user and exit.

**Setup Request**

When the server receives the Setup request it will validate the request by
checking the authentication data (if utilized) as well as the protocol version
and jumbo/traditional datagram support indicators. If the Setup request must
be rejected, a Setup response will be sent back to the client with a
corresponding command response value indicating the reason for the rejection.
If the Setup request is accepted, a new test connection is allocated and
initialized for the client. This new connection is associated with a new UDP
socket allocated from the UDP ephemeral port range. A timer is then set for the
new connection as a watchdog (in case the client goes quiet) and a Setup
response is sent back to the client. The Setup response includes the new port
number associated with the new test connection. As of version 9.0.0, the server
will immediately follow the Setup response with a Null request back to the
client from the new port. This opens the server's firewall, if one is present,
for the expected Test Activation request and subsequent data traffic. If a Test
Activation request is not received from the client on this new port number, the
watchdog will close the socket and deallocate the connection.

*Note: If the server's firewall is using a complex ruleset and/or PAT, and the
Null request is ineffective in opening the new port for inbound traffic from
the client, access to all UDP ephemeral ports will need to be preconfigured on
it.*

**Setup Response**

When the client receives the Setup response from the server it first checks the
command response value. If it indicates an error it will display a message to
the user (and exit if the required connection count falls below the minimum).
If it indicates success it will build a Test Activation request with all the
test parameters it desires such as the direction, the duration, etc. It will
then send the Test Activation request to the UDP port number the server
communicated in the Setup response. And if the client receives the Null request
sent by the server, which is not guaranteed given its own NAT/PAT processing,
it can simply be discarded.

**Test Activation request**

After the server receives the Test Activation request on the new connection, it
can choose to accept, ignore, or modify any of the test parameters. When the
Test Activation response is sent back, it will include all the test parameters
again to make the client aware of any changes. If an upstream test is being
requested, the transmission parameters from the appropriate row of the sending
rate table are also included. Note that the server additionally has the option
of completely rejecting the request and sending back an appropriate command
response value. If activation continues, the new connection is prepared for an
upstream OR downstream test with either a single timer to send status PDUs at
the specified interval OR dual timers to send load PDUs based on the specific
row of the sending rate table. The server then sends a Test Activation response
back to the client, the watchdog timer is updated and a test duration timer is
set to eventually stop the test. The new connection is now ready for testing.

**Test Activation response**

When the Test Activation response is received back at the client it first
checks the command response value. If it indicates an error it will display a
message to the user (and exit if the required connection count falls below the
minimum). If it indicates success it will update any test parameters modified
by the server. It will then prepare its connection for an upstream OR
downstream test with either dual timers set to send load PDUs (based on the
transmission parameters sent by the server) OR a single timer to send status
PDUs at the specified interval. The test initiation timer is then stopped and
a watchdog timer is started (in case the server goes quiet). The connection is
now ready for testing.

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

## JSON Output (client test results)
For examples of the JSON output fields see the included sample files named
"udpst-*.json". Available JSON output options include `-f json` (unformatted),
`-f jsonb` (brief & unformatted), and `-f jsonf` (formatted). To significantly
reduce the size of the JSON output, option `-s` (omit sub-interval results)
can be combined with `-f jsonb` (omit static input fields). Also, the
JSON key names are closely aligned with the objects and parameters of the
TR-181 data model available at https://device-data-model.broadband-forum.org
(see "IPLayerCapacity..." under "Device.IP.Diagnostics").

Included in the output is a numeric ErrorStatus field (which corresponds with
the software exit status) as well as a text ErrorMessage field. As of version
8.1.0, an additional ErrorMessage2 text field was also added to show any
penultimate warning or error message. This was done to better convey any
cause-and-effect relationship between events. If a test completes normally
without incident the ErrorStatus will be 0 (zero) and the ErrorMessage fields
will be empty. If a test completes, but encounters a warning or soft error,
the ErrorStatus values for warnings will begin at 1 (one). If a test fails to
complete, the ErrorStatus values will begin at 50 (up to maximum of 255). See
`udpst.h` for specific ErrorStatus values and ranges.

*Note: When stdout is not redirected to a file, JSON may appear clipped due to
non-blocking console writes.*

## Local Interface Traffic Rate
Where applicable, it is possible to also output the local interface traffic
rate (in Mbps) via the `-E intf` option. This can be informative when trying
to account for external traffic that may be consuming a non-trivial amount of
the interface bandwidth and competing with the measurement traffic. The rate is
obtained by querying the specific interface byte counters that correspond with
the direction of the test (i.e., `tx_bytes` for upstream tests and `rx_bytes`
for downstream tests). These values are obtained from the sysfs path
`/sys/class/net/<intf>/statistics`. An additional associated option `-M` is
also available to override normal behavior and use the interface rate instead
of the measurement traffic to determine a maximum.

When the `-E intf` option is utilized, the console output will show the
interface name in square brackets in the header info and the Ethernet rate of
the interface in square brackets after the L3/IP measured rate. When JSON
output is also enabled, the interface name appears in "Interface" and the
interface rate is in "InterfaceEthMbps". When this option is not utilized,
these JSON fields will contain an empty string and zero respectively.

## Server Bandwidth Management
The `-B mbps` option can be used on a server to designate a maximum available
bandwidth. Often, this would simply specify the speed of the interface
servicing tests and is managed separately for upstream and downstream (i.e.,
`-B 1000` indicates that 1 Gbps is available in each direction). One scenario
to achieve better server utilization is to run multiple server instances on a
machine with them bound to one or more different physical interfaces. In this
scenario, the bandwidth option for each is set to handle some portion of the
total aggregate available (with clients always utilizing several at once). For
example, 10 instances could be run with `-B 1000` for a 10G or `-B 10000` for
a 100G. This can be accomplished by either binding each to a different IP alias
or configuring them to use different UDP control ports (e.g., `-p 24601`,
`-p 24602`, `-p 24603`,...). And although single threaded, each running
instance supports multiple simultaneous overlapping tests.

When configured on the server, clients will also need to utilize the `-B mbps`
option in their test request to indicate the maximum bandwidth they may require
from the server for accurate testing. For example, a client connected via a 300
Mbps Internet service would specify `-B 300`. If the server's available
bandwidth is unable to accommodate what the client requires, due to tests
already in progress, the test request is rejected and the client produces an
error indicating this as the cause. The client can then retry the test a little
later (when server bandwidth may be available) or immediately try either an
alternate server instance (per the scenarios described above) or a different
server altogether. When the client is testing with multiple connections, the
provided bandwidth option is evenly divided across the attempted connections.
In this case, if only some connections are rejected due to insufficient server
capacity, and the required minimum connection count is available, the testing
will proceed normally.

*Note: This option does not alter the test methodology or the rate adjustment
algorithm in any way. It only provides test admission control to better manage
the server's network bandwidth.*

**Rate Limiting (Optional)**

To assist with server scale testing, an optional mode is available where each
test is limited to the bandwidth requested via the bandwidth management option
`-B mbps`. This allows tests to ramp-up normally, but limits the maximum
sending rate index possible with the rate adjustment algorithm. This simulates
a test limited by a client's "provisioned" speed, even though it may be
connected to the server(s) at a much higher speed. And because the rate 
adjustment algorithm only executes on the server, this functionality only needs
to be enabled on the server to take effect (where a notification message is
generated for each test that is rate limited). To enable this mode of operation
on the server(s): 
```
$ cmake -D RATE_LIMITING=ON .
```

*Note: Sending rates above the high-speed threshold (1 Gbps) are much less
granular than sending rates below it. Also, aggregate rates may end up slightly
below requested rates due to the traffic pattern and enforced limit of the
maximum sending rate a connection is limited to.*

## Increasing the Starting Sending Rate
While the `-I index` option designates a fixed sending rate, it is also possible
to set the starting rate with load adjustment enabled. Option `-I @index` allows
selection of a higher initial sending rate starting at the specified index in
the sending rate table (with the default equivalent to `-I @0` and shown as
`<Auto>`). Note that with or without the `@` character prefix, this option
relies on an index from the sending rate table (see `-S` for index values).

For maximum capacities up to 1 Gbps, the `-h delta` option has been available
for some time to allow customization of the ramp-up speed in the Type B
Algorithm. In some cases, and especially with few connections and maximum
capacities above 1 Gbps (where `-h delta` no longer has an impact), it can make
sense to start at an initial sending rate above index 0 (zero). For example, if
testing a 10 Gbps service with only one connection, specifying `-I @1000` would
start with a sending rate of 1 Gbps.

One important consideration with the `-I @index` option is that setting too high
a value can be counter-productive to finding an accurate maximum. This is
because when some devices or communication channels are suddenly overwhelmed by
the appearance of very-high sustained traffic, it can result in early congestion
and data loss that make it prematurely appear as if a maximum capacity has been
reached. As such, it is recommended to use starting rates of only 10-20% of the
expected maximum to avoid an early overload condition and false maximum.

## Linux Socket Buffer Optimization
For high speed testing (typically above 1 Gbps), the socket buffer maximums of
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
increased send and receive socket buffering available (shown in verbose mode).
However, the command-line option `-b buffer` can be used if even higher buffer
levels (granted at 2x the designated value) should be explicitly requested for
each socket.*

## Server Optimization
**Important considerations when jumbo frames are unavailable...**

By default, the sending rate table utilizes jumbo size datagrams when testing
above 1 Gbps. As expected, maximum performance is obtained when the network
also supports a jumbo MTU size (>= 9000 bytes). However, some environments are
restricted to a traditional MTU of 1500 bytes and would be required to fragment
the jumbo datagrams into multiple IP packets.

In these situations, the recommendation is to utilize the `-j` option on both
the client and server to restrict all datagrams to non-jumbo sizes. However,
because of the resulting higher socket I/O rate at high speeds, this may limit
the maximum rate that can be achieved. If jumbo size datagrams are still
desired and udpst was compiled with the GSO (Generic Segmentation Offload)
optimization, the default with reasonably recent Linux kernels, it will need to
be recompiled without it as GSO is incompatible with IP fragmentation. This can
be accomplished via the following:
```
$ cmake -D HAVE_GSO=OFF .
```

**NUMA Node Selection**

An important performance consideration is to instantiate the udpst processes
in the same Non-Uniform Memory Access (NUMA) node as the network interface.
This placement will limit cross-NUMA memory access on systems with more than
one NUMA node. Testing has shown this commonplace server optimization can
significantly increase sending rates while also reducing CPU utilization. The
goal is to set the CPU affinity of the udpst process to the same NUMA node as
the one handling the network interface used for testing. Follow the steps below
to achieve this:

First, obtain the NUMA node count (to verify applicability if >1) as well as
the CPU listing for each node.
```
$ lscpu | grep NUMA
NUMA node(s):                    2
NUMA node0 CPU(s):               0-13,28-41
NUMA node1 CPU(s):               14-27,42-55
```
Next, find the NUMA node that corresponds to the test interface (in this case
ens1f0 is associated with node 0).
```
$ cat /sys/class/net/ens1f0/device/numa_node
0
```
Finally, start the server instances with a CPU affinity that matches the NUMA
node of the test interface (node0 = 0-13,28-41).
```
By IP address:
$ taskset -c 0-13,28-41 udpst -x <Local_IP1>
$ taskset -c 0-13,28-41 udpst -x <Local_IP2>
...
$ taskset -c 0-13,28-41 udpst -x <Local_IPN>

By UDP control port:
$ taskset -c 0-13,28-41 udpst -x -p <Port1> <Local_IP>
$ taskset -c 0-13,28-41 udpst -x -p <Port2> <Local_IP>
...
$ taskset -c 0-13,28-41 udpst -x -p <PortN> <Local_IP>
```

In addition to the udpst server instances, multi-queue NICs will also heavily
utilize various CPUs on the same NUMA node for interrupt handling and I/O. This
contention for CPU resources should be considered when determining how many
udpst instances to run. In some circumstances it might be beneficial to
differentiate the CPU cores based on usage. One way to do this involves
banning some CPUs from interrupt handling in the *irqbalance* environment file
via the "IRQBALANCE_BANNED_CPULIST" variable (older versions use a
"IRQBALANCE_BANNED_CPUS" mask instead). Those CPUs that would then no longer
be burdened with also processing interrupts could be the ones specified in the
`taskset` CPU list when running udpst instances.

*The example discussed here only addresses a single network interface on one
NUMA node. Server utilization in this case could be further maximized by also
setting up a second network interface on the other NUMA node and repeating the
appropriate configuration. Ideally, always growing the server by two interfaces
at a time (one on each node).*

**Fragment Reassembly Memory**

If the `-j` option is not used and IP fragmentation of jumbo size datagrams
must be expected as a normal part of testing (where udpst must also be built
without the GSO optimization), it is important to make sure that adequate
memory is available for fragment reassembly. When not available, the "packet
reassemblies failed" counter under `netstat -s` and/or `netstat -s -6` (for
IPv6) will show the failures.

To increase the memory available to reassemble fragments, as well as limit the
time a fragment should be kept awaiting reassembly, the following could be
added to the /etc/sysctl.conf file:
```
net.ipv4.ipfrag_high_thresh=104857600
net.ipv4.ipfrag_time=3
net.ipv6.ip6frag_high_thresh=104857600
net.ipv6.ip6frag_time=3
```
To activate the new values without a reboot:
```
$ sudo sysctl -w net.ipv4.ipfrag_high_thresh=104857600
$ sudo sysctl -w net.ipv4.ipfrag_time=3
$ sudo sysctl -w net.ipv6.ip6frag_high_thresh=104857600
$ sudo sysctl -w net.ipv6.ip6frag_time=3
```
*The suggested thresholds shown above are 25x the typical default. However, if
the "packet reassemblies failed" counter continues to increase during testing,
the threshold values should be raised accordingly.*

**Transmit and Receive Rings**

For higher speed NICs (10G and above), increasing the transmit and receive ring
size is often required to maximize system throughput. To see the current value
and supported maximums, do the following:
```
$ ethtool -g <intf>
```
To increase the ring sizes to their available maximums, do the following:
```
$ sudo ethtool -G <intf> rx <max> tx <max>
```
*The settings will need to be added to the system configuration for the new
values to persist across reboots. The details of formally incorporating
*ethtool* settings into the boot process are distribution specific. However, an
"informal" approach could simply make use of the /etc/rc.local file.*

## Considerations for Older or Low-End Devices
There are two general categories of devices in this area, 1) those that operate
normally but lack the horsepower needed to reach a specific sending rate and
2) those that are unable to function properly because they do not support the
required clock resolution. In this case, an error message is produced
(“ERROR: Clock resolution (xxxxxxx ns) out of range”) and the software exits
because without the expected interval timer, sending rates would be skewed and
the rate-adaption algorithm would not function properly.

Devices in the first category may be helped by the `-T` option when a
traditional 1500 byte MTU is available end-to-end between client and server.
This option will increase the default datagram payload size so that full 1500
byte packets are generated. This reduces both the socket I/O and network packet
rates (see `-S` vs. `-ST` output). Another important option in these cases,
when jumbo frames are not supported by the network, is `-j`. This will disable
all jumbo datagram sizes and prevent any possible IP fragmentation. This can
happen with a very underpowered device when the server, attempting to drive the
client higher and higher, ends up at sending rates above 1 Gbps.

One specific device in this category worth mentioning is a Raspberry Pi 4
running Raspberry Pi OS (previously called Raspbian). Testing has shown that to
reach a 1 Gbps sending rate, it was necessary to set the CPU affinity of udpst
to avoid CPU 0 (the CPU handling network interrupts when *irqbalance* is not
used). Additionally, and especially when using a 32-bit Raspbian, both the `-T`
and `-j` options were also needed. And these options are recommended for any
device in this general category. The command to utilize these recommendations
is:
```
$ taskset -c 1-3 udpst -u -T -j <server>
```

Before moving on, a final consideration for the Raspberry Pi 4 (because this
may apply to other devices) has to do with multi-connection testing. Whenever
multiple flows aggressively congest the outbound multi-queue network interface,
the default behavior of 4-tuple hashing for queue assignment will allow some
flows to monopolize all the available bandwidth while others will be starved of
it. This can cause reliability and consistency issues with measurements as some
flows experience timeouts, or shutdown completely. Other than limiting tests to
only a single connection, XPS (Transmit Packet Steering) can be utilized to
align the Pi4 behavior with a typical server using multi-queue NICs (i.e., each
CPU mapped onto one queue). To achieve this for eth0, the following can be
added to the /etc/rc.local file:
```
echo 1 > /sys/class/net/eth0/queues/tx-0/xps_cpus
echo 2 > /sys/class/net/eth0/queues/tx-1/xps_cpus
echo 4 > /sys/class/net/eth0/queues/tx-2/xps_cpus
echo 8 > /sys/class/net/eth0/queues/tx-3/xps_cpus
```

For devices in the second category mentioned above (unsupported timer
resolution), a compile-time option (DISABLE_INT_TIMER) is available that does
not rely on an underlying system interval timer. However, the trade-off for
this mode of operation is that it results in high CPU utilization. But, clients
running on older or low-capability hosts may be able to execute tests where
they otherwise would not.
```
$ cmake -D DISABLE_INT_TIMER=ON .
```
*Note: Because of the increased CPU utilization, this option is not recommended
for standard server operation.*

## Output (Export) of Received Load Traffic Metadata
To allow for advanced post-analysis of received load traffic during testing, it
is possible to specify an output file (via the `-O [+]file` option) to
capture datagram metadata as CSV text. By default (starting in release 9.0.0),
metadata entries are only written when an RTT sample is available (i.e., only
when a status message exchange occurs). Accordingly, the filename parameter now
accepts a plus-sign prefix to indicate that the original behavior is actually
desired (all metadata should be output).

It must be stressed that when all metadata is being written, due to
the significant number of file writes, this capability is NOT intended for
large-scale usage or production environments. In fact, on hosts with slower
filesystems (e.g., SD card devices) it may cause udpst test traffic loss. In
such cases it would be beneficial to utilize a memory filesystem such as
`/dev/shm` for the output file while a test is running. Also, because this
function is only performed at the load receiver, it can only be used on the
client with downstream testing. When used at the server, only upstream testing
produces an output file. For multi-connection testing in either case, one file
is created for each connection. 

**File Naming**

The provided filename can contain a number of conversion specifications to
allow for dynamic filename creation. The following are introduced by a '#'
character:
- #i - Multi-connection index (0,1,2,...)
- #c - Multi-connection count (the total requested/attempted)
- #I - Multi-connection ID (random value common to each connection of a test)
- #l - Local IP address of data connection
- #r - Remote IP address of data connection
- #s - Source port of data connection
- #d - Destination port of data connection
- #M - Mode of operation ('S' = Server, 'C' = Client)
- #D - Direction of test ('U' = Upstream, 'D' = Downstream)
- #H - Server host name (or IP) specified on command-line
- #p - Control port used for test setup
- #E - Interface name specified with `-E intf` option (only valid on client)

In addition to the above, all conversion specifications supported by strftime()
(and introduced by a '%' character) can also be utilized - see strftime()
manpage for details. For example, an output file specified as
`-O udpst_%F_%H%M%S_#M_#D_#i-#c_#I.csv` would produce a filename similar to
`udpst_2023-05-30_152402_S_U_0-3_23831.csv`. Note, date and time references
in the filename use the local system timezone.

**File Format**

The CSV output file will contain the following columns:
- SeqNo : The sequence number of the datagram as assigned by the sender.
Datagrams are listed in the order they are received.
- PayLoad : The payload size of the datagram in bytes.
- SrcTxTime : The source transmit timestamp of the datagram (based on the
sender's clock).
- DstRxTime : The destination receive timestamp of the datagram (based on the
receiver's clock).
- OWD : The one-way delay of the datagram if the sender's and receiver's clocks
are sufficiently synchronized, else it merely reflects the difference in the
clocks (and could be negative). This value is in milliseconds.
- IntfMbps : The client interface Mbps when the `-E intf` option is used.
- IntfMbpsAlt : The client interface Mbps for the alternate direction.
- RTTTxTime : The transmit timestamp used for RTT (Round-Trip Time)
measurements and carried from the load receiver to the load sender in the
periodic status feedback messages.
- RTTRxTime : The receive timestamp (used for RTT measurements) of the load PDU
carrying the RTTTxTime that was sent to the load sender in the last status
feedback message.
- RTTRespDelay : The RTT response delay includes the time from when the status
feedback message was received and the very next load PDU was sent (i.e., the
turn-around time in the load sender). This value is in milliseconds.
- RTT : The resulting "network" RTT when the RTT response delay is subtracted
from the difference between the RTT transmit and receive times. This value is
in milliseconds.
- StatusLoss : The count of lost status feedback messages sent from the load
receiver to the load sender.

*Because RTT measurements are only periodically sampled (as part of each status
feedback message), those columns will be empty most of the time. Also, all
timestamps utilize microsecond resolution.*

## Multi-Key Authentication
For better support of large-scale deployments with various service offerings
and device types, multiple authentication keys are now supported. As of version
8.2.0, and in addition to the legacy authentication key still available on the
command-line, a key file can now be specified via `-K file` to allow up to 256
unique authentication keys (see `udpst.keys` example file). The CSV formatted
file expects a numeric key ID (0-255) followed by the key string (64 characters
max). Commas, spaces, tabs, and comments (anything beginning with a '#') are
all ignored.

*A default key ID of zero is assumed when one is not specified.*

The authentication process begins with the client using a shared key as input
to a KDF (Key Derivation Function) to create a 32-byte hash for the Setup
Request PDU. This hash and a key ID are inserted in the PDU prior to
transmission to the server. The key used to create
the hash can come from the command-line via the `-a key` option OR from a key
file specified via the `-K file` option. The key ID is specified via the
`-y keyid` option. When a key file is being utilized, the key ID option is also
used to determine which key in the key file will be used to create the hash.
If the key file only contains a single entry, and a key ID was not explicitly
specified on the command-line, that key ID and key will automatically be used.
Otherwise, when a key ID is not explicitly specified on the command-line, a
default key ID of zero is assumed.

When the server receives the Setup Request, and if it is utilizing a key file,
the included key ID will be used to select the key either directly (for
the previous protocol version 11) or as input to the KDF to
create the corresponding hash for comparison and validation. Note, the included
authentication timestamp is also used as input to the KDF. In addition to the
key file, a command-line key specified via `-a key` can also be used for
secondary/backup authentication of the client.
That is, if the authentication via a key file key fails for any reason (key ID
not found, hash comparison mismatch, etc.) authentication is automatically
attempted a second time using the command-line key. This flexibility allows for
an easier transition from the previous protocol version, clients using an older
command-line key, or clients moving from a zero (default) key ID to a non-zero
ID.

Lastly, for clients transitioning from no authentication to authentication, a
new compilation flag is available on the server that makes authentication
optional.
```
$ cmake -D AUTH_IS_OPTIONAL=ON .
```
*Note: This mode of operation is considered low security and should only be
utilized temporarily for a migration or upgrade of clients.*

## Optional Header Checksum and Integrity Checks
On systems where the standard UDP checksum is not being inserted by the
protocol stack/NIC, or is not being verified upon reception, corrupt datagrams
will be passed up to udpst. As of protocol version 11, an optional header
checksum can be calculated and inserted into all control and data PDU headers
to deal with this. Upon reception, udpst will automatically validate the header
checksum if populated. And although this mechanism can operate in one direction
at a time, it should be enabled on both the client and server for bidirectional
protection. The following compilation flag will enable this functionality on
the sender for all outgoing PDUs:
```
$ cmake -D ADD_HEADER_CSUM=ON .
```
*Note: Because of the small to moderate performance impact (depending on the
device), this flag is normally disabled since it is redundant when the standard
UDP checksum is being utilized.*

Independent of whether the header checksum is enabled as an additional PDU
integrity check (beyond size, format, etc.), new output messaging is displayed
when an invalid PDU is received. A bad PDU during the control phase (whether a
corrupted PDU or a rogue UDP datagram) will generate an ALERT while a bad PDU
during the data phase will generate a WARNING (and result in a warning exit
status and JSON ErrorStatus). In cases where the udpst control port on the
server is exposed to the open Internet, and verbose is enabled, this may result
in excessive alerts due to UDP port scanners and probing. If either of these
new output message types is not desired, the following compilation flags can be
used to suppress them (and the PDU is silently ignored):
```
$ cmake -D SUPP_INVPDU_ALERT=ON .
$ cmake -D SUPP_INVPDU_WARN=ON .
```

## Local Backpressure
In version 9.0.0, an enhancement was made to better accommodate backpressure
from the protocol stack. Whenever load PDUs are not accepted for transmission,
typically because the network interface is unable to send traffic out fast
enough, sequence numbers are adjusted to ignore the PDUs not accepted. This is
a change from the previous version, where PDUs not accepted by the protocol
stack would eventually be detected as loss by the far-end receiver. Note that
this older mode of operation is still available via the `-n` option.

## Server Performance Statistics
As of release 9.0.0, a server instance can periodically write operational
counters and performance metrics (in JSON) to a filesystem file via the
`-G file` option. The time intervals for data records and file writes are
controlled by the corresponding `STATS_RECORD_INT` and `STATS_FILE_INT`
compilation constants in the udpst.h file.

A statistics file is written every time the file interval timer expires (300
seconds by default). It will contain however many data records are appropriate
given the data record interval (10 seconds by default). At startup, the first
file may contain less than the expected record count. It is recommended that
the file interval be an even multiple of the data record interval. Also,
although the file is written at a fixed interval, the start of the first
interval is randomized to reduce file write synchronization when a large number
of server instances are running on the same machine.

The default data record and file write intervals are intended to provide good
measurement density for import into a time-series database and subsequent use
by visualization tools. However, if only passive monitoring is needed (for
simple alarming and long-term trending) the two intervals can be set to the
same (larger) value to reduce the amount of data. For example, setting both to
900 would produce a single file with one data record every 15 minutes.

When the file interval time expires the file is opened, written, and closed as
three back-to-back operations (the file does not remain open). More
specifically, it is written as a temporary file (with an added ".tmp" extension)
and renamed at the end. This allows files in the directory to be periodically
moved or purged in bulk, with wildcards, while avoiding partially written data
(e.g., mv *.json /datadir).

**File Naming**

All conversion specifications supported by strftime() (and introduced by a '%'
character) can be utilized for filenames - see strftime() manpage for details.
For example, a file specified as `-G udpst_ny4-vm2_eth2_instance3_%H%M%S.json`
would produce a filename similar to `udpst_ny4-vm2_eth2_instance3_224500.json`.
Note, date and time references in the filename use the local system timezone.

For easier file management, the time used for conversion specifications in
the filename is truncated to the same number of seconds as the file interval.
Therefore, a file interval of 300 seconds would produce time values on even
5-minute boundaries. Using the filename from the previous example, the files
produced would be:
```
udpst_ny4-vm2_eth2_instance3_224500.json
udpst_ny4-vm2_eth2_instance3_225000.json
udpst_ny4-vm2_eth2_instance3_225500.json
udpst_ny4-vm2_eth2_instance3_230000.json
udpst_ny4-vm2_eth2_instance3_230500.json
...
```
This approach has the benefit of automatically overwriting the oldest file
every 24 hours while still allowing reasonable time for its processing, storage,
or transfer. This methodology can be utilized to allow numerous overwrite
time frames (an hour, a day, a week, a month,...).

*Note: The times and timestamps within the files always use exact time
references with microsecond resolution.*

**File Contents**

The performance statistics file is in JSON format. At the top level it contains
static information about the server instance including a few of its key
configuration settings (i.e., hostname, IP address, PID, software version,...).
This includes a schema version that can be used by post processors to detect
file changes and maintain backward compatibility. Also at the top level is the
start time for the period covered by the file as well as an array of data
records included in that period.

Within each data record is time information indicating the period covered by
the record. Each data record contains a group of "maximum" values capturing the
largest value of a performance metric during the record period. This is
followed by a group of "average" values that show the averages of performance
metrics across the same period. All rates are per-second averages and are
calculated using the data record interval as the time period.

Additionally, at the top level and after the data record array, is a group of
"counters" that keep track of various events and errors. These counters are
only incremented and are never reset. There is also a process uptime
(indicating the number of seconds since the instance started) as well as
an end time for the period covered by the file. The counters show the
cumulative values as of the end time of the file.

An example file is included with the software `server_performance_stats.json`
as well as an abbreviated text version containing details about the various
fields and metrics.

