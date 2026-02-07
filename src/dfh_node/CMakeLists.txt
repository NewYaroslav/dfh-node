# CMake-описание библиотеки dfh_node.
# Содержит базовые исходники и публичные include-директории.
find_package(OpenSSL REQUIRED)

add_library(dfh_node STATIC
    auth_cache.cpp
    bounded_queue.cpp
    config.cpp
    config_api_key_store.cpp
    config_loader.cpp
    config_validator.cpp
    fingerprint_computer.cpp
    rate_limiter.cpp
    status.cpp
    task_scheduler.cpp
    worker_pool.cpp
    version.cpp
)

dfh_node_apply_warnings(dfh_node)

target_include_directories(dfh_node
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src/dfh_node
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(dfh_node
    PUBLIC
        nlohmann_json::nlohmann_json
        log-it-cpp
        OpenSSL::Crypto
)
