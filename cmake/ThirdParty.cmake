if(DFH_NODE_USE_SYSTEM_DEPS)
  message(STATUS "Third-party: using system dependencies (DFH_NODE_USE_SYSTEM_DEPS=ON)")
else()
  message(STATUS "Third-party: using pinned submodules in third_party/")

  # nlohmann/json (header-only)
  if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/CMakeLists.txt)
    add_subdirectory(third_party/nlohmann_json)
  else()
    message(FATAL_ERROR "nlohmann/json submodule not found. Run: git submodule update --init")
  endif()

  # log-it-cpp (header-only usage; avoid building its examples)
  if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp)
    add_library(log-it-cpp INTERFACE)
    target_include_directories(log-it-cpp INTERFACE
      ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp/include/logit_cpp
      ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp/libs/fmt/include
      ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp/libs/time-shield-cpp/include/time_shield_cpp
    )
  else()
    message(FATAL_ERROR "log-it-cpp submodule not found. Run: git submodule update --init")
  endif()
endif()

# TODO: add optional system dependency wiring for each library.
# Pins and licenses live in docs/third_party.md.
