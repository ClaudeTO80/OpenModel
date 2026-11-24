/**
 * @file ProcessRunner.cpp
 * @brief ProcessRunner implemented on top of TK4CPP::Process.
 *
 * TK4CPP::Process owns the child process, its output pipe and its process tree (process group on POSIX, Job Object
 * on Windows). This file adds what OpenModel needs on top: the output is cut into lines and kept until the page or
 * the CLI asks for it with poll(), and cancel() asks for a graceful stop and escalates to a kill after a grace
 * period given by the caller.
 */
#include <OpenModel/ProcessRunner.hpp>

#include <TK4CPP/Process.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <string_view>
#include <utility>

namespace OpenModel {
namespace {

namespace TP = TK4CPP::Process;

/**
 * @brief Time that TK4CPP::Process waits between its SIGTERM and its own SIGKILL.
 *
 * ProcessRunner::cancel() escalates to kill() itself, after the grace period the caller asked for. This value is set
 * so that TK4CPP never escalates on its own before that moment.
 */
constexpr std::chrono::hours kNoEarlyKill{24};

} // namespace

/** @brief State shared between the caller and the threads that serve one run. */
struct ProcessRunner::Impl {
    std::mutex mutex;
    std::condition_variable done;
    ProcessState state = ProcessState::Idle;
    int exitCode = -1;
    std::vector<std::string> lines;   ///< Complete output lines, in order.
    std::string partial;              ///< Output after the last newline, not yet a line.
    std::size_t delivered = 0;        ///< Lines already returned by poll().
    std::string error;
    std::chrono::steady_clock::time_point began, ended;
    bool cancelRequested = false;     ///< Guarded by mutex.
    std::unique_ptr<TP::Process> process;
    std::thread waiter;               ///< Waits for the child and publishes its exit.
    std::thread terminator;           ///< cancel(): asks the child to stop (SIGTERM).
    std::thread killer;               ///< cancel(): kills the child when the grace period has passed.

    /** @brief Adds the bytes of one stdout chunk, splitting them into lines (called by TK4CPP). */
    void append(std::string_view chunk) {
        std::lock_guard<std::mutex> lock(mutex);
        partial.append(chunk);
        std::size_t newline;
        while ((newline = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, newline);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            partial.erase(0, newline + 1);
        }
    }

    /** @brief Waits for every thread of this run. Must be called before the Process is destroyed. */
    void join() {
        for (std::thread* t : {&waiter, &terminator, &killer}) {
            if (t->joinable()) t->join();
        }
    }
};

ProcessRunner::ProcessRunner() = default;

ProcessRunner::~ProcessRunner() {
    if (!impl_) return;
    cancel(0.2);
    wait();
    impl_->join();
}

bool ProcessRunner::running() const {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state == ProcessState::Running;
}

ProcessStatus ProcessRunner::poll() {
    ProcessStatus s;
    if (!impl_) return s;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    s.state = impl_->state;
    s.exitCode = impl_->exitCode;
    s.error = impl_->error;
    s.totalLines = impl_->lines.size();
    for (std::size_t i = impl_->delivered; i < impl_->lines.size(); ++i) s.newLines.push_back(impl_->lines[i]);
    impl_->delivered = impl_->lines.size();
    const auto end = impl_->state == ProcessState::Running ? std::chrono::steady_clock::now() : impl_->ended;
    s.elapsedSeconds = std::chrono::duration<double>(end - impl_->began).count();
    return s;
}

void ProcessRunner::wait() {
    if (!impl_) return;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->done.wait(lock, [this] { return impl_->state != ProcessState::Running; });
}

std::string ProcessRunner::start(const std::vector<std::string>& argv, const std::filesystem::path& workDir) {
    if (argv.empty() || argv[0].empty()) return "No program to run";
    if (impl_) {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->state == ProcessState::Running) return "A process is already running";
        }
        impl_->join();
    }
    impl_ = std::make_unique<Impl>();
    Impl* d = impl_.get();

    TP::Options options;
    options.merge_stderr_to_stdout = true;   // one stream, in the order the program wrote it
    options.working_directory = workDir.string();
    options.terminate_grace = kNoEarlyKill;
    options.on_stdout = [d](std::string_view chunk) { d->append(chunk); };

    std::vector<std::string> arguments(argv.begin() + 1, argv.end());
    d->process = std::make_unique<TP::Process>(argv[0], std::move(arguments), std::move(options));
    if (const auto started = d->process->start(); !started) {
        std::string message = "Cannot start '" + argv[0] + "': " + started.error().message;
        impl_.reset();
        return message;
    }

    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->state = ProcessState::Running;
        d->began = std::chrono::steady_clock::now();
    }
    d->waiter = std::thread([d] {
        const auto result = d->process->wait();   // returns once the child has exited and its output is drained
        std::lock_guard<std::mutex> lock(d->mutex);
        if (!d->partial.empty()) {
            d->lines.push_back(std::move(d->partial));
            d->partial.clear();
        }
        d->ended = std::chrono::steady_clock::now();
        if (!result) {
            d->error = result.error().message;
            d->exitCode = -1;
            d->state = d->cancelRequested ? ProcessState::Cancelled : ProcessState::Failed;
        } else {
            const TP::ExitStatus& status = *result;
            // A death by signal is reported as 128 + signal, the shell convention, as documented in the header.
            if (status.kind == TP::ExitStatus::Kind::Exited) d->exitCode = status.code;
            else if (status.kind == TP::ExitStatus::Kind::Signaled) d->exitCode = 128 + status.signal;
            else d->exitCode = -1;
            if (d->cancelRequested) d->state = ProcessState::Cancelled;
            else d->state = status.success() ? ProcessState::Finished : ProcessState::Failed;
        }
        d->done.notify_all();
    });
    return {};
}

void ProcessRunner::cancel(double graceSeconds) {
    if (!impl_) return;
    Impl* d = impl_.get();
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        if (d->state != ProcessState::Running || d->cancelRequested) return;
        d->cancelRequested = true;
    }
    const auto grace = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration<double>(std::max(0.0, graceSeconds)));
    // SIGTERM to the process tree now; TK4CPP returns once the child is gone.
    d->terminator = std::thread([d] { d->process->terminate(); });
    // SIGKILL only if the child is still running when the grace period is over. Returns at once, not blocking
    // the caller, so the grace period is measured here and not in TK4CPP.
    d->killer = std::thread([d, grace] {
        if (!d->process->wait_for(grace)) d->process->kill();
    });
}

} // namespace OpenModel
