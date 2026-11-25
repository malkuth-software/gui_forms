// gui-forms-attach: the stdio bridge to a running GUI.Forms program (MCP
// 1.0.5; the owner's attach ruling, revision 2, 2026-11-24).
//
// A client starts it from .mcp.json as an ordinary stdio MCP server:
//
//   { "mcpServers": { "running-program": {
//       "command": "gui-forms-attach", "args": ["--program", "app.vendor.product"] } } }
//
// It reads the client's first message (initialize), finds the program
// among the user's running GUI.Forms programs, connects to its attach
// endpoint (a socket or pipe only the same user can reach), names the
// client as the client named itself, and once the program answers ok it
// carries MCP both ways until either side ends. A program launched with
// --mcp or --mcp-shared answers at once; any other program asks its user
// first, every connection.
//
//   gui-forms-attach --list             the running programs: pid, identity
//   gui-forms-attach [--program NAME]   bridge to the program whose MCP
//                                       identity is NAME (or whose pid it
//                                       is); with no --program, the one
//                                       program running
//
// When no program, or more than one, matches, or the program refuses, the
// bridge answers the client's initialize with a JSON-RPC error saying so
// (naming the programs it found) and exits with status 1.

#include "../../src/runtime/automation/attach_endpoint.hpp"

#include <stxlib/json.hpp>
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

using gui_forms::automation::AttachAnswer;
using gui_forms::automation::AttachHello;
using gui_forms::automation::AttachListing;
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

std::string view_text(stxlib::TextView text) {
    std::string made(text.data(), static_cast<std::size_t>(text.count()));
    return made;
}

// What the first message says: the client's name and version, and the
// request's id as JSON text ("null" when there is none).
struct FirstMessage final {
    std::string client_name{};
    std::string client_version{};
    std::string id_json{"null"};
};

std::string json_string(std::string_view text) {
    std::string quoted("\"");
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            quoted += '\\';
            quoted += character;
        } else if (byte < 0x20U) {
            static const char digits[] = "0123456789abcdef";
            quoted += "\\u00";
            quoted += digits[(byte >> 4U) & 0x0fU];
            quoted += digits[byte & 0x0fU];
        } else {
            quoted += character;
        }
    }
    quoted += '"';
    return quoted;
}

FirstMessage read_first(const std::string& message) {
    FirstMessage first{};
    const stxlib::OperationResult<stxlib::TextView> checked = stxlib::validate_utf8(message.data(), message.size());
    if (checked.failed()) return first;
    stxlib::OperationResult<stxlib::JsonDocument> parsed = stxlib::JsonDocument::parse(*checked.value());
    if (parsed.failed()) return first;
    const stxlib::JsonDocument& document = *parsed.value();
    const stxlib::JsonRef root = document.root();
    const stxlib::Optional<stxlib::JsonRef> id = document.member(root, "id");
    if (const stxlib::JsonRef* const found = id.value()) {
        const stxlib::Optional<stxlib::TextView> number = document.number_text(*found);
        const stxlib::Optional<stxlib::TextView> text = document.text(*found);
        if (const stxlib::TextView* const digits = number.value()) first.id_json = view_text(*digits);
        if (const stxlib::TextView* const name = text.value()) first.id_json = json_string(view_text(*name));
    }
    const stxlib::Optional<stxlib::JsonRef> params = document.member(root, "params");
    const stxlib::JsonRef* const params_found = params.value();
    if (params_found == nullptr) return first;
    const stxlib::Optional<stxlib::JsonRef> info = document.member(*params_found, "clientInfo");
    const stxlib::JsonRef* const info_found = info.value();
    if (info_found == nullptr) return first;
    const stxlib::Optional<stxlib::TextView> name = document.member_text(*info_found, "name");
    const stxlib::Optional<stxlib::TextView> version = document.member_text(*info_found, "version");
    if (const stxlib::TextView* const text = name.value()) first.client_name = view_text(*text);
    if (const stxlib::TextView* const text = version.value()) first.client_version = view_text(*text);
    return first;
}

bool write_text(stxlib::PipeWriter& output, std::string_view text) {
    const stxlib::View<stxlib::Byte> bytes(reinterpret_cast<const stxlib::Byte*>(text.data()), text.size());
    return output.write_all(bytes).succeeded();
}

// The client's initialize answered with an error, so the client says why.
void answer_error(stxlib::PipeWriter& output, const FirstMessage& first, const std::string& message) {
    const std::string reply = "{\"jsonrpc\":\"2.0\",\"id\":" + first.id_json +
                              ",\"error\":{\"code\":-32000,\"message\":" + json_string(message) + "}}\n";
    static_cast<void>(write_text(output, reply));
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
        std::printf("%lld\t%s\n", static_cast<long long>(listing.process), listing.identity.c_str());
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
    const FirstMessage first = read_first(message);
    const std::vector<AttachListing> listings = gui_forms::automation::list_attach_endpoints();
    const std::vector<AttachListing> found = matching(listings, wanted);
    if (found.empty()) {
        answer_error(streams.output, first,
                     listings.empty() ? std::string("no GUI.Forms program of this user is running")
                                      : "no running GUI.Forms program matches " + wanted + "; running: " +
                                            describe(listings));
        return 1;
    }
    if (found.size() > 1U) {
        answer_error(streams.output, first,
                     std::to_string(found.size()) + " GUI.Forms programs match (" + describe(found) +
                         "); start gui-forms-attach with --program and an identity or pid");
        return 1;
    }
    AttachStreams connection{};
    std::string error{};
    if (!gui_forms::automation::connect_attach_endpoint(found.front().address, connection, error)) {
        answer_error(streams.output, first, error);
        return 1;
    }
    AttachHello hello{};
    hello.client_name = first.client_name;
    hello.client_version = first.client_version;
    if (!gui_forms::automation::write_attach_line(connection.output, gui_forms::automation::attach_hello_line(hello))) {
        answer_error(streams.output, first, "the program closed the connection");
        return 1;
    }
    std::fprintf(stderr, "gui-forms-attach: waiting for %s to accept the connection\n",
                 found.front().identity.c_str());
    std::string line{};
    AttachAnswer answer{};
    if (!gui_forms::automation::read_attach_line(connection.input, line) ||
        !gui_forms::automation::parse_attach_answer(line, answer)) {
        answer_error(streams.output, first, "the program did not answer the connection");
        return 1;
    }
    if (!answer.accepted) {
        answer_error(streams.output, first, "the program refused the connection: " + answer.reason);
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
