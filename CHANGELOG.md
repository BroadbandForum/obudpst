# Changelog for UDPST 9.x.x Releases

*The udpst utility conforms to TR-471 (Issue 4). The latest TR-471 specification
can be found at https://www.broadband-forum.org/technical/download/TR-471.pdf*

## 2026-01-01: [UDPST 9.0.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v9.0.0)

**IMPORTANT: The default control port has changed from 25000 to 24601. For
backward compatibility, 8.2.0 clients will either need to use the new control
port via `-p 24601` or the server can be run with the legacy control port (via
`-p 25000`).**

The primary change in this release was making the software RFCxxx compatible.
This included enhancing authentication to cover each message exchanged in the
control phase as well as the use of a KDF (Key Derivation Function) to create
unique keys for both the client and server for each test. Note, authentication
mode 2 (HMAC in status messages) was considered overkill and was therefore not
implemented. An additional RFC requirement was the generation of a new Null
Request control message sent from the server to the client immediately after
the Setup Response. This enables client access, through the server's firewall,
to the new UDP ephemeral port that is opened for testing. For servers protected
by a standard firewall doing NAT, this should alleviate the need to have the
entire UDP ephemeral port range open and accessible. Lastly, the default UDP
control port needed to change from 25000 to 24601 and the protocol version
needed to be bumped to 20. However, this release remains fully backward
compatible with release 8.2.0 (protocol version 11).

Another significant enhancement was the ability to produce periodic server
performance statistics. Data records are created at regular intervals (10
seconds by default) and include both maximums and averages of various
rate-based performance metrics. The records, along with static instance
information and numerous event/error counters, are periodically written to a
filesystem file (as JSON) every 5 minutes (by default) -- see README for
specifics. Additionally, the following changes are also included...

* The sub-interval time period is now specified in ms instead of seconds
(`-P period`). The default time period remains the same (1000 ms). To go along
with this change the text output now displays the sub-interval seconds with a
single decimal digit (e.g., "1.0" instead of "1").
* While a server process is idling and awaiting test requests it will reduce
its primary timer interrupt interval from 100 us to 10 ms. This significantly
reduces the CPU utilization of server instances while not actively testing.
* The metadata export file now captures both traffic directions for an
interface specified via `-E intf`.
* The bandwidth option `-B mbps` now allows a zero mbps parameter. Although
this has no effect, and is equivalent to not providing the option at all, it
was done to simplify command-line parameter processing when using automated
scripts.
* Secondary (and unnecessary) recvmmsg() calls are no longer performed when all
messages are read on the first call. This is also true of received status
messages (given the long inter-arrival times).
* When datagrams are not accepted for transmission by the protocol stack, due
to internal backpressure, sequence numbers are adjusted to eliminate loss that
can be accounted for. This new functionality can be disabled via the `-n`
option.
* An out-of-order or duplicate status message will now produce a better WARNING
message instead of just using a max loss value of 65535 (-1). For example,
"REMOTE WARNING: Incoming status feedback messages lost (OoO/Dup)".
* RTT variation output now includes an average (in addition to the min and max).
Accordingly, the text output has changed from "min-max" to "min/avg/max" (the
same as one-way delay variation). Also, the JSON output now includes a "RTTAvg"
key in addition to "RTTMin" and "RTTMax".
* The default output (export) functionality has been modified to only write
metadata entries when an RTT sample is available (i.e., only when a status
message exchange occurs). As a result, the option parameter now allows a
plus-sign filename prefix (`-O [+]file`) to indicate that the original
behavior is actually desired. That is, all metadata should be output. This
change in the default operation should drastically reduce the number of
metadata entries written when testing at higher speeds.
* The count parameter for the bimodal option now accepts a minus sign prefix
(`-i [-]count`). This prefix suppresses sending rate adjustments during the
initial mode. This allows for dual-phase testing where the initial mode uses
one rate and the second mode uses another (either fixed or variable).
By default, the initial mode uses sending rate index 0 and the second mode
attempts to find a maximum, starting from zero. If a fixed rate is also
specified (e.g., `-I 500`), the first mode uses sending rate index 0 and the
second uses the specified fixed sending rate. In contrast, if a starting rate is
specified (e.g., `-I @500`), the first mode uses the fixed starting rate and the
second attempts to find a maximum, starting from that rate. In addition, bimodal
usage is now accompanied by a change in the summary output info. The single text
line "Summary" will now be two lines, one for each mode, output as "Sum[#-#]".
The JSON object "Summary" will now only cover the first mode and the second
mode summary will be in an array called "ModalSummary". This is consistent with
the existing "AtMax" and "ModalResult" structure for the maximum rate.

# Changelog for UDPST 8.x.x Releases

## 2024-07-11: [UDPST 8.2.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v8.2.0)

This release addresses the need to always handle interface byte counters as 64-bit values,
regardless of the machine architecture (OBUDPST-56). Also, for improved management of large-scale
deployments multi-key support has been added (OBUDPST-57). This capability allows for up to 256
authentication keys to be specified via a key file. The legacy authentication key from the
command-line is still supported and will act as a backup validation option to the key file if
provided.

Integrity checks for PDUs were enhanced via a compilation flag to insert an optional PDU header
checksum into control and data PDUs (OBUDPST-58). This functionality is needed when the standard UDP
checksum is not being utilized by either the sender or receiver system. Changes for OBUDPST-57 and
OBUDPST-58 resulted in the supported protocol version range on the server going from 10-10 to 10-11
(i.e., the server will continue to support existing version 10 clients, see README for details).
In addition,...

* Several minor compiler warnings were corrected
* Enhanced integrity checks will generate alerts for invalid control PDUs and warnings for invalid
data PDUs (both can be suppressed via compilation flags, causing them to be silently ignored)
* The maximum requestable bandwidth on the client, when using the "-B mbps" option, was increased from
10000 to 32767
* The 64-bit ntohll/htonll macros were modified to accommodate big-endian architectures
* An additional column for lost status feedback messages was added to the metadata export
* The maximum authentication key size was increase from 32 to 64
* A new ErrorStatus error value for multi-key and warning value for invalid data PDUs was added
* An additional column for interface Mbps (from "-E intf") was added to the metadata export
* A fix was added for scenarios where a timeout occurs, due to a lost test activation request, and
the allocated upstream bandwidth on the server is never deallocated

## 2023-11-17: [UDPST 8.1.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v8.1.0)

The main feature in this release is the option of exporting received load traffic metadata (sequence
numbers, timestamps,...) in CSV format. See README for feature details.

Also in this release, the ErrorStatus values used in the JSON output have been made much more
granular (instead of just 1 for warning and -1 for error). Now, warnings are allocated values starting
at 1 (one) - although only a handful are currently used. Error values start at 50 and can go up to a
maximum of 255 (see udpst.h for details). Additionally, an ErrorMessage2 text field was also added to
better communicate the cause-and-effect relationship between typical message pairs.

## 2023-04-07: [UDPST 8.0.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v8.0.0)

The primary new feature in this release is support for multiple connections (UDP flows) between
the client and one or more server instances (i.e., distributed servers). Additionally,...
* Reducing the starting sending rate (for index 0) while adding support for randomized packet sizes
* Stopping possible attacks that prevent graceful test shutdown by manipulating STOP bits in load PDUs
* Adding an optional '-1' (dash one) flag to the command-line to have the server cleanly exit after a single test
* Randomizing the start of load PDU generation (to better support multiple connections)
* Enhancing socket receive processing to provide load balancing of epoll events (to better support multiple connections)
* Adding GSO (Generic Segmentation Offload) and RecvMMsg()+Truncation performance optimizations
* Adding optional rate limiting for server scale testing
* Creation of framework to support automated functional tests via Docker containers and NetEm

*With these changes, new fields were required in the setup and load PDUs. As a result,
both the current and minimum protocol version needed to be bumped to 10.*

# Changelog for UDPST 7.x.x Releases

*The udpst utility conforms to TR-471 (Issue 3). The latest TR-471 specification can be found at
https://www.broadband-forum.org/technical/download/TR-471.pdf*

## 2022-11-18: [UDPST 7.5.1](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.5.1)

This release has one new feature, which is a response to testing/experience on mobile access:

* The team made the decision to change a default setting: UDPST will now Ignore Reordering (and duplication) in the Load Adjustment search algorithm. The Reordered and Duplicate packets are legitimate contributions to IP-Layer Capacity. The utility measures these metrics under all conditions, this only affects search processing in Type B and Type C algorithms.

Also, this release makes several updates to documentation (reflecting the latest features implemented in the utility). 

## 2022-05-05: [UDPST 7.5.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.5.0)

This release has one major new feature, which was anticipated by the version 9 protocol:

* Optional Load Adjustment (Search) Algorithm, Type C, briefly described as "Multiply and Retry"
* The "fast" ramp-up is now a multiplicative rate increase to congestion, reaching 1Gbps in ~1 second
* The "fast" ramp-up can be re-tried when conditions warrent, to ensure that the Max IP-Layer Capacity is reached
* This algorithm supports a search over the full range of rates, even if the subscribed rate is >1Gbps
* The Type B algorithm remains the default, for testing that does not benefit from "Multiply and Retry" aspects

## 2022-02-24: [UDPST 7.4.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.4.0)

This release has many new features, supported by the version 9 protocol:

* Best Practices file (Discusses the application of udpst for formal and automated testing across various speeds, with multiple servers, using current and new features.)
* Implementation of Random payload option. Performance suggests very limited compressibility.
* Server can accept a BW limit for mission-control, Client must supply a max BW for testing, -B option
* Fixed a reordering issue with fragments when Jumbo frames are enabled - uses single code thread.
* Optional Start rate for load-adjustment algorithm
* New README guidance for NUMA node selection. Fragmentation makes this more important.
* Option to support 1500 byte MTU. (-T)
* Bug fix for a 32-bit kernel and the L2 Interface counters wrapping as a result of smaller max count.
* Bug fix for unexpected sub-10Mbps testing.
* Backward compatibility: Servers can test with both protocol version 8 and version 9.
* Additional JSON fields: example files have been updated (additions only)

## 2021-11-17: [UDPST 7.3.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.3.0)

This release has a greatly expanded (optional) JSON-formatted version of the command-line output.

Further, this release implements virtually all the TR-471 Issue 2 and TR-181 IPLayerCapacity{} data model elements.
The File "ob-udpst_output_mapping_to_current_JSON and_TR-471_info model-v33-20211118_071424.pdf" provides the mapping
where names differ in small ways, or where additional names are provided in the JSON format or STDOUT.

We also include measurement of Ethernet Interface traffic during a test, to help identify when customer traffic was 
present that might cause the IP-Layer Capacity measurement to under-estimate the maximum capacity.

## 2021-08-20: [UDPST 7.2.1](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.2.1)

This supplemental release incorporates results from recent testing, where two changes 
in default parameters to protect the fast ramp-up seem prudent, and the status of default
values needed additional explanation:
* slowAdjThresh; Slow rate adjustment threshold value default=3 (changed from 2)
* seqErrThresh; Sequence error threshold value default=10 (changed from 0)
* The READ.md describes the default values, the changes in (7.2.1), and circumstances when 
test orgs. should consider changes to the default values.  In other words, the defaults are 
provided as a starting point; any test campaign should consider whether one or more default 
values need to be changed for their specific circumstances!

## 2021-07-16: [UDPST 7.2.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.2.0)

This release will have the first features for compute environment adaptation:
* OS limitations (detect sendmsg()-only or sendmmsg()-available)
* clock precision limitations and/or CPU power limitations (a pre-compile option)

Also, an optional JSON-formatted version of the command-line output.

## 2021-03-05: [UDPST 7.1.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.1.0)

This release addressed the following issues and new features: 
* Add guards to avoid errors in inclusion files
* The test protocol is described in detail in file ./protocol.md

## 2020-12-11: [UDPST 7.0.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.0.0)

This release addressed the following issues and new features: 
* Increase accuracy of trial and sub-interval rate calculations
* Final 10G sending rate not defined when jumbo sizes are disabled
* Development integration with Bamboo
* Analysis Option for Bi-modal Access Rates
* Add a configuration and build system
* Split implementation into a library and a frontend
* Update References in README.MD file, as per maadolph#1 in github
* Option to ignore out-of-order packets during Rate Adjustment

