cmake_minimum_required(VERSION 3.1)
project(device_bacnet_c LANGUAGES C)

# Package support
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

# Set additional C compiler flags
if (DEFINED CMAKE_C_COMPILER_ID)
    if (CMAKE_C_COMPILER_ID MATCHES GNU)
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wstrict-prototypes -Wmissing-prototypes")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
        set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")
        set (CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -ggdb -O0")
        set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
        set (CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -O3")
    endif ()
endif ()

file (GLOB BACNET_OBJECT_FILES "../../bacnet-stack/demo/object/device-client.c" "../../bacnet-stack/demo/object/device.h")
add_library(bacnet_objects STATIC ${BACNET_OBJECT_FILES})
include_directories(../../bacnet-stack/include ../../bacnet-stack/demo/object .)
#add_compile_definitions(BACDL_DEFINE=-DBACDL_ALL=1)
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include_directories(../../bacnet-stack/ports/linux)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    include_directories(../../bacnet-stack/ports/bsd)
endif()

find_package(Threads)
find_library(EDGEX_CSDK_LIB NAMES csdk PATHS ENV CSDK_DIR PATH_SUFFIXES lib)
find_path(EDGEX_CSDK_INCLUDE NAMES edgex/devsdk.h PATHS ENV CSDK_DIR PATH_SUFFIXES include)

SET(BAC_LIB "${CMAKE_SOURCE_DIR}/../../lib/ip/libbacnet.a")

#Uncomment the line below to build BACnet/MSTP
#SET(DATALINK "BACDL_MSTP=1")
SET(DATALINK "BACDL_BIP=1" CACHE STRING "Some user-specified option")

if (${DATALINK} STREQUAL "BACDL_MSTP=1")
    SET(BAC_LIB "${CMAKE_SOURCE_DIR}/../../lib/mstp/libbacnet.a")
endif()

ADD_LIBRARY(libbacnet STATIC IMPORTED)
SET_TARGET_PROPERTIES(libbacnet PROPERTIES IMPORTED_LOCATION ${BAC_LIB})

file(GLOB C_FILES ./*.c)
file(GLOB H_FILES ./*.h)

FILE(STRINGS "../../VERSION" VERSION_NUMBER)

add_executable(device-bacnet-c main.c ${C_FILES} ${H_FILES})

TARGET_COMPILE_DEFINITIONS(bacnet_objects PUBLIC ${DATALINK} BACAPP_LIGHTING_COMMAND BACAPP_PRINT_ENABLED VERSION="${VERSION_NUMBER}")

target_link_libraries(device-bacnet-c PUBLIC bacnet_objects libbacnet ${CMAKE_THREAD_LIBS_INIT} m ${EDGEX_CSDK_LIB})
target_include_directories(device-bacnet-c PRIVATE ${EDGEX_CSDK_INCLUDE})
