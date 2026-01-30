add_executable(dfh_node_smoke
  test_smoke.cpp
)

target_link_libraries(dfh_node_smoke
  PRIVATE
    dfh_node
)

add_test(NAME dfh_node_smoke COMMAND dfh_node_smoke)

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
