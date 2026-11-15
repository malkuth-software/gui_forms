// The hosted program the hosted-content tests start
// (gui_forms_hosted_content_tests). Its first argument picks how it behaves:
//
//   frames    publishes frames until asked to stop, then says goodbye and
//             exits 0; keeps a log of the input it receives as its
//             semantic text: "kd<code>" and "ku<code>" for keys, "t<text>"
//             and "c<text>" for committed and composing text, "pd", "pu"
//             and "pw" for pointer presses, releases and wheel (moves are
//             not logged), "f1" and "f0" for focus, separated by spaces
//   crash     publishes one frame, then aborts (a real crash: SIGABRT)
//   hang      publishes one frame, then ignores everything forever
//   garbage   writes bytes that are no message, then waits forever
//   oversize  publishes a frame larger than any frame may be, then frames
//   exit3     connects, then exits with status 3
//   standard3 connects, then exits with status 3 when standard input,
//             output and error are all open, 4 when any is closed
//   silent    never connects (the parent's start timeout ends it)

#include "gui_forms/content/hosted/hosted_client.hpp"
#include "gui_forms/events/input_events/input_events.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

[[noreturn]] void wait_forever() {
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}

// Fills `slot` with one opaque colour at the requested size (32 by 32
// before any request), bounded by the slot, and publishes it.
bool publish_frame(HostedClient& client, std::uint32_t slot, std::uint8_t shade) {
    const HostedConfigureRecord request = client.request();
    std::uint32_t width = request.width == 0U ? 32U : request.width;
    std::uint32_t height = request.height == 0U ? 32U : request.height;
    while (static_cast<std::uint64_t>(width) * height * 4U > client.slot_bytes()) {
        width = width / 2U + 1U;
        height = height / 2U + 1U;
    }
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
    const std::span<std::byte> pixels = client.slot_pixels(slot);
    const std::size_t used = static_cast<std::size_t>(row_bytes * height);
    for (std::size_t offset = 0U; offset + 4U <= used; offset += 4U) {
        pixels[offset] = static_cast<std::byte>(shade);
        pixels[offset + 1U] = static_cast<std::byte>(96U);
        pixels[offset + 2U] = static_cast<std::byte>(48U);
        pixels[offset + 3U] = static_cast<std::byte>(255U);
    }
    HostedFrameRecord record{};
    record.slot = slot;
    record.width = width;
    record.height = height;
    record.opaque = true;
    record.row_bytes = row_bytes;
    const bool sent = client.publish(record);
    return sent;
}

bool publish_one(HostedClient& client) {
    const std::optional<std::uint32_t> slot = client.acquire_slot(std::chrono::milliseconds(1000));
    if (!slot) return false;
    const bool sent = publish_frame(client, *slot, 200U);
    return sent;
}

// The log's token for one input event; empty for one not logged.
std::string input_token(const HostedClientInput& event) {
    switch (event.kind) {
    case HostedMessageKind::key: {
        const char* const prefix = event.key.action == static_cast<std::uint32_t>(KeyAction::up) ? "ku" : "kd";
        return std::string(prefix) + std::to_string(event.key.physical_key);
    }
    case HostedMessageKind::text: return std::string(event.composing ? "c" : "t") + event.text;
    case HostedMessageKind::pointer:
        if (event.pointer.action == static_cast<std::uint32_t>(PointerAction::down)) return "pd";
        if (event.pointer.action == static_cast<std::uint32_t>(PointerAction::up)) return "pu";
        if (event.pointer.action == static_cast<std::uint32_t>(PointerAction::wheel)) return "pw";
        return std::string{};
    case HostedMessageKind::focus: return event.focused ? "f1" : "f0";
    default:
        // The other kinds (the child's own messages, resize, goodbye) are not
        // input the log records.
        return std::string{};
    }
}

// The longest log the child keeps; older tokens are not trimmed, later
// ones are not logged.
constexpr std::size_t input_log_limit = 4096U;

int run_frames(HostedClient& client) {
    std::vector<HostedClientInput> input{};
    std::string log{};
    std::uint8_t shade = 0U;
    while (client.pump(std::chrono::milliseconds(20))) {
        input.clear();
        client.take_input(input);
        bool logged = false;
        for (const HostedClientInput& event : input) {
            const std::string token = input_token(event);
            if (token.empty() || log.size() + token.size() + 1U > input_log_limit) continue;
            if (!log.empty()) log.push_back(' ');
            log += token;
            logged = true;
        }
        if (logged) {
            const ContentSemantics answer{"Hosted child", log, ""};
            static_cast<void>(client.publish_semantics(answer));
        }
        const std::optional<std::uint32_t> slot = client.acquire_slot(std::chrono::milliseconds(0));
        if (slot) {
            static_cast<void>(publish_frame(client, *slot, shade));
            shade = static_cast<std::uint8_t>(shade + 8U);
        } else {
            static_cast<void>(client.heartbeat());
        }
    }
    static_cast<void>(client.goodbye());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view("frames");
    if (mode == "silent") wait_forever();
    std::unique_ptr<HostedClient> client = HostedClient::connect();
    if (!client) return 2;
    if (mode == "exit3") return 3;
    if (mode == "standard3") {
        bool all_open = true;
        for (int standard = 0; standard <= 2; ++standard) {
            if (fcntl(standard, F_GETFD) == -1) all_open = false;
        }
        const int status = all_open ? 3 : 4;
        return status;
    }
    if (mode == "crash") {
        static_cast<void>(publish_one(*client));
        std::abort();
    }
    if (mode == "hang") {
        static_cast<void>(publish_one(*client));
        wait_forever();
    }
    if (mode == "garbage") {
        const std::vector<std::byte> nonsense(16U, std::byte{0xFF});
        static_cast<void>(write((*client).channel_descriptor(), nonsense.data(), nonsense.size()));
        wait_forever();
    }
    if (mode == "oversize") {
        const std::optional<std::uint32_t> slot = (*client).acquire_slot(std::chrono::milliseconds(1000));
        if (slot) {
            HostedFrameRecord record{};
            record.slot = *slot;
            record.width = 20000U;
            record.height = 1U;
            record.opaque = true;
            record.row_bytes = 20000U * 4U;
            static_cast<void>((*client).publish(record));
        }
    }
    const int status = run_frames(*client);
    return status;
}
