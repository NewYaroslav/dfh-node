add_library(dfh_node STATIC
  version.cpp
)

target_include_directories(dfh_node
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
