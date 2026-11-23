// Hosted content on Windows (hosted_content.hpp; docs/user/content-surfaces.md,
// "Hosted content on Windows"). The program is started with CreateProcessW
// and inherits, through an explicit handle list, exactly its end of an
// overlapped named pipe (the channel), a pagefile-backed section (the frame
// region) and its standard input, output and error. It runs in a job object of its own that kills
// it when the job's last handle closes, so it ends with the application
// even when the application crashes. The content worker's thread watches it
// with WaitForMultipleObjects; the UI thread never waits on any of it:
// frames are copied into the surface's LiveSurface by the worker, and input
// sends never block (an overlapped write in flight and a backlog behind it).
//
// The Microsoft documentation this follows (learn.microsoft.com, read
// 2026-11-15):
//   - CreateProcessW, STARTUPINFOEXW, InitializeProcThreadAttributeList and
//     UpdateProcThreadAttribute: with PROC_THREAD_ATTRIBUTE_HANDLE_LIST the
//     child inherits only the listed handles, which must be inheritable,
//     and bInheritHandles must be TRUE. The command line is at most 32,767
//     characters with its terminator; CREATE_NO_WINDOW runs a console
//     program without a console window.
//   - "Handle Inheritance": an inherited handle has the same value in the
//     child, which is why the environment can name it.
//   - CreateNamedPipeW (PIPE_REJECT_REMOTE_CLIENTS, one instance,
//     FILE_FLAG_FIRST_PIPE_INSTANCE), CreateFileW on a pipe, ReadFile,
//     WriteFile, GetOverlappedResult, CancelIoEx and OVERLAPPED (ReadFile
//     and WriteFile set the event to nonsignaled before they start).
//     ConnectNamedPipe is not called: the child end is opened before the
//     process starts, so the pipe is already connected, and a second client
//     cannot open the single instance (ERROR_PIPE_BUSY).
//   - CreateFileMappingW with INVALID_HANDLE_VALUE: a section backed by the
//     paging file, whose size is fixed at creation; MapViewOfFile with
//     FILE_MAP_READ maps it read-only. The child's handle is duplicated with
//     FILE_MAP_READ | FILE_MAP_WRITE only (no SECTION_EXTEND_SIZE).
//   - CreateJobObjectW, SetInformationJobObject and
//     JOBOBJECT_BASIC_LIMIT_INFORMATION: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
//     and JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION, which sets
//     SEM_NOGPFAULTERRORBOX for the job's processes so an unhandled
//     exception ends the process with the exception code as its exit status
//     instead of opening an error box. AssignProcessToJobObject on the
//     suspended process, then ResumeThread.
//   - GetExitCodeProcess, TerminateProcess, WaitForMultipleObjects.
//   - "Parsing C++ command-line arguments" (the quoting rules) and
//     "Changing Environment Variables" (a block sorted by name without
//     regard to case).
//
// Inheritable handles exist only between their creation and the
// CreateProcessW call, and only the listed ones reach the hosted program.
// Another thread of the application that starts a process with
// bInheritHandles TRUE and no handle list in that window would inherit them
// too; the worker then sees the hosted program's exit without the channel
// breaking, and ends the session after the stop grace.

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif

#include "gui_forms/content/hosted/hosted_content.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <stxlib/stop_token.hpp>
#include <stxlib/threads.hpp>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace detail {

// The input channel's state, shared by the HostedInput copies and the
// program while it runs, guarded by `mutex`. While the process is
// connected, `channel` is the parent's pipe end and `write_event` its
// writes' event, both owned by the running program; null otherwise. Writes
// are overlapped: `in_flight` holds the bytes of the one write the system is
// carrying out (whole messages, in order), untouched while `writing`;
// `backlog` holds the whole messages queued behind it.
struct HostedInputState final {
    stxlib::Mutex mutex{};
    HANDLE channel{};
    HANDLE write_event{};
    OVERLAPPED write_overlapped{};
    bool writing{};
    std::vector<std::byte> in_flight{};
    std::vector<std::byte> backlog{};
    std::atomic<std::uint64_t> dropped{0U};
    std::atomic<bool> connected{false};
};

} // namespace detail

namespace {

// Input waiting behind more than this is dropped rather than queued.
constexpr std::size_t hosted_input_backlog_limit = 1024U * 1024U;
// The whole backlog's bound, for the messages that may not be dropped
// (releases, configure, focus): past it the program has stopped reading,
// which is a protocol failure (A-gf-host-02-F8, as on POSIX).
constexpr std::size_t hosted_input_backlog_hard_limit = 8U * hosted_input_backlog_limit;
// The longest start timeout and stop grace: a day. Longer ones are taken
// as this, so now() + timeout cannot overflow (A-gf-host-02-F13).
constexpr std::chrono::milliseconds hosted_maximum_timeout = std::chrono::hours(24);
// The most one overlapped write carries, so its count fits a DWORD; the
// rest follows when it completes.
constexpr std::size_t hosted_write_limit = 1024U * 1024U;
// How often the worker looks at the surface's request while idle.
constexpr DWORD hosted_wait_milliseconds = 50U;
// How long a killed process may take to end before its handle is closed
// regardless (Windows keeps no zombie to reap).
constexpr DWORD hosted_reap_milliseconds = 200U;
// How long after the process exits the channel may stay open (a process
// that inherited it by accident holds it) before the session ends anyway,
// beyond the stop grace.
constexpr std::chrono::milliseconds hosted_exit_drain{100};
constexpr std::size_t hosted_read_chunk = 64U * 1024U;
// The pipe's buffer in each direction (a size the system may round).
constexpr DWORD hosted_pipe_buffer_bytes = 64U * 1024U;
// CreateProcessW's command line limit, its terminator included.
constexpr std::size_t maximum_command_line_characters = 32767U;
// Exit codes from here up are NTSTATUS error codes: the process ended by an
// exception (an access violation, an illegal instruction, a fast fail).
constexpr DWORD hosted_crash_status_floor = 0xC0000000U;
// The exit code a process this file kills is given.
constexpr UINT hosted_killed_exit_code = 1U;

// The failed step and GetLastError's number: the number is the same on
// every host and in every locale, where FormatMessage's text is neither.
[[noreturn]] void fail_with(const char* step, DWORD error) {
    std::string message = "hosted content: ";
    message += step;
    message += " failed: Windows error ";
    message += std::to_string(error);
    throw std::runtime_error(message);
}

[[noreturn]] void fail_system(const char* step) {
    const DWORD error = GetLastError();
    fail_with(step, error);
}

// An owned kernel handle, closed on destruction. Null when empty; the
// functions that report failure with INVALID_HANDLE_VALUE are checked before
// their result is adopted.
class OwnedHandle final {
public:
    OwnedHandle() noexcept = default;
    explicit OwnedHandle(HANDLE handle) noexcept : handle_(handle) {}
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
    ~OwnedHandle() { reset(); }
    void reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr) static_cast<void>(CloseHandle(handle_));
        handle_ = handle;
    }
    // Gives the handle up to the caller, leaving this empty.
    [[nodiscard]] HANDLE release() noexcept {
        const HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{};
};

// The read-only view of the frame region, unmapped on destruction.
class ReadMapping final {
public:
    ReadMapping() noexcept = default;
    ReadMapping(const ReadMapping&) = delete;
    ReadMapping& operator=(const ReadMapping&) = delete;
    ~ReadMapping() {
        if (address_ != nullptr) static_cast<void>(UnmapViewOfFile(address_));
    }
    [[nodiscard]] bool map(HANDLE section, std::size_t size) noexcept {
        void* const mapped = MapViewOfFile(section, FILE_MAP_READ, 0U, 0U, size);
        if (mapped == nullptr) return false;
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

// A manual-reset event, initially nonsignaled.
HANDLE make_event() {
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) fail_system("CreateEventW");
    return event;
}

// A wait bound for WaitForSingleObject and WaitForMultipleObjects: never
// negative and never INFINITE.
DWORD wait_bound(std::int64_t milliseconds) noexcept {
    const std::int64_t clamped = std::clamp<std::int64_t>(milliseconds, 0, static_cast<std::int64_t>(INFINITE) - 1);
    const DWORD bound = static_cast<DWORD>(clamped);
    return bound;
}

// -- writes (any thread, under the input state's mutex) ------------------------

// Starts writing the front of `state.in_flight`. False when the channel is
// broken. Called with the state's mutex held and no write in flight.
bool start_write_locked(detail::HostedInputState& state) noexcept {
    const std::size_t size = std::min(state.in_flight.size(), hosted_write_limit);
    state.write_overlapped = OVERLAPPED{};
    state.write_overlapped.hEvent = state.write_event;
    const BOOL written =
        WriteFile(state.channel, state.in_flight.data(), static_cast<DWORD>(size), nullptr, &state.write_overlapped);
    if (written == FALSE && GetLastError() != ERROR_IO_PENDING) return false;
    // Finished at once or pending: either way finish_write_locked collects it.
    state.writing = true;
    return true;
}

// Collects the write in flight if it has finished, then starts the next:
// the rest of the in-flight bytes, else the backlog. False when the channel
// is broken. Called with the state's mutex held.
bool finish_write_locked(detail::HostedInputState& state) noexcept {
    if (!state.writing) return true;
    DWORD transferred = 0U;
    if (GetOverlappedResult(state.channel, &state.write_overlapped, &transferred, FALSE) == FALSE) {
        if (GetLastError() == ERROR_IO_INCOMPLETE) return true;
        state.writing = false;
        return false;
    }
    state.writing = false;
    const std::ptrdiff_t written = static_cast<std::ptrdiff_t>(transferred);
    state.in_flight.erase(state.in_flight.begin(), state.in_flight.begin() + written);
    // The backlog becomes the next write; the emptied buffer keeps its
    // capacity as the new backlog.
    if (state.in_flight.empty()) state.in_flight.swap(state.backlog);
    if (state.in_flight.empty()) return true;
    const bool started = start_write_locked(state);
    return started;
}

// Cancels the write in flight and waits for it to complete, so the system
// no longer reads `in_flight` or the OVERLAPPED. Called with the state's
// mutex held, before the channel closes.
void cancel_write_locked(detail::HostedInputState& state) noexcept {
    if (!state.writing) return;
    static_cast<void>(CancelIoEx(state.channel, &state.write_overlapped));
    DWORD transferred = 0U;
    static_cast<void>(GetOverlappedResult(state.channel, &state.write_overlapped, &transferred, TRUE));
    state.writing = false;
}

// Queues one whole message behind the write in flight and the backlog, or
// starts writing it when nothing is in flight, so messages are never
// interleaved. `droppable` input is dropped instead when the backlog is
// over its limit, and any message when it is over the hard limit. Called
// with the state's mutex held.
bool enqueue_locked(detail::HostedInputState& state, std::span<const std::byte> message, bool droppable) {
    if (state.channel == nullptr) return false;
    if (!finish_write_locked(state)) return false;
    if (state.writing) {
        if (droppable && state.backlog.size() > hosted_input_backlog_limit) return false;
        if (state.backlog.size() > hosted_input_backlog_hard_limit) return false;
        state.backlog.insert(state.backlog.end(), message.begin(), message.end());
        return true;
    }
    state.in_flight.assign(message.begin(), message.end());
    const bool started = start_write_locked(state);
    return started;
}

// Moves the writes along without waiting. False when the channel is broken.
bool pump_writes(detail::HostedInputState& state) {
    const stxlib::ScopedLock lock(state.mutex);
    const bool healthy = finish_write_locked(state);
    return healthy;
}

bool write_in_flight(detail::HostedInputState& state) {
    const stxlib::ScopedLock lock(state.mutex);
    const bool writing = state.writing;
    return writing;
}

// -- reads (the worker's thread) -----------------------------------------------

// What collecting the pending read found.
enum class ReadOutcome : std::uint8_t {
    // Still pending.
    pending,
    // bytes() holds what arrived (possibly nothing).
    received,
    // The channel closed or broke; nothing more will arrive.
    closed,
};

// The worker's overlapped reads of the channel. A read stays pending across
// waits; cancel() waits for a pending one to complete, so the system no
// longer writes the buffer or the OVERLAPPED, and the destructor does so on
// every exit. Declare it after the pipe handle it reads, so it is destroyed
// first.
class ChannelReader final {
public:
    ChannelReader(HANDLE channel, HANDLE event) : channel_(channel), event_(event), chunk_(hosted_read_chunk) {}
    ChannelReader(const ChannelReader&) = delete;
    ChannelReader& operator=(const ChannelReader&) = delete;
    ~ChannelReader() { cancel(); }

    // Starts a read unless one is pending. False when the channel is closed
    // or broken.
    [[nodiscard]] bool start() noexcept {
        if (pending_) return true;
        overlapped_ = OVERLAPPED{};
        overlapped_.hEvent = event_;
        const BOOL read = ReadFile(channel_, chunk_.data(), static_cast<DWORD>(chunk_.size()), nullptr, &overlapped_);
        if (read == FALSE && GetLastError() != ERROR_IO_PENDING) return false;
        pending_ = true;
        return true;
    }
    // The pending read's outcome, without waiting. Only while pending().
    [[nodiscard]] ReadOutcome collect() noexcept {
        DWORD transferred = 0U;
        if (GetOverlappedResult(channel_, &overlapped_, &transferred, FALSE) == FALSE) {
            if (GetLastError() == ERROR_IO_INCOMPLETE) return ReadOutcome::pending;
            pending_ = false;
            return ReadOutcome::closed;
        }
        pending_ = false;
        received_ = static_cast<std::size_t>(transferred);
        return ReadOutcome::received;
    }
    // The bytes the last collected read received; valid until the next start().
    [[nodiscard]] std::span<const std::byte> bytes() const& noexcept {
        const std::span<const std::byte> view(chunk_.data(), received_);
        return view;
    }
    std::span<const std::byte> bytes() const&& = delete;
    [[nodiscard]] bool pending() const noexcept { return pending_; }
    // The event a pending read signals.
    [[nodiscard]] HANDLE event() const noexcept { return event_; }
    void cancel() noexcept {
        if (!pending_) return;
        static_cast<void>(CancelIoEx(channel_, &overlapped_));
        DWORD transferred = 0U;
        static_cast<void>(GetOverlappedResult(channel_, &overlapped_, &transferred, TRUE));
        pending_ = false;
    }

private:
    HANDLE channel_{};
    HANDLE event_{};
    OVERLAPPED overlapped_{};
    std::vector<std::byte> chunk_;
    std::size_t received_{};
    bool pending_{};
};

// -- the process ---------------------------------------------------------------

// How a process ended, once it has.
struct ProcessEnd final {
    bool ended{};
    DWORD exit_code{};
};

// The child process and its job. The process is killed on destruction if
// still running; closing the job then ends anything else in it (the
// program's own children), by JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
class ChildProcess final {
public:
    ChildProcess() noexcept = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess() { kill_and_wait(); }

    // Takes ownership of both handles.
    void adopt(HANDLE process, HANDLE job) noexcept {
        process_.reset(process);
        job_.reset(job);
        end_ = ProcessEnd{};
    }
    [[nodiscard]] HANDLE process() const noexcept { return process_.get(); }

    // The end, without waiting; `ended` false while it runs.
    [[nodiscard]] ProcessEnd poll() noexcept {
        if (end_.ended || process_.get() == nullptr) return end_;
        if (WaitForSingleObject(process_.get(), 0U) != WAIT_OBJECT_0) return end_;
        DWORD code = 0U;
        // The creator's handle has every access right, so this does not
        // fail for an ended process; if it did, the end reads as status 1.
        if (GetExitCodeProcess(process_.get(), &code) == FALSE) code = 1U;
        end_.ended = true;
        end_.exit_code = code;
        return end_;
    }

    // Waits up to `bound` for it to end on its own.
    [[nodiscard]] ProcessEnd wait_for(std::chrono::milliseconds bound) noexcept {
        if (!end_.ended && process_.get() != nullptr) {
            static_cast<void>(WaitForSingleObject(process_.get(), wait_bound(bound.count())));
        }
        const ProcessEnd end = poll();
        return end;
    }

    // Kills it if it still runs and waits up to the reap bound for it to end.
    void kill_and_wait() noexcept {
        if (process_.get() == nullptr || poll().ended) return;
        static_cast<void>(TerminateProcess(process_.get(), hosted_killed_exit_code));
        static_cast<void>(WaitForSingleObject(process_.get(), hosted_reap_milliseconds));
        static_cast<void>(poll());
    }

private:
    OwnedHandle process_{};
    OwnedHandle job_{};
    ProcessEnd end_{};
};

// "0x" and eight upper-case hexadecimal digits.
std::string ntstatus_text(DWORD status) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        const DWORD nibble = (status >> static_cast<DWORD>(shift)) & 0xFU;
        text.push_back(digits[nibble]);
    }
    return text;
}

// -- the launch ----------------------------------------------------------------

// `text` as UTF-16 in `out`; false for text that is not UTF-8 or too long
// for the conversion's int count.
bool widen_utf8(std::string_view text, std::wstring& out) {
    out.clear();
    if (text.empty()) return true;
    if (text.size() > static_cast<std::size_t>(INT_MAX)) return false;
    const int size = static_cast<int>(text.size());
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (needed <= 0) return false;
    out.resize(static_cast<std::size_t>(needed));
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, out.data(), needed);
    const bool converted = written == needed;
    return converted;
}

// An ASCII constant (the environment names) as UTF-16.
std::wstring ascii_wide(std::string_view text) {
    std::wstring wide{};
    wide.reserve(text.size());
    for (const char character : text) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    return wide;
}

// Appends `argument` to a command line so the C runtime's parser gives it
// back unchanged ("Parsing C++ command-line arguments"): quoted when empty
// or holding white space or a quote; inside quotes, a quote is preceded by
// an odd number of backslashes and the backslashes before it or before the
// closing quote are doubled; other backslashes stand as they are.
void append_quoted_argument(std::wstring& command_line, const std::wstring& argument) {
    const bool plain = !argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos;
    if (plain) {
        command_line += argument;
        return;
    }
    command_line.push_back(L'"');
    std::size_t index = 0U;
    for (;;) {
        std::size_t backslashes = 0U;
        while (index < argument.size() && argument[index] == L'\\') {
            ++backslashes;
            ++index;
        }
        if (index == argument.size()) {
            command_line.append(backslashes * 2U, L'\\');
            break;
        }
        if (argument[index] == L'"') {
            command_line.append(backslashes * 2U + 1U, L'\\');
        } else {
            command_line.append(backslashes, L'\\');
        }
        command_line.push_back(argument[index]);
        ++index;
    }
    command_line.push_back(L'"');
}

// The program and its command line, prepared and checked before anything
// starts.
struct LaunchPlan final {
    // The executable's path with the preferred separators.
    std::wstring application{};
    // The program name (the path, quoted), then the quoted arguments.
    std::wstring command_line{};
};

// Throws std::invalid_argument for an argument that is not UTF-8 or holds a
// NUL, and for a command line over CreateProcessW's limit.
LaunchPlan plan_launch(const HostedContentOptions& options) {
    LaunchPlan plan{};
    std::filesystem::path executable = options.executable;
    executable.make_preferred();
    plan.application = executable.native();
    if (plan.application.find(L'\0') != std::wstring::npos) {
        throw std::invalid_argument("hosted content's program path holds a NUL character");
    }
    // The program name is parsed without escapes, up to the closing quote; a
    // Windows path cannot hold a quote.
    plan.command_line.push_back(L'"');
    plan.command_line += plan.application;
    plan.command_line.push_back(L'"');
    std::wstring wide{};
    for (const std::string& argument : options.arguments) {
        if (argument.find('\0') != std::string::npos || !widen_utf8(argument, wide)) {
            throw std::invalid_argument("hosted content arguments must be UTF-8 text without NUL characters");
        }
        plan.command_line.push_back(L' ');
        append_quoted_argument(plan.command_line, wide);
    }
    if (plan.command_line.size() + 1U > maximum_command_line_characters) {
        throw std::invalid_argument("hosted content's command line exceeds 32767 characters");
    }
    return plan;
}

// A variable's name: up to the first '=' after its first character (the
// per-drive directory entries, "=C:=C:\...", begin with '=').
std::wstring_view environment_name(const std::wstring& entry) noexcept {
    const std::size_t equals = entry.find(L'=', 1U);
    const std::wstring_view name = std::wstring_view(entry).substr(0U, equals);
    return name;
}

// Compares two names without regard to case, as the system orders the
// block. Names are at most 32,767 characters, so their sizes fit an int.
int compare_environment_names(std::wstring_view left, std::wstring_view right) noexcept {
    // CompareStringOrdinal's failure value, for a name no environment block holds.
    if (left.size() > static_cast<std::size_t>(INT_MAX) || right.size() > static_cast<std::size_t>(INT_MAX)) return 0;
    const int order = CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                           static_cast<int>(right.size()), TRUE);
    return order;
}

struct EnvironmentNameBefore final {
    bool operator()(const std::wstring& left, const std::wstring& right) const noexcept {
        const int order = compare_environment_names(environment_name(left), environment_name(right));
        const bool before = order == CSTR_LESS_THAN;
        return before;
    }
};

// The process's environment strings, freed on destruction.
class EnvironmentStrings final {
public:
    EnvironmentStrings() : block_(GetEnvironmentStringsW()) {
        if (block_ == nullptr) fail_system("GetEnvironmentStringsW");
    }
    EnvironmentStrings(const EnvironmentStrings&) = delete;
    EnvironmentStrings& operator=(const EnvironmentStrings&) = delete;
    ~EnvironmentStrings() { static_cast<void>(FreeEnvironmentStringsW(block_)); }
    // The block: NUL-terminated entries, ended by an empty one.
    [[nodiscard]] const wchar_t* block() const noexcept { return block_; }

private:
    wchar_t* block_{};
};

// The environment block the child starts with (CREATE_UNICODE_ENVIRONMENT):
// the parent's variables with the hosted ones set, sorted by name; each
// entry NUL-terminated, then one more NUL.
std::vector<wchar_t> child_environment(std::uint64_t slot_bytes, HANDLE channel, HANDLE frames) {
    const std::uintptr_t channel_value = reinterpret_cast<std::uintptr_t>(channel);
    const std::uintptr_t frames_value = reinterpret_cast<std::uintptr_t>(frames);
    const std::array<std::wstring, 4> ours{
        ascii_wide(hosted_environment_channel) + L"=" + std::to_wstring(channel_value),
        ascii_wide(hosted_environment_frames) + L"=" + std::to_wstring(frames_value),
        ascii_wide(hosted_environment_slot_bytes) + L"=" + std::to_wstring(slot_bytes),
        ascii_wide(hosted_environment_slot_count) + L"=" + std::to_wstring(hosted_slot_count),
    };
    std::vector<std::wstring> entries{};
    {
        const EnvironmentStrings inherited;
        const wchar_t* entry = inherited.block();
        while (*entry != L'\0') {
            std::wstring variable(entry);
            entry += variable.size() + 1U;
            bool replaced = false;
            for (const std::wstring& setting : ours) {
                const int order = compare_environment_names(environment_name(variable), environment_name(setting));
                if (order == CSTR_EQUAL) replaced = true;
            }
            if (!replaced) entries.push_back(std::move(variable));
        }
    }
    for (const std::wstring& setting : ours) entries.push_back(setting);
    std::stable_sort(entries.begin(), entries.end(), EnvironmentNameBefore{});
    std::vector<wchar_t> block{};
    for (const std::wstring& variable : entries) {
        block.insert(block.end(), variable.begin(), variable.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

// The child's standard input, output and error: inheritable duplicates of
// this process's, or the NUL device where this process has none, so the
// program starts with all three open and its diagnostics go where this
// process's go (A-gf-host-02-F4, as on POSIX). They are passed with
// STARTF_USESTDHANDLES and listed in the handle list beside the channel
// and the frames.
class StandardHandles final {
public:
    StandardHandles() noexcept = default;
    StandardHandles(const StandardHandles&) = delete;
    StandardHandles& operator=(const StandardHandles&) = delete;

    void prepare() {
        const std::array<DWORD, 3> which{STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        const HANDLE process = GetCurrentProcess();
        for (std::size_t index = 0U; index < which.size(); ++index) {
            const HANDLE own = GetStdHandle(which[index]);
            const bool usable = own != nullptr && own != INVALID_HANDLE_VALUE && GetFileType(own) != FILE_TYPE_UNKNOWN;
            HANDLE copy = nullptr;
            if (usable && DuplicateHandle(process, own, process, &copy, 0U, TRUE, DUPLICATE_SAME_ACCESS) != FALSE) {
                handles_[index].reset(copy);
                continue;
            }
            SECURITY_ATTRIBUTES inheritable{};
            inheritable.nLength = sizeof(inheritable);
            inheritable.bInheritHandle = TRUE;
            const HANDLE null_device = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                   &inheritable, OPEN_EXISTING, 0U, nullptr);
            if (null_device == INVALID_HANDLE_VALUE) fail_system("opening the NUL device");
            handles_[index].reset(null_device);
        }
    }
    [[nodiscard]] HANDLE input() const noexcept { return handles_[0].get(); }
    [[nodiscard]] HANDLE output() const noexcept { return handles_[1].get(); }
    [[nodiscard]] HANDLE error() const noexcept { return handles_[2].get(); }

private:
    std::array<OwnedHandle, 3> handles_{};
};

// The attribute list naming the handles the child inherits. The list
// and the handle array stay here until CreateProcessW has read them. The
// storage comes from operator new, aligned for any scalar type, which the
// list's pointer-sized fields need.
class InheritedHandles final {
public:
    InheritedHandles(HANDLE channel, HANDLE frames, const StandardHandles& standard) noexcept
        : handles_{channel, frames, standard.input(), standard.output(), standard.error()} {}
    InheritedHandles(const InheritedHandles&) = delete;
    InheritedHandles& operator=(const InheritedHandles&) = delete;
    ~InheritedHandles() {
        if (list_ != nullptr) DeleteProcThreadAttributeList(list_);
    }

    void prepare() {
        SIZE_T size = 0U;
        // Answers ERROR_INSUFFICIENT_BUFFER with the size it needs.
        static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1U, 0U, &size));
        if (size == 0U) fail_system("sizing the attribute list");
        storage_.resize(size);
        const LPPROC_THREAD_ATTRIBUTE_LIST list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (InitializeProcThreadAttributeList(list, 1U, 0U, &size) == FALSE) {
            fail_system("InitializeProcThreadAttributeList");
        }
        list_ = list;
        const SIZE_T handle_bytes = sizeof(HANDLE) * handles_.size();
        if (UpdateProcThreadAttribute(list_, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_.data(), handle_bytes,
                                      nullptr, nullptr) == FALSE) {
            fail_system("UpdateProcThreadAttribute");
        }
    }
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST list() const noexcept { return list_; }

private:
    std::array<HANDLE, 5> handles_{};
    std::vector<std::byte> storage_{};
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

// The frame region: a pagefile-backed section of `size` bytes. Its size is
// fixed, so the program cannot make the parent's reads fault by shrinking it.
HANDLE create_frame_region(std::uint64_t size) {
    const DWORD high = static_cast<DWORD>(size >> 32U);
    const DWORD low = static_cast<DWORD>(size & 0xFFFFFFFFULL);
    const HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, high, low, nullptr);
    if (section == nullptr) fail_system("CreateFileMappingW");
    return section;
}

// The child's copy of the section: inheritable, and able only to map it.
HANDLE inheritable_section(HANDLE section) {
    HANDLE copy = nullptr;
    const HANDLE process = GetCurrentProcess();
    if (DuplicateHandle(process, section, process, &copy, FILE_MAP_READ | FILE_MAP_WRITE, TRUE, 0U) == FALSE) {
        fail_system("duplicating the frame region");
    }
    return copy;
}

// The channel: the parent's end of a one-instance, local-only byte pipe
// (overlapped), and the child's end, opened at once (overlapped,
// inheritable), so no other client can take the instance.
void create_channel(OwnedHandle& parent_end, OwnedHandle& child_end) {
    static std::atomic<std::uint32_t> channel_counter{0U};
    const std::uint32_t serial = channel_counter.fetch_add(1U, std::memory_order_relaxed);
    std::wstring name = L"\\\\.\\pipe\\gui_forms_hosted.";
    name += std::to_wstring(GetCurrentProcessId());
    name += L".";
    name += std::to_wstring(serial);
    name += L".";
    name += std::to_wstring(GetTickCount64());
    const DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    const DWORD pipe_mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
    const HANDLE server = CreateNamedPipeW(name.c_str(), open_mode, pipe_mode, 1U, hosted_pipe_buffer_bytes,
                                           hosted_pipe_buffer_bytes, 0U, nullptr);
    if (server == INVALID_HANDLE_VALUE) fail_system("CreateNamedPipeW");
    parent_end.reset(server);
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    // Identification only: the parent cannot act as the program's user.
    const DWORD flags = FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION;
    const HANDLE client =
        CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, &inheritable, OPEN_EXISTING, flags, nullptr);
    if (client == INVALID_HANDLE_VALUE) fail_system("opening the channel's child end");
    child_end.reset(client);
}

// Starts the program suspended with exactly `channel`, `frames` and its
// standard handles inherited, puts it in a new job that kills it with its last handle and on
// an unhandled exception, resumes it, and hands both handles to `child`.
void spawn_child(const LaunchPlan& plan, std::uint64_t slot_bytes, HANDLE channel, HANDLE frames,
                 ChildProcess& child) {
    OwnedHandle job(CreateJobObjectW(nullptr, nullptr));
    if (job.get() == nullptr) fail_system("CreateJobObjectW");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE) {
        fail_system("SetInformationJobObject");
    }
    StandardHandles standard;
    standard.prepare();
    InheritedHandles inherited(channel, frames, standard);
    inherited.prepare();
    std::vector<wchar_t> environment = child_environment(slot_bytes, channel, frames);
    // CreateProcessW may write into the command line, so it gets a copy.
    std::vector<wchar_t> command_line(plan.command_line.begin(), plan.command_line.end());
    command_line.push_back(L'\0');
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = inherited.list();
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = standard.input();
    startup.StartupInfo.hStdOutput = standard.output();
    startup.StartupInfo.hStdError = standard.error();
    DWORD creation = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    // A console program shares the application's console when it has one;
    // an application without one (a GUI program) gets no console window.
    if (GetConsoleWindow() == nullptr) creation |= CREATE_NO_WINDOW;
    PROCESS_INFORMATION started{};
    if (CreateProcessW(plan.application.c_str(), command_line.data(), nullptr, nullptr, TRUE, creation,
                       environment.data(), nullptr, &startup.StartupInfo, &started) == FALSE) {
        fail_system("CreateProcessW");
    }
    OwnedHandle process(started.hProcess);
    const OwnedHandle thread(started.hThread);
    if (AssignProcessToJobObject(job.get(), process.get()) == FALSE) {
        const DWORD error = GetLastError();
        static_cast<void>(TerminateProcess(process.get(), hosted_killed_exit_code));
        fail_with("AssignProcessToJobObject", error);
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        static_cast<void>(TerminateProcess(process.get(), hosted_killed_exit_code));
        fail_with("ResumeThread", error);
    }
    child.adopt(process.release(), job.release());
}

// Sets the stop event when the surface asks the program to stop, so the
// worker's wait returns at once: a StopCallback's function, whose
// `context` is the event's HANDLE.
void wake_on_stop(void* const context) noexcept {
    const HANDLE event = *static_cast<const HANDLE*>(context);
    static_cast<void>(SetEvent(event));
}

// The running program, on the content worker's thread.
class WindowsHostedProgram final : public ContentProgram {
public:
    WindowsHostedProgram(HostedContentOptions options, LaunchPlan plan,
                         std::shared_ptr<detail::HostedInputState> input)
        : options_(std::move(options)), plan_(std::move(plan)), input_(std::move(input)) {}

    void run(ContentContext& context) override;

private:
    // Queues one parent message behind any backlog. Returns true when the
    // backlog is past its hard limit (the program stopped reading); the
    // message is then not queued.
    [[nodiscard]] bool queue_control(HostedMessageKind kind, std::span<const std::byte> payload);
    // queue_control, failing the protocol when the program stopped reading.
    void send_control(HostedMessageKind kind, std::span<const std::byte> payload);
    void send_configure(const ContentRequest& request);
    // Handles one message; throws std::runtime_error for a protocol breach.
    void handle(const HostedMessage& message, ContentFrameSink& sink, std::span<const std::byte> region);
    // Stops the process: a stop message, the grace, then TerminateProcess.
    void stop_process(ChannelReader& reader, HANDLE write_event);
    // The channel closed (or the process ended and its channel stayed
    // open): turns the process's end into the program's outcome (returns
    // for a clean exit, throws otherwise).
    void end_after_disconnect();
    [[noreturn]] void fail_protocol(const std::string& reason);
    void connect_input(HANDLE channel, HANDLE write_event);
    void disconnect_input() noexcept;

    HostedContentOptions options_;
    LaunchPlan plan_;
    std::shared_ptr<detail::HostedInputState> input_;
    ChildProcess child_{};
    std::vector<std::byte> outgoing_{};
    std::vector<std::byte> payload_{};
    bool started_{};
};

// Kills the child, if it still runs, on every exit from run(): the process
// never outlives its session.
struct ChildGuard final {
    ChildProcess& child;
    ~ChildGuard() { child.kill_and_wait(); }
};

void WindowsHostedProgram::connect_input(HANDLE channel, HANDLE write_event) {
    const stxlib::ScopedLock lock((*input_).mutex);
    (*input_).channel = channel;
    (*input_).write_event = write_event;
    (*input_).writing = false;
    (*input_).in_flight.clear();
    (*input_).backlog.clear();
}

void WindowsHostedProgram::disconnect_input() noexcept {
    const stxlib::ScopedLock lock((*input_).mutex);
    cancel_write_locked(*input_);
    (*input_).channel = nullptr;
    (*input_).write_event = nullptr;
    (*input_).in_flight.clear();
    (*input_).backlog.clear();
    (*input_).connected.store(false, std::memory_order_release);
}

bool WindowsHostedProgram::queue_control(HostedMessageKind kind, std::span<const std::byte> payload) {
    outgoing_.clear();
    append_hosted_message(outgoing_, kind, payload);
    bool stalled = false;
    {
        const stxlib::ScopedLock lock((*input_).mutex);
        const bool queued = enqueue_locked(*input_, outgoing_, false);
        stalled = !queued && (*input_).channel != nullptr &&
                  (*input_).backlog.size() > hosted_input_backlog_hard_limit;
    }
    return stalled;
}

void WindowsHostedProgram::send_control(HostedMessageKind kind, std::span<const std::byte> payload) {
    // A program whose reader has stopped while it keeps sending frames
    // would grow the backlog without end (A-gf-host-02-F8).
    if (queue_control(kind, payload)) fail_protocol("its input channel stopped reading");
}

void WindowsHostedProgram::send_configure(const ContentRequest& request) {
    HostedConfigureRecord record{};
    record.width = request.size.width;
    record.height = request.size.height;
    record.scale = request.scale;
    record.generation = request.generation;
    encode_hosted_configure(payload_, record);
    send_control(HostedMessageKind::configure, payload_);
}

void WindowsHostedProgram::fail_protocol(const std::string& reason) {
    child_.kill_and_wait();
    throw std::runtime_error("hosted program broke the protocol: " + reason);
}

void WindowsHostedProgram::handle(const HostedMessage& message, ContentFrameSink& sink,
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

void WindowsHostedProgram::stop_process(ChannelReader& reader, HANDLE write_event) {
    // Stopping anyway: a stalled reader only shortens the grace's use.
    static_cast<void>(queue_control(HostedMessageKind::stop, {}));
    // The grace: the program may exit on its own. Its writes are read and
    // discarded meanwhile, so a program blocked writing to a full channel
    // can still see the stop and leave.
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + options_.stop_grace;
    for (;;) {
        if (child_.poll().ended) return;
        const std::chrono::steady_clock::duration left = deadline - std::chrono::steady_clock::now();
        const std::int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(left).count();
        if (milliseconds <= 0) break;
        std::array<HANDLE, 3> watched{};
        DWORD count = 0U;
        watched[count] = child_.process();
        ++count;
        if (reader.pending()) {
            watched[count] = reader.event();
            ++count;
        }
        if (write_in_flight(*input_)) {
            watched[count] = write_event;
            ++count;
        }
        static_cast<void>(WaitForMultipleObjects(count, watched.data(), FALSE, wait_bound(milliseconds)));
        static_cast<void>(pump_writes(*input_));
        if (!reader.pending()) continue;
        const ReadOutcome outcome = reader.collect();
        if (outcome == ReadOutcome::received) static_cast<void>(reader.start());
    }
    child_.kill_and_wait();
}

void WindowsHostedProgram::end_after_disconnect() {
    const ProcessEnd end = child_.wait_for(options_.stop_grace + hosted_exit_drain);
    if (!end.ended) {
        child_.kill_and_wait();
        throw std::runtime_error("hosted program closed its channel and did not exit; it was killed");
    }
    if (end.exit_code == 0U) return;
    if (end.exit_code >= hosted_crash_status_floor) {
        throw std::runtime_error("hosted program crashed: NTSTATUS " + ntstatus_text(end.exit_code));
    }
    throw std::runtime_error("hosted program exited with status " + std::to_string(end.exit_code));
}

void WindowsHostedProgram::run(ContentContext& context) {
    ContentFrameSink sink = context.sink();
    const std::uint64_t region_bytes = options_.slot_bytes * hosted_slot_count;
    const OwnedHandle frames(create_frame_region(region_bytes));
    ReadMapping mapping;
    if (!mapping.map(frames.get(), static_cast<std::size_t>(region_bytes))) fail_system("MapViewOfFile");
    const OwnedHandle stop_event(make_event());
    const OwnedHandle read_event(make_event());
    const OwnedHandle write_event(make_event());
    OwnedHandle parent_end;
    OwnedHandle child_end;
    create_channel(parent_end, child_end);
    OwnedHandle child_frames(inheritable_section(frames.get()));

    const ChildGuard child_guard{child_};
    spawn_child(plan_, options_.slot_bytes, child_end.get(), child_frames.get(), child_);
    // The child holds its own copies now; with the parent's copy of its end
    // closed, the channel breaks when the child's closes.
    child_end.reset();
    child_frames.reset();
    connect_input(parent_end.get(), write_event.get());
    // Disconnects the input (cancelling a write in flight) before the pipe
    // handle closes, on every exit.
    struct InputGuard final {
        WindowsHostedProgram& program;
        ~InputGuard() { program.disconnect_input(); }
    };
    const InputGuard input_guard{*this};
    ChannelReader reader(parent_end.get(), read_event.get());
    // Declared before the callback that borrows it.
    HANDLE wake_event = stop_event.get();
    const stxlib::StopCallback wake(context.stop_token(), &wake_on_stop, &wake_event);

    HostedMessageReader messages(true);
    HostedMessage message{};
    std::uint64_t configured_generation = 0U;
    const std::chrono::steady_clock::time_point start_deadline =
        std::chrono::steady_clock::now() + options_.start_timeout;
    std::optional<std::chrono::steady_clock::time_point> exited_at{};
    bool disconnected = !reader.start();
    for (;;) {
        if (context.stop_requested()) {
            stop_process(reader, write_event.get());
            return;
        }
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (started_) {
            const ContentRequest request = sink.request();
            if (request.generation != configured_generation) {
                configured_generation = request.generation;
                send_configure(request);
            }
        } else if (now >= start_deadline) {
            child_.kill_and_wait();
            throw std::runtime_error("hosted program sent no hello within " +
                                     std::to_string(options_.start_timeout.count()) + " ms");
        }
        if (disconnected) {
            end_after_disconnect();
            return;
        }
        if (exited_at && now - *exited_at > options_.stop_grace + hosted_exit_drain) {
            // The process ended and something else still holds its channel.
            end_after_disconnect();
            return;
        }
        std::array<HANDLE, 4> watched{};
        DWORD count = 0U;
        watched[count] = stop_event.get();
        ++count;
        if (reader.pending()) {
            watched[count] = read_event.get();
            ++count;
        }
        if (!exited_at) {
            watched[count] = child_.process();
            ++count;
        }
        if (write_in_flight(*input_)) {
            watched[count] = write_event.get();
            ++count;
        }
        const DWORD waited = WaitForMultipleObjects(count, watched.data(), FALSE, hosted_wait_milliseconds);
        if (waited == WAIT_FAILED) fail_system("WaitForMultipleObjects");
        // Whatever woke the wait, every source is looked at without waiting.
        static_cast<void>(pump_writes(*input_));
        if (!exited_at && child_.poll().ended) exited_at = std::chrono::steady_clock::now();
        if (!reader.pending()) continue;
        const ReadOutcome outcome = reader.collect();
        if (outcome == ReadOutcome::closed) {
            disconnected = true;
        } else if (outcome == ReadOutcome::received) {
            messages.feed(reader.bytes());
            for (;;) {
                const HostedReadResult result = messages.next(message);
                if (result == HostedReadResult::need_more) break;
                if (result != HostedReadResult::message) fail_protocol(std::string(hosted_read_result_name(result)));
                handle(message, sink, mapping.bytes());
            }
            if (!reader.start()) disconnected = true;
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
        const stxlib::ScopedLock lock((*state_).mutex);
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
    LaunchPlan plan = plan_launch(options);
    std::shared_ptr<detail::HostedInputState> state = gui_forms::detail::make_library_shared<detail::HostedInputState>();
    HostedContent made{};
    made.program = std::make_unique<WindowsHostedProgram>(std::move(options), std::move(plan), state);
    made.input = HostedInput(std::move(state));
    return made;
}

} // namespace gui_forms
