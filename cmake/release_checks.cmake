# cmake/release_checks.cmake
# Release-consistency gate (configuration time).
#
# A release build (CMAKE_BUILD_TYPE=Release) embeds the CLEAN release number read from VERSION
# (TRUST_VERSION = TRUST_VERSION_SHORT - no git hash - see cmake/version.cmake). To avoid shipping
# a release whose number does not match the source it was built from, a release build is allowed
# only from a git state that exactly corresponds to that release:
#
#   1. branch     on a release branch named
#                   release/<MAJOR>.<MINOR>.X   (MAJOR.MINOR == VERSION), or
#                   release/<MAJOR>.X           (MAJOR == VERSION),
#                OR detached HEAD (checked out exactly at the release tag).
#   2. tag        HEAD is exactly on the ANNOTATED release tag v<X.Y.Z> named by VERSION
#                 (git describe --exact-match --tags HEAD == v<X.Y.Z>, and the tag is annotated).
#   3. clean      git status --porcelain is empty (no uncommitted / staged / untracked changes).
#   4. changelog  CHANGELOG.md carries a '[Release v<X.Y.Z>]' entry equal to VERSION.
#
# Any other git state is a CONFIGURATION ERROR. Every error message ends with a concrete
# 'git ...' command that fixes the situation.
# If git is unavailable (not a git checkout) a release build is also rejected.
#
# Usage (root CMakeLists.txt, after include(version)):
#     include(release_checks)
#     if(TRUST_BUILD_MODE STREQUAL "release")
#         trust_check_release_consistency()
#     endif()

include(clean_tree)

function(trust_check_release_consistency)
    set(_src_dir "${CMAKE_CURRENT_SOURCE_DIR}")

    if(NOT PROJECT_VERSION)
        message(FATAL_ERROR
            "trust_check_release_consistency: PROJECT_VERSION is empty - cannot verify the release state")
    endif()
    set(_tag "v${PROJECT_VERSION}")
    set(_major "${TRUST_VERSION_MAJOR}")
    set(_minor "${TRUST_VERSION_MINOR}")

    find_program(_git NAMES git)
    if(NOT _git)
        message(FATAL_ERROR
            "Release build configuration error: 'git' was not found.\n"
            "  VERSION (${_src_dir}/VERSION) = ${PROJECT_VERSION}\n"
            "  A release build must verify the branch / tag / clean-tree, which requires git.\n"
            "Fix:\n"
            "    install git, or configure from inside a git checkout of the repository.\n"
            "  Use a dev build (CMAKE_BUILD_TYPE=Debug / unset) for non-release work.")
    endif()

    # ---- 1. Branch ------------------------------------------------------------
    execute_process(
        COMMAND ${_git} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${_src_dir}"
        OUTPUT_VARIABLE _branch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _brc
    )
    if(NOT _brc EQUAL 0 OR NOT _branch)
        message(FATAL_ERROR
            "Release build configuration error: could not determine the current branch in\n"
            "  ${_src_dir} (git rev-parse --abbrev-ref HEAD failed).\n"
            "Fix:\n"
            "    check that this is a git checkout, then configure again.")
    endif()

    set(_branch_ok FALSE)
    set(_branch_reason "")
    if(_branch STREQUAL "HEAD")
        # Detached HEAD - allowed only when the exact release tag sits at HEAD (checked below).
        set(_branch_ok TRUE)
        set(_branch_reason "detached HEAD (exact release tag checked below)")
    elseif(_branch MATCHES "^release/[0-9]+\\.[0-9]+\\.[Xx]$")
        string(REGEX REPLACE "^release/([0-9]+\\.[0-9]+)\\.[Xx]$" "\\1" _line "${_branch}")
        if("${_line}" STREQUAL "${_major}.${_minor}")
            set(_branch_ok TRUE)
            set(_branch_reason "on release branch '${_branch}' (${_line} == ${_major}.${_minor})")
        else()
            set(_branch_reason "current branch '${_branch}' is the version line ${_line}, but VERSION=${PROJECT_VERSION} requires ${_major}.${_minor}")
        endif()
    elseif(_branch MATCHES "^release/[0-9]+\\.[Xx]$")
        string(REGEX REPLACE "^release/([0-9]+)\\.[Xx]$" "\\1" _maj "${_branch}")
        if("${_maj}" STREQUAL "${_major}")
            set(_branch_ok TRUE)
            set(_branch_reason "on release branch '${_branch}' (major ${_maj} == ${_major})")
        else()
            set(_branch_reason "current branch '${_branch}' is the major line ${_maj}, but VERSION=${PROJECT_VERSION} requires major ${_major}")
        endif()
    else()
        set(_branch_reason "current branch '${_branch}' is not a release branch")
    endif()

    if(NOT _branch_ok)
        message(FATAL_ERROR
            "Release build configuration error: ${_branch_reason}.\n"
            "  VERSION (from ${_src_dir}/VERSION) = ${PROJECT_VERSION}\n"
            "  A release build requires a release branch whose number matches VERSION:\n"
            "    release/${_major}.${_minor}.X   (matches MAJOR.MINOR == ${_major}.${_minor})\n"
            "    release/${_major}.X             (matches MAJOR == ${_major})\n"
            "  or a detached checkout of the annotated release tag ${_tag}.\n"
            "Fix:\n"
            "    git switch release/${_major}.${_minor}.X\n"
            "    # ... or create it if missing:   git switch -c release/${_major}.${_minor}.X\n"
            "    # ... or release the whole major line: release/${_major}.X\n"
            "    # ... or (detached at the exact release tag): git switch --detach ${_tag}\n"
            "  Use a dev build (CMAKE_BUILD_TYPE=Debug / unset) for non-release work.")
    endif()

    # ---- 2. Exact + annotated release tag at HEAD -----------------------------
    execute_process(
        COMMAND ${_git} describe --exact-match --tags HEAD
        WORKING_DIRECTORY "${_src_dir}"
        OUTPUT_VARIABLE _exact
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _erc
    )
    if(NOT _erc EQUAL 0 OR NOT "${_exact}" STREQUAL "${_tag}")
        message(FATAL_ERROR
            "Release build configuration error: HEAD is not exactly on the release tag ${_tag}.\n"
            "  VERSION = ${PROJECT_VERSION}; expected HEAD == the annotated tag ${_tag}.\n"
            "  git describe --exact-match --tags HEAD = '${_exact}' (rc=${_erc}).\n"
            "  Current state: ${_branch_reason}.\n"
            "Fix:\n"
            "    git switch --detach ${_tag}\n"
            "    # if the tag does not exist yet, create the annotated release tag at HEAD first:\n"
            "    git tag -a ${_tag} -m \"Release ${PROJECT_VERSION}\"\n"
            "    git switch --detach ${_tag}")
    endif()

    execute_process(
        COMMAND ${_git} cat-file -t refs/tags/${_tag}
        WORKING_DIRECTORY "${_src_dir}"
        OUTPUT_VARIABLE _ttype
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _trc
    )
    if(NOT _trc EQUAL 0 OR NOT "${_ttype}" STREQUAL "tag")
        message(FATAL_ERROR
            "Release build configuration error: tag ${_tag} exists but is not annotated (type '${_ttype}').\n"
            "  A release tag must be an annotated tag object (carries author/message and is the\n"
            "  immutable release point), not a lightweight pointer to a commit.\n"
            "Fix:\n"
            "    git tag -d ${_tag}\n"
            "    git tag -a ${_tag} -m \"Release ${PROJECT_VERSION}\"\n"
            "    # if the lightweight tag was already pushed:  git push --force origin ${_tag}")
    endif()

    # ---- 3. Clean tree (uncommitted / staged / untracked) ---------------------
    # Strict clean-tree check - single implementation shared with require_clean_tree.cmake
    # (see cmake/clean_tree.cmake). A release build must come from a fully committed tree.
    trust_check_clean_tree("${_src_dir}" "error")

    # ---- 4. CHANGELOG is in sync with VERSION ---------------------------------
    file(READ "${_src_dir}/CHANGELOG.md" _changelog LIMIT 4096 OFFSET 0)
    string(FIND "${_changelog}" "[Release ${_tag}]" _cl_idx)
    if(_cl_idx EQUAL -1)
        message(FATAL_ERROR
            "Release build configuration error: CHANGELOG.md does not announce this release.\n"
            "  VERSION = ${PROJECT_VERSION}, but the top of CHANGELOG.md has no '[Release ${_tag}]' entry.\n"
            "  Every release must be documented in CHANGELOG.md before it is built.\n"
            "Fix:\n"
            "    add a top entry to CHANGELOG.md:\n"
            "      ## [Release ${_tag}](https://github.com/afteri/trust-lang/releases/tag/${_tag})\n"
            "    then commit it:\n"
            "      git add CHANGELOG.md && git commit -m \"Release ${PROJECT_VERSION}\"")
    endif()

    message(STATUS "Release consistency OK: ${_branch_reason}; exact annotated tag ${_tag} at HEAD; clean tree; CHANGELOG in sync (VERSION=${PROJECT_VERSION})")
endfunction()
