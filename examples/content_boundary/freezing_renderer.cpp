// A renderer surface that freezes (docs/user/content-surfaces.md, "When
// content freezes"), driven headless. A content program draws a few frames
// on its own thread and then hangs: it stops publishing and never looks at
// its stop token again, as a renderer stuck in a loop would. The interface
// goes on answering (a counter button keeps counting), the watchdog marks
// the surface stale and keeps its last frame under a veil, the window offers
// Terminate, and terminating abandons the hung thread after the bounded
// grace. Nothing the hung thread does from then on reaches the interface.
//
// This is integrity, not security: the hung thread still runs in the
// process until the process exits. Content that cannot be trusted to stop
// belongs in a hosted process (hosted_crash.cpp).

#include <gui_forms/component.hpp>
#include <gui_forms/content.hpp>
#include <gui_forms/control/stable_id/stable_id.hpp>
#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/label.hpp>
#include <gui_forms/controls/panel.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/window/window.hpp>
#include <stxlib/clocks.hpp>
#include <stxlib/threads.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace gf = gui_forms;

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

// How many frames the renderer draws before it hangs.
constexpr std::uint32_t frames_before_freezing = 12U;

// Fills `pixels` (width by height, opaque premultiplied BGRA rows of
// width * 4 bytes) with a band that moves with `step`.
void draw_band(std::vector<std::byte>& pixels, std::uint32_t width, std::uint32_t height, std::uint32_t step) {
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    pixels.resize(row_bytes * height);
    const std::uint32_t band = (step * 8U) % (width == 0U ? 1U : width);
    for (std::uint32_t y = 0U; y < height; ++y) {
        std::byte* const row = pixels.data() + row_bytes * y;
        for (std::uint32_t x = 0U; x < width; ++x) {
            const bool lit = x >= band && x < band + 16U;
            std::byte* const pixel = row + static_cast<std::size_t>(x) * 4U;
            pixel[0] = static_cast<std::byte>(lit ? 240U : 64U);
            pixel[1] = static_cast<std::byte>(lit ? 200U : 48U);
            pixel[2] = static_cast<std::byte>(lit ? 80U : 32U);
            pixel[3] = static_cast<std::byte>(255U);
        }
    }
}

// The consumer's renderer. It owns its pixel buffer; it shares nothing with
// the rest of the program but its sink, so abandoning it is safe.
class FreezingRenderer final : public gf::ContentProgram {
public:
    void run(gf::ContentContext& context) override {
        std::uint64_t seen = 0U;
        gf::ContentRequest request = context.sink().request();
        while (request.size.width == 0U || request.size.height == 0U) {
            if (context.stop_requested()) return;
            request = context.wait_for_request(seen, std::chrono::milliseconds(100));
            seen = request.generation;
        }
        for (std::uint32_t step = 0U; step < frames_before_freezing && !context.stop_requested(); ++step) {
            draw_band(pixels_, request.size.width, request.size.height, step);
            gf::ContentFrame frame{};
            frame.width = request.size.width;
            frame.height = request.size.height;
            frame.row_bytes = static_cast<std::uint64_t>(request.size.width) * 4U;
            frame.opaque = true;
            frame.pixels = std::span<const std::byte>(pixels_.data(), pixels_.size());
            static_cast<void>(context.sink().publish(frame));
            stxlib::sleep_for(stxlib::Duration::milliseconds(16));
        }
        // The defect this example is about: the renderer hangs and never
        // checks its stop token again.
        for (;;) stxlib::sleep_for(stxlib::Duration::milliseconds(50));
    }

private:
    std::vector<std::byte> pixels_{};
};

// The window's behaviour: a status line, a counter that shows the interface
// still answers, and Terminate, offered while the content is stale.
class Shell final : public gf::Component {
public:
    Shell(gf::ContentSurface& surface, gf::Label& status, gf::Button& terminate, gf::Button& count)
        : surface_(surface), status_(status), terminate_(terminate) {
        gf::on(surface.status_changed(), *this, &Shell::status_changed);
        gf::on(terminate.clicked(), *this, &Shell::terminate_clicked);
        gf::on(count.clicked(), *this, &Shell::count_clicked);
        terminate_.enabled = false;
    }

    std::uint32_t counted{};

private:
    void status_changed(gf::ContentSurfaceStatus status) {
        status_.text = std::string("Renderer: ") + std::string(gf::content_surface_status_name(status));
        // Offer Terminate only while the content has stopped answering.
        terminate_.enabled = status == gf::ContentSurfaceStatus::stale;
    }
    void terminate_clicked() { surface_.terminate(); }
    void count_clicked() { ++counted; }

    gf::ContentSurface& surface_;
    gf::Label& status_;
    gf::Button& terminate_;
};

struct StatusIs final {
    gf::ContentSurfaceStatus wanted{};
    bool operator()(const gf::ContentSurface& surface) const { return surface.status() == wanted; }
};

// The host's loop, run by hand: deliver posted work, run the surface's
// timer check, and press the counter, until `done` holds or five seconds
// pass.
bool run_until(gf::Window& window, gf::ContentSurface& surface, gf::Button& count, StatusIs done) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(window.drain_posted_work());
        surface.check_watchdog(gf::FrameClock::now());
        static_cast<void>(count.perform_click());
        if (done(surface)) return true;
        stxlib::sleep_for(stxlib::Duration::milliseconds(5));
    }
    return false;
}

} // namespace

int main() {
    std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("freezing.root"));
    std::shared_ptr<gf::ContentSurface> surface = gf::make_control<gf::ContentSurface>(gf::StableId("freezing.surface"));
    std::shared_ptr<gf::Label> status = gf::make_control<gf::Label>(gf::StableId("freezing.status"));
    std::shared_ptr<gf::Button> terminate_button = gf::make_control<gf::Button>(gf::StableId("freezing.terminate"), "Terminate");
    std::shared_ptr<gf::Button> count = gf::make_control<gf::Button>(gf::StableId("freezing.count"), "Count");
    (*surface).set_requested_bounds({8.0, 8.0, 304.0, 160.0});
    (*status).set_requested_bounds({8.0, 176.0, 200.0, 24.0});
    (*terminate_button).set_requested_bounds({8.0, 208.0, 96.0, 28.0});
    (*count).set_requested_bounds({112.0, 208.0, 96.0, 28.0});
    (*root).add_child(surface);
    (*root).add_child(status);
    (*root).add_child(terminate_button);
    (*root).add_child(count);
    gf::Window window(root, gf::Size{320.0, 244.0});
    Shell shell(*surface, *status, *terminate_button, *count);
    window.perform_layout();

    // Stale after 200 ms without a frame; a termination waits 250 ms.
    (*surface).set_stale_interval(std::chrono::milliseconds(200));
    (*surface).start_program(std::make_unique<FreezingRenderer>(), true);

    require(run_until(window, *surface, *count, StatusIs{gf::ContentSurfaceStatus::live}), "the renderer shows frames");
    require(run_until(window, *surface, *count, StatusIs{gf::ContentSurfaceStatus::stale}),
            "the watchdog notices the frozen renderer");
    require((*terminate_button).enabled.read(), "the window offers Terminate");
    require((*surface).live_surface() != nullptr, "the last good frame stays on screen, veiled");
    const std::uint64_t frames_shown = (*surface).statistics().frames_published;
    const std::uint32_t counted_while_frozen = shell.counted;
    require(counted_while_frozen > 0U, "the interface answered while the renderer was frozen");

    require((*terminate_button).perform_click(), "Terminate is pressed");
    require(run_until(window, *surface, *count, StatusIs{gf::ContentSurfaceStatus::abandoned}),
            "the hung renderer is abandoned after the bounded grace");
    require(shell.counted > counted_while_frozen, "the interface answered throughout");

    std::cout << "Frozen renderer: " << frames_shown << " frames, then stale; "
              << "terminated and abandoned while the counter reached " << shell.counted << "; "
              << gf::abandoned_content_workers() << " abandoned thread(s) left to the process exit\n";
    return 0;
}
