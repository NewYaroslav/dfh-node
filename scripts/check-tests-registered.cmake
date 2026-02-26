# Проверка консистентности тестов:
# 1) каждый tests/test_*.cpp должен быть упомянут в add_executable(...);
# 2) для соответствующего target должен быть add_test(NAME <target> COMMAND <target>).

if(NOT DEFINED REPO_ROOT)
    message(FATAL_ERROR "Не задан параметр REPO_ROOT")
endif()

set(TESTS_DIR "${REPO_ROOT}/tests")
set(CMAKE_LISTS_PATH "${TESTS_DIR}/CMakeLists.txt")

if(NOT EXISTS "${CMAKE_LISTS_PATH}")
    message(FATAL_ERROR "Не найден файл: ${CMAKE_LISTS_PATH}")
endif()

file(READ "${CMAKE_LISTS_PATH}" CMAKE_TEXT)

# Извлекаем все блоки add_executable(...).
string(REGEX MATCHALL "add_executable[ \t\r\n]*\\([^\\)]*\\)" EXEC_BLOCKS "${CMAKE_TEXT}")

set(SOURCE_TO_TARGETS "")
foreach(BLOCK IN LISTS EXEC_BLOCKS)
    string(REGEX REPLACE ".*add_executable[ \t\r\n]*\\([ \t\r\n]*([A-Za-z0-9_\\-]+).*" "\\1" TARGET "${BLOCK}")
    string(REGEX MATCHALL "([A-Za-z0-9_./\\-]+\\.cpp)" SOURCES "${BLOCK}")

    foreach(SRC IN LISTS SOURCES)
        string(REPLACE "\\" "/" SRC_NORM "${SRC}")
        list(APPEND SOURCE_TO_TARGETS "${SRC_NORM}|${TARGET}")
    endforeach()
endforeach()

# Извлекаем add_test(NAME X COMMAND Y), берем только X==Y.
string(REGEX MATCHALL "add_test[ \t\r\n]*\\([ \t\r\n]*NAME[ \t\r\n]+([A-Za-z0-9_\\-]+)[ \t\r\n]+COMMAND[ \t\r\n]+([A-Za-z0-9_\\-]+)[^\\)]*\\)" TEST_BLOCKS "${CMAKE_TEXT}")

set(TARGETS_WITH_ADD_TEST "")
foreach(BLOCK IN LISTS TEST_BLOCKS)
    string(REGEX REPLACE ".*NAME[ \t\r\n]+([A-Za-z0-9_\\-]+)[ \t\r\n]+COMMAND[ \t\r\n]+([A-Za-z0-9_\\-]+).*" "\\1;\\2" NAME_AND_CMD "${BLOCK}")
    list(GET NAME_AND_CMD 0 TEST_NAME)
    list(GET NAME_AND_CMD 1 TEST_CMD)
    if(TEST_NAME STREQUAL TEST_CMD)
        list(APPEND TARGETS_WITH_ADD_TEST "${TEST_NAME}")
    endif()
endforeach()
list(REMOVE_DUPLICATES TARGETS_WITH_ADD_TEST)

file(GLOB TEST_CPP_FILES "${TESTS_DIR}/test_*.cpp")

set(MISSING_EXECUTABLE "")
set(MISSING_ADD_TEST "")

foreach(TEST_FILE IN LISTS TEST_CPP_FILES)
    get_filename_component(TEST_NAME "${TEST_FILE}" NAME)
    set(REL_SRC "tests/${TEST_NAME}")
    set(LOCAL_SRC "${TEST_NAME}")

    set(CANDIDATE_TARGETS "")
    foreach(PAIR IN LISTS SOURCE_TO_TARGETS)
        string(REPLACE "|" ";" PARTS "${PAIR}")
        list(GET PARTS 0 SRC_PART)
        list(GET PARTS 1 TARGET_PART)

        if(SRC_PART STREQUAL "${REL_SRC}" OR SRC_PART STREQUAL "${LOCAL_SRC}")
            list(APPEND CANDIDATE_TARGETS "${TARGET_PART}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CANDIDATE_TARGETS)

    if(CANDIDATE_TARGETS STREQUAL "")
        list(APPEND MISSING_EXECUTABLE "${TEST_NAME}")
        continue()
    endif()

    set(HAS_ADD_TEST FALSE)
    foreach(CANDIDATE_TARGET IN LISTS CANDIDATE_TARGETS)
        if("${CANDIDATE_TARGET}" IN_LIST TARGETS_WITH_ADD_TEST)
            set(HAS_ADD_TEST TRUE)
            break()
        endif()
    endforeach()

    if(NOT HAS_ADD_TEST)
        string(JOIN ", " TARGETS_CSV ${CANDIDATE_TARGETS})
        list(APPEND MISSING_ADD_TEST "${TEST_NAME} -> targets: ${TARGETS_CSV}")
    endif()
endforeach()

if(NOT MISSING_EXECUTABLE STREQUAL "" OR NOT MISSING_ADD_TEST STREQUAL "")
    if(NOT MISSING_EXECUTABLE STREQUAL "")
        message(STATUS "Не зарегистрированы в add_executable:")
        foreach(ITEM IN LISTS MISSING_EXECUTABLE)
            message(STATUS "  - ${ITEM}")
        endforeach()
    endif()
    if(NOT MISSING_ADD_TEST STREQUAL "")
        message(STATUS "Нет add_test(NAME <target> COMMAND <target>):")
        foreach(ITEM IN LISTS MISSING_ADD_TEST)
            message(STATUS "  - ${ITEM}")
        endforeach()
    endif()
    message(FATAL_ERROR "Проверка регистрации тестов не пройдена.")
endif()

message(STATUS "Проверка регистрации тестов пройдена: все tests/test_*.cpp подключены и зарегистрированы в CTest.")
