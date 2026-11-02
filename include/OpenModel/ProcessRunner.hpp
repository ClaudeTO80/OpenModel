#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * @file ProcessRunner.hpp
 * @brief Run an external solver in the background, collecting its output line by line.
 *
 * POSIX implementation (fork/exec in its own process group, merged stdout+stderr through a pipe, a reader
 * thread). On Windows start() reports an error: that path is not implemented.
 */

namespace OpenModel {

enum class ProcessState { Idle, Running, Finished, Failed, Cancelled };

/** @brief Snapshot returned by ProcessRunner::poll(). */
struct ProcessStatus {
    ProcessState state = ProcessState::Idle;
    int exitCode = -1;                 ///< Valid once Finished/Failed (a signal death is reported as 128 + signal).
    std::vector<std::string> newLines; ///< Output lines produced since the previous poll.
    std::size_t totalLines = 0;
    double elapsedSeconds = 0.0;
    std::string error;                 ///< Why start() failed or why the process failed to launch.
};

class ProcessRunner {
public:
    ProcessRunner();
    ~ProcessRunner();
    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;

    /**
     * @brief Start `argv[0]` with the arguments in `workDir`. Fails if a process is already running.
     * @return empty string on success, otherwise the error message.
     */
    std::string start(const std::vector<std::string>& argv, const std::filesystem::path& workDir);

    /** @brief Return the lines produced since the last call and the current state. */
    ProcessStatus poll();

    /** @brief Ask the process group to terminate (SIGTERM, then SIGKILL after `graceSeconds`). Returns immediately. */
    void cancel(double graceSeconds = 3.0);

    /** @brief Block until the process is over (used by the CLI and tests). */
    void wait();

    bool running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace OpenModel
