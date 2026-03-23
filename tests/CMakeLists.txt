# CMake-описание тестовых таргетов и регистрации в CTest.
# Содержит smoke-тесты и unit-тесты конфигурации.
add_test(
    NAME test_tests_registry_consistency
    COMMAND ${CMAKE_COMMAND}
            -DREPO_ROOT=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/scripts/check-tests-registered.cmake
)

add_test(
    NAME test_comment_style
    COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckDoxygenStyle.cmake
)

add_executable(dfh_node_smoke
    test_smoke.cpp
)

target_link_libraries(dfh_node_smoke
    PRIVATE
        dfh_node
)

add_test(NAME dfh_node_smoke COMMAND dfh_node_smoke)

# Копируем минимальный конфиг в build для тестов приложения.
configure_file(
    ${CMAKE_SOURCE_DIR}/examples/config_minimal.json
    ${CMAKE_BINARY_DIR}/test_configs/config_minimal.json
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/tests/app_configs/config_logging_trace.json
    ${CMAKE_BINARY_DIR}/test_configs/config_logging_trace.json
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/tests/app_configs/config_logging_debug.json
    ${CMAKE_BINARY_DIR}/test_configs/config_logging_debug.json
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/tests/app_configs/config_logging_warn_upper.json
    ${CMAKE_BINARY_DIR}/test_configs/config_logging_warn_upper.json
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/tests/app_configs/config_logging_error.json
    ${CMAKE_BINARY_DIR}/test_configs/config_logging_error.json
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/tests/app_configs/config_invalid_env.json
    ${CMAKE_BINARY_DIR}/test_configs/config_invalid_env.json
    COPYONLY
)

add_test(NAME app_no_args COMMAND $<TARGET_FILE:dfh_node_app>)
set_tests_properties(app_no_args PROPERTIES WILL_FAIL TRUE)

add_test(NAME app_valid_config
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_minimal.json
)

add_test(NAME app_missing_config COMMAND $<TARGET_FILE:dfh_node_app> --config missing.json)
set_tests_properties(app_missing_config PROPERTIES WILL_FAIL TRUE)

add_test(NAME app_config_without_value COMMAND $<TARGET_FILE:dfh_node_app> --config)
set_tests_properties(app_config_without_value PROPERTIES WILL_FAIL TRUE)

add_test(NAME app_invalid_validation_config
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_invalid_env.json
)
set_tests_properties(app_invalid_validation_config PROPERTIES WILL_FAIL TRUE)

add_test(NAME app_valid_config_trace
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_logging_trace.json
)

add_test(NAME app_valid_config_debug
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_logging_debug.json
)

add_test(NAME app_valid_config_warn_upper
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_logging_warn_upper.json
)

add_test(NAME app_valid_config_error
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_logging_error.json
)

add_executable(test_config_defaults
    test_config_defaults.cpp
)

target_link_libraries(test_config_defaults
    PRIVATE
        dfh_node
)

add_test(NAME test_config_defaults COMMAND test_config_defaults)

add_executable(test_config_loader
    test_config_loader.cpp
)

target_link_libraries(test_config_loader
    PRIVATE
        dfh_node
)

add_test(NAME test_config_loader COMMAND test_config_loader)

add_executable(test_config_validator
    test_config_validator.cpp
)

target_link_libraries(test_config_validator
    PRIVATE
        dfh_node
)

add_test(NAME test_config_validator COMMAND test_config_validator)

add_executable(test_task_scheduler
    test_task_scheduler.cpp
)

target_link_libraries(test_task_scheduler
    PRIVATE
        dfh_node
)

add_test(NAME test_task_scheduler COMMAND test_task_scheduler)

add_executable(test_worker_pool
    test_worker_pool.cpp
)

target_link_libraries(test_worker_pool
    PRIVATE
        dfh_node
)

add_test(NAME test_worker_pool COMMAND test_worker_pool)

add_executable(test_bounded_queue
    test_bounded_queue.cpp
)

target_link_libraries(test_bounded_queue
    PRIVATE
        dfh_node
)

add_test(NAME test_bounded_queue COMMAND test_bounded_queue)

add_executable(test_scope
    test_scope.cpp
)

target_link_libraries(test_scope
    PRIVATE
        dfh_node
)

add_test(NAME test_scope COMMAND test_scope)

add_executable(test_scope_auth
    test_scope_auth.cpp
)

target_link_libraries(test_scope_auth
    PRIVATE
        dfh_node
)

add_test(NAME test_scope_auth COMMAND test_scope_auth)

add_executable(test_fingerprint_computer
    test_fingerprint_computer.cpp
)

target_link_libraries(test_fingerprint_computer
    PRIVATE
        dfh_node
)

add_test(NAME test_fingerprint_computer COMMAND test_fingerprint_computer)

add_executable(test_config_api_key_store
    test_config_api_key_store.cpp
)

target_link_libraries(test_config_api_key_store
    PRIVATE
        dfh_node
)

add_test(NAME test_config_api_key_store COMMAND test_config_api_key_store)

add_executable(test_disk_monitor
    test_disk_monitor.cpp
)

target_link_libraries(test_disk_monitor
    PRIVATE
        dfh_node
)

add_test(NAME test_disk_monitor COMMAND test_disk_monitor)

add_executable(test_mdbx_api_key_store
    test_mdbx_api_key_store.cpp
)

target_link_libraries(test_mdbx_api_key_store
    PRIVATE
        dfh_node
)

add_test(NAME test_mdbx_api_key_store COMMAND test_mdbx_api_key_store)

add_executable(test_composite_api_key_store
    test_composite_api_key_store.cpp
)

target_link_libraries(test_composite_api_key_store
    PRIVATE
        dfh_node
)

add_test(NAME test_composite_api_key_store COMMAND test_composite_api_key_store)

add_executable(test_api_key_manager
    test_api_key_manager.cpp
)

target_link_libraries(test_api_key_manager
    PRIVATE
        dfh_node
)

add_test(NAME test_api_key_manager COMMAND test_api_key_manager)

add_executable(test_auth_cache
    test_auth_cache.cpp
)

target_link_libraries(test_auth_cache
    PRIVATE
        dfh_node
)

add_test(NAME test_auth_cache COMMAND test_auth_cache)

add_executable(test_rate_limiter
    test_rate_limiter.cpp
)

target_link_libraries(test_rate_limiter
    PRIVATE
        dfh_node
)

add_test(NAME test_rate_limiter COMMAND test_rate_limiter)

add_executable(test_ws_connection_limiter
    test_ws_connection_limiter.cpp
)

target_link_libraries(test_ws_connection_limiter
    PRIVATE
        dfh_node
)

add_test(NAME test_ws_connection_limiter COMMAND test_ws_connection_limiter)

add_executable(test_auth_service
    test_auth_service.cpp
)

target_link_libraries(test_auth_service
    PRIVATE
        dfh_node
)

add_test(NAME test_auth_service COMMAND test_auth_service)

add_executable(test_unified_gate
    test_unified_gate.cpp
)

target_link_libraries(test_unified_gate
    PRIVATE
        dfh_node
)

add_test(NAME test_unified_gate COMMAND test_unified_gate)

add_executable(test_sha256_utils
    test_sha256_utils.cpp
)

target_link_libraries(test_sha256_utils
    PRIVATE
        dfh_node
)

add_test(NAME test_sha256_utils COMMAND test_sha256_utils)

add_executable(test_canonical_request
    test_canonical_request.cpp
)

target_link_libraries(test_canonical_request
    PRIVATE
        dfh_node
)

add_test(NAME test_canonical_request COMMAND test_canonical_request)

add_executable(test_nonce_store
    test_nonce_store.cpp
)

target_link_libraries(test_nonce_store
    PRIVATE
        dfh_node
)

add_test(NAME test_nonce_store COMMAND test_nonce_store)

add_executable(test_anti_replay_validator
    test_anti_replay_validator.cpp
)

target_link_libraries(test_anti_replay_validator
    PRIVATE
        dfh_node
)

add_test(NAME test_anti_replay_validator COMMAND test_anti_replay_validator)

add_executable(test_gate_e2e
    test_gate_e2e.cpp
)

target_link_libraries(test_gate_e2e
    PRIVATE
        dfh_node
)

add_test(NAME test_gate_e2e COMMAND test_gate_e2e)

add_executable(test_status
    test_status.cpp
)

target_link_libraries(test_status
    PRIVATE
        dfh_node
)

add_test(NAME test_status COMMAND test_status)

add_executable(test_dfh_adapter_dto
    test_dfh_adapter_dto.cpp
)

target_link_libraries(test_dfh_adapter_dto
    PRIVATE
        dfh_node
)

add_test(NAME test_dfh_adapter_dto COMMAND test_dfh_adapter_dto)

add_executable(test_fake_dfh_adapter
    test_fake_dfh_adapter.cpp
)

target_link_libraries(test_fake_dfh_adapter
    PRIVATE
        dfh_node
)

add_test(NAME test_fake_dfh_adapter COMMAND test_fake_dfh_adapter)

add_executable(test_dfh_adapter_e2e
    test_dfh_adapter_e2e.cpp
)

target_link_libraries(test_dfh_adapter_e2e
    PRIVATE
        dfh_node
)

add_test(NAME test_dfh_adapter_e2e COMMAND test_dfh_adapter_e2e)

add_executable(test_http_error_map
    test_http_error_map.cpp
)

target_link_libraries(test_http_error_map
    PRIVATE
        dfh_node
)

add_test(NAME test_http_error_map COMMAND test_http_error_map)

add_executable(test_http_dto_parser
    test_http_dto_parser.cpp
)

target_link_libraries(test_http_dto_parser
    PRIVATE
        dfh_node
)

add_test(NAME test_http_dto_parser COMMAND test_http_dto_parser)

add_executable(test_sync_dto_parser
    test_sync_dto_parser.cpp
)

target_link_libraries(test_sync_dto_parser
    PRIVATE
        dfh_node
)

add_test(NAME test_sync_dto_parser COMMAND test_sync_dto_parser)

add_executable(test_http_reply_handle
    test_http_reply_handle.cpp
)

target_link_libraries(test_http_reply_handle
    PRIVATE
        dfh_node
)

add_test(NAME test_http_reply_handle COMMAND test_http_reply_handle)

add_executable(test_http_integration
    test_http_integration.cpp
)

target_link_libraries(test_http_integration
    PRIVATE
        dfh_node
)

add_test(NAME test_http_integration COMMAND test_http_integration)

add_executable(test_admin_router
    test_admin_router.cpp
)

target_link_libraries(test_admin_router
    PRIVATE
        dfh_node
)

add_test(NAME test_admin_router COMMAND test_admin_router)

add_executable(test_sync_router
    test_sync_router.cpp
)

target_link_libraries(test_sync_router
    PRIVATE
        dfh_node
)

add_test(NAME test_sync_router COMMAND test_sync_router)

add_executable(test_ops_endpoints
    test_ops_endpoints.cpp
)

target_link_libraries(test_ops_endpoints
    PRIVATE
        dfh_node
)

add_test(NAME test_ops_endpoints COMMAND test_ops_endpoints)

add_executable(test_ws_protocol
    test_ws_protocol.cpp
)

target_link_libraries(test_ws_protocol
    PRIVATE
        dfh_node
)

add_test(NAME test_ws_protocol COMMAND test_ws_protocol)

add_executable(test_ws_session_registry
    test_ws_session_registry.cpp
)

target_link_libraries(test_ws_session_registry
    PRIVATE
        dfh_node
)

add_test(NAME test_ws_session_registry COMMAND test_ws_session_registry)

add_executable(test_ws_dto_parser
    test_ws_dto_parser.cpp
)

target_link_libraries(test_ws_dto_parser
    PRIVATE
        dfh_node
)

add_test(NAME test_ws_dto_parser COMMAND test_ws_dto_parser)

add_executable(test_ws_integration
    test_ws_integration.cpp
)

target_link_libraries(test_ws_integration
    PRIVATE
        dfh_node
)

add_test(NAME test_ws_integration COMMAND test_ws_integration)

add_executable(test_ws_runtime_components
    test_ws_runtime_components.cpp
)

target_link_libraries(test_ws_runtime_components
    PRIVATE
        dfh_node
)

add_test(NAME test_ws_runtime_components COMMAND test_ws_runtime_components)

add_executable(test_disk_low
    test_disk_low.cpp
)

target_link_libraries(test_disk_low
    PRIVATE
        dfh_node
)

add_test(NAME test_disk_low COMMAND test_disk_low)
