// gui-forms-attach: a stdio bridge to a running GUI.Forms program's socket
// (MCP 1.0.5; the owner's attach design, revision 3, 2026-11-25).
//
// Every running GUI.Forms program listens on a socket that speaks MCP itself
// (newline-delimited JSON-RPC), so `nc -U`, socat or a few lines of Python
// reach it directly (docs/user/talk-to-a-program.md). This bridge is only a
// convenience for a client that insists on starting a stdio server from
// .mcp.json:
//
//   { "mcpServers": { "running-program": {
//       "command": "gui-forms-attach", "args": ["--program", "app.vendor.product"] } } }
//
// It reads the client's first message, finds the program among the user's
// running GUI.Forms programs (their discovery entries in the run
// directory), connects to its socket, and carries the bytes both ways,
// first message included, until either side ends. The program decides the
// rest: only a program launched with --mcp-shared or --mcp-fcfs listens
// (one launched without either has no socket), and it accepts at once; a
// refusal comes back as the program's JSON-RPC error.
//
//   gui-forms-attach --list             the running programs: pid, identity,
//                                       tools, socket, name
//   gui-forms-attach [--program NAME]   bridge to the program whose MCP
//                                       identity is NAME (or whose pid it
//                                       is); with no --program, the one
//                                       program running
//
// When no program, or more than one, matches, the bridge answers the
// client's initialize with a JSON-RPC error saying so (naming the programs
// it found) and exits with status 1.

#include "../../src/runtime/automation/attach_endpoint.hpp"

#include <stxlib/pipes.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/threads.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gui_forms::automation::AttachListing;
using gui_forms::automation::AttachOpening;
using gui_forms::automation::AttachStreams;

constexpr std::size_t maximum_first_message = 1U << 20U;
constexpr std::string_view bridge_version = "1.0.5";

// The client's first message, read a byte at a time up to its newline
// (MCP's stdio framing), so nothing after it is taken.
bool read_message(stxlib::PipeReader& input, std::string& message) {
    message.clear();
    while (message.size() < maximum_first_message) {
        stxlib::Byte byte = 0U;
        const stxlib::OperationResult<stxlib::PipeRead> read = input.read(stxlib::MutableView<stxlib::Byte>(&byte, 1U));
        if (read.failed() || (*read.value()).is_end()) return false;
        if (byte == static_cast<stxlib::Byte>('\n')) return true;
        message += static_cast<char>(byte);
    }
    return false;
}

bool write_text(stxlib::PipeWriter& output, std::string_view text) {
    const stxlib::View<stxlib::Byte> bytes(reinterpret_cast<const stxlib::Byte*>(text.data()), text.size());
    return output.write_all(bytes).succeeded();
}

// The client's initialize answered with an error, so the client says why.
void answer_error(stxlib::PipeWriter& output, const std::string& first, const std::string& message) {
    const AttachOpening opening = gui_forms::automation::read_attach_opening(first);
    static_cast<void>(write_text(output, gui_forms::automation::json_rpc_error_line(opening.id_json, -32000, message)));
    std::fprintf(stderr, "gui-forms-attach: %s\n", message.c_str());
}

std::string describe(const std::vector<AttachListing>& listings) {
    std::string text{};
    for (const AttachListing& listing : listings) {
        if (!text.empty()) text += ", ";
        text += listing.identity + " (pid " + std::to_string(listing.process) + ")";
    }
    return text;
}

// The programs `wanted` names: its pid, else its identity exactly, else the
// identities that contain it.
std::vector<AttachListing> matching(const std::vector<AttachListing>& listings, const std::string& wanted) {
    std::vector<AttachListing> found{};
    if (wanted.empty()) return listings;
    for (const AttachListing& listing : listings) {
        if (std::to_string(listing.process) == wanted || listing.identity == wanted) found.push_back(listing);
    }
    if (!found.empty()) return found;
    for (const AttachListing& listing : listings) {
        if (listing.identity.find(wanted) != std::string::npos) found.push_back(listing);
    }
    return found;
}

// One direction of the bridge: everything `from` reads goes to `to`; at
// the end of `from`, `to` closes, so the other side sees the end too.
struct Pump final {
    stxlib::PipeReader* from{};
    stxlib::PipeWriter* to{};
};

void pump(Pump& direction) noexcept {
    stxlib::Byte buffer[16384] = {};
    for (;;) {
        const stxlib::OperationResult<stxlib::PipeRead> read =
            (*direction.from).read(stxlib::MutableView<stxlib::Byte>(buffer, sizeof(buffer)));
        if (read.failed() || (*read.value()).is_end()) break;
        const stxlib::usize count = *(*read.value()).byte_count();
        if ((*direction.to).write_all(stxlib::View<stxlib::Byte>(buffer, count)).failed()) break;
    }
    (*direction.to).close();
}

// Either direction's end ends the bridge: the process's exit closes every
// handle of the connection, which is what tells the program its client is
// gone (closing one direction's descriptor would not, while the other
// direction's is open).
[[noreturn]] void finish() noexcept {
    static_cast<void>(std::fflush(nullptr));
    std::_Exit(0);
}

void pump_thread(const stxlib::StopSignal&, void* argument) noexcept {
    pump(*static_cast<Pump*>(argument));
    finish();
}

int list_programs() {
    const std::vector<AttachListing> listings = gui_forms::automation::list_attach_endpoints();
    for (const AttachListing& listing : listings) {
        std::printf("%lld\t%s\t%zu tools\t%s\t%s\n", static_cast<long long>(listing.process),
                    listing.identity.c_str(), listing.tools, listing.address.c_str(), listing.name.c_str());
    }
    if (listings.empty()) std::fputs("gui-forms-attach: no GUI.Forms program of this user is running\n", stderr);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string wanted{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--list") return list_programs();
        if (argument == "--program" && index + 1 < argc) {
            wanted = argv[index + 1];
            ++index;
            continue;
        }
        if (argument == "--version") {
            std::printf("gui-forms-attach %.*s\n", static_cast<int>(bridge_version.size()), bridge_version.data());
            return 0;
        }
        std::fprintf(stderr, "usage: gui-forms-attach [--list | --program NAME]\n");
        return 2;
    }
    stxlib::OperationResult<stxlib::ProtocolStreams> claimed = stxlib::claim_protocol_streams();
    if (claimed.failed()) {
        std::fputs("gui-forms-attach: standard input and output could not be claimed\n", stderr);
        return 1;
    }
    stxlib::ProtocolStreams streams = std::move(*claimed.value());
    std::string message{};
    if (!read_message(streams.input, message)) return 0;
    const std::vector<AttachListing> listings = gui_forms::automation::list_attach_endpoints();
    const std::vector<AttachListing> found = matching(listings, wanted);
    if (found.empty()) {
        answer_error(streams.output, message,
                     listings.empty() ? std::string("no GUI.Forms program of this user is running")
                                      : "no running GUI.Forms program matches " + wanted + "; running: " +
                                            describe(listings));
        return 1;
    }
    if (found.size() > 1U) {
        answer_error(streams.output, message,
                     std::to_string(found.size()) + " GUI.Forms programs match (" + describe(found) +
                         "); start gui-forms-attach with --program and an identity or pid");
        return 1;
    }
    AttachStreams connection{};
    std::string error{};
    if (!gui_forms::automation::connect_attach_endpoint(found.front().address, connection, error)) {
        answer_error(streams.output, message, error);
        return 1;
    }
    // The first message, then both directions.
    message += '\n';
    if (!write_text(connection.output, message)) return 1;
    Pump upward{&streams.input, &connection.output};
    Pump downward{&connection.input, &streams.output};
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&pump_thread, &upward);
    if (started.failed()) {
        std::fputs("gui-forms-attach: the bridge's thread did not start\n", stderr);
        return 1;
    }
    stxlib::Thread client_side = std::move(*started.value());
    pump(downward);
    // The program ended the connection: the client's side may be blocked
    // reading standard input, which the process's exit ends.
    static_cast<void>(client_side.abandon());
    finish();
}
