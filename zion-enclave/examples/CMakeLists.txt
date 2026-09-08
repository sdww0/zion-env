cmake_minimum_required(VERSION 3.10)
project(zion_examples C CXX ASM)

# check if SDK has been built and ZION_SDK_DIR configuration variable exists
if (NOT DEFINED ZION_SDK_DIR)
  message(FATAL_ERROR "Please set ZION_SDK_DIR configuration variable to an installed SDK path")
endif()

set(ENV{ZION_SDK_DIR} ${ZION_SDK_DIR})

if (DEFINED PATH)
  set(ENV{PATH} ${PATH})
endif()

include(${ZION_SDK_DIR}/cmake/macros.cmake)
include(ExternalProject)
find_package(Git REQUIRED)

use_riscv_toolchain(${ZION_BITS})

# export include directory
include_directories(AFTER ${ZION_SDK_DIR}/include)

# set paths to the libraries
set(ZION_LIB_HOST ${ZION_SDK_DIR}/lib/libzion-host.a)
set(ZION_LIB_EDGE ${ZION_SDK_DIR}/lib/libzion-edge.a)
set(ZION_LIB_VERIFIER ${ZION_SDK_DIR}/lib/libzion-verifier.a)
set(ZION_LIB_EAPP ${ZION_SDK_DIR}/lib/libzion-eapp.a)

# create a phony target "examples"
add_custom_target("examples")

# add all examples below
add_subdirectory(hello)
#add_subdirectory(hello-native)
#add_subdirectory(attestation)
#add_subdirectory(tests)
