# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Third-party dependencies, pinned by version AND by SHA-256 (ADR 0003, ADR 0009).
#
# Archives are downloaded once into TEKNO_DEPS_CACHE and shared by every build directory, so a
# new preset or a clean build does not download anything again and works offline afterwards.
# To use a local checkout instead (e.g. to debug JUCE), configure with
#     -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE

include(FetchContent)

set(TEKNO_DEPS_CACHE "${PROJECT_SOURCE_DIR}/.cache/deps" CACHE PATH
    "Directory where downloaded dependency archives are kept (shared by all build dirs)")

# Bumping any of these is a decision: record it as an ADR first.
set(TEKNO_JUCE_VERSION   "9.0.5")
set(TEKNO_JUCE_SHA256    "d92207f51ba728d3d195b6d5e93ecd049717a915fc250ff6844c32ea7272315a")
set(TEKNO_CATCH2_VERSION "3.16.1")
set(TEKNO_CATCH2_SHA256  "d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d")

function(tekno_declare_archive name version url sha256)
    set(archive "${TEKNO_DEPS_CACHE}/${name}-${version}.tar.gz")

    if(NOT EXISTS "${archive}")
        message(STATUS "tekno: downloading ${name} ${version} (once, into ${TEKNO_DEPS_CACHE})")
        file(DOWNLOAD "${url}" "${archive}.part"
            EXPECTED_HASH SHA256=${sha256}
            TLS_VERIFY ON
            STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${archive}.part")
            list(GET status 1 reason)
            message(FATAL_ERROR "tekno: download of ${name} failed: ${reason}")
        endif()
        file(RENAME "${archive}.part" "${archive}")
    endif()

    FetchContent_Declare(${name}
        URL "${archive}"
        URL_HASH SHA256=${sha256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endfunction()

# --- JUCE ---------------------------------------------------------------------------------------
tekno_declare_archive(JUCE "${TEKNO_JUCE_VERSION}"
    "https://github.com/juce-framework/JUCE/archive/refs/tags/${TEKNO_JUCE_VERSION}.tar.gz"
    "${TEKNO_JUCE_SHA256}")

set(JUCE_BUILD_EXTRAS   OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JUCE)

# --- Catch2 (tests only) ------------------------------------------------------------------------
if(TEKNO_BUILD_TESTS)
    tekno_declare_archive(Catch2 "${TEKNO_CATCH2_VERSION}"
        "https://github.com/catchorg/Catch2/archive/refs/tags/v${TEKNO_CATCH2_VERSION}.tar.gz"
        "${TEKNO_CATCH2_SHA256}")
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
