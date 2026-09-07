# cmake/version.cmake
# Version, build mode and git hash extraction.
#
# Version model (three CMake/C macros, deliberately decomposed):
#   TRUST_VERSION_SHORT = "X.Y.Z"                      (clean release number, no hash)
#   TRUST_VERSION_FULL  = "X.Y.Z-<git_hash>"           (identifying dev string)
#   TRUST_VERSION       = effective version depending on the build mode:
#                         release -> TRUST_VERSION_SHORT, dev -> TRUST_VERSION_FULL.
# The effective TRUST_VERSION is what product code, caches and artifact names embed
# (see version.h.in.cmake). TRUST_BUILD_MODE is computed here from CMAKE_BUILD_TYPE.
#
# NOTE: this module also defines TRUST_BUILD_MODE (release|dev) - the SINGLE source of truth;
# it is included in the root CMakeLists.txt BEFORE the option defaults that depend on it.
# The release-build git-state gate (branch/tag == VERSION) lives in cmake/release_checks.cmake
# and is invoked from the root CMakeLists.txt when TRUST_BUILD_MODE == "release".

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_RAW)
string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION)

string(REPLACE "." ";" VERSION_PARTS "${PROJECT_VERSION}")
list(GET VERSION_PARTS 0 TRUST_VERSION_MAJOR)
list(GET VERSION_PARTS 1 TRUST_VERSION_MINOR)
list(GET VERSION_PARTS 2 TRUST_VERSION_PATCH)

# Clean release number (short, no git hash).
set(TRUST_VERSION_SHORT "${PROJECT_VERSION}")

# -- Build mode: release vs dev --
# Release == CMAKE_BUILD_TYPE=Release (CMake convention); Debug / unset => dev.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(TRUST_BUILD_MODE "release")
else()
    set(TRUST_BUILD_MODE "dev")
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE TRUST_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE TRUST_GIT_HASH_RC
)
if(NOT TRUST_GIT_HASH_RC EQUAL 0 OR NOT TRUST_GIT_HASH)
    set(TRUST_GIT_HASH "unknown")
endif()

# Full identifying string (always carries the git hash).
set(TRUST_VERSION_FULL "${TRUST_VERSION_SHORT}-${TRUST_GIT_HASH}")

# Effective version selected by the build mode:
#   release -> clean "X.Y.Z" (no git hash),
#   dev     -> "X.Y.Z-<git_hash>" (traceable to a concrete commit).
if(TRUST_BUILD_MODE STREQUAL "release")
    set(TRUST_VERSION "${TRUST_VERSION_SHORT}")
else()
    set(TRUST_VERSION "${TRUST_VERSION_FULL}")
endif()

string(TIMESTAMP TRUST_DATE_BUILD "%Y-%m-%d")

