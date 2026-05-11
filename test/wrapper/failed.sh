#!/bin/bash
# Wrapper for test targets that prints [FAILED] in bold red on failure
# Usage: failed.sh <target_name> <command...>

esc=$(printf '\033')
RED="${esc}[1;31m"
RESET="${esc}[0m"

TARGET="$1"
shift

if ! "$@"; then
    echo "${RED}[FAILED]${RESET} target: ${TARGET}"
    exit 1
fi