add_library(dfh_node STATIC
    config.cpp
    config_loader.cpp
    config_validator.cpp
    status.cpp
    version.cpp
)

dfh_node_apply_warnings(dfh_node)

target_include_directories(dfh_node
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(dfh_node
    PUBLIC
        nlohmann_json::nlohmann_json
        log-it-cpp
)
