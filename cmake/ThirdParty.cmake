# Подключение сторонних зависимостей.
# Поддерживается режим системных пакетов и режим submodule.
if(DFH_NODE_USE_SYSTEM_DEPS)
    message(STATUS "Third-party: using system dependencies (DFH_NODE_USE_SYSTEM_DEPS=ON)")
else()
    message(STATUS "Third-party: using pinned submodules in third_party/")

    # nlohmann/json (только заголовки).
    if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/CMakeLists.txt)
        add_subdirectory(third_party/nlohmann_json)
    else()
        message(FATAL_ERROR "nlohmann/json submodule not found. Run: git submodule update --init")
    endif()

    # log-it-cpp (используем только заголовки; не собираем примеры).
    if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp)
        add_library(log-it-cpp INTERFACE)
        target_include_directories(log-it-cpp INTERFACE
            ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp/include/logit_cpp
            ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp/libs/fmt/include
            ${CMAKE_SOURCE_DIR}/third_party/log-it-cpp/libs/time-shield-cpp/include/time_shield_cpp
        )
        target_compile_options(log-it-cpp INTERFACE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-deprecated-declarations>
            $<$<CXX_COMPILER_ID:MSVC>:/wd4996>
        )
    else()
        message(FATAL_ERROR "log-it-cpp submodule not found. Run: git submodule update --init")
    endif()
endif()

# Simple-Web-Server (header-only, MIT).
set(SWS_DIR "${PROJECT_SOURCE_DIR}/third_party/simple-web-server")
set(ASIO_DIR "${PROJECT_SOURCE_DIR}/third_party/asio")
if(EXISTS "${ASIO_DIR}/include/asio.hpp")
    set(ASIO_INCLUDE_DIR "${ASIO_DIR}/include")
elseif(EXISTS "${ASIO_DIR}/asio/include/asio.hpp")
    set(ASIO_INCLUDE_DIR "${ASIO_DIR}/asio/include")
else()
    set(ASIO_INCLUDE_DIR "")
endif()

if(EXISTS "${SWS_DIR}/server_http.hpp" AND NOT ASIO_INCLUDE_DIR STREQUAL "")
    add_library(simple-web-server INTERFACE)
    target_include_directories(simple-web-server INTERFACE
        "${SWS_DIR}"
        "${ASIO_INCLUDE_DIR}"
    )
    target_compile_definitions(simple-web-server INTERFACE
        ASIO_STANDALONE
        USE_STANDALONE_ASIO
    )

    find_package(Threads REQUIRED)
    target_link_libraries(simple-web-server INTERFACE Threads::Threads)
    if(WIN32)
        target_link_libraries(simple-web-server INTERFACE ws2_32 mswsock)
    endif()
else()
    message(FATAL_ERROR "simple-web-server/asio submodules not found. Run: git submodule update --init third_party/simple-web-server third_party/asio")
endif()

# TODO: добавить подключение системных пакетов для каждой библиотеки.
# Пины и лицензии — в docs/third_party.md.
