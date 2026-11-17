// The hosted program of the installed hosted-client consumer: connects to
// its parent, publishes a 16 by 16 frame and its semantic text, and answers
// heartbeats until asked to stop, then says goodbye. Links
// GUIForms::HostedClient alone.

#include <gui_forms/content/hosted/hosted_client.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>

namespace {

constexpr std::uint32_t frame_side = 16U;
constexpr std::uint64_t frame_row_bytes = static_cast<std::uint64_t>(frame_side) * 4U;

// Fills `slot` with one opaque colour and publishes it; false when the
// slot is too small or the parent is gone.
bool publish_frame(gui_forms::HostedClient& client, std::uint32_t slot) {
    const std::uint64_t used = frame_row_bytes * frame_side;
    if (used > client.slot_bytes()) return false;
    const std::span<std::byte> pixels = client.slot_pixels(slot);
    for (std::size_t offset = 0U; offset + 4U <= static_cast<std::size_t>(used); offset += 4U) {
        pixels[offset] = static_cast<std::byte>(40U);
        pixels[offset + 1U] = static_cast<std::byte>(120U);
        pixels[offset + 2U] = static_cast<std::byte>(200U);
        pixels[offset + 3U] = static_cast<std::byte>(255U);
    }
    gui_forms::HostedFrameRecord record{};
    record.slot = slot;
    record.width = frame_side;
    record.height = frame_side;
    record.opaque = true;
    record.row_bytes = frame_row_bytes;
    record.damage = gui_forms::ContentPixelRect{0U, 0U, frame_side, frame_side};
    const bool sent = client.publish(record);
    return sent;
}

} // namespace

int main() {
    std::unique_ptr<gui_forms::HostedClient> client = gui_forms::HostedClient::connect();
    if (!client) {
        std::fputs("hosted_client_child: not started as hosted content\n", stderr);
        return 2;
    }
    const std::optional<std::uint32_t> slot = (*client).acquire_slot(std::chrono::milliseconds(1000));
    if (!slot || !publish_frame(*client, *slot)) return 3;
    const gui_forms::ContentSemantics semantics{"Installed hosted child", "one frame", ""};
    static_cast<void>((*client).publish_semantics(semantics));
    while ((*client).pump(std::chrono::milliseconds(20))) {
        static_cast<void>((*client).heartbeat());
    }
    static_cast<void>((*client).goodbye());
    return 0;
}
