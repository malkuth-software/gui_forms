// The hosted program hosted_crash.cpp starts (docs/user/content-surfaces.md,
// "Hosted content"). It connects to GUI.Forms through HostedClient, draws a
// band that moves across its region for a while, and then really crashes:
// it aborts (SIGABRT, what a failed assertion or a corrupted heap ends in),
// and the system ends its process. SIGABRT rather than SIGSEGV keeps the
// crash a crash under the sanitizers, which turn a SIGSEGV into a report and
// an ordinary exit. Run by itself (not started by GUI.Forms) it says so and
// exits 2.

#include <gui_forms/content/hosted/hosted_client.hpp>

#include <stxlib/optional.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <thread>

namespace gf = gui_forms;

namespace {

// How many frames the program draws before it crashes.
constexpr std::uint32_t frames_before_crashing = 20U;

// Draws frame `step` into `slot` at the size the parent asked for (64 by
// 64 before it asks), halved until it fits the slot, and publishes it.
bool publish_band(gf::HostedClient& client, std::uint32_t slot, std::uint32_t step) {
    const gf::HostedConfigureRecord request = client.request();
    std::uint32_t width = request.width == 0U ? 64U : request.width;
    std::uint32_t height = request.height == 0U ? 64U : request.height;
    while (static_cast<std::uint64_t>(width) * height * 4U > client.slot_bytes()) {
        width = width / 2U + 1U;
        height = height / 2U + 1U;
    }
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
    const std::span<std::byte> pixels = client.slot_pixels(slot);
    const std::uint32_t band = (step * 6U) % width;
    for (std::uint32_t y = 0U; y < height; ++y) {
        std::byte* const row = pixels.data() + static_cast<std::size_t>(row_bytes * y);
        for (std::uint32_t x = 0U; x < width; ++x) {
            const bool lit = x >= band && x < band + 12U;
            std::byte* const pixel = row + static_cast<std::size_t>(x) * 4U;
            pixel[0] = static_cast<std::byte>(lit ? 60U : 40U);
            pixel[1] = static_cast<std::byte>(lit ? 220U : 60U);
            pixel[2] = static_cast<std::byte>(lit ? 120U : 30U);
            pixel[3] = static_cast<std::byte>(255U);
        }
    }
    gf::HostedFrameRecord record{};
    record.slot = slot;
    record.width = width;
    record.height = height;
    record.opaque = true;
    record.row_bytes = row_bytes;
    const bool published = client.publish(record);
    return published;
}

} // namespace

int main() {
    std::unique_ptr<gf::HostedClient> client = gf::HostedClient::connect();
    if (!client) {
        std::cerr << "crashing_program: start me through hosted_crash (GUI.Forms hosted content)\n";
        return 2;
    }
    std::uint32_t step = 0U;
    while ((*client).pump(std::chrono::milliseconds(0))) {
        if (step == frames_before_crashing) {
            // The defect this example is about. Only this process ends.
            std::abort();
        }
        const stxlib::Optional<std::uint32_t> slot = (*client).acquire_slot(std::chrono::milliseconds(100));
        if (slot.absent()) continue;
        if (!publish_band(*client, (*slot.value()), step)) break;
        ++step;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    // Asked to stop before the crash: leave politely.
    static_cast<void>((*client).goodbye());
    return 0;
}
