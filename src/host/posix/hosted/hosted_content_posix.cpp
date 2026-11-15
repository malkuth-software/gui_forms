// Hosted content on macOS and Linux (hosted_content.hpp): the program is
// started with posix_spawn, given a socket pair's end as its channel and a
// shared memory region for its frames, and watched from the content
// worker's thread. The UI thread never waits on any of it: frames are
// copied into the surface's LiveSurface by the worker, and input sends are
// non-blocking.
//
// The frame region: on Linux a sealed memfd (it can neither shrink nor grow
// once sized, so the program cannot turn the parent's reads into SIGBUS);
// on macOS an unlinked POSIX shared memory object, which the system lets be
// sized only once. The parent maps it read-only.
//
// The parent never receives SIGPIPE from a closed channel: every send uses
// MSG_NOSIGNAL (Linux) or the socket's SO_NOSIGPIPE (macOS).

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "gui_forms/content/hosted/hosted_content.hpp"
#include "../../../core/thread_records/thread_records_end.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern char** environ;

namespace gui_forms {

namespace detail {

// The input channel's state, shared by the HostedInput copies and the
// program while it runs. `descriptor` is the parent's end of the socket
// while the process is connected, -1 otherwise; it and `backlog` (bytes of
// whole messages not yet sent, in order) are guarded by `mutex`.
struct HostedInputState final {
    std::mutex mutex{};
    int descriptor{-1};
    std::vector<std::byte> backlog{};
    std::atomic<std::uint64_t> dropped{0U};
    std::atomic<bool> connected{false};
};

} // namespace detail

namespace {

// Input waiting longer than this is dropped rather than queued.
constexpr std::size_t hosted_input_backlog_limit = 1024U * 1024U;
// The longest start timeout and stop grace: a day. Longer ones are taken
// as this, so now() + timeout cannot overflow (A-gf-host-02-F13).
constexpr std::chrono::milliseconds hosted_maximum_timeout = std::chrono::hours(24);
// How often the worker looks at the surface's request while idle.
constexpr int hosted_poll_milliseconds = 50;
// How long a killed process may take to be reaped before a detached thread
// takes over the wait (a process in uninterruptible sleep).
constexpr std::chrono::milliseconds hosted_reap_bound{200};
constexpr std::size_t hosted_read_chunk = 64U * 1024U;

int send_flags() noexcept {
#if defined(MSG_NOSIGNAL)
    const int flags = MSG_NOSIGNAL | MSG_DONTWAIT;
#else
    const int flags = MSG_DONTWAIT;
#endif
    return flags;
}

// Sends what the socket takes now; returns the bytes sent, 0 when it would
// block, or -1 when the channel is closed or broken.
std::ptrdiff_t send_some(int descriptor, const std::byte* data, std::size_t size) noexcept {
    for (;;) {
        const ssize_t sent = send(descriptor, data, size, send_flags());
        if (sent >= 0) return static_cast<std::ptrdiff_t>(sent);
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
}

// Queues one whole message behind the backlog, or sends it at once when
// the backlog is empty; a partly sent message's rest joins the backlog, so
// messages are never interleaved. `droppable` input is dropped instead when
// the backlog is over its limit. Called with the state's mutex held.
bool enqueue_locked(detail::HostedInputState& state, std::span<const std::byte> message, bool droppable) {
    if (state.descriptor < 0) return false;
    if (!state.backlog.empty()) {
        if (droppable && state.backlog.size() > hosted_input_backlog_limit) return false;
        state.backlog.insert(state.backlog.end(), message.begin(), message.end());
        return true;
    }
    const std::ptrdiff_t sent = send_some(state.descriptor, message.data(), message.size());
    if (sent < 0) return false;
    const std::size_t taken = static_cast<std::size_t>(sent);
    if (taken < message.size()) state.backlog.insert(state.backlog.end(), message.begin() + taken, message.end());
    return true;
}

// Sends as much of the backlog as the socket takes. False when the channel
// is broken.
bool flush_backlog(detail::HostedInputState& state) {
    const std::scoped_lock lock(state.mutex);
    if (state.descriptor < 0 || state.backlog.empty()) return true;
    const std::ptrdiff_t sent = send_some(state.descriptor, state.backlog.data(), state.backlog.size());
    if (sent < 0) return false;
    state.backlog.erase(state.backlog.begin(), state.backlog.begin() + sent);
    return true;
}

bool backlog_pending(detail::HostedInputState& state) {
    const std::scoped_lock lock(state.mutex);
    const bool pending = !state.backlog.empty();
    return pending;
}

// An owned descriptor, closed on destruction.
class Descriptor final {
public:
    Descriptor() noexcept = default;
    explicit Descriptor(int descriptor) noexcept : descriptor_(descriptor) {}
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() { reset(); }
    void reset(int descriptor = -1) noexcept {
        if (descriptor_ >= 0) static_cast<void>(close(descriptor_));
        descriptor_ = descriptor;
    }
    [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
    int descriptor_{-1};
};

// The read-only mapping of the frame region, unmapped on destruction.
class Mapping final {
public:
    Mapping() noexcept = default;
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    ~Mapping() {
        if (address_ != nullptr) static_cast<void>(munmap(address_, size_));
    }
    [[nodiscard]] bool map(int descriptor, std::size_t size) noexcept {
        void* const mapped = mmap(nullptr, size, PROT_READ, MAP_SHARED, descriptor, 0);
        if (mapped == MAP_FAILED) return false;
        address_ = mapped;
        size_ = size;
        return true;
    }
    [[nodiscard]] std::span<const std::byte> bytes() const& noexcept {
        const std::span<const std::byte> view(static_cast<const std::byte*>(address_), size_);
        return view;
    }
    std::span<const std::byte> bytes() const&& = delete;

private:
    void* address_{};
    std::size_t size_{};
};

[[nodiscard]] bool set_close_on_exec(int descriptor) noexcept {
    const int flags = fcntl(descriptor, F_GETFD);
    if (flags < 0) return false;
    const bool result = fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
    return result;
}

[[nodiscard]] bool set_non_blocking(int descriptor) noexcept {
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0) return false;
    const bool result = fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
    return result;
}

// The failed step and errno's number: the number is the same on every
// host and in every locale, where strerror's text is neither (and it
// returns a static buffer; std-rules strerror).
[[noreturn]] void fail_system(const char* step) {
    const int error = errno;
    std::string message = "hosted content: ";
    message += step;
    message += " failed: errno ";
    message += std::to_string(error);
    throw std::runtime_error(message);
}

// The frame region, sized and sealed; the descriptor is close-on-exec.
int create_frame_region(std::uint64_t size) {
#if defined(__linux__)
    const int descriptor = memfd_create("gui_forms_hosted_frames", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (descriptor < 0) fail_system("memfd_create");
    if (ftruncate(descriptor, static_cast<off_t>(size)) != 0) {
        static_cast<void>(close(descriptor));
        fail_system("ftruncate");
    }
    if (fcntl(descriptor, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0) {
        static_cast<void>(close(descriptor));
        fail_system("sealing the frame region");
    }
    return descriptor;
#else
    static std::atomic<std::uint32_t> region_counter{0U};
    const std::uint32_t serial = region_counter.fetch_add(1U, std::memory_order_relaxed);
    // macOS limits the name to 31 bytes; it is unlinked at once.
    const std::string name = "/gfh." + std::to_string(static_cast<long>(getpid())) + "." + std::to_string(serial);
    const int descriptor = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (descriptor < 0) fail_system("shm_open");
    static_cast<void>(shm_unlink(name.c_str()));
    if (!set_close_on_exec(descriptor) || ftruncate(descriptor, static_cast<off_t>(size)) != 0) {
        static_cast<void>(close(descriptor));
        fail_system("sizing the frame region");
    }
    return descriptor;
#endif
}

// A duplicate at 10 or above, close-on-exec, so the dup2 to the child's
// fixed numbers (3 and 4) never maps a descriptor onto itself.
int high_duplicate(int descriptor) {
    const int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 10);
    if (duplicate < 0) fail_system("duplicating a descriptor");
    return duplicate;
}

const char* signal_name(int signal_number) noexcept {
    switch (signal_number) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    default: return "signal";
    }
}

// How a process ended.
struct ProcessEnd final {
    bool reaped{};
    bool exited{};
    int exit_status{};
    int signal_number{};
};

ProcessEnd end_of(int status) noexcept {
    ProcessEnd end{};
    end.reaped = true;
    if (WIFEXITED(status)) {
        end.exited = true;
        end.exit_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        end.signal_number = WTERMSIG(status);
    }
    return end;
}

// A detached wait for a killed process that did not end within the reap
// bound, so it never stays a zombie.
class LateReaper final {
public:
    explicit LateReaper(pid_t process) noexcept : process_(process) {}
    void operator()() const noexcept {
        // Closes last: the thread's records are released here, on the thread.
        const gui_forms::detail::ThreadRecordsEnd thread_records_end;
        int status = 0;
        while (waitpid(process_, &status, 0) < 0 && errno == EINTR) {
        }
    }

private:
    pid_t process_{};
};

// The child process: killed and reaped on destruction if still running, so
// no exception path leaves it behind.
class ChildProcess final {
public:
    ChildProcess() noexcept = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess() { static_cast<void>(kill_and_reap()); }

    void adopt(pid_t process) noexcept { process_ = process; }
    [[nodiscard]] bool running() const noexcept { return process_ > 0; }

    // Reaps without waiting; none while it runs.
    [[nodiscard]] ProcessEnd poll() noexcept {
        ProcessEnd end{};
        if (process_ <= 0) return end;
        int status = 0;
        const pid_t reaped = waitpid(process_, &status, WNOHANG);
        if (reaped == process_) {
            process_ = 0;
            end = end_of(status);
        } else if (reaped < 0 && errno == ECHILD) {
            // Reaped by other code (a wait elsewhere, SIGCHLD ignored): the
            // child has ended, without a status this process can read, and
            // its number may already name another process, which must
            // never be signalled (A-gf-host-02-F7).
            process_ = 0;
            end.reaped = true;
        }
        return end;
    }

    // Waits up to `bound` for it to end on its own.
    [[nodiscard]] ProcessEnd wait_for(std::chrono::milliseconds bound) noexcept {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + bound;
        for (;;) {
            const ProcessEnd end = poll();
            if (end.reaped || process_ <= 0) return end;
            if (std::chrono::steady_clock::now() >= deadline) return end;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Kills it and reaps it within the reap bound; past that a detached
    // thread reaps it later.
    ProcessEnd kill_and_reap() noexcept {
        if (process_ <= 0) return ProcessEnd{};
        // A child already ended, or reaped elsewhere, is not signalled: its
        // number may have been reused (A-gf-host-02-F7).
        const ProcessEnd ended = poll();
        if (ended.reaped || process_ <= 0) return ended;
        static_cast<void>(kill(process_, SIGKILL));
        const ProcessEnd end = wait_for(hosted_reap_bound);
        if (end.reaped || process_ <= 0) return end;
        try {
            const LateReaper late_reaper(process_);
            std::thread reaper(late_reaper);
            reaper.detach();
        } catch (...) {
            static_cast<void>(0);
        }
        process_ = 0;
        return end;
    }

private:
    pid_t process_{};
};

// Writes one byte to the wake pipe when the surface asks the program to
// stop, so the worker's poll returns at once.
class WakeOnStop final {
public:
    explicit WakeOnStop(int descriptor) noexcept : descriptor_(descriptor) {}
    void operator()() const noexcept {
        const std::byte signal_byte{1};
        static_cast<void>(write(descriptor_, &signal_byte, 1U));
    }

private:
    int descriptor_{-1};
};

// The environment the child starts with: the parent's, with the hosted
// variables set.
class ChildEnvironment final {
public:
    explicit ChildEnvironment(std::uint64_t slot_bytes) {
        const std::array<std::pair<std::string_view, std::string>, 4> ours{{
            {hosted_environment_channel, std::to_string(hosted_channel_descriptor)},
            {hosted_environment_frames, std::to_string(hosted_frames_descriptor)},
            {hosted_environment_slot_bytes, std::to_string(slot_bytes)},
            {hosted_environment_slot_count, std::to_string(hosted_slot_count)},
        }};
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            const std::string_view variable(*entry);
            bool replaced = false;
            for (const std::pair<std::string_view, std::string>& setting : ours) {
                const bool named = variable.size() > setting.first.size() &&
                                   variable.substr(0U, setting.first.size()) == setting.first &&
                                   variable[setting.first.size()] == '=';
                if (named) replaced = true;
            }
            if (!replaced) strings_.emplace_back(variable);
        }
        for (const std::pair<std::string_view, std::string>& setting : ours) {
            std::string assignment(setting.first);
            assignment += '=';
            assignment += setting.second;
            strings_.push_back(std::move(assignment));
        }
        pointers_.reserve(strings_.size() + 1U);
        for (std::string& value : strings_) pointers_.push_back(value.data());
        pointers_.push_back(nullptr);
    }
    [[nodiscard]] char* const* pointers() noexcept { return pointers_.data(); }

private:
    std::vector<std::string> strings_{};
    std::vector<char*> pointers_{};
};

// The program's argument vector: the executable's path, then the options'
// arguments.
class ChildArguments final {
public:
    ChildArguments(const std::filesystem::path& executable, const std::vector<std::string>& arguments) {
        strings_.push_back(executable.string());
        strings_.insert(strings_.end(), arguments.begin(), arguments.end());
        pointers_.reserve(strings_.size() + 1U);
        for (std::string& value : strings_) pointers_.push_back(value.data());
        pointers_.push_back(nullptr);
    }
    [[nodiscard]] char* const* pointers() noexcept { return pointers_.data(); }
    [[nodiscard]] const char* path() const noexcept { return strings_.front().c_str(); }

private:
    std::vector<std::string> strings_{};
    std::vector<char*> pointers_{};
};

// posix_spawn's file actions and attributes, destroyed on scope exit.
class SpawnSetup final {
public:
    SpawnSetup() {
        if (posix_spawn_file_actions_init(&actions_) != 0) throw std::runtime_error("hosted content: spawn setup failed");
        if (posix_spawnattr_init(&attributes_) != 0) {
            posix_spawn_file_actions_destroy(&actions_);
            throw std::runtime_error("hosted content: spawn setup failed");
        }
    }
    SpawnSetup(const SpawnSetup&) = delete;
    SpawnSetup& operator=(const SpawnSetup&) = delete;
    ~SpawnSetup() {
        posix_spawnattr_destroy(&attributes_);
        posix_spawn_file_actions_destroy(&actions_);
    }
    posix_spawn_file_actions_t actions_{};
    posix_spawnattr_t attributes_{};
};

pid_t spawn_child(const HostedContentOptions& options, int channel, int frames) {
    SpawnSetup setup;
    const int channel_high = high_duplicate(channel);
    const Descriptor channel_guard(channel_high);
    const int frames_high = high_duplicate(frames);
    const Descriptor frames_guard(frames_high);
    if (posix_spawn_file_actions_adddup2(&setup.actions_, channel_high, hosted_channel_descriptor) != 0 ||
        posix_spawn_file_actions_adddup2(&setup.actions_, frames_high, hosted_frames_descriptor) != 0) {
        throw std::runtime_error("hosted content: spawn file actions failed");
    }
#if !defined(POSIX_SPAWN_CLOEXEC_DEFAULT) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    // Linux has no close-on-exec default: every descriptor above the
    // channel and the frames is closed in the child, so it inherits none of
    // this process's files, sockets or pipes that lack FD_CLOEXEC
    // (A-gf-host-02-F15). glibc 2.34 provides the action; an older C
    // library still relies on FD_CLOEXEC.
    if (posix_spawn_file_actions_addclosefrom_np(&setup.actions_, hosted_frames_descriptor + 1) != 0) {
        throw std::runtime_error("hosted content: spawn file actions failed");
    }
#endif
    // The child starts with every signal unblocked and SIGPIPE at its
    // default, whatever this thread has.
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    int flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    // macOS: only the file actions' targets are inherited, so standard
    // input, output and error are named too: inherited where this process
    // has them, /dev/null where it does not. Without them the child started
    // with 0, 1 and 2 closed, lost its diagnostics, and its first files
    // took those numbers (A-gf-host-02-F4).
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
    for (int standard = 0; standard <= 2; ++standard) {
        const bool open_here = fcntl(standard, F_GETFD) != -1;
        const int added = open_here
            ? posix_spawn_file_actions_addinherit_np(&setup.actions_, standard)
            : posix_spawn_file_actions_addopen(&setup.actions_, standard, "/dev/null", O_RDWR, 0);
        if (added != 0) throw std::runtime_error("hosted content: spawn file actions failed");
    }
#endif
    posix_spawnattr_setsigmask(&setup.attributes_, &empty_mask);
    posix_spawnattr_setsigdefault(&setup.attributes_, &defaults);
    posix_spawnattr_setflags(&setup.attributes_, static_cast<short>(flags));
    ChildArguments arguments(options.executable, options.arguments);
    ChildEnvironment environment(options.slot_bytes);
    pid_t process = 0;
    const int spawned = posix_spawn(&process, arguments.path(), &setup.actions_, &setup.attributes_,
                                    arguments.pointers(), environment.pointers());
    if (spawned != 0) {
        errno = spawned;
        fail_system("posix_spawn");
    }
    return process;
}

// The running program, on the content worker's thread.
class PosixHostedProgram final : public ContentProgram {
public:
    PosixHostedProgram(HostedContentOptions options, std::shared_ptr<detail::HostedInputState> input)
        : options_(std::move(options)), input_(std::move(input)) {}

    void run(ContentContext& context) override;

private:
    // Sends one parent message, queued behind any backlog.
    void send_control(HostedMessageKind kind, std::span<const std::byte> payload);
    void send_configure(const ContentRequest& request);
    // Handles one message; throws std::runtime_error for a protocol breach.
    void handle(const HostedMessage& message, ContentFrameSink& sink, std::span<const std::byte> region);
    // Stops the process: a stop message, the grace, then SIGKILL.
    void stop_process(int channel);
    // The channel closed: reaps the process and turns its end into the
    // program's outcome (returns for a clean exit, throws otherwise).
    void end_after_disconnect();
    [[noreturn]] void fail_protocol(const std::string& reason);
    void disconnect_input() noexcept;

    HostedContentOptions options_;
    std::shared_ptr<detail::HostedInputState> input_;
    ChildProcess child_{};
    std::vector<std::byte> outgoing_{};
    std::vector<std::byte> payload_{};
    bool started_{};
};

void PosixHostedProgram::disconnect_input() noexcept {
    const std::scoped_lock lock((*input_).mutex);
    (*input_).descriptor = -1;
    (*input_).backlog.clear();
    (*input_).connected.store(false, std::memory_order_release);
}

void PosixHostedProgram::send_control(HostedMessageKind kind, std::span<const std::byte> payload) {
    outgoing_.clear();
    append_hosted_message(outgoing_, kind, payload);
    const std::scoped_lock lock((*input_).mutex);
    static_cast<void>(enqueue_locked(*input_, outgoing_, false));
}

void PosixHostedProgram::send_configure(const ContentRequest& request) {
    HostedConfigureRecord record{};
    record.width = request.size.width;
    record.height = request.size.height;
    record.scale = request.scale;
    record.generation = request.generation;
    encode_hosted_configure(payload_, record);
    send_control(HostedMessageKind::configure, payload_);
}

void PosixHostedProgram::fail_protocol(const std::string& reason) {
    static_cast<void>(child_.kill_and_reap());
    throw std::runtime_error("hosted program broke the protocol: " + reason);
}

void PosixHostedProgram::handle(const HostedMessage& message, ContentFrameSink& sink,
                                std::span<const std::byte> region) {
    if (!started_ && message.kind != HostedMessageKind::hello) {
        fail_protocol(std::string(hosted_message_kind_name(message.kind)) + " before hello");
    }
    switch (message.kind) {
    case HostedMessageKind::hello: {
        std::uint32_t version = 0U;
        if (started_) fail_protocol("a second hello");
        if (!decode_hosted_hello(message.payload, version)) fail_protocol("an unsupported protocol version");
        started_ = true;
        (*input_).connected.store(true, std::memory_order_release);
        send_configure(sink.request());
        return;
    }
    case HostedMessageKind::frame: {
        HostedFrameRecord record{};
        if (!decode_hosted_frame(message.payload, record)) fail_protocol("a malformed frame record");
        const HostedFrameView view = view_hosted_frame(record, region, hosted_slot_count, options_.slot_bytes);
        if (view.check == HostedFrameCheck::refused_slot) fail_protocol("a frame in a slot that does not exist");
        // A refused frame is counted by the sink as in-process content's
        // is, and the last good frame stays; an accepted one is copied out
        // here, on the worker's thread.
        static_cast<void>(sink.publish(view.frame));
        encode_hosted_slot(payload_, record.slot);
        send_control(HostedMessageKind::release, payload_);
        return;
    }
    case HostedMessageKind::heartbeat:
        sink.heartbeat();
        return;
    case HostedMessageKind::semantics: {
        ContentSemantics semantics{};
        if (!decode_hosted_semantics(message.payload, semantics)) fail_protocol("semantic text that is not valid");
        static_cast<void>(sink.publish_semantics(std::move(semantics)));
        return;
    }
    case HostedMessageKind::goodbye:
        // The program is about to exit on its own; the end of the channel,
        // which follows, ends the session, so nothing is done here.
        return;
    case HostedMessageKind::configure:
    case HostedMessageKind::release:
    case HostedMessageKind::key:
    case HostedMessageKind::text:
    case HostedMessageKind::pointer:
    case HostedMessageKind::stop:
    case HostedMessageKind::focus:
        // The parent's own kinds: a child never sends them (fails below).
        break;
    default:
        // A kind outside the protocol (fails below).
        break;
    }
    // The reader admits only the child's kinds; anything else is a breach.
    fail_protocol("a parent's message kind");
}

void PosixHostedProgram::stop_process(int channel) {
    send_control(HostedMessageKind::stop, {});
    static_cast<void>(flush_backlog(*input_));
    // The grace: the program may exit on its own, which closes the channel.
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + options_.stop_grace;
    std::vector<std::byte> discard(hosted_read_chunk);
    bool closed = false;
    while (!closed) {
        const std::chrono::steady_clock::duration left = deadline - std::chrono::steady_clock::now();
        const std::int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(left).count();
        if (milliseconds <= 0) break;
        pollfd watched{};
        watched.fd = channel;
        watched.events = POLLIN;
        // poll takes an int; a longer wait is taken in turns (A-gf-host-02-F13).
        const std::int64_t turn = std::min<std::int64_t>(milliseconds, std::numeric_limits<int>::max());
        const int ready = ::poll(&watched, 1U, static_cast<int>(turn));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) break;
        const ssize_t received = recv(channel, discard.data(), discard.size(), 0);
        if (received == 0 || (received < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            closed = true;
        }
    }
    const ProcessEnd exited = closed ? child_.wait_for(std::chrono::milliseconds(20)) : ProcessEnd{};
    if (!exited.reaped) static_cast<void>(child_.kill_and_reap());
}

void PosixHostedProgram::end_after_disconnect() {
    ProcessEnd end = child_.wait_for(options_.stop_grace + std::chrono::milliseconds(100));
    if (!end.reaped) {
        static_cast<void>(child_.kill_and_reap());
        throw std::runtime_error("hosted program closed its channel and did not exit; it was killed");
    }
    if (end.exited && end.exit_status == 0) return;
    if (end.exited) {
        throw std::runtime_error("hosted program exited with status " + std::to_string(end.exit_status));
    }
    throw std::runtime_error(std::string("hosted program crashed: ") + signal_name(end.signal_number) + " (signal " +
                             std::to_string(end.signal_number) + ")");
}

void PosixHostedProgram::run(ContentContext& context) {
    ContentFrameSink sink = context.sink();
    const std::uint64_t region_bytes = options_.slot_bytes * hosted_slot_count;
    const Descriptor frames(create_frame_region(region_bytes));
    Mapping mapping;
    if (!mapping.map(frames.get(), static_cast<std::size_t>(region_bytes))) fail_system("mmap");

    int pair[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) fail_system("socketpair");
    const Descriptor parent_end(pair[0]);
    Descriptor child_end(pair[1]);
    if (!set_close_on_exec(parent_end.get()) || !set_close_on_exec(child_end.get()) ||
        !set_non_blocking(parent_end.get())) {
        fail_system("configuring the channel");
    }
#if defined(SO_NOSIGPIPE)
    const int one = 1;
    static_cast<void>(setsockopt(parent_end.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)));
#endif
    int wake_pair[2] = {-1, -1};
    if (pipe(wake_pair) != 0) fail_system("pipe");
    const Descriptor wake_read(wake_pair[0]);
    const Descriptor wake_write(wake_pair[1]);
    if (!set_close_on_exec(wake_read.get()) || !set_close_on_exec(wake_write.get()) ||
        !set_non_blocking(wake_read.get()) || !set_non_blocking(wake_write.get())) {
        fail_system("configuring the wake pipe");
    }

    child_.adopt(spawn_child(options_, child_end.get(), frames.get()));
    child_end.reset();
    {
        const std::scoped_lock lock((*input_).mutex);
        (*input_).descriptor = parent_end.get();
        (*input_).backlog.clear();
    }
    // Disconnects the input before the descriptors close, on every exit.
    struct InputGuard final {
        PosixHostedProgram& program;
        ~InputGuard() { program.disconnect_input(); }
    };
    const InputGuard input_guard{*this};
    const std::stop_callback<WakeOnStop> wake(context.stop_token(), WakeOnStop(wake_write.get()));

    HostedMessageReader reader(true);
    HostedMessage message{};
    std::vector<std::byte> chunk(hosted_read_chunk);
    std::array<std::byte, 64> drained{};
    std::uint64_t configured_generation = 0U;
    const std::chrono::steady_clock::time_point start_deadline =
        std::chrono::steady_clock::now() + options_.start_timeout;
    for (;;) {
        if (context.stop_requested()) {
            stop_process(parent_end.get());
            return;
        }
        if (started_) {
            const ContentRequest request = sink.request();
            if (request.generation != configured_generation) {
                configured_generation = request.generation;
                send_configure(request);
            }
        } else if (std::chrono::steady_clock::now() >= start_deadline) {
            static_cast<void>(child_.kill_and_reap());
            throw std::runtime_error("hosted program sent no hello within " +
                                     std::to_string(options_.start_timeout.count()) + " ms");
        }
        std::array<pollfd, 2> watched{};
        watched[0].fd = parent_end.get();
        watched[0].events = static_cast<short>(POLLIN | (backlog_pending(*input_) ? POLLOUT : 0));
        watched[1].fd = wake_read.get();
        watched[1].events = POLLIN;
        const int ready = ::poll(watched.data(), watched.size(), hosted_poll_milliseconds);
        if (ready < 0) {
            if (errno == EINTR) continue;
            fail_system("poll");
        }
        if ((watched[1].revents & POLLIN) != 0) {
            while (read(wake_read.get(), drained.data(), drained.size()) > 0) {
            }
        }
        if ((watched[0].revents & POLLOUT) != 0) static_cast<void>(flush_backlog(*input_));
        const bool readable = (watched[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        if (!readable) continue;
        bool disconnected = false;
        const ssize_t received = recv(parent_end.get(), chunk.data(), chunk.size(), 0);
        if (received == 0) {
            disconnected = true;
        } else if (received < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) disconnected = true;
        } else {
            reader.feed(std::span<const std::byte>(chunk.data(), static_cast<std::size_t>(received)));
            for (;;) {
                const HostedReadResult result = reader.next(message);
                if (result == HostedReadResult::need_more) break;
                if (result != HostedReadResult::message) fail_protocol(std::string(hosted_read_result_name(result)));
                handle(message, sink, mapping.bytes());
            }
        }
        if (disconnected) {
            end_after_disconnect();
            return;
        }
    }
}

} // namespace

HostedContentSupport hosted_content_support() noexcept {
    return HostedContentSupport::available;
}

HostedInput::HostedInput(std::shared_ptr<detail::HostedInputState> state) noexcept : state_(std::move(state)) {}

bool HostedInput::send(HostedMessageKind kind, std::span<const std::byte> payload, bool droppable) {
    if (!state_) return false;
    std::vector<std::byte> message{};
    append_hosted_message(message, kind, payload);
    bool queued = false;
    {
        const std::scoped_lock lock((*state_).mutex);
        if ((*state_).connected.load(std::memory_order_acquire)) queued = enqueue_locked(*state_, message, droppable);
    }
    if (!queued) (*state_).dropped.fetch_add(1U, std::memory_order_relaxed);
    return queued;
}

bool HostedInput::send_key(const KeyEvent& event) {
    HostedKeyRecord record{};
    record.action = static_cast<std::uint32_t>(event.action);
    record.physical_key = event.physical_key;
    record.modifiers = static_cast<std::uint32_t>(event.modifiers);
    record.repeat = event.repeat;
    std::vector<std::byte> payload{};
    encode_hosted_key(payload, record);
    const bool droppable = event.action != KeyAction::up;
    const bool sent = send(HostedMessageKind::key, payload, droppable);
    return sent;
}

bool HostedInput::send_text(std::string_view text, bool composing) {
    // Text over the payload limit is dropped and counted, as the header
    // promises, rather than thrown (A-gf-host-02-F17).
    if (text.size() > hosted_maximum_payload_bytes - 4U) {
        if (state_) (*state_).dropped.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    std::vector<std::byte> payload{};
    encode_hosted_text(payload, text, composing);
    const bool sent = send(HostedMessageKind::text, payload, true);
    return sent;
}

bool HostedInput::send_pointer(const PointerEvent& event, Point device_position) {
    HostedPointerRecord record{};
    record.action = static_cast<std::uint32_t>(event.action);
    record.button = static_cast<std::uint32_t>(event.button);
    record.modifiers = static_cast<std::uint32_t>(event.modifiers);
    record.click_count = event.click_count;
    record.x = device_position.x;
    record.y = device_position.y;
    record.wheel_x = event.wheel_delta.x;
    record.wheel_y = event.wheel_delta.y;
    std::vector<std::byte> payload{};
    encode_hosted_pointer(payload, record);
    const bool droppable = event.action != PointerAction::up;
    const bool sent = send(HostedMessageKind::pointer, payload, droppable);
    return sent;
}

bool HostedInput::send_focus(bool focused) {
    std::vector<std::byte> payload{};
    encode_hosted_focus(payload, focused);
    const bool sent = send(HostedMessageKind::focus, payload, false);
    return sent;
}

std::uint64_t HostedInput::dropped() const noexcept {
    if (!state_) return 0U;
    const std::uint64_t count = (*state_).dropped.load(std::memory_order_relaxed);
    return count;
}

bool HostedInput::connected() const noexcept {
    if (!state_) return false;
    const bool result = (*state_).connected.load(std::memory_order_acquire);
    return result;
}

HostedContent make_hosted_content(HostedContentOptions options) {
    if (options.executable.empty() || !options.executable.is_absolute()) {
        throw std::invalid_argument("hosted content needs the program's absolute path");
    }
    if (options.slot_bytes == 0U || options.slot_bytes > maximum_hosted_slot_bytes) {
        throw std::invalid_argument("hosted content slot size out of range");
    }
    if (options.start_timeout < std::chrono::milliseconds(0) || options.stop_grace < std::chrono::milliseconds(0)) {
        throw std::invalid_argument("hosted content timeouts may not be negative");
    }
    // A caller's largest duration (meaning no limit) would overflow the
    // deadline arithmetic; both are taken as at most a day
    // (A-gf-host-02-F13).
    options.start_timeout = std::min(options.start_timeout, hosted_maximum_timeout);
    options.stop_grace = std::min(options.stop_grace, hosted_maximum_timeout);
    std::shared_ptr<detail::HostedInputState> state = std::make_shared<detail::HostedInputState>();
    HostedContent made{};
    made.program = std::make_unique<PosixHostedProgram>(std::move(options), state);
    made.input = HostedInput(std::move(state));
    return made;
}

} // namespace gui_forms
