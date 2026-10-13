// -----------------------------------------------------------------------
// Unit tests for GDB/MI transport lifecycle (debug/mi/mi_transport.hpp)
// Process-level behavior (real gdb) is covered by the gdb_main integration test.
// -----------------------------------------------------------------------

#include "debug/mi/mi_transport.hpp"

#include <gtest/gtest.h>

namespace {

using trust::debug::detail::MiTransport;

TEST(MiTransportTest, HasNoStdoutBeforeStart) {
    MiTransport transport;
    EXPECT_EQ(transport.stdoutFd(), -1);
}

TEST(MiTransportTest, SendCommandFailsBeforeStart) {
    MiTransport transport;
    EXPECT_EQ(transport.sendCommand("-exec-run"), -1);
}

TEST(MiTransportTest, ReadLineTimesOutBeforeStart) {
    MiTransport transport;
    EXPECT_TRUE(transport.readLine(1).empty());
}

TEST(MiTransportTest, CloseIsIdempotent) {
    MiTransport transport;
    transport.close();
    transport.close();
    EXPECT_EQ(transport.stdoutFd(), -1);
}

} // namespace
