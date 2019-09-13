# device-bacnet-c
Device service for BACnet protocol written in C

## Build Instructions

### Building The BACnet Device Service
Before building the BACnet device service, please ensure
that you have the EdgeX C-SDK installed and make sure that
the current directory is the BACnet device service directory
(device-bacnet-c). To build the BACnet device service, enter
the command below into the command line to run the build
script.

	./scripts/build.sh

This will build two device services: device-bacnet-ip and
device-bacnet-mstp. To only build one of them, run the
build script with the parameter "ip" or "mstp" depending
on which device service you want to build. The command below
shows an example of this.

	./script/build.sh mstp

In the event that your C-SDK is not installed in the system
default paths, you may specify its location using the environment
variable CSDK_DIR

After having built the device service, the executable can be
found in ./build/release/device-bacnet-{ip, mstp}/device-bacnet-c.

## BBMD setup
To run the BACnet/IP device service using a BBMD device,
please set the environement variables BBMD\_ADDRESS and
BBMD\_PORT to the values corresponding to the BBMD device.
