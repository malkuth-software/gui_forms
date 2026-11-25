// The attach endpoint on POSIX hosts (src/runtime/automation/
// attach_endpoint.hpp): a Unix domain stream socket that only the user can
// reach.
//
// - The directory: macOS's per-user temporary directory
//   (confstr(_CS_DARWIN_USER_TEMP_DIR), mode 0700 and the user's own), else
//   $XDG_RUNTIME_DIR (the same guarantee by specification), else
//   /tmp/gui-forms-attach-<uid>; then gui-forms-attach inside it, made
//   0700. The final directory is checked with lstat: a directory (not a
//   link), owned by the user, with no group or other permission; anything
//   else refuses the endpoint, since a directory someone else can enter
//   would let them reach the socket.
// - The socket: <pid>.<identity>.sock, bound with the process's umask
//   narrowed to 0177 and chmod'ed 0600 after, close-on-exec, the listening
//   descriptor non-blocking; an old file of the same name (a reused pid) is
//   removed first.
// - Each accepted peer's user is read from the kernel (getpeereid; Linux:
//   SO_PEERCRED) and must be the user's own (attach_peer_allowed); another
//   user's connection, or one whose user cannot be read, is closed unread.
// - interrupt() writes to a self-pipe that accept() polls beside the
//   socket.
// - Writes never raise SIGPIPE: macOS sets SO_NOSIGPIPE on every connected
//   socket, and the process ignores SIGPIPE besides (claim_protocol_streams
//   and create_pipe call ignore_broken_pipes; this file calls it too).

#include "../../../runtime/automation/attach_endpoint.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <stxlib/environment.hpp>
#include <stxlib/events.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
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

namespace gui_forms::automation {

namespace {

constexpr std::string_view directory_name = "gui-forms-attach";
constexpr std::string_view socket_suffix = ".sock";
constexpr int listen_backlog = 4;

// The variable `name`'s value, or empty.
std::string variable(std::string_view name) {
    const stxlib::OperationResult<stxlib::TextView> checked = stxlib::validate_utf8(name.data(), name.size());
    if (checked.failed()) return std::string{};
    const stxlib::OperationResult<stxlib::Utf8String> value =
        stxlib::read_variable(*checked.value(), stxlib::maximum_environment_bytes);
    if (value.failed()) return std::string{};
    const stxlib::TextView read = (*value.value()).view();
    std::string made(read.data(), static_cast<std::size_t>(read.count()));
    return made;
}

// The system's message for `code`, through std::system_category (the
// house rule: no strerror).
std::string host_message(int code) {
    const std::string message = std::system_category().message(code);
    return message;
}

// Whether `path` is a directory of the user's own that no one else may
// enter or list: lstat, so a link is refused.
bool private_directory(const std::string& path) {
    struct stat status {};
    if (lstat(path.c_str(), &status) != 0) return false;
    const bool directory = S_ISDIR(status.st_mode);
    const bool own = status.st_uid == getuid();
    const bool closed = (status.st_mode & static_cast<mode_t>(077)) == 0U;
    return directory && own && closed;
}

// The parent the user's attach directory goes in: a directory that is
// already the user's alone.
std::string user_parent() {
#if defined(__APPLE__)
    char buffer[1024] = {};
    const std::size_t length = confstr(_CS_DARWIN_USER_TEMP_DIR, buffer, sizeof(buffer));
    if (length > 0U && length <= sizeof(buffer)) {
        std::string parent(buffer);
        while (parent.size() > 1U && parent.back() == '/') parent.pop_back();
        if (private_directory(parent)) return parent;
    }
#endif
    const std::string runtime = variable("XDG_RUNTIME_DIR");
    if (!runtime.empty() && runtime.front() == '/' && private_directory(runtime)) return runtime;
    return std::string{};
}

// The user's attach directory, made when missing; empty, with `error` set,
// when it cannot be made or is not private.
std::string attach_directory(bool create, std::string& error) {
    const std::string parent = user_parent();
    std::string directory{};
    if (!parent.empty()) {
        directory = parent + "/" + std::string(directory_name);
    } else {
        directory = "/tmp/" + std::string(directory_name) + "-" + std::to_string(static_cast<unsigned long>(getuid()));
    }
    if (create && mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        error = "the attach directory " + directory + " could not be made: " + host_message(errno);
        return std::string{};
    }
    if (!private_directory(directory)) {
        error = "the attach directory " + directory + " is not private to this user (it must be a directory of the "
                "user's own with mode 0700)";
        return std::string{};
    }
    return directory;
}

// `identity` kept to letters, digits, '.', '-' and '_', other bytes as '-',
// at most 96 bytes.
std::string endpoint_identity(const std::string& identity) {
    std::string kept{};
    for (const char character : identity) {
        const bool plain = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') || character == '.' || character == '-' ||
                           character == '_';
        kept += plain ? character : '-';
        if (kept.size() == 96U) break;
    }
    if (kept.empty()) kept = "program";
    return kept;
}

bool set_close_on_exec(int descriptor) noexcept {
    const int flags = fcntl(descriptor, F_GETFD);
    return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool set_blocking(int descriptor, bool blocking) noexcept {
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0) return false;
    const int wanted = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return fcntl(descriptor, F_SETFL, wanted) == 0;
}

void no_broken_pipe_signal(int descriptor) noexcept {
#if defined(SO_NOSIGPIPE)
    const int on = 1;
    static_cast<void>(setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)));
#else
    static_cast<void>(descriptor);
#endif
}

// The connected peer's user; false when the kernel does not say.
bool peer_user(int descriptor, std::uint64_t& user) noexcept {
#if defined(__linux__)
    struct ucred credentials {};
    socklen_t size = sizeof(credentials);
    if (getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0) return false;
    user = static_cast<std::uint64_t>(credentials.uid);
    return true;
#else
    uid_t peer = 0;
    gid_t group = 0;
    if (getpeereid(descriptor, &peer, &group) != 0) return false;
    user = static_cast<std::uint64_t>(peer);
    return true;
#endif
}

// A sockaddr_un for `path`; false when the path does not fit.
bool socket_address(const std::string& path, sockaddr_un& address) noexcept {
    address = sockaddr_un{};
    address.sun_family = AF_UNIX;
    if (path.size() + 1U > sizeof(address.sun_path)) return false;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
    return true;
}

// The two streams over one connected socket: the descriptor reads, a
// close-on-exec duplicate writes.
bool streams_over(int descriptor, AttachStreams& streams) noexcept {
    const int writer = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (writer < 0) {
        static_cast<void>(close(descriptor));
        return false;
    }
    streams.input = stxlib::PipeReader::adopt(static_cast<stxlib::i64>(descriptor));
    streams.output = stxlib::PipeWriter::adopt(static_cast<stxlib::i64>(writer));
    return true;
}

// The process ID at the front of an endpoint's file name, or 0.
std::int64_t name_process(std::string_view name) noexcept {
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos || dot == 0U) return 0;
    std::int64_t process = 0;
    const std::from_chars_result parsed = std::from_chars(name.data(), name.data() + dot, process);
    if (parsed.ec != std::errc{} || parsed.ptr != name.data() + dot) return 0;
    return process;
}

bool process_alive(std::int64_t process) noexcept {
    if (process <= 0) return false;
    if (kill(static_cast<pid_t>(process), 0) == 0) return true;
    return errno == EPERM;
}

// Orders listings by process ID, the oldest (usually) first.
struct OlderProcess final {
    bool operator()(const AttachListing& left, const AttachListing& right) const noexcept {
        return left.process < right.process;
    }
};

} // namespace

struct AttachEndpoint::State final {
    std::string path{};
    int listener{-1};
    int wake_read{-1};
    int wake_write{-1};
    std::atomic<bool> interrupted{false};
};

AttachEndpoint::AttachEndpoint(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<AttachEndpoint> AttachEndpoint::listen(const std::string& identity, std::string& error) {
    stxlib::ignore_broken_pipes();
    const std::string directory = attach_directory(true, error);
    if (directory.empty()) return nullptr;
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).path = directory + "/" + std::to_string(static_cast<long long>(getpid())) + "." +
                    endpoint_identity(identity) + std::string(socket_suffix);
    sockaddr_un address{};
    if (!socket_address((*state).path, address)) {
        error = "the attach socket's path is too long: " + (*state).path;
        return nullptr;
    }
    int wake[2] = {-1, -1};
    if (pipe(wake) != 0) {
        error = "the attach endpoint's wake pipe could not be made: " + host_message(errno);
        return nullptr;
    }
    (*state).wake_read = wake[0];
    (*state).wake_write = wake[1];
    static_cast<void>(set_close_on_exec(wake[0]));
    static_cast<void>(set_close_on_exec(wake[1]));
    std::unique_ptr<AttachEndpoint> endpoint(new AttachEndpoint(std::move(state)));
    State& made = *(*endpoint).state_;
    made.listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (made.listener < 0) {
        error = "the attach socket could not be made: " + host_message(errno);
        return nullptr;
    }
    static_cast<void>(set_close_on_exec(made.listener));
    // A file left under this name by an ended process with the same pid.
    static_cast<void>(unlink(made.path.c_str()));
    const mode_t previous = umask(0177);
    const int bound = bind(made.listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    const int bind_error = errno;
    static_cast<void>(umask(previous));
    if (bound != 0) {
        error = "the attach socket could not be bound at " + made.path + ": " + host_message(bind_error);
        return nullptr;
    }
    if (chmod(made.path.c_str(), 0600) != 0 || ::listen(made.listener, listen_backlog) != 0 ||
        !set_blocking(made.listener, false)) {
        error = "the attach socket could not listen at " + made.path + ": " + host_message(errno);
        return nullptr;
    }
    return endpoint;
}

AttachEndpoint::~AttachEndpoint() {
    if (!state_) return;
    State& state = *state_;
    if (state.listener >= 0) {
        static_cast<void>(close(state.listener));
        static_cast<void>(unlink(state.path.c_str()));
    }
    if (state.wake_read >= 0) static_cast<void>(close(state.wake_read));
    if (state.wake_write >= 0) static_cast<void>(close(state.wake_write));
}

AttachWait AttachEndpoint::accept(AttachStreams& streams, std::string& refused) {
    State& state = *state_;
    for (;;) {
        if (state.interrupted.load()) return AttachWait::interrupted;
        pollfd watched[2] = {};
        watched[0].fd = state.listener;
        watched[0].events = POLLIN;
        watched[1].fd = state.wake_read;
        watched[1].events = POLLIN;
        const int ready = poll(watched, 2U, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            refused = "the attach endpoint stopped: " + host_message(errno);
            return AttachWait::failed;
        }
        if ((watched[1].revents & POLLIN) != 0 || state.interrupted.load()) return AttachWait::interrupted;
        if ((watched[0].revents & POLLIN) == 0) continue;
        const int connection = ::accept(state.listener, nullptr, nullptr);
        if (connection < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED) continue;
            refused = "the attach endpoint stopped: " + host_message(errno);
            return AttachWait::failed;
        }
        static_cast<void>(set_close_on_exec(connection));
        static_cast<void>(set_blocking(connection, true));
        no_broken_pipe_signal(connection);
        std::uint64_t peer = 0U;
        if (!peer_user(connection, peer)) {
            static_cast<void>(close(connection));
            refused = "a connection whose user could not be read was refused";
            return AttachWait::refused;
        }
        if (!attach_peer_allowed(static_cast<std::uint64_t>(getuid()), peer)) {
            static_cast<void>(close(connection));
            refused = "a connection from another user was refused";
            return AttachWait::refused;
        }
        if (!streams_over(connection, streams)) {
            refused = "a connection could not be taken (no descriptor for its writer)";
            return AttachWait::refused;
        }
        return AttachWait::connected;
    }
}

void AttachEndpoint::interrupt() noexcept {
    State& state = *state_;
    state.interrupted.store(true);
    const char wake = 'w';
    static_cast<void>(write(state.wake_write, &wake, 1U));
}

const std::string& AttachEndpoint::address() const noexcept {
    return (*state_).path;
}

std::vector<AttachListing> list_attach_endpoints() {
    std::vector<AttachListing> listings{};
    std::string error{};
    const std::string directory = attach_directory(false, error);
    if (directory.empty()) return listings;
    DIR* const opened = opendir(directory.c_str());
    if (opened == nullptr) return listings;
    for (dirent* entry = readdir(opened); entry != nullptr; entry = readdir(opened)) {
        const std::string_view name((*entry).d_name);
        if (name.size() <= socket_suffix.size() || name.substr(name.size() - socket_suffix.size()) != socket_suffix) {
            continue;
        }
        const std::int64_t process = name_process(name);
        if (!process_alive(process)) continue;
        const std::size_t dot = name.find('.');
        AttachListing listing{};
        listing.process = process;
        listing.identity = std::string(name.substr(dot + 1U, name.size() - socket_suffix.size() - dot - 1U));
        listing.address = directory + "/" + std::string(name);
        listings.push_back(std::move(listing));
    }
    static_cast<void>(closedir(opened));
    std::sort(listings.begin(), listings.end(), OlderProcess{});
    return listings;
}

void abort_attach_connection(std::int64_t handle) noexcept {
    if (handle >= 0) static_cast<void>(shutdown(static_cast<int>(handle), SHUT_RDWR));
}

bool connect_attach_endpoint(const std::string& address, AttachStreams& streams, std::string& error) {
    stxlib::ignore_broken_pipes();
    sockaddr_un socket_name{};
    if (!socket_address(address, socket_name)) {
        error = "the attach socket's path is too long: " + address;
        return false;
    }
    const int connection = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connection < 0) {
        error = "a socket could not be made: " + host_message(errno);
        return false;
    }
    static_cast<void>(set_close_on_exec(connection));
    no_broken_pipe_signal(connection);
    if (connect(connection, reinterpret_cast<const sockaddr*>(&socket_name), sizeof(socket_name)) != 0) {
        error = "the program at " + address + " could not be reached: " + host_message(errno);
        static_cast<void>(close(connection));
        return false;
    }
    if (!streams_over(connection, streams)) {
        error = "no descriptor was left for the connection's writer";
        return false;
    }
    return true;
}

} // namespace gui_forms::automation
