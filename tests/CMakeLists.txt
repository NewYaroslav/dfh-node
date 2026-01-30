add_executable(dfh_node_smoke
  test_smoke.cpp
)

target_link_libraries(dfh_node_smoke
  PRIVATE
    dfh_node
)

add_test(NAME dfh_node_smoke COMMAND dfh_node_smoke)

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
