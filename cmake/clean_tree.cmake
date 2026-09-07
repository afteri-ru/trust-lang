# cmake/clean_tree.cmake
# Shared clean-tree gate. Single implementation of the strict "clean working tree" check
# (git status --porcelain - catches uncommitted, staged and untracked changes) used in two
# places so the porcelain logic is not duplicated:
#   - at configure time for a release build : cmake/release_checks.cmake  (severity "error")
#   - at build time for artifact targets     : cmake/require_clean_tree.cmake.in (severity by mode)
#
# Defines: trust_check_clean_tree(<src_dir> <severity>)
#   severity "error"   -> a dirty tree is a FATAL_ERROR (release build / release artifacts),
#                          the message ends with a `git ...` fix command.
#   severity "warning" -> a dirty tree is a WARNING (artifacts force-enabled on a dev build).

function(trust_check_clean_tree src_dir severity)
    find_program(_trust_git NAMES git)
    if(NOT _trust_git)
        message(FATAL_ERROR
            "Clean-tree check: 'git' not found. Verifying a clean/committed tree needs git in\n"
            "  ${src_dir}.\n"
            "Fix:\n"
            "    install git, or run from inside a git checkout of the repository.")
    endif()

    execute_process(
        COMMAND ${_trust_git} status --porcelain
        WORKING_DIRECTORY "${src_dir}"
        OUTPUT_VARIABLE _trust_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE _trust_git_err
        RESULT_VARIABLE _trust_rc
    )
    if(NOT _trust_rc EQUAL 0)
        message(FATAL_ERROR
            "Clean-tree check: 'git status --porcelain' failed in ${src_dir} (not a git checkout?).\n"
            "${_trust_git_err}")
    endif()
    if(_trust_dirty STREQUAL "")
        return()
    endif()

    set(_detail "  Working tree: ${src_dir}\n  Uncommitted changes (incl. staged/untracked):\n  ${_trust_dirty}")
    if(severity STREQUAL "error")
        message(FATAL_ERROR
            "The working tree has uncommitted changes - a release build/artifact must come from a\n"
            "fully committed state (no uncommitted / staged / untracked changes).\n"
            "${_detail}\n"
            "Fix:\n"
            "    git add <path> && git commit -m \"...\"   # commit the changes\n"
            "    # or stash them temporarily and re-run:   git stash push")
    else()
        message(WARNING
            "Building an artifact target with uncommitted changes (dev build).\n"
            "${_detail}\n"
            "The produced artifact will not correspond to a single committed state.")
    endif()
endfunction()
