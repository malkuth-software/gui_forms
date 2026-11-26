// The attach endpoint on POSIX hosts (src/runtime/automation/
// attach_endpoint.hpp): a Unix domain stream socket that only the user can
// reach, beside a discovery entry that says what listens on it.
//
// - The run directory: ~/Library/Caches/gui-forms/run on macOS (the home
//   from STXlib's known_directory), $XDG_RUNTIME_DIR/gui-forms elsewhere
//   when that variable names a private directory (the XDG Base Directory
//   Specification guarantees one), else /tmp/gui-forms-<uid>. Each
//   directory made on the way is made 0700; the run directory is checked
//   with lstat: a directory (not a link), owned by the user, with no group
//   or other permission. Anything else refuses the endpoint, since a
//   directory someone else can enter would let them reach the socket.
// - The socket: <pid>.sock, bound with the process's umask narrowed to 0177
//   and chmod'ed 0600 after, close-on-exec, the listening descriptor
//   non-blocking; an old file of the same name (a reused pid) is removed
//   first.
// - The discovery entry: <pid>.info, mode 0600, written whole to
//   <pid>.info.new and renamed into place, so a reader never sees half of
//   one.
// - Each accepted peer's user is read from the kernel (getpeereid; Linux:
//   SO_PEERCRED) and must be the user's own (attach_peer_allowed); another
//   user's connection, or one whose user cannot be read, is closed unread.
// - interrupt() writes to a self-pipe that accept() polls beside the
//   socket.
// - Writes never raise SIGPIPE: macOS sets SO_NOSIGPIPE on every connected
//   socket, and the process ignores SIGPIPE besides (claim_protocol_streams
//   and create_pipe call ignore_broken_pipes; this file calls it too).
// - The command folder (CommandDoor, attach_endpoint.hpp): /tmp/<id>, made
//   with mkdir 0700 (an ID whose name exists is drawn again) and checked
//   after with lstat. Its `in` FIFO is held open for writing by the program
//   too, so a writer's close is never an end of input; its `out` FIFO is
//   opened for writing, non-blocking, only while a reply waits and a
//   reader has it open, written, then closed, so a `cat` of it ends after
//   the reply. A reply waits at most door_reply_seconds for a reader.

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

#include <stxlib/clocks.hpp>
#include <stxlib/entropy.hpp>
#include <stxlib/environment.hpp>
#include <stxlib/events.hpp>
#include <stxlib/files.hpp>
#include <stxlib/path.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/threads.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gui_forms::automation {

namespace {

constexpr std::string_view socket_suffix = ".sock";
constexpr std::string_view info_suffix = ".info";
constexpr int listen_backlog = 8;
// How often a peek looks again while a first line is still arriving.
constexpr int peek_slice_milliseconds = 10;

#if !defined(__APPLE__)
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
#endif

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

#if defined(__APPLE__)
// The user's home directory, or empty.
std::string home_directory() {
    const stxlib::OperationResult<stxlib::Path> found = stxlib::known_directory(stxlib::KnownDirectory::home);
    if (found.failed()) return std::string{};
    const stxlib::PathText text = (*found.value()).text();
    if (!text.is_unicode()) return std::string{};
    const stxlib::TextView view = text.unicode();
    std::string home(view.data(), static_cast<std::size_t>(view.count()));
    while (home.size() > 1U && home.back() == '/') home.pop_back();
    return home;
}
#endif

// The run directory's path, and the directories above it this file may
// make (outermost first); empty when none can be named.
std::string run_directory_path(std::vector<std::string>& parents) {
#if defined(__APPLE__)
    const std::string home = home_directory();
    if (home.empty() || home.front() != '/') return std::string{};
    parents.push_back(home + "/Library");
    parents.push_back(home + "/Library/Caches");
    parents.push_back(home + "/Library/Caches/gui-forms");
    return home + "/Library/Caches/gui-forms/run";
#else
    static_cast<void>(parents);
    const std::string runtime = variable("XDG_RUNTIME_DIR");
    if (!runtime.empty() && runtime.front() == '/' && private_directory(runtime)) return runtime + "/gui-forms";
    return "/tmp/gui-forms-" + std::to_string(static_cast<unsigned long>(getuid()));
#endif
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

// The peer's process ID, for the console's label of its lines; 0 when the
// kernel does not say (LOCAL_PEERPID on macOS, SO_PEERCRED on Linux).
std::int64_t peer_process(int descriptor) noexcept {
#if defined(__linux__)
    struct ucred credentials {};
    socklen_t size = sizeof(credentials);
    if (getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0) return 0;
    return static_cast<std::int64_t>(credentials.pid);
#elif defined(__APPLE__)
    pid_t process = 0;
    socklen_t size = sizeof(process);
    if (getsockopt(descriptor, SOL_LOCAL, LOCAL_PEERPID, &process, &size) != 0) return 0;
    return static_cast<std::int64_t>(process);
#else
    static_cast<void>(descriptor);
    return 0;
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

// The process ID an entry's file name begins with, when the name is
// <pid><suffix>; 0 otherwise.
std::int64_t name_process(std::string_view name, std::string_view suffix) noexcept {
    if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) return 0;
    const std::size_t digits = name.size() - suffix.size();
    std::int64_t process = 0;
    const std::from_chars_result parsed = std::from_chars(name.data(), name.data() + digits, process);
    if (parsed.ec != std::errc{} || parsed.ptr != name.data() + digits) return 0;
    return process;
}

bool process_alive(std::int64_t process) noexcept {
    if (process <= 0) return false;
    if (kill(static_cast<pid_t>(process), 0) == 0) return true;
    return errno == EPERM;
}

// The whole file at `path`, at most `limit` bytes; false when it cannot be
// read.
bool read_small_file(const std::string& path, std::size_t limit, std::string& text) {
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return false;
    text.clear();
    char buffer[4096] = {};
    for (;;) {
        const ssize_t read_count = read(descriptor, buffer, sizeof(buffer));
        if (read_count < 0 && errno == EINTR) continue;
        if (read_count <= 0) break;
        text.append(buffer, static_cast<std::size_t>(read_count));
        if (text.size() > limit) break;
    }
    static_cast<void>(close(descriptor));
    return text.size() <= limit;
}

// Writes `text` to `path` whole: to path.new, mode 0600, then renamed into
// place.
bool write_file_whole(const std::string& path, std::string_view text) {
    const std::string staged = path + ".new";
    const int descriptor = open(staged.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) return false;
    std::size_t done = 0U;
    while (done < text.size()) {
        const ssize_t wrote = write(descriptor, text.data() + done, text.size() - done);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) break;
        done += static_cast<std::size_t>(wrote);
    }
    const bool closed = close(descriptor) == 0;
    if (done != text.size() || !closed || rename(staged.c_str(), path.c_str()) != 0) {
        static_cast<void>(unlink(staged.c_str()));
        return false;
    }
    return true;
}

// Orders listings by process ID, the oldest (usually) first.
struct OlderProcess final {
    bool operator()(const AttachListing& left, const AttachListing& right) const noexcept {
        return left.process < right.process;
    }
};

} // namespace

std::string attach_run_directory(bool create, std::string& error) {
    std::vector<std::string> parents{};
    const std::string directory = run_directory_path(parents);
    if (directory.empty()) {
        error = "no run directory can be named for this user (no home directory)";
        return std::string{};
    }
    if (create) {
        // Directories that exist keep their modes; only new ones are 0700.
        for (const std::string& parent : parents) {
            if (mkdir(parent.c_str(), 0700) != 0 && errno != EEXIST) {
                error = "the directory " + parent + " could not be made: " + host_message(errno);
                return std::string{};
            }
        }
        if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
            error = "the run directory " + directory + " could not be made: " + host_message(errno);
            return std::string{};
        }
    }
    if (!private_directory(directory)) {
        error = "the run directory " + directory + " is missing or not private to this user (it must be a "
                "directory of the user's own with mode 0700)";
        return std::string{};
    }
    return directory;
}

std::string attach_socket_path(std::int64_t process, std::string& error) {
    std::vector<std::string> parents{};
    const std::string directory = run_directory_path(parents);
    if (directory.empty()) {
        error = "no run directory can be named for this user (no home directory)";
        return std::string{};
    }
    return directory + "/" + std::to_string(static_cast<long long>(process)) + std::string(socket_suffix);
}

struct AttachEndpoint::State final {
    std::string path{};
    std::string info_path{};
    AttachListing entry{};
    int listener{-1};
    int wake_read{-1};
    int wake_write{-1};
    std::atomic<bool> interrupted{false};
};

AttachEndpoint::AttachEndpoint(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<AttachEndpoint> AttachEndpoint::listen(const AttachProgram& program, std::string& error) {
    stxlib::ignore_broken_pipes();
    const std::string directory = attach_run_directory(true, error);
    if (directory.empty()) return nullptr;
    const std::string process = std::to_string(static_cast<long long>(getpid()));
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).path = directory + "/" + process + std::string(socket_suffix);
    (*state).info_path = directory + "/" + process + std::string(info_suffix);
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
    // Files left under these names by an ended process with the same pid.
    static_cast<void>(unlink(made.path.c_str()));
    static_cast<void>(unlink(made.info_path.c_str()));
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
    made.entry.identity = program.identity;
    made.entry.name = program.name.empty() ? program.identity : program.name;
    made.entry.process = static_cast<std::int64_t>(getpid());
    made.entry.executable = program.executable;
    made.entry.access = program.access;
    made.entry.door = program.door;
    made.entry.address = made.path;
    if (!write_file_whole(made.info_path, attach_info_text(made.entry))) {
        error = "the discovery entry " + made.info_path + " could not be written: " + host_message(errno);
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
        static_cast<void>(unlink(state.info_path.c_str()));
    }
    if (state.wake_read >= 0) static_cast<void>(close(state.wake_read));
    if (state.wake_write >= 0) static_cast<void>(close(state.wake_write));
}

AttachWait AttachEndpoint::accept(AttachStreams& streams, std::string& refused, std::int64_t timeout_milliseconds) {
    State& state = *state_;
    const stxlib::IntervalTime deadline = stxlib::IntervalTime::now().after(
        stxlib::Duration::milliseconds(timeout_milliseconds < 0 ? 0 : timeout_milliseconds));
    for (;;) {
        if (state.interrupted.load()) return AttachWait::interrupted;
        int wait = -1;
        if (timeout_milliseconds >= 0) {
            const stxlib::IntervalTime now = stxlib::IntervalTime::now();
            if (deadline <= now) return AttachWait::timed_out;
            wait = static_cast<int>(std::min<std::int64_t>(deadline.since(now).count_milliseconds() + 1, 60000));
        }
        pollfd watched[2] = {};
        watched[0].fd = state.listener;
        watched[0].events = POLLIN;
        watched[1].fd = state.wake_read;
        watched[1].events = POLLIN;
        const int ready = poll(watched, 2U, wait);
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
        const std::int64_t process = peer_process(connection);
        if (!streams_over(connection, streams)) {
            refused = "a connection could not be taken (no descriptor for its writer)";
            return AttachWait::refused;
        }
        streams.peer_process = process;
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
    DIR* const opened = opendir(directory.c_str());
    if (opened == nullptr) return listings;
    std::vector<std::string> stale{};
    for (dirent* entry = readdir(opened); entry != nullptr; entry = readdir(opened)) {
        const std::string name((*entry).d_name);
        const std::int64_t socket_process = name_process(name, socket_suffix);
        const std::int64_t info_process = name_process(name, info_suffix);
        const std::int64_t process = socket_process != 0 ? socket_process : info_process;
        if (process == 0) continue;
        const std::string path = directory + "/" + name;
        if (!process_alive(process)) {
            // An ended program's command folder goes with its entry.
            std::string text{};
            AttachListing ended{};
            if (info_process != 0 && read_small_file(path, 64U * 1024U, text) && parse_attach_info(text, ended) &&
                !ended.door.empty()) {
                remove_door_folder(ended.door, process);
            }
            stale.push_back(path);
            continue;
        }
        if (info_process == 0) continue;
        std::string text{};
        AttachListing listing{};
        if (!read_small_file(path, 64U * 1024U, text) || !parse_attach_info(text, listing)) continue;
        listing.process = process;
        listings.push_back(std::move(listing));
    }
    static_cast<void>(closedir(opened));
    // The entries of ended processes (a program that crashed, or was
    // killed, leaves its files behind).
    for (const std::string& path : stale) static_cast<void>(unlink(path.c_str()));
    std::sort(listings.begin(), listings.end(), OlderProcess{});
    return listings;
}

void abort_attach_connection(std::int64_t handle) noexcept {
    if (handle >= 0) static_cast<void>(shutdown(static_cast<int>(handle), SHUT_RDWR));
}

bool peek_attach_line(const AttachStreams& streams, std::string& line, std::int64_t timeout_milliseconds) {
    const int descriptor = static_cast<int>(streams.input.native_handle());
    const stxlib::IntervalTime deadline =
        stxlib::IntervalTime::now().after(stxlib::Duration::milliseconds(timeout_milliseconds));
    std::vector<char> buffer(maximum_opening_line + 1U);
    std::size_t seen = 0U;
    for (;;) {
        const stxlib::IntervalTime now = stxlib::IntervalTime::now();
        if (deadline <= now) return false;
        pollfd watched{};
        watched.fd = descriptor;
        watched.events = POLLIN;
        const int wait = static_cast<int>(std::min<std::int64_t>(deadline.since(now).count_milliseconds() + 1, 60000));
        const int ready = poll(&watched, 1U, seen == 0U ? wait : std::min(wait, peek_slice_milliseconds));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (ready == 0) continue;
        const ssize_t peeked = recv(descriptor, buffer.data(), buffer.size(), MSG_PEEK);
        if (peeked < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return false;
        }
        if (peeked == 0) return false;
        const std::string_view held(buffer.data(), static_cast<std::size_t>(peeked));
        const std::size_t newline = held.find('\n');
        if (newline != std::string_view::npos) {
            line.assign(held.data(), newline);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (held.size() > maximum_opening_line) return false;
        // Part of a line: poll would answer at once, so wait a slice for the
        // rest before looking again.
        if (held.size() == seen) stxlib::sleep_for(stxlib::Duration::milliseconds(peek_slice_milliseconds));
        seen = held.size();
    }
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


// ---- the command folder ---------------------------------------------------------------

namespace {

constexpr std::string_view door_base = "/tmp/";
// The door's own files, the only ones removed with its folder.
constexpr std::string_view door_files[] = {"in", "out", "help", "info", "help.new", "info.new"};
// How often a waiting reply looks for a reader of `out`.
constexpr int door_reader_slice_milliseconds = 20;
// How long a reader that stops reading may hold a reply's write.
constexpr int door_write_wait_milliseconds = 5000;
constexpr std::size_t door_read_bytes = 64U * 1024U;

// A fresh ID: 8 hexadecimal digits from the host's entropy; empty when it
// gives none.
std::string fresh_door_id() {
    stxlib::Byte bytes[4] = {};
    if (stxlib::fill_entropy(stxlib::MutableView<stxlib::Byte>(bytes, 4U)).failed()) return std::string{};
    constexpr std::string_view digits = "0123456789abcdef";
    std::string id{};
    for (const stxlib::Byte byte : bytes) {
        id += digits[static_cast<std::size_t>(byte >> 4U)];
        id += digits[static_cast<std::size_t>(byte & 0x0fU)];
    }
    return id;
}

// Writes all of `text` to the non-blocking `descriptor`, waiting for room
// at most door_write_wait_milliseconds at a time; false when the reader
// left or stalled.
bool write_all(int descriptor, std::string_view text) noexcept {
    std::size_t done = 0U;
    while (done < text.size()) {
        const ssize_t wrote = write(descriptor, text.data() + done, text.size() - done);
        if (wrote > 0) {
            done += static_cast<std::size_t>(wrote);
            continue;
        }
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd watched{};
            watched.fd = descriptor;
            watched.events = POLLOUT;
            const int ready = poll(&watched, 1U, door_write_wait_milliseconds);
            if (ready > 0 && (watched.revents & POLLOUT) != 0) continue;
            if (ready < 0 && errno == EINTR) continue;
        }
        return false;
    }
    return true;
}

void drain(int descriptor) noexcept {
    char buffer[64] = {};
    while (read(descriptor, buffer, sizeof(buffer)) > 0) {
    }
}

struct WaitingReply final {
    std::string text{};
    stxlib::IntervalTime deadline{};
};

void door_writer_main(const stxlib::StopSignal& signal, void* argument) noexcept;

} // namespace

struct CommandDoor::State final {
    std::string id{};
    std::string folder{};
    std::string in_path{};
    std::string out_path{};
    // `in`, read non-blocking, and the program's own writer that keeps it
    // from ever reading an end.
    int in_read{-1};
    int in_hold{-1};
    // Wake pipes: one for read_command's wait, one for the writer thread.
    int reader_wake[2] = {-1, -1};
    int writer_wake[2] = {-1, -1};
    std::atomic<bool> interrupted{false};
    std::atomic<bool> stopping{false};
    std::string buffer{};
    stxlib::Mutex mutex{};
    // Under mutex: the replies waiting for a reader, oldest first.
    std::deque<WaitingReply> replies{};
    stxlib::Thread writer{};
    bool writer_joinable{};
};

namespace {

void close_descriptor(int& descriptor) noexcept {
    if (descriptor >= 0) static_cast<void>(close(descriptor));
    descriptor = -1;
}

bool wake_pipe(int (&ends)[2]) noexcept {
    if (pipe(ends) != 0) return false;
    for (const int end : ends) {
        static_cast<void>(set_close_on_exec(end));
        static_cast<void>(set_blocking(end, false));
    }
    return true;
}

// Delivers the waiting replies whenever a reader has `out` open.
void door_writer_main(const stxlib::StopSignal&, void* argument) noexcept {
    CommandDoor::State& state = *static_cast<CommandDoor::State*>(argument);
    for (;;) {
        if (state.stopping.load()) return;
        bool waiting = false;
        {
            const stxlib::ScopedLock lock(state.mutex);
            const stxlib::IntervalTime now = stxlib::IntervalTime::now();
            while (!state.replies.empty() && state.replies.front().deadline <= now) state.replies.pop_front();
            waiting = !state.replies.empty();
        }
        pollfd watched{};
        watched.fd = state.writer_wake[0];
        watched.events = POLLIN;
        static_cast<void>(poll(&watched, 1U, waiting ? door_reader_slice_milliseconds : -1));
        drain(state.writer_wake[0]);
        if (state.stopping.load()) return;
        if (!waiting) continue;
        // Opens only while a reader has it open (ENXIO otherwise).
        int out = open(state.out_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (out < 0) continue;
        std::deque<WaitingReply> taken{};
        {
            const stxlib::ScopedLock lock(state.mutex);
            taken.swap(state.replies);
        }
        for (const WaitingReply& waiting_reply : taken) {
            if (!write_all(out, waiting_reply.text)) break;
        }
        close_descriptor(out);
    }
}

} // namespace

CommandDoor::CommandDoor(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

std::unique_ptr<CommandDoor> CommandDoor::open(std::string& error) {
    stxlib::ignore_broken_pipes();
    std::unique_ptr<State> state = std::make_unique<State>();
    for (int attempt = 0; attempt < 16 && (*state).folder.empty(); ++attempt) {
        const std::string id = fresh_door_id();
        if (id.empty()) {
            error = "the host gave no entropy for the command folder's ID";
            return nullptr;
        }
        const std::string folder = std::string(door_base) + id;
        if (mkdir(folder.c_str(), 0700) == 0) {
            (*state).id = id;
            (*state).folder = folder;
        } else if (errno != EEXIST) {
            error = "the command folder " + folder + " could not be made: " + host_message(errno);
            return nullptr;
        }
    }
    if ((*state).folder.empty()) {
        error = "no free name for the command folder in /tmp";
        return nullptr;
    }
    std::unique_ptr<CommandDoor> door(new CommandDoor(std::move(state)));
    State& made = *(*door).state_;
    // /tmp is shared: the folder must be the one made here.
    if (!private_directory(made.folder)) {
        error = "the command folder " + made.folder + " is not a private directory of this user's";
        made.folder.clear();
        return nullptr;
    }
    made.in_path = made.folder + "/in";
    made.out_path = made.folder + "/out";
    for (const std::string& fifo : {made.in_path, made.out_path}) {
        if (mkfifo(fifo.c_str(), 0600) != 0 || chmod(fifo.c_str(), 0600) != 0) {
            error = "the pipe " + fifo + " could not be made: " + host_message(errno);
            return nullptr;
        }
    }
    made.in_read = ::open(made.in_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (made.in_read >= 0) made.in_hold = ::open(made.in_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (made.in_read < 0 || made.in_hold < 0) {
        error = "the pipe " + made.in_path + " could not be opened: " + host_message(errno);
        return nullptr;
    }
    if (!wake_pipe(made.reader_wake) || !wake_pipe(made.writer_wake)) {
        error = "the command folder's wake pipes could not be made: " + host_message(errno);
        return nullptr;
    }
    (*door).write_info("");
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&door_writer_main, &made);
    if (started.failed()) {
        error = "the command folder's writer thread did not start";
        return nullptr;
    }
    made.writer = std::move(*started.value());
    made.writer_joinable = true;
    return door;
}

CommandDoor::~CommandDoor() {
    if (!state_) return;
    State& state = *state_;
    state.stopping.store(true);
    interrupt();
    if (state.writer_wake[1] >= 0) {
        const char wake = 'w';
        static_cast<void>(write(state.writer_wake[1], &wake, 1U));
    }
    if (state.writer_joinable) static_cast<void>(state.writer.join());
    // A reader waiting on `out` gets the replies still waiting (the reply to
    // the call that ended the program) and then its end, never a wait that
    // outlives the folder.
    if (!state.out_path.empty()) {
        int out = ::open(state.out_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (out >= 0) {
            const stxlib::ScopedLock lock(state.mutex);
            for (const WaitingReply& waiting : state.replies) {
                if (!write_all(out, waiting.text)) break;
            }
            state.replies.clear();
            close_descriptor(out);
        }
    }
    close_descriptor(state.in_read);
    close_descriptor(state.in_hold);
    for (int& end : state.reader_wake) close_descriptor(end);
    for (int& end : state.writer_wake) close_descriptor(end);
    if (state.folder.empty()) return;
    for (const std::string_view file : door_files) {
        static_cast<void>(unlink((state.folder + "/" + std::string(file)).c_str()));
    }
    static_cast<void>(rmdir(state.folder.c_str()));
}

const std::string& CommandDoor::id() const noexcept {
    return (*state_).id;
}

const std::string& CommandDoor::folder() const noexcept {
    return (*state_).folder;
}

std::string CommandDoor::send_line(std::string_view command) const {
    const State& state = *state_;
    std::string line = "echo '" + std::string(command) + "' > " + state.in_path + "; cat " + state.out_path;
    return line;
}

DoorWait CommandDoor::read_command(std::string& line, std::int64_t timeout_milliseconds) {
    State& state = *state_;
    const stxlib::IntervalTime deadline = stxlib::IntervalTime::now().after(
        stxlib::Duration::milliseconds(timeout_milliseconds < 0 ? 0 : timeout_milliseconds));
    for (;;) {
        const std::size_t newline = state.buffer.find('\n');
        if (newline != std::string::npos) {
            line.assign(state.buffer, 0U, newline);
            state.buffer.erase(0U, newline + 1U);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return DoorWait::line;
        }
        if (state.buffer.size() > maximum_console_line) state.buffer.clear();
        if (state.interrupted.load()) return DoorWait::interrupted;
        const stxlib::IntervalTime now = stxlib::IntervalTime::now();
        if (timeout_milliseconds >= 0 && deadline <= now) return DoorWait::timed_out;
        const int wait = timeout_milliseconds < 0
                             ? -1
                             : static_cast<int>(std::min<std::int64_t>(deadline.since(now).count_milliseconds() + 1,
                                                                       60000));
        pollfd watched[2] = {};
        watched[0].fd = state.in_read;
        watched[0].events = POLLIN;
        watched[1].fd = state.reader_wake[0];
        watched[1].events = POLLIN;
        const int ready = poll(watched, 2U, wait);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return DoorWait::failed;
        }
        if ((watched[1].revents & POLLIN) != 0 || state.interrupted.load()) return DoorWait::interrupted;
        if ((watched[0].revents & (POLLIN | POLLHUP)) == 0) continue;
        char chunk[4096] = {};
        std::size_t taken = 0U;
        for (;;) {
            const ssize_t read_count = read(state.in_read, chunk, sizeof(chunk));
            if (read_count < 0 && errno == EINTR) continue;
            if (read_count <= 0) break;
            state.buffer.append(chunk, static_cast<std::size_t>(read_count));
            taken += static_cast<std::size_t>(read_count);
            if (taken >= door_read_bytes) break;
        }
    }
}

void CommandDoor::interrupt() noexcept {
    State& state = *state_;
    state.interrupted.store(true);
    if (state.reader_wake[1] >= 0) {
        const char wake = 'w';
        static_cast<void>(write(state.reader_wake[1], &wake, 1U));
    }
}

void CommandDoor::reply(std::string framed) {
    State& state = *state_;
    {
        const stxlib::ScopedLock lock(state.mutex);
        WaitingReply waiting{};
        waiting.text = std::move(framed);
        waiting.deadline =
            stxlib::IntervalTime::now().after(stxlib::Duration::milliseconds(door_reply_seconds * 1000));
        state.replies.push_back(std::move(waiting));
        while (state.replies.size() > door_reply_limit) state.replies.pop_front();
    }
    const char wake = 'w';
    static_cast<void>(write(state.writer_wake[1], &wake, 1U));
}

void CommandDoor::write_help(std::string_view text) {
    State& state = *state_;
    const stxlib::ScopedLock lock(state.mutex);
    static_cast<void>(write_file_whole(state.folder + "/help", text));
}

void CommandDoor::write_info(std::string_view text) {
    State& state = *state_;
    // The ID and pid lead, so an ended program's folder is known as its
    // (remove_door_folder) from the moment it is made.
    std::string whole = "id: " + state.id + "\npid: " + std::to_string(static_cast<long long>(getpid())) + "\n";
    whole.append(text.data(), text.size());
    const stxlib::ScopedLock lock(state.mutex);
    static_cast<void>(write_file_whole(state.folder + "/info", whole));
}

std::string door_folder_path(std::string_view id) {
    std::string folder = std::string(door_base) + std::string(id);
    return folder;
}

void remove_door_folder(std::string_view id, std::int64_t process) noexcept {
    try {
        if (!door_id_valid(id) || process <= 0) return;
        const std::string folder = door_folder_path(id);
        if (!private_directory(folder)) return;
        std::string text{};
        if (!read_small_file(folder + "/info", 64U * 1024U, text)) return;
        const std::string wanted = "\npid: " + std::to_string(static_cast<long long>(process)) + "\n";
        if (("\n" + text).find(wanted) == std::string::npos) return;
        for (const std::string_view file : door_files) {
            static_cast<void>(unlink((folder + "/" + std::string(file)).c_str()));
        }
        static_cast<void>(rmdir(folder.c_str()));
    } catch (...) {
        // Out of memory: the folder stays for a later listing.
    }
}

} // namespace gui_forms::automation
