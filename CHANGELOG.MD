# CHANGELOG.MD for udpst 7.1.0 Release

*The udpst utility conforms to TR-471. The latest TR-471 specification can be found at
https://www.broadband-forum.org/technical/download/TR-471.pdf *

## 2021-08-20: [UDPST 7.2.1](https://github.com/BroadbandForum/OB-UDPST/releases/tag/v7.2.1)

This supplemental release incorporates results from recent testing, where two changes 
in default parameters to protect the fast ramp-up seem prudent, and the status of default
values needed additional explanation:
* slowAdjThresh; Slow rate adjustment threshold value default=3 (changed from 2)
* seqErrThresh; Sequence error threshold value default=10 (changed from 0)
* The READ.md describes the default values, the changes in (7.2.1), and circumstances when 
test orgs. should consider changes to the default values.  In other words, the defaults are 
provided as a starting point; any test campaign should consider whether one or more default 
values need to be changed for their specific circumstances!


## 2021-07-16: [UDPST 7.2.0](https://github.com/BroadbandForum/OB-UDPST/releases/tag/v7.2.0)

This release will have the first features for compute environment adaptation:
* OS limitations (detect sendmsg()-only or sendmmsg()-available)
* clock precision limitations and/or CPU power limitations (a pre-compile option)

Also, an optional JSON-formatted version of the command-line output.

## 2021-03-05: [UDPST 7.1.0](https://github.com/BroadbandForum/OB-UDPST/releases/tag/v7.1.0)

This release addressed the following issues and new features: 
* Add guards to avoid errors in inclusion files
* The test protocol is described in detail in file ./protocol.md

# CHANGELOG.MD for udpst 7.0.0 Release

## 2020-12-11: [UDPST 7.0.0](https://github.com/BroadbandForum/OB-UDPST/releases/tag/v7.0.0)

This release addressed the following issues and new features: 
* Increase accuracy of trial and sub-interval rate calculations
* Final 10G sending rate not defined when jumbo sizes are disabled
* Development integration with Bamboo
* Analysis Option for Bi-modal Access Rates
* Add a configuration and build system
* Split implementation into a library and a frontend
* Update References in README.MD file, as per maadolph#1 in github
* Option to ignore out-of-order packets during Rate Adjustment


