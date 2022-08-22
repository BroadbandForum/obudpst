#!/bin/sh
$NETEM_COMMAND || exit
/app/udpst $UDPST_COMMAND