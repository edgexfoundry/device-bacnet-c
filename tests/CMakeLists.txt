cmake_minimum_required(VERSION 3.1)
project(bacnet_tests LANGUAGES C)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/../src/cmake)

if (DEFINED CMAKE_C_COMPILER_ID)
    if (CMAKE_C_COMPILER_ID MATCHES GNU)
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wstrict-prototypes -Wmissing-prototypes")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
        set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")
    endif ()
endif ()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK REQUIRED check)

find_library(EDGEX_CSDK_LIB NAMES csdk PATHS ENV CSDK_DIR PATH_SUFFIXES lib)
if(EDGEX_CSDK_LIB STREQUAL "EDGEX_CSDK_LIB-NOTFOUND")
    message(FATAL_ERROR "No C SDK library found. Please set CSDK_DIR.")
endif()

find_path(EDGEX_CSDK_INCLUDE NAMES devsdk/devsdk.h PATHS ENV CSDK_DIR PATH_SUFFIXES include)
if(EDGEX_CSDK_INCLUDE STREQUAL "EDGEX_CSDK_INCLUDE-NOTFOUND")
    message(FATAL_ERROR "No C SDK header found. Please set CSDK_DIR.")
endif()

set (IOT_VER 1.6)
find_package (IOT REQUIRED)

SET(BAC_LIB "${CMAKE_SOURCE_DIR}/../lib/ip/libbacnet.a")
ADD_LIBRARY(libbacnet STATIC IMPORTED)
SET_TARGET_PROPERTIES(libbacnet PROPERTIES IMPORTED_LOCATION ${BAC_LIB})

file (GLOB BACNET_OBJECT_FILES "${CMAKE_SOURCE_DIR}/../bacnet-stack/demo/object/device-client.c")
add_library(bacnet_objects_test STATIC ${BACNET_OBJECT_FILES})
target_include_directories(bacnet_objects_test PUBLIC
    ${CMAKE_SOURCE_DIR}/../bacnet-stack/include
    ${CMAKE_SOURCE_DIR}/../bacnet-stack/demo/object
    ${CMAKE_SOURCE_DIR}/../src/c
)
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    target_include_directories(bacnet_objects_test PUBLIC ${CMAKE_SOURCE_DIR}/../bacnet-stack/ports/linux)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    target_include_directories(bacnet_objects_test PUBLIC ${CMAKE_SOURCE_DIR}/../bacnet-stack/ports/bsd)
endif()
target_compile_definitions(bacnet_objects_test PUBLIC BACDL_BIP=1 BACAPP_LIGHTING_COMMAND BACAPP_PRINT_ENABLED)

find_package(Threads)

enable_testing()

add_executable(test_invariant_main
    test_invariant_main.c
    ../src/c/main.c
)

# -DUNIT_TEST removes 'static' from bacnet_alloc_exception so the test can link to it
target_compile_definitions(test_invariant_main PRIVATE UNIT_TEST BACDL_BIP=1 BACAPP_LIGHTING_COMMAND BACAPP_PRINT_ENABLED)

target_include_directories(test_invariant_main PRIVATE
    ${CHECK_INCLUDE_DIRS}
    ${EDGEX_CSDK_INCLUDE}
    ${IOT_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/../bacnet-stack/include
    ${CMAKE_SOURCE_DIR}/../bacnet-stack/demo/object
    ${CMAKE_SOURCE_DIR}/../src/c
)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    target_include_directories(test_invariant_main PRIVATE ${CMAKE_SOURCE_DIR}/../bacnet-stack/ports/linux)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    target_include_directories(test_invariant_main PRIVATE ${CMAKE_SOURCE_DIR}/../bacnet-stack/ports/bsd)
endif()

target_link_libraries(test_invariant_main
    bacnet_objects_test
    libbacnet
    ${CMAKE_THREAD_LIBS_INIT}
    m
    ${EDGEX_CSDK_LIB}
    ${IOT_LIBRARY}
    ${CHECK_LIBRARIES}
)

add_test(NAME invariant_main COMMAND test_invariant_main)
