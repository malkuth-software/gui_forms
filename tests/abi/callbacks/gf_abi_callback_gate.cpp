// The callback gate (STXlib swap-in A2, survey B2 and B8, decision S2 = 1):
// a client's callback runs behind STXlib's callback gate inside exactly one
// shadow frame, the frame the library describes (an event's or a posted
// callback's); a C++ client that lets an exception out of its callback is
// recorded and fails fast through GUI.Forms' report. A failure the class
// bridge parks and rethrows later (ParkedFailure) keeps the shadow record of
// its original throw, from its own copy when the thread's record was
// overwritten in between (S3: one record per thread in a release build).
//
// Without an argument the program checks the returning paths and prints
// "callback gate tests passed". With a mode it lets an exception out of a
// client callback and must not print "survived"; CTest passes the mode when
// the report line appears (cmake/expect_fail_fast.cmake).

#include "gui_forms/c/gf_abi.h"

#include "../../../src/abi/stable/failure/abi_failure.hpp"
#include "../../../src/abi/stable/registry/abi_registry.hpp"
#include "../../../src/core/failure/failure_snapshot.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <stxlib/shadow.hpp>

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

// The class bridge's park (survey B8): base catches the raiser below's
// exception inside its slot gate and parks it with its shadow record; the
// raise rethrows it later under another gate, which files the original
// throw's frames, not the rethrow's.
constexpr abi1::SlotIdentity parking_slot{GF_TABLE_DIAGNOSTICS, 90U};
constexpr abi1::SlotIdentity rethrowing_slot{GF_TABLE_DIAGNOSTICS, 91U};

void throw_under_layout_frame() {
    const ShadowFrameScope layout(ShadowFrameKind::layout_pass, "parked.thrower", "measure", 0U);
    throw std::runtime_error("thrown before the park");
}

gf_status parking_body(abi1::ParkedFailure& parked) {
    try {
        throw_under_layout_frame();
    } catch (...) {
        const stxlib::ShadowReference reference = parked.park();
        const gf_status failed = abi1::fail_with_current_exception(parking_slot, reference);
        return failed;
    }
    return GF_STATUS_OK;
}

gf_status rethrowing_body(abi1::ParkedFailure& parked) {
    parked.rethrow();
    return GF_STATUS_OK;
}

std::string_view snapshot_message(const detail::FailureSnapshot& snapshot) {
    return std::string_view(snapshot.message.data(), snapshot.message_size);
}

// The snapshot holds the parking slot's frame and the layout frame of the
// throw, outermost first.
bool holds_original_throw(const detail::FailureSnapshot& snapshot) {
    const bool frames = snapshot.frame_count == 2U && snapshot.frames[0].kind == ShadowFrameKind::library_call &&
                        snapshot.frames[0].interface_id == GF_TABLE_DIAGNOSTICS &&
                        snapshot.frames[0].function_index == parking_slot.slot_index &&
                        snapshot.frames[1].kind == ShadowFrameKind::layout_pass &&
                        text_of(snapshot.frames[1].subject, snapshot.frames[1].subject_size) == "parked.thrower";
    return frames;
}

void test_parked_failure() {
    abi1::ParkedFailure parked;
    const gf_status first = abi1::call_slot(parking_slot, parking_body, parked);
    require(first == GF_STATUS_INTERNAL, "a parked failure is filed by the handler that parked it");
    require(static_cast<bool>(parked.exception) && parked.shadow.present(),
            "the park keeps the exception and its shadow record");
    const detail::FailureSnapshot& parked_snapshot = detail::thread_failure_snapshot();
    require(holds_original_throw(parked_snapshot) && snapshot_message(parked_snapshot) == "thrown before the park",
            "the park's snapshot holds the frames of the throw");
    const std::uint64_t parked_sequence = parked_snapshot.sequence;

    const gf_status second = abi1::call_slot(rethrowing_slot, rethrowing_body, parked);
    require(second == GF_STATUS_INTERNAL, "the rethrown failure fails the outer gate");
    require(!parked.exception && !parked.shadow.present(), "the rethrow holds nothing afterwards");
    const detail::FailureSnapshot& rethrown = detail::thread_failure_snapshot();
    require(rethrown.sequence == parked_sequence + 1U && rethrown.function_index == rethrowing_slot.slot_index,
            "the outer gate files the rethrown failure at its own slot");
    require(holds_original_throw(rethrown), "the outer gate files the original throw's frames, not the rethrow's");

    // Nothing parked: rethrow returns and the gate passes.
    const gf_status empty = abi1::call_slot(rethrowing_slot, rethrowing_body, parked);
    require(empty == GF_STATUS_OK, "an empty park rethrows nothing");

    ShadowStackState after{};
    static_cast<void>(copy_shadow_frames(std::span<ShadowFrame>(), after));
    require(after.depth == 0U, "the park and the rethrow leave no frame open");
}

// Under S3 (one shadow record per thread in a release build, four in a
// development build): failures filed on the thread between the park and the
// rethrow overwrite the parked record, and the outer gate still files the
// original throw's frames, from the failure's own copy (the owner's
// decision). More intervening failures than any build keeps records.
constexpr abi1::SlotIdentity intervening_slot{GF_TABLE_DIAGNOSTICS, 92U};

void throw_under_paint_frame() {
    const ShadowFrameScope paint(ShadowFrameKind::paint_pass, "intervening.thrower", "paint", 0U);
    throw std::runtime_error("thrown between the park and the rethrow");
}

gf_status intervening_body() {
    throw_under_paint_frame();
    return GF_STATUS_OK;
}

void test_parked_failure_outlives_captures() {
    abi1::ParkedFailure parked;
    require(abi1::call_slot(parking_slot, parking_body, parked) == GF_STATUS_INTERNAL, "the failure is parked");
    require(parked.frames != nullptr && (*parked.frames).parked.frame_count == 2U &&
                (*parked.frames).parked.reference.sequence == parked.shadow.sequence,
            "the park copies its record's frames into the failure");
    const std::size_t intervening = stxlib::shadow_record_capacity + 1U;
    for (std::size_t index = 0U; index < intervening; ++index) {
        require(abi1::call_slot(intervening_slot, intervening_body) == GF_STATUS_INTERNAL,
                "a failure between the park and the rethrow is filed");
    }
    const detail::FailureSnapshot& between = detail::thread_failure_snapshot();
    require(between.function_index == intervening_slot.slot_index && !holds_original_throw(between),
            "the intervening failures file their own frames");

    require(abi1::call_slot(rethrowing_slot, rethrowing_body, parked) == GF_STATUS_INTERNAL,
            "the rethrown failure fails the outer gate");
    const detail::FailureSnapshot& rethrown = detail::thread_failure_snapshot();
    require(rethrown.function_index == rethrowing_slot.slot_index && holds_original_throw(rethrown) &&
                snapshot_message(rethrown) == "thrown before the park",
            "the outer gate files the original throw's frames after the record was overwritten");
    require(parked.frames == nullptr, "the rethrow holds no copy afterwards");

    // The copy was filed as a new record for the rethrow only: the next
    // failure files its own frames.
    require(abi1::call_slot(intervening_slot, intervening_body) == GF_STATUS_INTERNAL, "a later failure is filed");
    require(!holds_original_throw(detail::thread_failure_snapshot()), "a filed copy is attached to no other failure");
}

// A nested park: an inner frame parks and rethrows its own failure while an
// outer one holds a failure; each rethrow files its own throw's frames.
constexpr abi1::SlotIdentity inner_parking_slot{GF_TABLE_DIAGNOSTICS, 93U};

gf_status inner_parking_body(abi1::ParkedFailure& parked) {
    try {
        throw_under_paint_frame();
    } catch (...) {
        const stxlib::ShadowReference reference = parked.park();
        const gf_status failed = abi1::fail_with_current_exception(inner_parking_slot, reference);
        return failed;
    }
    return GF_STATUS_OK;
}

bool holds_inner_throw(const detail::FailureSnapshot& snapshot) {
    const bool frames = snapshot.frame_count == 2U && snapshot.frames[0].kind == ShadowFrameKind::library_call &&
                        snapshot.frames[0].function_index == inner_parking_slot.slot_index &&
                        snapshot.frames[1].kind == ShadowFrameKind::paint_pass &&
                        text_of(snapshot.frames[1].subject, snapshot.frames[1].subject_size) ==
                            "intervening.thrower";
    return frames;
}

void test_nested_parks() {
    abi1::ParkedFailure outer;
    abi1::ParkedFailure inner;
    require(abi1::call_slot(parking_slot, parking_body, outer) == GF_STATUS_INTERNAL, "the outer failure is parked");
    require(abi1::call_slot(inner_parking_slot, inner_parking_body, inner) == GF_STATUS_INTERNAL,
            "the inner failure is parked while the outer is held");
    require(abi1::call_slot(rethrowing_slot, rethrowing_body, inner) == GF_STATUS_INTERNAL &&
                holds_inner_throw(detail::thread_failure_snapshot()),
            "the inner rethrow files the inner throw's frames");
    require(abi1::call_slot(rethrowing_slot, rethrowing_body, outer) == GF_STATUS_INTERNAL &&
                holds_original_throw(detail::thread_failure_snapshot()),
            "the outer rethrow still files the outer throw's frames");
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
        test_parked_failure();
        test_parked_failure_outlives_captures();
        test_nested_parks();
    } catch (const std::exception& error) {
        std::cerr << "callback gate test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "callback gate tests passed\n";
    return 0;
}
