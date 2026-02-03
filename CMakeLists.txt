# Главный CMake-файл проекта dfh-node.
# Подключает опции и добавляет подкаталоги модулей.
# Логика сборки вынесена в cmake/*.cmake и CMakeLists.txt модулей.
cmake_minimum_required(VERSION 3.20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

project(dfh-node LANGUAGES CXX)

include(Options)
include(ProjectDefaults)
include(Warnings)
include(ThirdParty)

add_subdirectory(src/dfh_node)
add_subdirectory(src/app)

if(DFH_NODE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
