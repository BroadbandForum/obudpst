# OB-UDPST - Best Practices for Structured Testing
For those interested in using OB-UDPST (Open Broadband-UDP Speed Test) for more
formal and automated testing across various speeds, this text is intended as a
convenient summarization of applicable "best practices" listed throughout the
README.

## General Assumptions...
These assumptions, as well as the subsequent recommendations, are based on an
architecture that generally involves regionally deployed test servers supporting
tests from various network-owned client devices on the customer premises.
1. Jumbo frames are NOT supported between the client and server, at any
provisioned speed.
2. A traditional 1500 byte MTU is available end-to-end without fragmentation.
3. Older or low-end client devices would benefit from any available performance
recommendations.
4. Because the client devices may encounter background user traffic competing
with the test traffic, they should (additionally) record the total layer 2
interface bandwidth via the `-E intf` option. This total should also be used,
via the `-M` option, to determine the overall test maximum.

Supplemental options not included below:
* An authentication scheme between the client and server via `-a key`.
* Server error logging for udpst via `-l logfile`.

## Test Servers (with the following assumptions)...
* A physical server has either multiple 10G interfaces or a single 100G
interface and is using the bandwidth management option `-B mbps` when binding
udpst instances to the IP addresses. In the case of a single 100G, this can be
done via multiple IP aliases or by using different udpst control ports via the
`-p port` option.
* Each udpst instance uses a CPU affinity that matches the NUMA node handling
its respective test interface (see "Server Optimization..." within README).
* Each udpst instance is run as a background daemon process.
* At least one udpst instance is dedicated for High-Speed (HS) testing above
1 Gbps. This is done to prevent HS test blocking when, due to the bandwidth
management limit of `-B 10000`, an instance is unable to service a full 10G
test because it always has some number of Low-Speed (LS) tests in progress.
* Jumbo datagrams are left enabled for HS tests (the default). Even though
fragmentation will occur without jumbo frame support, this prevents the socket
I/O rate from becoming excessive and limiting the max sending rate.
```
Server example with four 10G interfaces and two NUMA nodes.
Node 0 (CPUs 0-13,28-41) is associated with the first two interfaces and node 1
(CPUs 14-27,42-55) is associated with the last two:

taskset -c 0-13,28-41  udpst -x -B 10000 -T -j <Server_LS-IP1>
taskset -c 0-13,28-41  udpst -x -B 10000 -T -j <Server_LS-IP2>
taskset -c 14-27,42-55 udpst -x -B 10000 -T -j <Server_LS-IP3>
taskset -c 14-27,42-55 udpst -x -B 10000 -T <Server_HS-IP1>
```
```
Server example with a single 100G interface on NUMA node 0 (CPUs 0-13,28-41):

taskset -c 0-13,28-41 udpst -x -B 10000 -T -j <Server_LS-IP1>
taskset -c 0-13,28-41 udpst -x -B 10000 -T -j <Server_LS-IP2>
taskset -c 0-13,28-41 udpst -x -B 10000 -T -j <Server_LS-IP3>
...
taskset -c 0-13,28-41 udpst -x -B 10000 -T <Server_HS-IP1>
taskset -c 0-13,28-41 udpst -x -B 10000 -T <Server_HS-IP2>
...

Alternate server example using different control port ranges for LS and HS:

taskset -c 0-13,28-41 udpst -x -B 10000 -T -j -p 25001 <Server_IP>
taskset -c 0-13,28-41 udpst -x -B 10000 -T -j -p 25002 <Server_IP>
taskset -c 0-13,28-41 udpst -x -B 10000 -T -j -p 25003 <Server_IP>
...
taskset -c 0-13,28-41 udpst -x -B 10000 -T -p 25501 <Server_IP>
taskset -c 0-13,28-41 udpst -x -B 10000 -T -p 25502 <Server_IP>
...
```
*Note: In the first two examples the DNS name resolution might return multiple
addresses in a round-robin or randomized order, where the client always starts
with the first one, or maybe the client selects one randomly from a static
list. In the alternate example, the client would select a random control port
from either predetermined range.*

## Client Devices (with the following assumptions)...
* If the device has a multi-core processor, udpst uses a CPU affinity that
avoids the CPU handling network interrupts (typically 0).
* If the device has a 64-bit processor, it uses a 64-bit kernel with a 64-bit
build of udpst.
* A client knows its maximum provisioned speed and specifies it in each test
request via the `-B mbps` option (which the server uses for admission control).
* A client knows which local network interface will be used and specifies it
via the `-E intf` option.
* A downstream test is done via `-d` (shown below) or `-u` for upstream.
* Formatted JSON output is used for easier text parsing.
* When the provisioned speed is above 1 Gbps, tests are started at 1 Gbps via
`-I @1000` to avoid unnecessary ramp-up time.
```
Client example for (LS) provisioned speeds up to 1 Gbps:

taskset -c 1-3 udpst -d -B <mbps> -E <intf> -M -T -j -f jsonf <Server_LS> >udpst.json
```
```
Client example for (HS) provisioned speeds above 1 Gbps:

taskset -c 1-3 udpst -d -B <mbps> -E <intf> -M -I @1000 -T -f jsonf <Server_HS> >udpst.json
```
```
Alternate client example using a locally chosen LS control port:

taskset -c 1-3 udpst -d -B <mbps> -E <intf> -M -T -j -f jsonf -p <port> <Server_LS> >udpst.json
```
*Note: If udpst exits with a non-zero status value and the JSON file contains
an "ErrorStatus" of -1 (negative one) and an "ErrorMessage" of "ERROR:
Required max bandwidth exceeds available server capacity", the client can wait
some number of seconds and retry the test. Or, based on the server examples
above, immediately choose a different entry from either the returned DNS
address list or the predetermined control port range. Ultimately, any retry
behavior would be controlled by whatever script launches udpst.*

