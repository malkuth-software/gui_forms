#pragma once

// The automation's attach endpoint (MCP 1.0.5; the owner's attach ruling,
// revision 2, 2026-11-24): how a model connects to a program that is
// already running. Each program listens on a local endpoint that only its
// own user can reach, and the gui-forms-attach bridge (tools/attach), which
// a client starts from .mcp.json as an ordinary stdio server, connects to it
// and carries MCP both ways. The operating system's user account is the
// guard: "guarding who connects to the app is quite literally the only
// permission that should matter" (owner).
//
// - POSIX (src/surface/posix/attach/attach_endpoint_posix.cpp): a Unix
//   domain stream socket, mode 0600, in a directory of the user's own that
//   is mode 0700 and owned by the user (refused when it is not): macOS's
//   per-user temporary directory, else $XDG_RUNTIME_DIR, else
//   /tmp/gui-forms-attach-<uid>. Each accepted peer's user is checked
//   again (getpeereid; Linux SO_PEERCRED) and another user's connection is
//   closed unread.
// - Windows (src/surface/windows/attach/attach_endpoint_windows.cpp): a
//   named pipe whose DACL grants only the user's SID, rejecting remote
//   clients; each client process's token user is checked against the
//   server's before anything is read.
// - Elsewhere (the web host): no endpoint; listen says so.
//
// The endpoint's name carries the program's process ID and MCP identity,
// so the bridge can list the running programs without connecting to any.
//
// The protocol on a connection, before MCP: the bridge writes one hello
// line naming the client as the client named itself (self-reported, never
// verified), the program answers one line, ok or refused with a reason,
// and then the connection carries MCP's stdio framing (one JSON-RPC message
// a line) in both directions. A program launched with --mcp or
// --mcp-shared answers ok at once; any other program asks its user first,
// for every connection (attach_server.cpp).
//
// This file is platform-neutral and holds no gui_forms type, so the bridge
// compiles the platform file and attach_protocol.cpp with nothing else of
// the library.

#include <stxlib/pipes.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {

// One connection's two directions: what the peer wrote, and where to write
// to it. On POSIX both ends are descriptors of one socket; on Windows,
// handles of one pipe instance.
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
    // The endpoint failed (said in `refused`); it no longer listens.
    failed,
};

// A running program's endpoint, as the bridge lists it.
struct AttachListing final {
    // The program's MCP identity (automation_identity), as its endpoint
    // names it.
    std::string identity{};
    std::int64_t process{};
    // What connect_attach_endpoint takes: a socket path or a pipe name.
    std::string address{};
};

class AttachEndpoint final {
public:
    // Listens for this process as `identity` (letters, digits, '.', '-' and
    // '_' are kept, other bytes become '-'). Null, with `error` set, when
    // the user's private directory cannot be made or is not private, or
    // the endpoint cannot be made.
    [[nodiscard]] static std::unique_ptr<AttachEndpoint> listen(const std::string& identity, std::string& error);
    // Stops listening and removes the endpoint's name.
    ~AttachEndpoint();
    AttachEndpoint(const AttachEndpoint&) = delete;
    AttachEndpoint& operator=(const AttachEndpoint&) = delete;

    // Waits for the next connection. One thread waits at a time.
    [[nodiscard]] AttachWait accept(AttachStreams& streams, std::string& refused);
    // Ends a wait in accept, and every later one, with interrupted. Any
    // thread.
    void interrupt() noexcept;
    [[nodiscard]] const std::string& address() const noexcept;

    struct State;

private:
    explicit AttachEndpoint(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

// The endpoints of the user's running programs, oldest process first. An
// endpoint whose process has ended is left out.
[[nodiscard]] std::vector<AttachListing> list_attach_endpoints();

// Connects to the endpoint at `address` (from list_attach_endpoints).
// False, with `error` set, when it cannot.
[[nodiscard]] bool connect_attach_endpoint(const std::string& address, AttachStreams& streams, std::string& error);

// Ends a blocking read or write on a connection's handle (`handle`, from
// AttachStreams::input.native_handle()) from another thread, so a peer
// that stalls before MCP starts cannot hold a stopping program: POSIX
// shutdown(2) of both directions, Windows CancelIoEx. The handle stays
// open; its owner closes it.
void abort_attach_connection(std::int64_t handle) noexcept;

// Whether a peer of user `peer` may use an endpoint of user `own`: only
// the same user. The platform files check every peer with it; a test
// checks the rule itself (another user's peer is refused).
[[nodiscard]] constexpr bool attach_peer_allowed(std::uint64_t own, std::uint64_t peer) noexcept {
    return own == peer;
}

// ---- the protocol before MCP (attach_protocol.cpp) ----

// The bridge's hello: who the client says it is.
struct AttachHello final {
    std::string client_name{};
    std::string client_version{};
};

// The program's answer to a hello.
struct AttachAnswer final {
    bool accepted{};
    // Why not, for a refusal.
    std::string reason{};
};

// The longest line either side writes before MCP, newline included.
inline constexpr std::size_t maximum_attach_line = 1024U;

// `hello` as its line: "GUI.Forms attach 1", a tab, the client's name, a
// tab, its version, a newline. Tabs, newlines and other control bytes in
// the names become spaces, and each name is cut to 256 bytes at a UTF-8
// boundary.
[[nodiscard]] std::string attach_hello_line(const AttachHello& hello);
// The hello read from `line` (its newline removed). False for a line that
// is not one.
[[nodiscard]] bool parse_attach_hello(std::string_view line, AttachHello& hello);
// `answer` as its line: "ok", or "refused", a tab and the reason; then a
// newline.
[[nodiscard]] std::string attach_answer_line(const AttachAnswer& answer);
[[nodiscard]] bool parse_attach_answer(std::string_view line, AttachAnswer& answer);

// Reads one line from `input` a byte at a time, so nothing after it is
// taken from MCP's stream: true with the line (no newline) in `line`;
// false at the end of the stream, on a failure, or past
// maximum_attach_line.
[[nodiscard]] bool read_attach_line(stxlib::PipeReader& input, std::string& line);
// Writes `line` whole; false on a failure.
[[nodiscard]] bool write_attach_line(stxlib::PipeWriter& output, std::string_view line);

} // namespace gui_forms::automation
