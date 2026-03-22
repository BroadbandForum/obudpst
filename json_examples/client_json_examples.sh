#!/bin/bash
#
# Create example client JSON files for various test scenarios
# -----------------------------------------------------------
#

#
# Define array of server instances (in this case, multiple instances on one server connected
# via a GigE)
#
servers=("test-server1.com:24601"
	"test-server1.com:24602"
	"test-server1.com:24603"
	"test-server1.com:24604")

#
# Execute a multi-flow upload test to all server instances with jumbo sizes disabled but
# traditional sizes (1500 byte MTU) enabled. Output results as formatted JSON. Set the minimum
# connection count to 3, allowing one instance to be unreachable. Specify a maximum required
# bandwidth of 975 Mbps. This option is used by the server for admission control. Although this
# would be considered a 1 Gbps service, a value of 975 represents the theoretical maximum of a
# basic GigE ((1500 / 1538) * 1000). Typically, the customer's provisioned bandwidth is specified.
#
../udpst -u -j -T -f jsonf -C 3 -B 975 ${servers[*]} >client_example_01.json

#
# A very concise version of the previous example. The JSON output is 'brief' (no header or input
# object) and the sub-interval details are not included (only the summary and maximum rate info).
#
../udpst -u -j -T -f jsonb -s -C 3 -B 975 ${servers[*]} >client_example_02.json

#
# Include interface traffic data for 'enp1s0' (normally, the WAN interface on the customer prem
# device). This allows tracking customer transit traffic in addition to the generated test traffic
# (i.e., the total network traffic). And by using the '-M' option, this total will also be used to
# determine the test maximum.
#
../udpst -u -j -T -f jsonf -E enp1s0 -M ${servers[*]} >client_example_03.json

#
# Execute a one minute low-rate test, with sub-interval details not included, on one randomly
# selected server. This test is used to regularly check for loss, delay, and stability with a
# minimal bandwidth load (~0.12 Mbps) on the network.
#
../udpst -u -j -T -f jsonf -t 60 -I 0 -s ${servers[$((RANDOM % 4))]} >client_example_04.json

#
# Execute a fixed-rate test at 90% of the expected maximum (975 Mbps * 0.9 / 4 servers = 220 Mbps).
# This test is normally used to verify new service or post-repair restoration.
#
../udpst -u -j -T -f jsonf -I 220 ${servers[*]} >client_example_05.json

#
# Execute a bi-modal test with an initial mode of 5 sub-intervals and a total test time of 15
# seconds (resulting in a second mode of 10 sub-intervals). This is helpful when a network service
# is expected to have initial performance characteristics, optimized for short duration flows, that
# change later for extended duration flows. The different maximums for both modes are of interest.
#
../udpst -u -j -T -f jsonf -i 5 -t 15 ${servers[*]} >client_example_06.json

#
# Execute a bimodal, dual-phase test where rate adjustments are suppressed (via the minus sign
# prefix) for the first 5 sub-intervals. These sub-intervals will test at the lowest initial
# sending rate (~0.12 Mbps * 4 servers), after which the standard search for a maximum rate begins.
# This approach allows a single test to check for unloaded loss and delay before testing for a
# maximum (where loss and delay are expected).
#
../udpst -u -j -T -f jsonf -i -5 -t 15 ${servers[*]} >client_example_07.json

#
# This follows the same initial mode behavior as above. However, instead of searching for a maximum
# in the second mode, a fixed rate of 880 Mbps (220 Mbps × 4 servers) is used...approximately 90%
# of the expected maximum. This enables testing for loss and delay under both unloaded and heavily
# loaded conditions while avoiding the congestion-induced impairments typical of a maximum-rate
# search.
#
../udpst -u -j -T -f jsonf -i -5 -t 15 -I 220 ${servers[*]} >client_example_08.json

#
# This test uses ~90% of the expected maximum for the initial mode and searches for a maximum rate
# in the second mode.
#
../udpst -u -j -T -f jsonf -i -5 -t 15 -I @220 ${servers[*]} >client_example_09.json

#
# This demonstrates the JSON output and error info when an in-progress test fails.
#
echo "Invoke manual server failure 5 seconds after continuation."
read -p "Press Enter to continue..."
../udpst -u -j -T -f jsonf ${servers[*]} >client_example_10.json

#
# This scenario shows the JSON output when the minimum number of servers are unreachable and
# testing never begins.
#
../udpst -u -j -T -f jsonf ${servers[*]} >client_example_11.json

