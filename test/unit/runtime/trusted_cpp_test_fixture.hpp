#ifndef TRUSTED_CPP_TEST_FIXTURE_HPP
#define TRUSTED_CPP_TEST_FIXTURE_HPP
// Shared fixture for trusted-cpp runtime tests (trusted_cpp_test.cpp etc.).
// Includes BOTH the plain reference header (Shared/Weak/Locker, always used) and the sync header
// (SyncShared<V, Mutex>, used only when multi-threaded synchronization is required).
#include "trust/trusted-cpp.hpp"
#include "trust/trusted-cpp-sync.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Fixture intentionally empty; tests exercise Locker/Shared/Weak/SyncShared types directly.
class TrustedCppTest : public ::testing::Test {};

#endif // TRUSTED_CPP_TEST_FIXTURE_HPP
