#include <OpenModel/ProcessRunner.hpp>

#include <chrono>
#include <condition_variable>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace OpenModel {

struct ProcessRunner::Impl {
    std::mutex mutex;
    std::condition_variable done;
    ProcessState state = ProcessState::Idle;
    int exitCode = -1;
    std::vector<std::string> lines;
    std::size_t delivered = 0;
    std::string error;
    std::chrono::steady_clock::time_point began, ended;
    std::thread reader;
    std::thread killer;
    std::atomic<bool> cancelRequested{false};
    int pid = -1;                     ///< POSIX: process group leader.
#ifdef _WIN32
    HANDLE processHandle = nullptr;   ///< Windows: the child process; cleared when it has ended.
#endif

    void join() {
        if (reader.joinable()) reader.join();
        if (killer.joinable()) killer.join();
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

#ifdef _WIN32

namespace {

/** Quote one argument for the Windows command line (MSVCRT rules: backslashes before a quote are doubled). */
std::string quoteWindowsArgument(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out += '"';
        } else {
            out.append(backslashes, '\\');
            out += c;
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

std::string windowsErrorText(DWORD code) {
    char* text = nullptr;
    const DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
    std::string out = (n && text) ? std::string(text, n) : "error " + std::to_string(code);
    if (text) LocalFree(text);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}

} // namespace

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

    // Stdout and stderr of the child share one pipe; the read end stays in this process.
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &inherit, 0)) {
        const std::string why = windowsErrorText(GetLastError());
        impl_.reset();
        return "pipe failed: " + why;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    std::string command;
    for (std::size_t i = 0; i < argv.size(); ++i) command += (i ? " " : "") + quoteWindowsArgument(argv[i]);
    std::vector<char> commandLine(command.begin(), command.end());
    commandLine.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul ? nul : GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION info{};
    const std::string dir = workDir.string();
    const BOOL created = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        dir.empty() ? nullptr : dir.c_str(), &startup, &info);
    const DWORD createError = created ? 0 : GetLastError();
    CloseHandle(writePipe);
    if (nul) CloseHandle(nul);
    if (!created) {
        CloseHandle(readPipe);
        impl_.reset();
        return "Cannot start '" + argv[0] + "': " + windowsErrorText(createError);
    }
    CloseHandle(info.hThread);

    Impl* d = impl_.get();
    d->processHandle = info.hProcess;
    d->state = ProcessState::Running;
    d->began = std::chrono::steady_clock::now();
    d->reader = std::thread([d, readPipe]() {
        std::string pending;
        char buffer[4096];
        DWORD n = 0;
        while (ReadFile(readPipe, buffer, sizeof buffer, &n, nullptr) && n > 0) {
            pending.append(buffer, n);
            std::lock_guard<std::mutex> lock(d->mutex);
            std::size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                d->lines.push_back(std::move(line));
                pending.erase(0, nl + 1);
            }
        }
        CloseHandle(readPipe);
        const HANDLE process = d->processHandle;   // stays valid until the state is set below
        WaitForSingleObject(process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        {
            std::lock_guard<std::mutex> lock(d->mutex);
            if (!pending.empty()) d->lines.push_back(pending);
            d->ended = std::chrono::steady_clock::now();
            d->exitCode = static_cast<int>(code);
            d->processHandle = nullptr;
            d->state = d->cancelRequested ? ProcessState::Cancelled : (code == 0 ? ProcessState::Finished : ProcessState::Failed);
            d->done.notify_all();
        }
        CloseHandle(process);
    });
    return {};
}

void ProcessRunner::cancel(double graceSeconds) {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != ProcessState::Running || impl_->cancelRequested) return;
        impl_->cancelRequested = true;
    }
    // A console program has no SIGTERM here: it is terminated once the grace period has passed without an exit.
    Impl* d = impl_.get();
    if (d->killer.joinable()) d->killer.join();
    d->killer = std::thread([d, graceSeconds]() {
        std::unique_lock<std::mutex> lock(d->mutex);
        if (!d->done.wait_for(lock, std::chrono::duration<double>(graceSeconds), [d] { return d->state != ProcessState::Running; }))
            if (d->processHandle) TerminateProcess(d->processHandle, 1);
    });
}

#else

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
    int fds[2];
    if (pipe(fds) != 0) return std::string("pipe failed: ") + std::strerror(errno);
    // A second pipe tells the parent whether exec worked (closed on exec = success).
    int status[2];
    if (pipe(status) != 0) { close(fds[0]); close(fds[1]); return std::string("pipe failed: ") + std::strerror(errno); }
    fcntl(status[1], F_SETFD, FD_CLOEXEC);

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    const std::string dir = workDir.string();

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]); close(fds[1]); close(status[0]); close(status[1]);
        return std::string("fork failed: ") + std::strerror(errno);
    }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]); close(fds[1]); close(status[0]);
        if (!dir.empty() && chdir(dir.c_str()) != 0) {
            const int e = errno;
            (void)!write(status[1], &e, sizeof e);
            _exit(127);
        }
        execvp(args[0], args.data());
        const int e = errno;
        (void)!write(status[1], &e, sizeof e);
        _exit(127);
    }
    setpgid(pid, pid);
    close(fds[1]);
    close(status[1]);
    int childErrno = 0;
    ssize_t got;
    do { got = read(status[0], &childErrno, sizeof childErrno); } while (got < 0 && errno == EINTR);
    close(status[0]);
    if (got == static_cast<ssize_t>(sizeof childErrno)) {
        int st = 0;
        waitpid(pid, &st, 0);
        close(fds[0]);
        impl_.reset();
        return "Cannot start '" + argv[0] + "': " + std::strerror(childErrno);
    }

    Impl* d = impl_.get();
    d->pid = pid;
    d->state = ProcessState::Running;
    d->began = std::chrono::steady_clock::now();
    const int readFd = fds[0];
    d->reader = std::thread([d, readFd, pid]() {
        std::string pending;
        char buffer[4096];
        for (;;) {
            const ssize_t n = read(readFd, buffer, sizeof buffer);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            pending.append(buffer, static_cast<std::size_t>(n));
            std::size_t nl;
            std::lock_guard<std::mutex> lock(d->mutex);
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                d->lines.push_back(std::move(line));
                pending.erase(0, nl + 1);
            }
        }
        close(readFd);
        int st = 0;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        std::lock_guard<std::mutex> lock(d->mutex);
        if (!pending.empty()) d->lines.push_back(pending);
        d->ended = std::chrono::steady_clock::now();
        if (WIFEXITED(st)) d->exitCode = WEXITSTATUS(st);
        else if (WIFSIGNALED(st)) d->exitCode = 128 + WTERMSIG(st);
        d->state = d->cancelRequested ? ProcessState::Cancelled : (d->exitCode == 0 ? ProcessState::Finished : ProcessState::Failed);
        d->done.notify_all();
    });
    return {};
}

void ProcessRunner::cancel(double graceSeconds) {
    if (!impl_) return;
    int pid;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != ProcessState::Running || impl_->cancelRequested) return;
        impl_->cancelRequested = true;
        pid = impl_->pid;
    }
    kill(-pid, SIGTERM);
    Impl* d = impl_.get();
    if (d->killer.joinable()) d->killer.join();
    d->killer = std::thread([d, pid, graceSeconds]() {
        std::unique_lock<std::mutex> lock(d->mutex);
        if (!d->done.wait_for(lock, std::chrono::duration<double>(graceSeconds), [d] { return d->state != ProcessState::Running; }))
            kill(-pid, SIGKILL);
    });
}

#endif

} // namespace OpenModel
