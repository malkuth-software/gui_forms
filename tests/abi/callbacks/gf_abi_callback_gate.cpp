// The callback gate (STXlib swap-in A2, survey B2 and B8, decision S2 = 1):
// a client's callback runs behind STXlib's callback gate inside exactly one
// shadow frame, the frame the library describes (an event's or a posted
// callback's); a C++ client that lets an exception out of its callback is
// recorded and fails fast through GUI.Forms' report.
//
// Without an argument the program checks the returning paths and prints
// "callback gate tests passed". With a mode it lets an exception out of a
// client callback and must not print "survived"; CTest passes the mode when
// the report line appears (cmake/expect_fail_fast.cmake).

#include "gui_forms/c/gf_abi.h"

#include "../../../src/abi/stable/registry/abi_registry.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// What a returning callback saw of the thread's frames.
struct SeenFrames final {
    std::array<ShadowFrame, shadow_stack_capacity> frames{};
    ShadowStackState state{};
    std::size_t recorded = 0U;
    int calls = 0;
};

void GF_CALL copying_invoke(void* context, gf_event* event) {
    static_cast<void>(event);
    SeenFrames& seen = *static_cast<SeenFrames*>(context);
    seen.recorded = copy_shadow_frames(std::span<ShadowFrame>(seen.frames), seen.state);
    ++seen.calls;
}

gf_status GF_CALL answering_client(void* context, std::uint64_t value, std::uint64_t* answer) {
    static_cast<void>(context);
    *answer = value + 1U;
    return GF_STATUS_OK;
}

// A C++ client breaking the header's rule.
void GF_CALL throwing_invoke(void* context, gf_event* event) {
    static_cast<void>(context);
    static_cast<void>(event);
    throw std::runtime_error("a client callback threw");
}

gf_event named_event(std::string_view name) {
    gf_event event{};
    event.struct_size = sizeof(gf_event);
    event.name.data = name.data();
    event.name.size = name.size();
    return event;
}

std::string_view text_of(const std::array<char, shadow_frame_subject_capacity>& text, std::uint32_t size) {
    return std::string_view(text.data(), size);
}

std::string_view name_of(const ShadowFrame& frame) {
    return std::string_view(frame.name.data(), frame.name_size);
}

// A callback runs inside the one frame the library describes, and the
// gate closes it.
void test_described_frame() {
    SeenFrames seen;
    const gf_callback callback{sizeof(gf_callback), 0U, &copying_invoke, nullptr, &seen};
    abi1::CallbackBinding binding(callback);
    binding.disarm();
    gf_event event = named_event("click");
    const abi1::CallbackFrame frame{GF_SHADOW_FRAME_KIND_EVENT_CALLBACK, "callback.probe", "click"};
    binding.invoke(frame, event);
    require(seen.calls == 1, "the callback ran");
    require(seen.recorded == 1U && seen.state.depth == 1U, "a callback runs inside one frame, not the gate's too");
    require(seen.frames[0].kind == ShadowFrameKind::event_callback &&
                text_of(seen.frames[0].subject, seen.frames[0].subject_size) == "callback.probe" &&
                name_of(seen.frames[0]) == "click",
            "the frame is the event's, with its sender and name");

    const abi1::CallbackFrame posted{GF_SHADOW_FRAME_KIND_POSTED_CALLBACK, "callback.target", std::string_view{}};
    binding.invoke(posted, event);
    require(seen.calls == 2 && seen.recorded == 1U && seen.frames[0].kind == ShadowFrameKind::posted_callback,
            "a posted callback's frame is a posted_callback frame");

    // Without a described frame: an event_callback frame named by the event.
    gf_event shown = named_event("shown");
    binding.invoke(shown);
    require(seen.calls == 3 && seen.recorded == 1U && seen.frames[0].kind == ShadowFrameKind::event_callback &&
                name_of(seen.frames[0]) == "shown" && seen.frames[0].subject_size == 0U,
            "an undescribed callback's frame is named by its event");

    ShadowStackState after{};
    static_cast<void>(copy_shadow_frames(std::span<ShadowFrame>(), after));
    require(after.depth == 0U, "every callback frame closes");
}

// A client function with a result passes it back through the gate.
void test_client_result() {
    void* context = nullptr;
    std::uint64_t value = 41U;
    std::uint64_t answer = 0U;
    std::uint64_t* answer_output = &answer;
    const abi1::CallbackFrame frame{GF_SHADOW_FRAME_KIND_EVENT_CALLBACK, "callback.probe", "measure"};
    const gf_status status = abi1::call_client(frame, &answering_client, context, value, answer_output);
    require(status == GF_STATUS_OK && answer == 42U, "a client's status and outputs pass through the gate");
}

void throwing_event_mode() {
    const gf_callback callback{sizeof(gf_callback), 0U, &throwing_invoke, nullptr, nullptr};
    abi1::CallbackBinding binding(callback);
    binding.disarm();
    gf_event event = named_event("click");
    const abi1::CallbackFrame frame{GF_SHADOW_FRAME_KIND_EVENT_CALLBACK, "callback.thrower", "click"};
    binding.invoke(frame, event);
}

// A death test must not open the system's crash dialog.
void quiet_crash_reporting() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER) || defined(_UCRT)
    _set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        quiet_crash_reporting();
        const std::string_view mode = argv[1];
        if (mode == "throwing_event") {
            throwing_event_mode();
        } else {
            std::fprintf(stderr, "unknown mode\n");
            return EXIT_FAILURE;
        }
        std::printf("survived\n");
        return EXIT_SUCCESS;
    }
    try {
        test_described_frame();
        test_client_result();
    } catch (const std::exception& error) {
        std::cerr << "callback gate test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "callback gate tests passed\n";
    return 0;
}
