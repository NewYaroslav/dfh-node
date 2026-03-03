# CMake-описание библиотеки dfh_node.
# Содержит базовые исходники и публичные include-директории.
find_package(OpenSSL REQUIRED)

add_library(dfh_node STATIC
    core/status.cpp
    core/version.cpp
    config/config.cpp
    config/config_api_key_store.cpp
    config/config_loader.cpp
    config/config_validator.cpp
    security/anti_replay_validator.cpp
    security/canonical_request.cpp
    security/fingerprint_computer.cpp
    security/nonce_store.cpp
    security/sha256_utils.cpp
    auth/auth_cache.cpp
    auth/auth_service.cpp
    auth/rate_limiter.cpp
    auth/unified_gate.cpp
    auth/ws_connection_limiter.cpp
    scheduler/bounded_queue.cpp
    scheduler/task_scheduler.cpp
    scheduler/worker_pool.cpp
    adapter/dfh_adapter_dto.cpp
    adapter/fake_dfh_adapter.cpp
    transport/http/http_dto_parser.cpp
    transport/http/http_error_map.cpp
    transport/http/http_reply_handle.cpp
    transport/http/http_router.cpp
    transport/http/http_server.cpp
    transport/ws/ws_dto_parser.cpp
    transport/ws/ws_message_handler.cpp
    transport/ws/ws_protocol.cpp
    transport/ws/ws_router.cpp
    transport/ws/ws_server.cpp
    transport/ws/ws_session_registry.cpp
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
        simple-web-server
        simple-websocket-server
        msgpack-c
)
