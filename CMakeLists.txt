cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

project(dfh-node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DFH_NODE_BUILD_TESTS "Build tests" ON)
option(DFH_NODE_BUILD_EXAMPLES "Build examples" ON)
option(DFH_NODE_USE_SYSTEM_DEPS "Use system dependencies instead of third_party" OFF)
option(DFH_NODE_ENABLE_TLS "Enable TLS support" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(Options)

message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD} (extensions: ${CMAKE_CXX_EXTENSIONS})")
message(STATUS "DFH_NODE_BUILD_TESTS: ${DFH_NODE_BUILD_TESTS}")
message(STATUS "DFH_NODE_BUILD_EXAMPLES: ${DFH_NODE_BUILD_EXAMPLES}")
message(STATUS "DFH_NODE_USE_SYSTEM_DEPS: ${DFH_NODE_USE_SYSTEM_DEPS}")
message(STATUS "DFH_NODE_ENABLE_TLS: ${DFH_NODE_ENABLE_TLS}")
