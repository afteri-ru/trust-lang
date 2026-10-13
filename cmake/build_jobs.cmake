# cmake/build_jobs.cmake
# Script-режим (`cmake -P`) для политики параллелизма из cmake/parallelism.cmake.
# Пересчитывает число задач под ТЕКУЩИЙ cgroup-лимит и пишет его в OUT.
#
# Использование:
#   cmake -DOUT=<file> [-DTRUST_BUILD_JOBS=<n>] \
#         [-DTRUST_BUILD_MEMORY_RESERVE_MIB=<mib>] [-DTRUST_BUILD_MEMORY_PER_JOB_MIB=<mib>] \
#         -P cmake/build_jobs.cmake

cmake_minimum_required(VERSION 3.28)

if(NOT OUT)
    message(FATAL_ERROR "build_jobs.cmake: OUT=<file> is required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/parallelism.cmake")
trust_compute_build_jobs(_jobs)
file(WRITE "${OUT}" "${_jobs}\n")
