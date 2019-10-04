#!/bin/sh

DATALINK=$1

if [ ${DATALINK} ]; then
  /device-bacnet-${DATALINK}/device-bacnet-c --registry consul://edgex-core-consul:8500 --confdir /res/${DATALINK} --name device-bacnet-${DATALINK}
else
  /device-bacnet-ip/device-bacnet-c --registry consul://edgex-core-consul:8500 --confdir /res/ip --name device-bacnet-ip &
  /device-bacnet-mstp/device-bacnet-c --registry consul://edgex-core-consul:8500 --confdir /res/mstp --name device-bacnet-mstp
fi
