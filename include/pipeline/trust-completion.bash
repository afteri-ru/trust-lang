#!/usr/bin/env bash
# include/pipeline/trust-completion.bash
# Bash completion for the `trust` CLI.
#
# Usage: source this file (e.g. in ~/.bashrc) to enable tab-completion:
#   . /path/to/trust-lang/include/pipeline/trust-completion.bash
#
# When you type the first characters of an option and press <Tab>, bash either
# completes the option or (if several match) lists the possible variants.
#
# Everything is data-driven from the driver option table (no hardcoded option
# list here):
#   - `trust --complete-options` - all option names + -W diagnostics;
#   - `trust --complete-files`   - options whose value is a file/path.
# Both lists are fetched once per resolved binary and cached, so they never
# drift from the CLI.
#
# The binary is resolved by probing candidates in order: $TRUST_BIN, a `./trust`
# in the current directory, a `_build/trust` relative to this script, then a PATH
# entry named `trust`. A candidate is accepted only if it answers
# `--complete-options` AND reports itself via `--version` with the project brand,
# i.e. "TrustLang <version>". This uniquely identifies our toolchain (the binary
# name `trust` itself collides with p11-kit's `trust`), rejecting any foreign
# `trust` on PATH and any other binary that happens to support `--complete-options`.
# Completion therefore works from a build directory without putting it in PATH,
# and from the repository by sourcing this script.

_trust() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD-1]}"
    local bin="" opts="" ver="" found="" c
    local -a candidates=()
    local src sdir
    src="${BASH_SOURCE[0]}"
    sdir="$(cd "$(dirname "$src")" 2>/dev/null && pwd)"

    # Ordered candidates for the `trust` binary:
    #   1. explicit $TRUST_BIN;
    #   2. a `./trust` in the current directory (no PATH setup needed);
    #   3. a `./_build/trust` in the current directory;
    #   4. a `_build/trust` relative to this script (dev/repo layout);
    #   5. a PATH entry named `trust`.
    # Many systems ship p11-kit's `trust` on PATH, so we never trust `command -v`
    # alone: we probe each candidate and use the first that actually answers
    # `--complete-options` and identifies as TrustLang.
    [[ -n "${TRUST_BIN:-}" ]] && candidates+=("$TRUST_BIN")
    [[ -f ./trust && -x ./trust ]] && candidates+=("./trust")
    [[ -f ./_build/trust && -x ./_build/trust ]] && candidates+=("./_build/trust")
    [[ -n "$sdir" && -f "$sdir/../_build/trust" && -x "$sdir/../_build/trust" ]] \
        && candidates+=("$sdir/../_build/trust")
    if command -v trust >/dev/null 2>&1; then
        candidates+=("trust")
    fi
    # Final fallback so the cache key stays defined even if nothing matches.
    if ((${#candidates[@]} == 0)); then
        candidates+=("trust")
    fi

    # Find a working binary: reuse the cache only if it still names a valid
    # candidate (opts non-empty), otherwise probe the candidates. Only a binary
    # that answers `--complete-options` AND identifies itself via `--version`
    # with the project brand ("TrustLang <version>") is accepted - this rejects
    # any foreign `trust` on PATH (e.g. p11-kit's) and any other binary that
    # merely supports `--complete-options`.
    for c in "${candidates[@]}"; do
        if [[ -n "$_TRUST_OPTS" && "$c" == "$_TRUST_BIN" ]]; then
            found="$c"
            break
        fi
    done
    if [[ -z "$found" ]]; then
        for c in "${candidates[@]}"; do
            opts="$( "$c" --complete-options 2>/dev/null )"
            [[ -n "$opts" ]] || continue
            ver="$( "$c" --version 2>/dev/null )"
            [[ "$ver" == TrustLang\ * ]] || continue
            found="$c"
            break
        done
    fi

    if [[ -n "$found" ]]; then
        if [[ "$found" != "$_TRUST_BIN" || -z "$_TRUST_OPTS" ]]; then
            _TRUST_BIN="$found"
            _TRUST_OPTS="$opts"
            _TRUST_FILES="$( "$found" --complete-files 2>/dev/null )"
        fi
        bin="$found"
    else
        # No valid binary found: clear the cache so the next completion retries
        # (a stale "failed" cache would otherwise stay broken forever).
        _TRUST_BIN=""
        _TRUST_OPTS=""
        _TRUST_FILES=""
        bin=""
    fi

    # If the previous word is a file/path-valued option, complete file names.
    # _TRUST_FILES is newline-separated, so test membership token-by-token.
    if [[ -n "$prev" ]]; then
        local f
        for f in $_TRUST_FILES; do
            if [[ "$f" == "$prev" ]]; then
                COMPREPLY=( $(compgen -f -- "$cur") )
                compopt -o filenames 2>/dev/null
                return
            fi
        done
    fi

    # Options start with '-': complete against the full option/diagnostic list.
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$_TRUST_OPTS" -- "$cur") )
        return
    fi

    # Otherwise complete file names (the input source file).
    COMPREPLY=( $(compgen -f -- "$cur") )
    compopt -o filenames 2>/dev/null
}

complete -F _trust trust
