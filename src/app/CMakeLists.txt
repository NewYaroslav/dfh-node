# CMake-описание исполняемого приложения dfh_node_app.
# Линкуется с библиотекой dfh_node.
add_executable(dfh_node_app
    main.cpp
)

dfh_node_apply_warnings(dfh_node_app)

target_link_libraries(dfh_node_app PRIVATE dfh_node)
