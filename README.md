# device-bacnet-c
Device service for BACnet protocol written in C. This service
may be built to support BACnet devices connected via
ethernet (/IP) or serial (/MSTP).

## Build Instructions

### Requirements
This device service requires the EdgeX Device SDK for C
(device-sdk-c), version 1.x

### Building the BACnet Device Service
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
found in `./build/release/device-bacnet-{ip, mstp}/device-bacnet-c`

## Running the Service

With no options specified the service runs with a name of "device-bacnet", the
default configuration profile, no registry and a configuration directory of res/ip (for Ethernet) or res/mstp (for Serial).
These settings may be changed on the command line as follows:

```
   -n, --name <name>          : Set the device service name
   -r [url], --registry [url] : Use the registry service
   -p, --profile <name>       : Set the profile name
   -c, --confdir <dir>        : Set the configuration directory
```

## BBMD Setup
To run the BACnet/IP device service using a BBMD device, set
BBMD\_ADDRESS and BBMD\_PORT to the values corresponding to 
the BBMD device under the [Driver] section of the TOML
configuration.

## Supported BACnet Services
The device service uses the Who-Is BACnet service when doing
a discovery call, the Read-Property service when doing a GET
request, and the Write-Property service when doing a PUT
request.

## Limitations
The service can access Boolean, String, Int (Signed and Unsigned) and Float (Real and Double) types. Other BACnet types such as Enum, OctetString, Date and Time are not supported.
