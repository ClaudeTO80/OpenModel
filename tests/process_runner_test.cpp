/**
 * @file process_runner_test.cpp
 * @brief Tests of OpenModel::ProcessRunner on top of TK4CPP::Process: output lines, exit codes, working directory,
 *        cancel with and without a SIGTERM handler, signal deaths, double start and a missing program.
 *
 * POSIX only: the programs are run through /bin/sh. On Windows the test does nothing and succeeds.
 */
#include <OpenModel/ProcessRunner.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK_TRUE(cond)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                \
        }                                                                                \
    } while (0)

/** Polls until the run is over or the timeout passes; every line produced is appended to `lines`. */
OpenModel::ProcessStatus collect(OpenModel::ProcessRunner& runner, std::vector<std::string>& lines,
                                 std::chrono::seconds timeout = std::chrono::seconds(20)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        OpenModel::ProcessStatus status = runner.poll();
        lines.insert(lines.end(), status.newLines.begin(), status.newLines.end());
        if (status.state != OpenModel::ProcessState::Running) return status;
        if (std::chrono::steady_clock::now() > deadline) {
            std::fprintf(stderr, "timeout waiting for the process\n");
            ++g_failures;
            runner.cancel(0.0);
            runner.wait();
            return runner.poll();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

#ifndef _WIN32

void testOutputAndExitCode() {
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "echo one; echo two >&2; printf partial; exit 3"}, {}).empty());
    std::vector<std::string> lines;
    const auto status = collect(runner, lines);
    CHECK_TRUE(status.state == OpenModel::ProcessState::Failed);
    CHECK_TRUE(status.exitCode == 3);
    // stdout and stderr are merged in the order they were written; the last line has no newline.
    CHECK_TRUE((lines == std::vector<std::string>{"one", "two", "partial"}));
    CHECK_TRUE(status.totalLines == 3);
}

void testSuccess() {
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "echo ok"}, {}).empty());
    std::vector<std::string> lines;
    const auto status = collect(runner, lines);
    CHECK_TRUE(status.state == OpenModel::ProcessState::Finished);
    CHECK_TRUE(status.exitCode == 0);
    CHECK_TRUE((lines == std::vector<std::string>{"ok"}));
    CHECK_TRUE(!runner.running());
}

void testWorkingDirectory() {
    const auto dir = std::filesystem::temp_directory_path() / "openmodel_process_runner_test";
    std::filesystem::create_directories(dir);
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "pwd -P"}, dir).empty());
    std::vector<std::string> lines;
    const auto status = collect(runner, lines);
    CHECK_TRUE(status.state == OpenModel::ProcessState::Finished);
    CHECK_TRUE(lines.size() == 1);
    if (lines.size() == 1) CHECK_TRUE(std::filesystem::equivalent(lines[0], dir));
    std::filesystem::remove_all(dir);
}

void testCancelWithGrace() {
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "sleep 30"}, {}).empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runner.cancel(0.5);
    runner.wait();
    const auto status = runner.poll();
    CHECK_TRUE(status.state == OpenModel::ProcessState::Cancelled);
    CHECK_TRUE(status.elapsedSeconds < 10.0);
}

void testCancelEscalatesWhenSigtermIsIgnored() {
    // The shell ignores SIGTERM, so only the kill after the grace period can stop it.
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "trap '' TERM; sleep 30"}, {}).empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    runner.cancel(0.5);
    runner.wait();
    const auto status = runner.poll();
    CHECK_TRUE(status.state == OpenModel::ProcessState::Cancelled);
    CHECK_TRUE(status.elapsedSeconds < 10.0);
}

void testDeathBySignal() {
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "kill -9 $$"}, {}).empty());
    std::vector<std::string> lines;
    const auto status = collect(runner, lines);
    CHECK_TRUE(status.state == OpenModel::ProcessState::Failed);
    CHECK_TRUE(status.exitCode == 128 + 9);   // the convention documented in ProcessRunner.hpp
}

void testStartWhileRunning() {
    OpenModel::ProcessRunner runner;
    CHECK_TRUE(runner.start({"/bin/sh", "-c", "sleep 30"}, {}).empty());
    CHECK_TRUE(!runner.start({"/bin/sh", "-c", "echo again"}, {}).empty());
    runner.cancel(0.0);
    runner.wait();
}

void testMissingProgram() {
    OpenModel::ProcessRunner runner;
    const std::string error = runner.start({"/nonexistent/openmodel-no-such-program"}, {});
    CHECK_TRUE(!error.empty());
    CHECK_TRUE(!runner.running());
}

#endif // !_WIN32

} // namespace

int main() {
#ifdef _WIN32
    std::printf("process_runner_test: skipped on Windows (uses /bin/sh)\n");
    return 0;
#else
    testOutputAndExitCode();
    testSuccess();
    testWorkingDirectory();
    testCancelWithGrace();
    testCancelEscalatesWhenSigtermIsIgnored();
    testDeathBySignal();
    testStartWhileRunning();
    testMissingProgram();
    if (g_failures == 0) std::printf("process_runner_test: all checks passed\n");
    else std::printf("process_runner_test: %d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
#endif
}
