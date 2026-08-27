// Test file: public runtime error/abort helpers (trust/assert.hpp).
//   - trust::formatMessage - форматирование диагностического сообщения.
//   - trust__abort__ - печать в errs() и завершение через SIGABRT (в дочернем
//     процессе, с захватом stderr через pipe).
//   - секция "trust/assert.hpp" встроена в trust-runtime.so/.a.

#include "trust/assert.hpp"
#include "utils/elf.hpp"

#include <gtest/gtest.h>

#include <csignal>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// -- formatMessage -------------------------------------------

TEST(FormatMessageTest, PrefixBasenameAndArgs) {
    auto s = trust::formatMessage("/a/b/file.cpp", 42, "val={}", 7);
    EXPECT_EQ(s, "file.cpp:42: val=7");
}

TEST(FormatMessageTest, NoDirectory) {
    auto s = trust::formatMessage("file.cpp", 3, "msg");
    EXPECT_EQ(s, "file.cpp:3: msg");
}

TEST(FormatMessageTest, MultipleArgs) {
    auto s = trust::formatMessage("x", 1, "{} {} {}", "a", 2, "c");
    EXPECT_EQ(s, "x:1: a 2 c");
}

// -- trust__abort__ (дочерний процесс) ----------------------

// Запускает trust__abort__ в дочернем процессе, захватывая stderr через pipe.
// Возвращает raw wait-статус; текст stderr - в capturedErr.
int runAbort(const std::string& msg, bool trace, std::string& capturedErr) {
    int fds[2];
    if (::pipe(fds) != 0) {
        return -1;
    }
    pid_t pid = ::fork();
    if (pid == 0) {
        // Дочерний: перенаправляем stderr в pipe и вызываем trust__abort__.
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        trust::trust__abort__("error_test.cpp", 0, msg, trace);
        ::_exit(0); // не достижимо (trust__abort__ завершает через std::_Exit)
    }
    ::close(fds[1]);
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fds[0], buf, sizeof(buf))) > 0) {
        capturedErr.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fds[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return status;
}

TEST(AbortTest, PrintsMessageAndAborts) {
    std::string err;
    int status = runAbort("boom", false, err);
    // trust__abort__ завершается graceful (std::_Exit(EXIT_FAILURE)) БЕЗ SIGABRT/core-dump;
    // сообщение печатается в stderr, код возврата ненулевой.
    EXPECT_TRUE(WIFEXITED(status)) << "expected normally-exited child";
    EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE);
    EXPECT_NE(err.find("boom"), std::string::npos);
}

TEST(AbortTest, TraceAddsBacktrace) {
    std::string err;
    int status = runAbort("boom", true, err);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE);
    EXPECT_NE(err.find("boom"), std::string::npos);
    EXPECT_NE(err.find('#'), std::string::npos) << "backtrace should contain frames";
}

// -- Секция "trust/assert.hpp" в рантайм-библиотеке ---------

TEST(RuntimeHeaderTest, AssertHeaderEmbeddedInSharedAndStatic) {
    auto so = trust::utils::readSectionFromLibrary(TRUST_RUNTIME_SHARED_PATH, "trust/assert.hpp");
    ASSERT_TRUE(so.has_value());
    auto a = trust::utils::readSectionFromLibrary(TRUST_RUNTIME_STATIC_PATH, "trust/assert.hpp");
    ASSERT_TRUE(a.has_value());

    const std::string so_str(so->begin(), so->end());
    const std::string a_str(a->begin(), a->end());
    EXPECT_NE(so_str.find("#pragma once"), std::string::npos);
    EXPECT_NE(so_str.find("trust__abort__"), std::string::npos);
    EXPECT_NE(a_str.find("trust::formatMessage"), std::string::npos);
}

} // namespace
