#!/bin/sh
set -e -x

CSDK_VER=3.0.0

# Dependencies
if [ ! -d deps ]
then
  mkdir deps

  cd /device-bacnet-c/deps

# get c-sdk from edgexfoundry
  wget https://github.com/edgexfoundry/device-sdk-c/archive/v${CSDK_VER}.zip
  unzip v${CSDK_VER}.zip
  cd device-sdk-c-${CSDK_VER}
  ./scripts/build.sh
  cp -rf include/* /usr/include/
  cp build/release/c/libcsdk.so /usr/lib/
  mkdir -p /usr/share/doc/edgex-csdk
  cp Attribution.txt /usr/share/doc/edgex-csdk
  cp LICENSE /usr/share/doc/edgex-csdk
  rm -rf /device-bacnet-c/deps
fi
