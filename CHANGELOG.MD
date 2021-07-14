# CHANGELOG.MD for udpst 7.1.0 Release

*The udpst utility conforms to TR-471. The latest TR-471 specification can be found at
https://www.broadband-forum.org/technical/download/TR-471.pdf *

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


