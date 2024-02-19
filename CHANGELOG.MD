# CHANGELOG.MD for udpst 8.x.x Releases

*The udpst utility conforms to TR-471 (Issue 4). The latest TR-471 specification can be found at
https://www.broadband-forum.org/technical/download/TR-471.pdf*

# 2024-01-26: [UDPST 8.1.1] (https://github.com/BroadbandForum/OBUDPST/releases/tag/v8.1.1)

This point release addresses the need to always handle interface byte counters as 64-bit values,
regardless of the machine architecture. Several minor compiler warnings were also corrected.
When verbose is enabled, the server will now display a message when the control port receives
a datagram that has an invalid size, ID, or format for a setup request PDU (else it will be silently
ignored...as it was before).

The maximum requestable bandwidth on the client, when using the "-B mbps" option, was increased from
10000 to 32767. Also, the 64-bit ntohll/htonll macros were modified to accommodate big-endian
architectures.

# 2023-11-17: [UDPST 8.1.0] (https://github.com/BroadbandForum/OBUDPST/releases/tag/v8.1.0)

The main feature in this release is the option of exporting received load traffic metadata (sequence
numbers, timestamps,...) in CSV format. See README for feature details.

Also in this release, the ErrorStatus values used in the JSON output have been made much more
granular (instead of just 1 for warning and -1 for error). Now, warnings are allocated values starting
at 1 (one) - although only a handful are currently used. Error values start at 50 and can go up to a
maximum of 255 (see udpst.h for details). Additionally, an ErrorMessage2 text field was also added to
better communicate the cause-and-effect relationship between typical message pairs.

# 2023-04-07: [UDPST 8.0.0] (https://github.com/BroadbandForum/OBUDPST/releases/tag/v8.0.0)

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

# CHANGELOG.MD for udpst 7.x.x Releases

*The udpst utility conforms to TR-471 (Issue 3). The latest TR-471 specification can be found at
https://www.broadband-forum.org/technical/download/TR-471.pdf*

# 2022-11-18: [UDPST 7.5.1] (https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.5.1)

This release has one new feature, which is a response to testing/experience on mobile access:

* The team made the decision to change a default setting: UDPST will now Ignore Reordering (and duplication) in the Load Adjustment search algorithm. The Reordered and Duplicate packets are legitimate contributions to IP-Layer Capacity. The utility measures these metrics under all conditions, this only affects search processing in Type B and Type C algorithms.

Also, this release makes several updates to documentation (reflecting the latest features implemented in the utility). 

# 2022-05-05: [UDPST 7.5.0] (https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.5.0)

This release has one major new feature, which was anticipated by the version 9 protocol:

* Optional Load Adjustment (Search) Algorithm, Type C, briefly described as "Multiply and Retry"
* The "fast" ramp-up is now a multiplicative rate increase to congestion, reaching 1Gbps in ~1 second
* The "fast" ramp-up can be re-tried when conditions warrent, to ensure that the Max IP-Layer Capacity is reached
* This algorithm supports a search over the full range of rates, even if the subscribed rate is >1Gbps
* The Type B algorithm remains the default, for testing that does not benefit from "Multiply and Retry" aspects

# 2022-02-24: [UDPST 7.4.0] (https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.4.0)

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

# 2021-11-17: [UDPST 7.3.0](https://github.com/BroadbandForum/OBUDPST/releases/tag/v7.3.0)

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

# CHANGELOG.MD for udpst 7.0.0 Release

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

