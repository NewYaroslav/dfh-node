add_library(dfh_node STATIC
  version.cpp
)

dfh_node_apply_warnings(dfh_node)

target_include_directories(dfh_node
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
