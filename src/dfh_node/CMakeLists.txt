# CMake-описание библиотеки dfh_node.
# Содержит базовые исходники и публичные include-директории.
find_package(OpenSSL REQUIRED)

add_library(dfh_node STATIC
    anti_replay_validator.cpp
    auth_service.cpp
    auth_cache.cpp
    bounded_queue.cpp
    canonical_request.cpp
    config.cpp
    config_api_key_store.cpp
    config_loader.cpp
    config_validator.cpp
    dfh_adapter_dto.cpp
    fingerprint_computer.cpp
    nonce_store.cpp
    rate_limiter.cpp
    sha256_utils.cpp
    status.cpp
    task_scheduler.cpp
    unified_gate.cpp
    worker_pool.cpp
    ws_connection_limiter.cpp
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
