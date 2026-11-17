// The parent of the installed hosted-client consumer: starts
// hosted_client_child on a ContentSurface (GUIForms::Controls), waits for
// its frame to arrive over the channel, then closes the content, which asks
// the child to stop and reaps it. No window: the frame is counted by the
// surface's statistics, not shown.

#include <gui_forms/content/hosted/hosted_content.hpp>
#include <gui_forms/control/control/control.hpp>
#include <gui_forms/controls/content_surface.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

namespace {

int failures = 0;

void expect(bool condition, const char* name) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "hosted_client_consumer: failed: %s\n", name);
}

// Whether the surface counted a frame within five seconds.
bool frame_arrives(const gui_forms::ContentSurface& surface) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (surface.statistics().frames_published >= 1U) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

int run() {
    expect(gui_forms::hosted_content_support() == gui_forms::HostedContentSupport::available,
           "hosted content is available on this platform");
    std::shared_ptr<gui_forms::ContentSurface> surface =
        gui_forms::make_control<gui_forms::ContentSurface>(gui_forms::StableId("consumer.hosted.surface"));
    (*surface).set_termination_grace(std::chrono::milliseconds(2000));
    gui_forms::HostedContentOptions options{};
    options.executable = GUI_FORMS_CONSUMER_HOSTED_CHILD;
    options.slot_bytes = 64U * 1024U;
    options.stop_grace = std::chrono::milliseconds(500);
    const gui_forms::HostedInput input = (*surface).start_hosted_content(options, true);
    expect((*surface).program_running(), "the hosted program runs");
    expect(frame_arrives(*surface), "the child's frame arrives over the channel");
    expect(input.connected(), "the input channel is connected while the child runs");
    (*surface).close_content();
    expect((*surface).status() == gui_forms::ContentSurfaceStatus::disconnected, "closed content is disconnected");
    expect(!(*surface).program_running(), "closing reaps the hosted program");
    return failures;
}

} // namespace

int main() {
    int result = 1;
    try {
        result = run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "hosted_client_consumer: %s\n", error.what());
        result = 1;
    }
    std::puts(result == 0 ? "hosted_client_consumer: passed" : "hosted_client_consumer: failed");
    return result == 0 ? 0 : 1;
}
