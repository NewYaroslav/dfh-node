if(DFH_NODE_USE_SYSTEM_DEPS)
  message(STATUS "Third-party: using system dependencies (DFH_NODE_USE_SYSTEM_DEPS=ON)")
else()
  message(STATUS "Third-party: using pinned submodules in third_party/")
endif()

# TODO: add third_party submodules here (add_subdirectory).
# TODO: add optional system dependency wiring for each library.
# Pins and licenses live in docs/third_party.md.
