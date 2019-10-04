#!/bin/sh
set -e -x

# Find root directory and system type

ROOT=$(dirname $(dirname $(readlink -f $0)))
echo $ROOT
cd $ROOT

# Get and compile BACnet stack

if [ ! -f $ROOT/lib/ip/libbacnet.a ] || [ ! -f $ROOT/lib/mstp/libbacnet.a ]; then
  if [ ! -d $ROOT/bacnet-stack ]; then
    git clone https://github.com/IOTechSystems/bacnet-stack.git 
  fi
  cd $ROOT/bacnet-stack
  git checkout v0.8.6-IOTech
  mkdir -p $ROOT/lib/ip
  mkdir -p $ROOT/lib/mstp
  make BACDL_DEFINE=-DBACDL_MSTP=1 clean library
  cp lib/libbacnet.a $ROOT/lib/mstp
  make BACDL_DEFINE=-DBACDL_BIP=1 clean library
  cp lib/libbacnet.a $ROOT/lib/ip
fi

# Cmake release build for BACnet/IP
mkdir -p $ROOT/build/release/device-bacnet-ip
cd $ROOT/build/release/device-bacnet-ip
cmake -DDATALINK:STRING=BACDL_BIP=1 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release $ROOT/src/c
make 2>&1 | tee release.log

# Cmake release build for BACnet/MSTP
mkdir -p $ROOT/build/release/device-bacnet-mstp
cd $ROOT/build/release/device-bacnet-mstp
cmake -DDATALINK:STRING=BACDL_MSTP=1 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release $ROOT/src/c
make 2>&1 | tee release.log
