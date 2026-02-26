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

add_test(NAME app_no_args COMMAND $<TARGET_FILE:dfh_node_app>)
set_tests_properties(app_no_args PROPERTIES WILL_FAIL TRUE)

add_test(NAME app_valid_config
    COMMAND $<TARGET_FILE:dfh_node_app> --config ${CMAKE_BINARY_DIR}/test_configs/config_minimal.json
)

add_test(NAME app_missing_config COMMAND $<TARGET_FILE:dfh_node_app> --config missing.json)
set_tests_properties(app_missing_config PROPERTIES WILL_FAIL TRUE)

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
