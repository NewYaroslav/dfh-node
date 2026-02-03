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

# TODO: добавить подключение системных пакетов для каждой библиотеки.
# Пины и лицензии — в docs/third_party.md.
