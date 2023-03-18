# OB-UDPST - Best Practices for Structured Testing
For those interested in using OB-UDPST (Open Broadband - UDP Speed Test) for
more formal and automated testing across various speeds, this text is intended
as a convenient summarization of applicable "best practices" listed throughout
the README.

*Note: As of release 8.0.0 this document has been significantly modified to
take advantage of the new multi-connection/distributed-server functionality.*

## General Assumptions...
These assumptions, as well as the subsequent recommendations, are based on an
architecture that generally involves regionally deployed test servers
supporting tests from various network-owned client devices on the customer
premises.
1. Jumbo frames are NOT supported between the client and server (although
highly recommended). As such, IP fragmentation is expected when connection
counts result in connection sending rates above 1 Gbps.
2. A traditional 1500 byte MTU is available end-to-end without fragmentation.
3. Multiple connections (UDP flows) are used between the client and
server(s) to better utilize link aggregation and ECMP within the network.
4. All tests are performed to multiple server instances to provide load
balancing as well as avoid individual high-bandwidth "elephant" flows.
5. A minimum number of connections is specified to allow at least one server
instance to be unreachable or unavailable.
6. Older or low-end client devices would benefit from any available performance
recommendations.
7. Because the client devices may encounter background user traffic competing
with the test traffic, they should (additionally) record the total layer 2
interface bandwidth via the `-E intf` option. This total should also be used,
via the `-M` option, to determine the overall test maximum.

Supplemental options not included below:
* An authentication scheme between the client and server via `-a key`.
* Server error logging for udpst via `-l logfile`.

## Test Servers (with the following assumptions)...
* A physical server has either multiple 10G interfaces or a single 100G
interface and is using the bandwidth management option `-B mbps` when binding
udpst instances to them. This can be done via multiple IP aliases or by using
different UDP control ports specified with the `-p port` option.
* Any applicable server optimizations within the README (re: socket buffering,
fragment reassembly memory, tx/rx rings, etc.) have been reviewed and applied.
* Each udpst instance uses a CPU affinity that matches the NUMA node handling
its respective test interface (see README for additional details).
* Each udpst instance is run as a background daemon process.
```
Server example with two 10G interfaces on two different NUMA nodes.
One interface is on node 0 (CPUs 0-13,28-41) and the other is on node 1 (CPUs
14-27,42-55). A total of 4 server instances are run across them using a
bandwidth value of 5 Gbps each.

$ taskset -c 0-13,28-41  udpst -x -B 5000 -T <Local_IP1>
$ taskset -c 0-13,28-41  udpst -x -B 5000 -T <Local_IP2>
$ taskset -c 14-27,42-55 udpst -x -B 5000 -T <Local_IP3>
$ taskset -c 14-27,42-55 udpst -x -B 5000 -T <Local_IP4>
```
```
Server example with a 100G interface on NUMA node 0 (CPU cores 0-13,28-41).

$ taskset -c 0-13,28-41 udpst -x -B 10000 -T <Local_IP1>
$ taskset -c 0-13,28-41 udpst -x -B 10000 -T <Local_IP2>
...
$ taskset -c 0-13,28-41 udpst -x -B 10000 -T <Local_IP10>

Alternate 100G server example using different UDP control ports.

$ taskset -c 0-13,28-41 udpst -x -B 10000 -T -p 25001 <Local_IP>
$ taskset -c 0-13,28-41 udpst -x -B 10000 -T -p 25002 <Local_IP>
...
$ taskset -c 0-13,28-41 udpst -x -B 10000 -T -p 25010 <Local_IP>
```

## Client Devices (with the following assumptions)...
* The device has a multi-core processor and is not running *irqbalance*. The
CPU affinity of udpst is set to avoid the CPU core handling network interrupts
(assumed to be 0 in this example).
* If the device has a 64-bit processor, it uses a 64-bit kernel with a 64-bit
build of udpst.
* A client knows its maximum provisioned speed and specifies it in each test
request via the `-B mbps` option (which the server uses for admission control).
* A client knows which local network interface will be used and specifies it
via the `-E intf` option.
* Formatted JSON output is used for easier text parsing.
* A client will specify a minimum number of connections that will allow for one
server to be unreachable or unavailable.
* Clients with provisioned speeds up to 1 Gbps will use higher connection
counts while those provisioned above 1 Gbps will use lower connection counts.
This will avoid jumbo datagram sizes at low speeds and take advantage of them
at high speeds.
```
Client example for low-speed services (<=1G) on a 4-core CPU device. Testing
uses 12 connections to 12 server instances on 3 different physical servers. A
minimum connection count of 8 allows one server to be offline or unreachable.

$ taskset -c 1-3 udpst -d -B <mbps> -E <intf> -M -T -f jsonf -C 8 \
  <Server1_IP1> <Server1_IP2> <Server1_IP3> <Server1_IP4> <Server2_IP1> \
  <Server2_IP2> <Server2_IP3> <Server2_IP4> <Server3_IP1> <Server3_IP2> \
  <Server3_IP3> <Server3_IP4> >udpst.json
```
```
Alternate client example using different UDP control ports.

$ taskset -c 1-3 udpst -d -B <mbps> -E <intf> -M -T -f jsonf -C 8 \
  <Server1_IP>:25005 <Server1_IP>:25006 <Server1_IP>:25007 <Server1_IP>:25008 \
  <Server2_IP>:25001 <Server2_IP>:25002 <Server2_IP>:25003 <Server2_IP>:25004 \
  <Server3_IP>:25002 <Server3_IP>:25004 <Server3_IP>:25006 <Server3_IP>:25008 \
  >udpst.json
```
```
Client example for high-speed services (>1G) with 3 connections to 3 server
instances on 3 different physical servers. A minimum connection count of 2
allows one server to be offline or unreachable.

$ taskset -c 1-3 udpst -d -B <mbps> -E <intf> -M -T -f jsonf -C 2 \
  <Server1_IP1> <Server2_IP1> <Server3_IP1> >udpst.json
```

