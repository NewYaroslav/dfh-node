# Проверка стиля комментариев в C/C++ исходниках проекта.
# Запрещает блочный стиль /* ... */; допускаются только // и ///.

if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is not set")
endif()

set(_scan_dirs
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/tests"
)

set(_violations "")

foreach(_dir IN LISTS _scan_dirs)
    if(NOT EXISTS "${_dir}")
        continue()
    endif()

    file(GLOB_RECURSE _files
        "${_dir}/*.hpp"
        "${_dir}/*.cpp"
        "${_dir}/*.h"
        "${_dir}/*.cc"
        "${_dir}/*.cxx"
    )

    foreach(_file IN LISTS _files)
        file(READ "${_file}" _content)
        string(FIND "${_content}" "/*" _block_pos)
        if(NOT _block_pos EQUAL -1)
            list(APPEND _violations "${_file}")
        endif()
    endforeach()
endforeach()

if(_violations)
    list(REMOVE_DUPLICATES _violations)
    string(JOIN "\n  - " _joined ${_violations})
    message(FATAL_ERROR
        "Обнаружены запрещенные блочные комментарии в стиле '/* ... */'.\n"
        "Используйте только '//' и '///'.\n"
        "Файлы:\n"
        "  - ${_joined}\n"
    )
endif()

message(STATUS "Проверка стиля комментариев пройдена: используются только '//' и '///'.")
