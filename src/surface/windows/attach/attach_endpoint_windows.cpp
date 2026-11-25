// The attach endpoint on Windows (src/runtime/automation/attach_endpoint.hpp):
// an AF_UNIX stream socket (Windows 10 1803 and later), as on the POSIX
// hosts, so the same clients work everywhere.
//
// - The run directory: %LOCALAPPDATA%\gui-forms\run (STXlib's
//   known_directory, user_cache), given a protected DACL that grants only
//   the user's SID ("D:P(A;OICI;GA;;;<SID>)"), whether this process made
//   it or found it: setting the DACL fails on a directory the user does
//   not own, and that refuses the endpoint. The socket file and the
//   discovery entry inherit it.
// - The socket: <pid>.sock, bound in that directory; the discovery entry
//   <pid>.info, written whole to <pid>.info.new and moved into place.
// - The sockets are made without WSA_FLAG_OVERLAPPED, so their handles take
//   STXlib's synchronous ReadFile and WriteFile, as a pipe's do (AF_UNIX's
//   provider is a kernel one, whose socket handles are file handles).
// - Each accepted peer is checked before anything is read: its process
//   (SIO_AF_UNIX_GETPEERPID) must have the server's token user
//   (attach_peer_allowed over the two SIDs' equality); another user's
//   connection, or one whose user cannot be read, is closed unread.
// - interrupt() sets the flag and connects once, which ends the wait.

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

#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#include <afunix.h>
#include <sddl.h>

#include <stxlib/clocks.hpp>
#include <stxlib/files.hpp>
#include <stxlib/path.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/threads.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(SIO_AF_UNIX_GETPEERPID)
#define SIO_AF_UNIX_GETPEERPID _WSAIOR(IOC_VENDOR, 256)
#endif

namespace gui_forms::automation {

namespace {

constexpr std::string_view socket_suffix = ".sock";
constexpr std::string_view info_suffix = ".info";
constexpr int listen_backlog = 8;
constexpr int peek_slice_milliseconds = 10;

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

// Winsock, started once per call that needs it (WSAStartup counts; the
// count is left for the process's end).
bool winsock_ready() noexcept {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

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

bool process_alive(std::int64_t process_id) noexcept {
    if (process_id <= 0) return false;
    const OwnedHandle process(OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(process_id)));
    if (!process.valid()) return GetLastError() == ERROR_ACCESS_DENIED;
    return WaitForSingleObject(process.get(), 0U) == WAIT_TIMEOUT;
}

// The run directory's path (backslashes), or empty.
std::string run_directory_path() {
    const stxlib::OperationResult<stxlib::Path> found = stxlib::known_directory(stxlib::KnownDirectory::user_cache);
    if (found.failed()) return std::string{};
    const stxlib::PathText text = (*found.value()).text();
    if (!text.is_unicode()) return std::string{};
    const stxlib::TextView view = text.unicode();
    std::string directory(view.data(), static_cast<std::size_t>(view.count()));
    std::replace(directory.begin(), directory.end(), '/', '\\');
    while (directory.size() > 3U && directory.back() == '\\') directory.pop_back();
    if (directory.empty()) return directory;
    return directory + "\\gui-forms\\run";
}

// Gives `directory` a protected DACL granting only `sid`, inherited by
// what is made in it. False when it cannot be set (a directory of someone
// else's).
bool make_private(const std::string& directory, const std::vector<unsigned char>& sid) {
    const std::wstring text = L"D:P(A;OICI;GA;;;" + sid_text(sid) + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return false;
    }
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL acl = nullptr;
    bool set = false;
    if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present) {
        std::wstring path = wide(directory);
        const DWORD result =
            SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                  acl, nullptr);
        set = result == ERROR_SUCCESS;
    }
    static_cast<void>(LocalFree(descriptor));
    return set;
}

void close_socket(SOCKET socket_handle) noexcept {
    if (socket_handle != INVALID_SOCKET) static_cast<void>(closesocket(socket_handle));
}

// A synchronous AF_UNIX stream socket (no WSA_FLAG_OVERLAPPED), so ReadFile
// and WriteFile take its handle.
SOCKET unix_socket() noexcept {
    const SOCKET made = WSASocketW(AF_UNIX, SOCK_STREAM, 0, nullptr, 0U, WSA_FLAG_NO_HANDLE_INHERIT);
    return made;
}

bool socket_address(const std::string& path, sockaddr_un& address) noexcept {
    address = sockaddr_un{};
    address.sun_family = AF_UNIX;
    if (path.size() + 1U > sizeof(address.sun_path)) return false;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
    return true;
}

bool set_blocking(SOCKET socket_handle, bool blocking) noexcept {
    u_long mode = blocking ? 0U : 1U;
    return ioctlsocket(socket_handle, FIONBIO, &mode) == 0;
}

// The two streams over one connected socket: the socket reads, a
// duplicate of its handle writes. Closes the socket on a failure.
bool streams_over(SOCKET socket_handle, AttachStreams& streams) noexcept {
    const HANDLE reader = reinterpret_cast<HANDLE>(socket_handle);
    HANDLE writer = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), reader, GetCurrentProcess(), &writer, 0U, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        close_socket(socket_handle);
        return false;
    }
    streams.input = stxlib::PipeReader::adopt(static_cast<stxlib::i64>(reinterpret_cast<std::intptr_t>(reader)));
    streams.output = stxlib::PipeWriter::adopt(static_cast<stxlib::i64>(reinterpret_cast<std::intptr_t>(writer)));
    return true;
}

std::int64_t name_process(std::string_view name, std::string_view suffix) noexcept {
    if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) return 0;
    const std::size_t digits = name.size() - suffix.size();
    std::int64_t process = 0;
    const std::from_chars_result parsed = std::from_chars(name.data(), name.data() + digits, process);
    if (parsed.ec != std::errc{} || parsed.ptr != name.data() + digits) return 0;
    return process;
}

bool read_small_file(const std::string& path, std::size_t limit, std::string& text) {
    const OwnedHandle file(CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) return false;
    text.clear();
    char buffer[4096] = {};
    for (;;) {
        DWORD read_count = 0U;
        if (!ReadFile(file.get(), buffer, sizeof(buffer), &read_count, nullptr) || read_count == 0U) break;
        text.append(buffer, read_count);
        if (text.size() > limit) return false;
    }
    return true;
}

bool write_file_whole(const std::string& path, std::string_view text) {
    const std::wstring staged = wide(path + ".new");
    {
        const OwnedHandle file(
            CreateFileW(staged.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.valid()) return false;
        DWORD wrote = 0U;
        if (!WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) ||
            wrote != text.size()) {
            static_cast<void>(DeleteFileW(staged.c_str()));
            return false;
        }
    }
    if (!MoveFileExW(staged.c_str(), wide(path).c_str(), MOVEFILE_REPLACE_EXISTING)) {
        static_cast<void>(DeleteFileW(staged.c_str()));
        return false;
    }
    return true;
}

struct OlderProcess final {
    bool operator()(const AttachListing& left, const AttachListing& right) const noexcept {
        return left.process < right.process;
    }
};

} // namespace

std::string attach_run_directory(bool create, std::string& error) {
    const std::string directory = run_directory_path();
    if (directory.empty()) {
        error = "no run directory can be named for this user (no local application data folder)";
        return std::string{};
    }
    const std::vector<unsigned char> own = token_user(GetCurrentProcess());
    if (own.empty()) {
        error = "the user's SID could not be read";
        return std::string{};
    }
    const std::string parent = directory.substr(0U, directory.size() - std::string_view("\\run").size());
    if (create) {
        if (!CreateDirectoryW(wide(parent).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            error = "the directory " + parent + " could not be made: " + host_message(GetLastError());
            return std::string{};
        }
        if (!CreateDirectoryW(wide(directory).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            error = "the run directory " + directory + " could not be made: " + host_message(GetLastError());
            return std::string{};
        }
        if (!make_private(directory, own)) {
            error = "the run directory " + directory + " could not be made private to this user: " +
                    host_message(GetLastError());
            return std::string{};
        }
        return directory;
    }
    const DWORD attributes = GetFileAttributesW(wide(directory).c_str());
    const bool plain_directory = attributes != INVALID_FILE_ATTRIBUTES &&
                                 (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U &&
                                 (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U;
    if (!plain_directory) {
        error = "the run directory " + directory + " is missing";
        return std::string{};
    }
    return directory;
}

std::string attach_socket_path(std::int64_t process, std::string& error) {
    const std::string directory = run_directory_path();
    if (directory.empty()) {
        error = "no run directory can be named for this user (no local application data folder)";
        return std::string{};
    }
    return directory + "\\" + std::to_string(static_cast<long long>(process)) + std::string(socket_suffix);
}

struct AttachEndpoint::State final {
    std::string path{};
    std::string info_path{};
    AttachListing entry{};
    std::vector<unsigned char> own{};
    SOCKET listener{INVALID_SOCKET};
    std::atomic<bool> interrupted{false};
};

AttachEndpoint::AttachEndpoint(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<AttachEndpoint> AttachEndpoint::listen(const AttachProgram& program, std::string& error) {
    if (!winsock_ready()) {
        error = "Winsock did not start";
        return nullptr;
    }
    const std::string directory = attach_run_directory(true, error);
    if (directory.empty()) return nullptr;
    const std::string process = std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).own = token_user(GetCurrentProcess());
    (*state).path = directory + "\\" + process + std::string(socket_suffix);
    (*state).info_path = directory + "\\" + process + std::string(info_suffix);
    sockaddr_un address{};
    if (!socket_address((*state).path, address)) {
        error = "the attach socket's path is too long: " + (*state).path;
        return nullptr;
    }
    std::unique_ptr<AttachEndpoint> endpoint(new AttachEndpoint(std::move(state)));
    State& made = *(*endpoint).state_;
    made.listener = unix_socket();
    if (made.listener == INVALID_SOCKET) {
        error = "the attach socket could not be made: " + host_message(static_cast<DWORD>(WSAGetLastError()));
        return nullptr;
    }
    // Files left under these names by an ended process with the same pid.
    static_cast<void>(DeleteFileW(wide(made.path).c_str()));
    static_cast<void>(DeleteFileW(wide(made.info_path).c_str()));
    if (bind(made.listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(made.listener, listen_backlog) != 0 || !set_blocking(made.listener, false)) {
        error = "the attach socket could not listen at " + made.path + ": " +
                host_message(static_cast<DWORD>(WSAGetLastError()));
        close_socket(made.listener);
        made.listener = INVALID_SOCKET;
        return nullptr;
    }
    made.entry.identity = program.identity;
    made.entry.name = program.name.empty() ? program.identity : program.name;
    made.entry.process = static_cast<std::int64_t>(GetCurrentProcessId());
    made.entry.executable = program.executable;
    made.entry.address = made.path;
    if (!write_file_whole(made.info_path, attach_info_text(made.entry))) {
        error = "the discovery entry " + made.info_path + " could not be written: " + host_message(GetLastError());
        return nullptr;
    }
    return endpoint;
}

AttachEndpoint::~AttachEndpoint() {
    if (!state_) return;
    State& state = *state_;
    if (state.listener != INVALID_SOCKET) {
        close_socket(state.listener);
        static_cast<void>(DeleteFileW(wide(state.path).c_str()));
    }
    static_cast<void>(DeleteFileW(wide(state.info_path).c_str()));
}

AttachWait AttachEndpoint::accept(AttachStreams& streams, std::string& refused, std::int64_t timeout_milliseconds) {
    State& state = *state_;
    const stxlib::IntervalTime deadline = stxlib::IntervalTime::now().after(
        stxlib::Duration::milliseconds(timeout_milliseconds < 0 ? 0 : timeout_milliseconds));
    for (;;) {
        if (state.interrupted.load()) return AttachWait::interrupted;
        INT wait = -1;
        if (timeout_milliseconds >= 0) {
            const stxlib::IntervalTime now = stxlib::IntervalTime::now();
            if (deadline <= now) return AttachWait::timed_out;
            wait = static_cast<INT>(std::min<std::int64_t>(deadline.since(now).count_milliseconds() + 1, 60000));
        }
        WSAPOLLFD watched{};
        watched.fd = state.listener;
        watched.events = POLLRDNORM;
        const int ready = WSAPoll(&watched, 1U, wait);
        if (ready == SOCKET_ERROR) {
            refused = "the attach endpoint stopped: " + host_message(static_cast<DWORD>(WSAGetLastError()));
            return AttachWait::failed;
        }
        if (state.interrupted.load()) return AttachWait::interrupted;
        if (ready == 0) continue;
        const SOCKET connection = ::accept(state.listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET) {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK || code == WSAECONNRESET || code == WSAEINTR) continue;
            refused = "the attach endpoint stopped: " + host_message(static_cast<DWORD>(code));
            return AttachWait::failed;
        }
        if (state.interrupted.load()) {
            close_socket(connection);
            return AttachWait::interrupted;
        }
        static_cast<void>(set_blocking(connection, true));
        ULONG peer = 0U;
        DWORD returned = 0U;
        if (WSAIoctl(connection, SIO_AF_UNIX_GETPEERPID, nullptr, 0U, &peer, sizeof(peer), &returned, nullptr,
                     nullptr) != 0) {
            close_socket(connection);
            refused = "a connection whose process could not be read was refused";
            return AttachWait::refused;
        }
        if (!same_user(state.own, process_user(peer))) {
            close_socket(connection);
            refused = "a connection from another user was refused";
            return AttachWait::refused;
        }
        if (!streams_over(connection, streams)) {
            refused = "a connection could not be taken (no handle for its writer)";
            return AttachWait::refused;
        }
        return AttachWait::connected;
    }
}

void AttachEndpoint::interrupt() noexcept {
    State& state = *state_;
    state.interrupted.store(true);
    // Connecting once ends a WSAPoll that waits for a connection.
    sockaddr_un address{};
    if (!socket_address(state.path, address)) return;
    const SOCKET poke = unix_socket();
    if (poke == INVALID_SOCKET) return;
    static_cast<void>(connect(poke, reinterpret_cast<const sockaddr*>(&address), sizeof(address)));
    close_socket(poke);
}

const std::string& AttachEndpoint::address() const noexcept {
    return (*state_).path;
}

void AttachEndpoint::publish(std::size_t tools, std::string_view note) {
    State& state = *state_;
    const std::size_t newline = note.find('\n');
    state.entry.tools = tools;
    state.entry.note = std::string(note.substr(0U, newline));
    static_cast<void>(write_file_whole(state.info_path, attach_info_text(state.entry)));
}

std::vector<AttachListing> list_attach_endpoints() {
    std::vector<AttachListing> listings{};
    std::string error{};
    const std::string directory = attach_run_directory(false, error);
    if (directory.empty()) return listings;
    WIN32_FIND_DATAW found{};
    const std::wstring pattern = wide(directory + "\\*");
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return listings;
    std::vector<std::string> stale{};
    do {
        const std::string name = narrow(found.cFileName);
        const std::int64_t socket_process = name_process(name, socket_suffix);
        const std::int64_t info_process = name_process(name, info_suffix);
        const std::int64_t process = socket_process != 0 ? socket_process : info_process;
        if (process == 0) continue;
        const std::string path = directory + "\\" + name;
        if (!process_alive(process)) {
            stale.push_back(path);
            continue;
        }
        if (info_process == 0) continue;
        std::string text{};
        AttachListing listing{};
        if (!read_small_file(path, 64U * 1024U, text) || !parse_attach_info(text, listing)) continue;
        listing.process = process;
        listings.push_back(std::move(listing));
    } while (FindNextFileW(search, &found));
    static_cast<void>(FindClose(search));
    for (const std::string& path : stale) static_cast<void>(DeleteFileW(wide(path).c_str()));
    std::sort(listings.begin(), listings.end(), OlderProcess{});
    return listings;
}

void abort_attach_connection(std::int64_t handle) noexcept {
    if (handle == 0 || handle == -1) return;
    const SOCKET socket_handle = static_cast<SOCKET>(static_cast<std::uintptr_t>(handle));
    static_cast<void>(shutdown(socket_handle, SD_BOTH));
    static_cast<void>(CancelIoEx(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(handle)), nullptr));
}

bool peek_attach_line(const AttachStreams& streams, std::string& line, std::int64_t timeout_milliseconds) {
    const SOCKET socket_handle = static_cast<SOCKET>(static_cast<std::uintptr_t>(streams.input.native_handle()));
    const stxlib::IntervalTime deadline =
        stxlib::IntervalTime::now().after(stxlib::Duration::milliseconds(timeout_milliseconds));
    std::vector<char> buffer(maximum_opening_line + 1U);
    std::size_t seen = 0U;
    for (;;) {
        const stxlib::IntervalTime now = stxlib::IntervalTime::now();
        if (deadline <= now) return false;
        WSAPOLLFD watched{};
        watched.fd = socket_handle;
        watched.events = POLLRDNORM;
        const INT wait = static_cast<INT>(std::min<std::int64_t>(deadline.since(now).count_milliseconds() + 1, 60000));
        const int ready = WSAPoll(&watched, 1U, seen == 0U ? wait : std::min<INT>(wait, peek_slice_milliseconds));
        if (ready == SOCKET_ERROR) return false;
        if (ready == 0) continue;
        const int peeked = recv(socket_handle, buffer.data(), static_cast<int>(buffer.size()), MSG_PEEK);
        if (peeked <= 0) return false;
        const std::string_view held(buffer.data(), static_cast<std::size_t>(peeked));
        const std::size_t newline = held.find('\n');
        if (newline != std::string_view::npos) {
            line.assign(held.data(), newline);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (held.size() > maximum_opening_line) return false;
        if (held.size() == seen) stxlib::sleep_for(stxlib::Duration::milliseconds(peek_slice_milliseconds));
        seen = held.size();
    }
}

bool connect_attach_endpoint(const std::string& address, AttachStreams& streams, std::string& error) {
    if (!winsock_ready()) {
        error = "Winsock did not start";
        return false;
    }
    sockaddr_un socket_name{};
    if (!socket_address(address, socket_name)) {
        error = "the attach socket's path is too long: " + address;
        return false;
    }
    const SOCKET connection = unix_socket();
    if (connection == INVALID_SOCKET) {
        error = "a socket could not be made: " + host_message(static_cast<DWORD>(WSAGetLastError()));
        return false;
    }
    if (connect(connection, reinterpret_cast<const sockaddr*>(&socket_name), sizeof(socket_name)) != 0) {
        error = "the program at " + address +
                " could not be reached: " + host_message(static_cast<DWORD>(WSAGetLastError()));
        close_socket(connection);
        return false;
    }
    if (!streams_over(connection, streams)) {
        error = "no handle was left for the connection's writer";
        return false;
    }
    return true;
}

} // namespace gui_forms::automation
