// The attach endpoint on Windows (src/runtime/automation/attach_endpoint.hpp):
// a named pipe that only the user can open.
//
// - The name: \\.\pipe\gui-forms-attach.<user SID>.<pid>.<identity>. The
//   first instance is made with FILE_FLAG_FIRST_PIPE_INSTANCE, so a pipe of
//   that name made earlier by anyone else fails the endpoint instead of
//   sharing it.
// - The DACL grants generic all to the user's SID and nothing to anyone
//   else ("D:P(A;;GA;;;<SID>)", protected from inheritance), and the pipe
//   rejects remote clients (PIPE_REJECT_REMOTE_CLIENTS).
// - Each client is checked again before anything is read: its process's
//   token user (GetNamedPipeClientProcessId, OpenProcessToken, TokenUser)
//   must equal the server's (attach_peer_allowed over the two SIDs' equality);
//   another user's client, or one whose user cannot be read, is
//   disconnected unread.
// - accept blocks in ConnectNamedPipe on a synchronous instance (the
//   streams read and write it with ReadFile and WriteFile, as STXlib's
//   pipes do); interrupt() sets the flag and opens the pipe once as a
//   client, which ends the wait.
// - The bridge opens the pipe with SECURITY_IDENTIFICATION (the program can
//   learn who connected but never act as the bridge's user), and checks the
//   server process's user the same way before it says hello.

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif

#include "../../../runtime/automation/attach_endpoint.hpp"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gui_forms::automation {

namespace {

constexpr std::wstring_view pipe_root = L"\\\\.\\pipe\\";
constexpr std::wstring_view pipe_prefix = L"gui-forms-attach.";
constexpr DWORD pipe_buffer = 65536U;
constexpr DWORD connect_wait_milliseconds = 5000U;

std::string host_message(DWORD code) {
    const std::string message = std::system_category().message(static_cast<int>(code));
    return message;
}

std::wstring wide(std::string_view text) {
    if (text.empty()) return std::wstring{};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return std::wstring{};
    std::wstring made(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), made.data(), count));
    return made;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) return std::string{};
    const int count =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return std::string{};
    std::string made(static_cast<std::size_t>(count), '\0');
    static_cast<void>(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), made.data(), count,
                                          nullptr, nullptr));
    return made;
}

// Closes a handle when it leaves scope.
class OwnedHandle final {
public:
    explicit OwnedHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~OwnedHandle() {
        if (valid()) static_cast<void>(CloseHandle(handle_));
    }
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{};
};

// The token user of `process`'s token, as its SID's bytes; empty when it
// cannot be read.
std::vector<unsigned char> token_user(HANDLE process) {
    std::vector<unsigned char> sid{};
    HANDLE token_handle = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token_handle)) return sid;
    const OwnedHandle token(token_handle);
    DWORD size = 0U;
    static_cast<void>(GetTokenInformation(token.get(), TokenUser, nullptr, 0U, &size));
    if (size == 0U) return sid;
    std::vector<unsigned char> buffer(size);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) return sid;
    const TOKEN_USER& user = *reinterpret_cast<const TOKEN_USER*>(buffer.data());
    const DWORD length = GetLengthSid(user.User.Sid);
    sid.resize(length);
    if (!CopySid(length, sid.data(), user.User.Sid)) sid.clear();
    return sid;
}

// The process `process_id`'s token user; empty when it cannot be read.
std::vector<unsigned char> process_user(ULONG process_id) {
    const OwnedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    if (!process.valid()) return std::vector<unsigned char>{};
    std::vector<unsigned char> sid = token_user(process.get());
    return sid;
}

// Whether two SIDs read as bytes name one user (attach_peer_allowed's rule
// for SIDs).
bool same_user(const std::vector<unsigned char>& own, const std::vector<unsigned char>& peer) noexcept {
    if (own.empty() || peer.empty()) return false;
    const bool same = EqualSid(const_cast<unsigned char*>(own.data()), const_cast<unsigned char*>(peer.data())) != 0;
    return attach_peer_allowed(same ? 1U : 0U, 1U);
}

// The SID's string form ("S-1-5-21-..."); empty when it cannot be made.
std::wstring sid_text(const std::vector<unsigned char>& sid) {
    if (sid.empty()) return std::wstring{};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(const_cast<unsigned char*>(sid.data()), &text)) return std::wstring{};
    std::wstring made(text);
    static_cast<void>(LocalFree(text));
    return made;
}

std::wstring endpoint_identity(const std::string& identity) {
    std::wstring kept{};
    for (const char character : identity) {
        const bool plain = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') || character == '.' || character == '-' ||
                           character == '_';
        kept += plain ? static_cast<wchar_t>(character) : L'-';
        if (kept.size() == 96U) break;
    }
    if (kept.empty()) kept = L"program";
    return kept;
}

// The prefix every endpoint of the user carries: gui-forms-attach.<SID>.
std::wstring user_prefix(const std::vector<unsigned char>& own) {
    std::wstring prefix(pipe_prefix);
    prefix += sid_text(own);
    prefix += L'.';
    return prefix;
}

bool process_alive(std::int64_t process_id) noexcept {
    if (process_id <= 0) return false;
    const OwnedHandle process(OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(process_id)));
    if (!process.valid()) return GetLastError() == ERROR_ACCESS_DENIED;
    return WaitForSingleObject(process.get(), 0U) == WAIT_TIMEOUT;
}

// The two streams over one pipe handle: the handle reads, a duplicate
// writes. Closes `pipe` on a failure.
bool streams_over(HANDLE pipe, AttachStreams& streams) noexcept {
    HANDLE writer = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), pipe, GetCurrentProcess(), &writer, 0U, FALSE, DUPLICATE_SAME_ACCESS)) {
        static_cast<void>(CloseHandle(pipe));
        return false;
    }
    streams.input = stxlib::PipeReader::adopt(static_cast<stxlib::i64>(reinterpret_cast<std::intptr_t>(pipe)));
    streams.output = stxlib::PipeWriter::adopt(static_cast<stxlib::i64>(reinterpret_cast<std::intptr_t>(writer)));
    return true;
}

} // namespace

struct AttachEndpoint::State final {
    std::wstring name{};
    std::string address{};
    std::vector<unsigned char> own{};
    PSECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};
    HANDLE instance{INVALID_HANDLE_VALUE};
    bool first{true};
    std::atomic<bool> interrupted{false};

    // A new instance, waiting for its client; false with `error` set.
    bool make_instance(std::string& error) {
        const DWORD open_mode = PIPE_ACCESS_DUPLEX | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0U);
        const DWORD pipe_mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
        instance = CreateNamedPipeW(name.c_str(), open_mode, pipe_mode, PIPE_UNLIMITED_INSTANCES, pipe_buffer,
                                    pipe_buffer, 0U, &attributes);
        if (instance == INVALID_HANDLE_VALUE) {
            error = "the attach pipe could not be made: " + host_message(GetLastError());
            return false;
        }
        first = false;
        return true;
    }
};

AttachEndpoint::AttachEndpoint(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<AttachEndpoint> AttachEndpoint::listen(const std::string& identity, std::string& error) {
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).own = token_user(GetCurrentProcess());
    const std::wstring sid = sid_text((*state).own);
    if (sid.empty()) {
        error = "the user's SID could not be read";
        return nullptr;
    }
    const std::wstring descriptor_text = L"D:P(A;;GA;;;" + sid + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor_text.c_str(), SDDL_REVISION_1,
                                                              &(*state).descriptor, nullptr)) {
        error = "the attach pipe's security descriptor could not be made: " + host_message(GetLastError());
        return nullptr;
    }
    (*state).attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    (*state).attributes.lpSecurityDescriptor = (*state).descriptor;
    (*state).attributes.bInheritHandle = FALSE;
    (*state).name = std::wstring(pipe_root) + user_prefix((*state).own) +
                    std::to_wstring(static_cast<unsigned long>(GetCurrentProcessId())) + L"." +
                    endpoint_identity(identity);
    (*state).address = narrow((*state).name);
    std::unique_ptr<AttachEndpoint> endpoint(new AttachEndpoint(std::move(state)));
    // The first instance now: it claims the name, so a pipe someone else
    // made under it fails here rather than later.
    if (!(*(*endpoint).state_).make_instance(error)) return nullptr;
    return endpoint;
}

AttachEndpoint::~AttachEndpoint() {
    if (!state_) return;
    State& state = *state_;
    if (state.instance != INVALID_HANDLE_VALUE) static_cast<void>(CloseHandle(state.instance));
    if (state.descriptor != nullptr) static_cast<void>(LocalFree(state.descriptor));
}

AttachWait AttachEndpoint::accept(AttachStreams& streams, std::string& refused) {
    State& state = *state_;
    for (;;) {
        if (state.interrupted.load()) return AttachWait::interrupted;
        if (state.instance == INVALID_HANDLE_VALUE && !state.make_instance(refused)) return AttachWait::failed;
        const BOOL connected = ConnectNamedPipe(state.instance, nullptr);
        const DWORD code = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && code != ERROR_PIPE_CONNECTED) {
            if (code == ERROR_NO_DATA) {
                // The client came and went: the next one.
                static_cast<void>(DisconnectNamedPipe(state.instance));
                continue;
            }
            refused = "the attach endpoint stopped: " + host_message(code);
            return AttachWait::failed;
        }
        HANDLE pipe = state.instance;
        state.instance = INVALID_HANDLE_VALUE;
        if (state.interrupted.load()) {
            static_cast<void>(DisconnectNamedPipe(pipe));
            static_cast<void>(CloseHandle(pipe));
            return AttachWait::interrupted;
        }
        ULONG client = 0U;
        if (!GetNamedPipeClientProcessId(pipe, &client)) {
            static_cast<void>(DisconnectNamedPipe(pipe));
            static_cast<void>(CloseHandle(pipe));
            refused = "a connection whose process could not be read was refused";
            return AttachWait::refused;
        }
        if (!same_user(state.own, process_user(client))) {
            static_cast<void>(DisconnectNamedPipe(pipe));
            static_cast<void>(CloseHandle(pipe));
            refused = "a connection from another user was refused";
            return AttachWait::refused;
        }
        if (!streams_over(pipe, streams)) {
            refused = "a connection could not be taken (no handle for its writer)";
            return AttachWait::refused;
        }
        return AttachWait::connected;
    }
}

void AttachEndpoint::interrupt() noexcept {
    State& state = *state_;
    state.interrupted.store(true);
    // Opening the pipe once ends a ConnectNamedPipe that waits for it.
    const OwnedHandle poke(CreateFileW(state.name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
                                       SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
}

const std::string& AttachEndpoint::address() const noexcept {
    return (*state_).address;
}

namespace {

// Orders listings by process ID.
struct OlderProcess final {
    bool operator()(const AttachListing& left, const AttachListing& right) const noexcept {
        return left.process < right.process;
    }
};

} // namespace

std::vector<AttachListing> list_attach_endpoints() {
    std::vector<AttachListing> listings{};
    const std::vector<unsigned char> own = token_user(GetCurrentProcess());
    const std::wstring prefix = user_prefix(own);
    if (own.empty()) return listings;
    WIN32_FIND_DATAW found{};
    const std::wstring pattern = std::wstring(pipe_root) + L"*";
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return listings;
    do {
        const std::wstring_view name(found.cFileName);
        if (name.size() <= prefix.size() || name.substr(0U, prefix.size()) != prefix) continue;
        const std::wstring_view rest = name.substr(prefix.size());
        const std::size_t dot = rest.find(L'.');
        if (dot == std::wstring_view::npos || dot == 0U) continue;
        const std::string process_text = narrow(rest.substr(0U, dot));
        std::int64_t process = 0;
        const std::from_chars_result parsed =
            std::from_chars(process_text.data(), process_text.data() + process_text.size(), process);
        if (parsed.ec != std::errc{} || !process_alive(process)) continue;
        AttachListing listing{};
        listing.process = process;
        listing.identity = narrow(rest.substr(dot + 1U));
        listing.address = narrow(std::wstring(pipe_root) + std::wstring(name));
        listings.push_back(std::move(listing));
    } while (FindNextFileW(search, &found));
    static_cast<void>(FindClose(search));
    std::sort(listings.begin(), listings.end(), OlderProcess{});
    return listings;
}

void abort_attach_connection(std::int64_t handle) noexcept {
    if (handle == 0 || handle == -1) return;
    static_cast<void>(CancelIoEx(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(handle)), nullptr));
}

bool connect_attach_endpoint(const std::string& address, AttachStreams& streams, std::string& error) {
    const std::wstring name = wide(address);
    if (name.empty()) {
        error = "the attach address is empty";
        return false;
    }
    if (!WaitNamedPipeW(name.c_str(), connect_wait_milliseconds)) {
        error = "the program at " + address + " could not be reached: " + host_message(GetLastError());
        return false;
    }
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
                              SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        error = "the program at " + address + " could not be opened: " + host_message(GetLastError());
        return false;
    }
    ULONG server = 0U;
    if (!GetNamedPipeServerProcessId(pipe, &server) ||
        !same_user(token_user(GetCurrentProcess()), process_user(server))) {
        static_cast<void>(CloseHandle(pipe));
        error = "the pipe at " + address + " is not served by a program of this user";
        return false;
    }
    if (!streams_over(pipe, streams)) {
        error = "no handle was left for the connection's writer";
        return false;
    }
    return true;
}

} // namespace gui_forms::automation
