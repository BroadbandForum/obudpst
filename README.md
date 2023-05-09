# OB-UDPST
Open Broadband-UDP Speed Test (OB-UDPST) is a client/server software utility to
demonstrate one approach of doing IP capacity measurements as described by:

- Broadband Forum TR-471 Issue 3 (12/2022): _Maximum IP-Layer Capacity Metric,
Related Metrics, and Measurements_, BBF TR-471,
https://www.broadband-forum.org/technical/download/TR-471.pdf

- ITU-T Recommendation Y.1540 (revised 03/2023): _Internet protocol data communication
service - IP packet transfer and availability performance parameters_,
ITU-T Y.1540, https://www.itu.int/rec/T-REC-Y.1540-201912-I/en 
   
- ITU-T Y-series Supplement 60 (2022): _Interpreting ITU-T Y.1540 maximum
IP-layer capacity measurements_, ITU-T Y.Sup60,
https://www.itu.int/rec/T-REC-Y.Sup60/en

- ETSI Technical Specification 103 222 Part 2 (2019-08): _Reference
benchmarking and KPIs for High speed internet_, ETSI TS 103 222-2, V1.2.1,
https://www.etsi.org/deliver/etsi_ts/103200_103299/10322202/01.02.01_60/ts_10322202v010201p.pdf

- IETF RFC 9097: _Metrics and Methods for One-way IP Capacity_, 
https://datatracker.ietf.org/doc/html/rfc9097

- ETSI Technical Report 103 702 (2020-11): _Speech and multimedia Transmission
Quality (STQ); QoS parameters and test scenarios for assessing network
capabilities in 5G performance measurements_, ETSI TR 103 702, V1.1.1
https://www.etsi.org/deliver/etsi_tr/103700_103799/103702/01.01.01_60/tr_103702v010101p.pdf

## Overview
Utilizing an adaptive transmission rate, via a pre-built table of discreet
sending rates (starting at 0.11 Mbps), UDP datagrams are sent from client
to server(s) or server(s) to client to determine the maximum available IP-layer
capacity between them. The load traffic is only sent in one direction at a
time, and status feedback messages are sent periodically in the opposite
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
    Only service requests that utilize a matching authentication key
```
*Note: The server must be reachable on the UDP control port [default **25000**]
and all UDP ephemeral ports (**32768 - 60999** as of the Linux 2.4 kernel,
available via `cat /proc/sys/net/ipv4/ip_local_port_range`).*

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
Capacity has been reached. This option is activated using `-A c` (with the
more linear Type B algorithm remaining the default).

One change to the default settings was included in Release 7.5.1. All Load
Adjustment search algorithms will now Ignore Reordering (and duplication) as
a component of sequence errors: only packet loss will increase the sequence
error count. The optional use of the `-R` option will now revert the behavior
from "Ignore" back to "Include". This change was justified by recognizing that
both out-of-order and duplicate datagrams are a legitimate part of IP-Layer
Capacity.

See the following publication (which is updated frequently) for more details
on testing in the circumstances described above:
- ITU-T Y-series Supplement 60 (06/22): _Interpreting ITU-T Y.1540 maximum
IP-layer capacity measurements_, ITU-T Y.Sup60,
https://www.itu.int/rec/T-REC-Y.Sup60/en

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
"Considerations for Older Hardware and Low-End Devices".

## Building OB-UDPST
To build OB-UDPST a local installation of CMake version 3.0 or higher is required. Please obtain it
for your particular build system either using the locally available packages (`apt-get install cmake` or `yum install cmake3`) or
consult with [https://cmake.org] for other download options.

```
$ cmake .
$ make
```
*Note: Authentication functionality uses a command-line key along with the
OpenSSL crypto library to create and validate a HMAC-SHA256 signature (which is
used in the setup request to the server). Although the makefile will build even
if the expected directory is not present, disabling the key and library
dependency, the additional files needed to support authentication should be
relatively easy to obtain (e.g., `sudo apt-get install libssl-dev` or
`sudo yum install openssl-devel`).*

## Test Processing Walkthrough (all messaging and PDUs use UDP)
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
the user (and exit if the required connection count falls below the minimum).
If it indicates success it will build a Test Activation request with all the
test parameters it desires such as the direction, the duration, etc. It will
then send the Test Activation request to the UDP port number the server
communicated in the Setup response.

**Test Activation request**

After the server receives the Test Activation request on the new connection, it
can choose to accept, ignore or modify any of the test parameters. When the
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

**More Info**

For much more detail on the test protocol, see the ./protocol.md file. Also, an
Internet-Draft,
https://datatracker.ietf.org/doc/draft-ietf-ippm-capacity-protocol/ describes
Protocol Version 9 in even more detail.

## JSON Output
For examples of the JSON output fields see the included sample files named
"udpst-*.json". Available JSON output options include `-f json` (unformatted),
`-f jsonb` (brief & unformatted), and `-f jsonf` (formatted). To significantly
reduce the size of the JSON output, option `-s` (omit sub-interval results)
can be combined with `-f jsonb` (omit static input fields). 

Included in the output is a numeric ErrorStatus field (which corresponds with
the software exit status) as well as a text ErrorMessage field. If a test
completes normally without incident the error status will be 0 (zero) and the
error message will be empty. If a test completes, but encounters a soft error
or warning, the error status will be 1 (one) and the most recent warning
message will be included. If a test fails to complete, the error status will be
-1 (negative one) and the most recent warning or error message will be included.

The file "ob-udpst_output_mapping.pdf" provides a mapping between JSON key
names, TR-471 names, TR-181 names, and the ob-udpst STDOUT names for various
results.

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
or configuring them to use different UDP control ports (e.g., `-p 25000`,
`-p 25001`, `-p 25002`,...). And although single threaded, each running
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

## Increasing the Starting Sending Rate (Considerations)
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

## Server Optimization - Particularly When Jumbo Frames Are Unavailable
By default, the sending rate table utilizes jumbo size datagrams when testing
above 1 Gbps. As expected, maximum performance is obtained when the network
also supports a jumbo MTU size (9000+ bytes). However, some environments are
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

## Considerations for Older Hardware and Low-End Devices
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

