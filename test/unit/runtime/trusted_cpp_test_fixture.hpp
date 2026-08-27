#ifndef TRUSTED_CPP_TEST_FIXTURE_HPP
#define TRUSTED_CPP_TEST_FIXTURE_HPP
// Shared fixture for trusted-cpp runtime tests (trusted_cpp_test.cpp etc.).
#include "runtime/trusted-cpp.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

// Fixture intentionally empty; tests exercise Sync/Locker/Shared/Weak types directly.
class TrustedCppTest : public ::testing::Test {};

#endif // TRUSTED_CPP_TEST_FIXTURE_HPP
