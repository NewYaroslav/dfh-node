# Базовые настройки проекта и проверки окружения.
# Запрещает in-source сборку и фиксирует стандарт C++.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD} (extensions: ${CMAKE_CXX_EXTENSIONS})")
message(STATUS "DFH_NODE_BUILD_TESTS: ${DFH_NODE_BUILD_TESTS}")
message(STATUS "DFH_NODE_BUILD_EXAMPLES: ${DFH_NODE_BUILD_EXAMPLES}")
message(STATUS "DFH_NODE_USE_SYSTEM_DEPS: ${DFH_NODE_USE_SYSTEM_DEPS}")
message(STATUS "DFH_NODE_ENABLE_TLS: ${DFH_NODE_ENABLE_TLS}")
