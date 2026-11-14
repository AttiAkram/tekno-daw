# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Enforces the dependency direction of ADR 0011 (app -> ui -> core, never backwards).
# All targets share one JUCE library, so the compiler cannot stop src/core from including GUI
# headers. This script does, and runs as a CTest test:  cmake -DSOURCE_DIR=<repo> -P <this file>

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "usage: cmake -DSOURCE_DIR=<repo root> -P CheckLayering.cmake")
endif()

set(gui_modules "juce_graphics|juce_gui_basics|juce_gui_extra|juce_opengl|juce_audio_utils|juce_animation")

# layer -> regex of forbidden includes
set(rules
    "core|#include[ \t]*[<\"]((ui|app)/|(${gui_modules})/)"
    "ui|#include[ \t]*[<\"]app/")

set(violations "")
foreach(rule IN LISTS rules)
    string(FIND "${rule}" "|" sep)
    string(SUBSTRING "${rule}" 0 ${sep} layer)
    math(EXPR start "${sep} + 1")
    string(SUBSTRING "${rule}" ${start} -1 forbidden)

    file(GLOB_RECURSE files "${SOURCE_DIR}/src/${layer}/*.h" "${SOURCE_DIR}/src/${layer}/*.cpp")
    foreach(file IN LISTS files)
        file(STRINGS "${file}" lines REGEX "^[ \t]*#include")
        foreach(line IN LISTS lines)
            if(line MATCHES "${forbidden}")
                file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
                string(APPEND violations "\n  ${rel}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR "Layering violations (allowed direction: app -> ui -> core):${violations}")
endif()
message(STATUS "Layering OK")
