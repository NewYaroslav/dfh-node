add_executable(dfh_node_smoke
  test_smoke.cpp
)

target_link_libraries(dfh_node_smoke
  PRIVATE
    dfh_node
)

add_test(NAME dfh_node_smoke COMMAND dfh_node_smoke)
