#pragma once

// The automation's attach endpoint (MCP 1.0.5; the owner's attach design,
// revision 3, 2026-11-25): "when the program comes up, its listening on a
// socket. why not just make that a server like ssh, but some other protocol
// like finger? claude then simply starts a console session to the program.
// and that becomes mcp itself."
//
// Every running program listens on a Unix domain stream socket that only its
// own user can reach, and the socket speaks MCP itself: newline-delimited
// JSON-RPC, MCP's stdio framing. `nc -U <socket>`, socat or a few lines of
// Python is a complete client, so any agent bootstraps MCP with standard
// tools and no configuration. A first line that is not JSON opens a plain
// text console instead (help, tools, call, transcript, resume, quit), for agents and
// people alike. gui-forms-attach (tools/attach) remains as a stdio bridge
// for clients that insist on standard input and output.
//
// The guard is the operating system's user account: "guarding who connects
// to the app is quite literally the only permission that should matter"
// (owner).
// - The run directory, one per user: $XDG_RUNTIME_DIR/gui-forms (else
//   /tmp/gui-forms-<uid>) on Linux, ~/Library/Caches/gui-forms/run on
//   macOS, %LOCALAPPDATA%\gui-forms\run on Windows. POSIX makes it 0700
//   and refuses one that is not a directory of the user's own with no group
//   or other permission; Windows gives it a protected DACL granting only
//   the user's SID.
// - In it, per running program: <pid>.sock (POSIX mode 0600) and
//   <pid>.info, the discovery entry (name, identity, pid, executable,
//   socket path, tool count, the first line of the program's note), so
//   `ls` and `cat *.info` show what is running. Entries of ended processes
//   are removed by the next listing.
// - Each accepted peer's user is read from the kernel and must be the
//   program's own (attach_peer_allowed): getpeereid (macOS, BSD),
//   SO_PEERCRED (Linux), SIO_AF_UNIX_GETPEERPID and the peer process's
//   token user (Windows). Another user's connection is closed unread.
//
// Windows uses AF_UNIX too (Windows 10 1803 and later), so the same
// commands work on every desktop host. The web host has no endpoint.
//
// This file is platform-neutral and holds no gui_forms type, so the bridge
// compiles the platform file and attach_protocol.cpp with nothing else of
// the library.

#include <stxlib/pipes.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {

// One connection's two directions over one connected socket.
struct AttachStreams final {
    stxlib::PipeReader input{};
    stxlib::PipeWriter output{};
};

// How one wait for a connection ended.
enum class AttachWait : std::uint8_t {
    // A peer of the same user connected: the streams are set.
    connected,
    // A peer of another user, or one whose user could not be read, was
    // closed unread; `refused` says which. The endpoint listens on.
    refused,
    // interrupt() ended the wait.
    interrupted,
    // The wait's time limit passed with no connection.
    timed_out,
    // The endpoint failed (said in `refused`); it no longer listens.
    failed,
};

// What a program's discovery entry says about it.
struct AttachProgram final {
    // The program's MCP identity (automation_identity).
    std::string identity{};
    // Its name for people (the first window's title, or the identity).
    std::string name{};
    std::string executable{};
};

// A running program's discovery entry, as list_attach_endpoints reads it.
struct AttachListing final {
    std::string identity{};
    std::string name{};
    std::int64_t process{};
    std::string executable{};
    // The socket's path: what connect_attach_endpoint takes.
    std::string address{};
    std::size_t tools{};
    // The first line of the program's note for a model, or empty.
    std::string note{};
};

class AttachEndpoint final {
public:
    // Listens for this process: <pid>.sock and <pid>.info in the user's run
    // directory. Null, with `error` set, when the directory cannot be made
    // or is not private, or the socket cannot listen.
    [[nodiscard]] static std::unique_ptr<AttachEndpoint> listen(const AttachProgram& program, std::string& error);
    // Stops listening and removes the socket and the discovery entry.
    ~AttachEndpoint();
    AttachEndpoint(const AttachEndpoint&) = delete;
    AttachEndpoint& operator=(const AttachEndpoint&) = delete;

    // Waits for the next connection, at most `timeout_milliseconds` (-1:
    // without limit). One thread waits at a time.
    [[nodiscard]] AttachWait accept(AttachStreams& streams, std::string& refused,
                                    std::int64_t timeout_milliseconds = -1);
    // Ends a wait in accept, and every later one, with interrupted. Any
    // thread.
    void interrupt() noexcept;
    [[nodiscard]] const std::string& address() const noexcept;
    // Rewrites the discovery entry with the served tool count and the
    // note's first line (written whole, then renamed into place). Any
    // thread; calls are serialised by the caller.
    void publish(std::size_t tools, std::string_view note);

    struct State;

private:
    explicit AttachEndpoint(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

// The user's run directory (made when `create` and missing); empty, with
// `error` set, when it cannot be made or is not private.
[[nodiscard]] std::string attach_run_directory(bool create, std::string& error);
// Where process `process`'s socket is or would be; empty, with `error`
// set, when the run directory cannot be named.
[[nodiscard]] std::string attach_socket_path(std::int64_t process, std::string& error);

// The discovery entries of the user's running programs, oldest process
// first. An entry whose process has ended is removed and left out.
[[nodiscard]] std::vector<AttachListing> list_attach_endpoints();

// Connects to the socket at `address`. False, with `error` set, when it
// cannot.
[[nodiscard]] bool connect_attach_endpoint(const std::string& address, AttachStreams& streams, std::string& error);

// Ends a blocking read or write on a connection's socket (`handle`, from
// AttachStreams::input.native_handle()) from another thread, so a peer
// that stalls cannot hold a stopping program: shutdown of both directions.
// The handle stays open; its owner closes it.
void abort_attach_connection(std::int64_t handle) noexcept;

// The first line the peer sent, without taking it from the stream (MSG_PEEK),
// so MCP's server still reads it: true with the line (no newline) once a
// whole line arrived within `timeout_milliseconds`; false at the end of the
// stream, on a failure or the limit, or past maximum_opening_line.
[[nodiscard]] bool peek_attach_line(const AttachStreams& streams, std::string& line,
                                    std::int64_t timeout_milliseconds);

// Whether a peer of user `peer` may use an endpoint of user `own`: only
// the same user. The platform files check every peer with it; a test
// checks the rule itself (another user's peer is refused).
[[nodiscard]] constexpr bool attach_peer_allowed(std::uint64_t own, std::uint64_t peer) noexcept {
    return own == peer;
}

// ---- the opening and the console (attach_protocol.cpp) ----

// The longest first line peeked (an MCP initialize is far shorter), and the
// longest console line read.
inline constexpr std::size_t maximum_opening_line = 64U * 1024U;
inline constexpr std::size_t maximum_console_line = 1024U * 1024U;

// What a connection's first line says.
struct AttachOpening final {
    // The line is a JSON object: the connection is MCP. Otherwise it is a
    // console session and the line its first command.
    bool json{};
    // The request's method and id (as JSON text, "null" for none).
    std::string method{};
    std::string id_json{"null"};
    // initialize's clientInfo: self-reported, never verified.
    std::string client_name{};
    std::string client_version{};
    // A session token to resume (initialize's params.resume, or
    // params._meta["gui-forms/resume"]).
    std::string resume{};
};

[[nodiscard]] AttachOpening read_attach_opening(std::string_view line);

enum class ConsoleVerb : std::uint8_t {
    // A blank line: nothing to do.
    none,
    help,
    tools,
    call,
    transcript,
    resume,
    quit,
    // Anything else, or a call whose arguments could not be read; `error`
    // says why.
    unknown,
};

struct ConsoleCommand final {
    ConsoleVerb verb{ConsoleVerb::none};
    std::string tool{};
    // A call's arguments as one JSON object.
    std::string arguments_json{"{}"};
    std::string token{};
    // help's topic (a tool's name, `tools`, `mcp`, ...); empty for the
    // overview.
    std::string topic{};
    std::string error{};
};

// One console line: `help [topic]`, `tools`, `call <tool> [<json object> |
// key=value ...]`, `transcript`, `resume <token>`, `quit`. A key=value's value is JSON
// when it reads as a number, true, false, null, an object, an array or a
// quoted string, and a string otherwise.
[[nodiscard]] ConsoleCommand parse_console_command(std::string_view line);

// A JSON-RPC error response line (newline included) for request `id_json`.
[[nodiscard]] std::string json_rpc_error_line(std::string_view id_json, int code, std::string_view message);

// A discovery entry's text ("key: value" lines) and its reading.
[[nodiscard]] std::string attach_info_text(const AttachListing& listing);
[[nodiscard]] bool parse_attach_info(std::string_view text, AttachListing& listing);

// A new session token: 32 hexadecimal digits of host entropy; empty when the
// host gives none.
[[nodiscard]] std::string make_session_token();

// Reads one line from `input` a byte at a time, so nothing after it is
// taken: true with the line (no newline, a trailing CR removed) in `line`;
// false at the end of the stream, on a failure, or past `maximum` bytes.
[[nodiscard]] bool read_attach_line(stxlib::PipeReader& input, std::string& line,
                                    std::size_t maximum = maximum_console_line);
// Writes `text` whole; false on a failure.
[[nodiscard]] bool write_attach_line(stxlib::PipeWriter& output, std::string_view text);

} // namespace gui_forms::automation
