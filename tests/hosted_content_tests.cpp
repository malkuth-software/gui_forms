// Hosted content (owner decisions 2026-11-13, "The two topologies"): a
// sub-surface program in its own process, frames through shared memory,
// input through a channel, behind ContentSurface. The protocol checks run
// everywhere; the process cases run on macOS and Linux, starting
// gui_forms_hosted_child (tests/hosted_child.cpp) in its modes: a program
// that publishes and answers input, one that crashes for real, one that
// hangs, one that writes garbage, one whose frame is too large, one that
// exits with a status and one that never connects.

#include "gui_forms/content/hosted/hosted_content.hpp"
#include "gui_forms/content/hosted/hosted_protocol.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/content_surface.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// -- the protocol --------------------------------------------------------------

void messages_round_trip_and_split_feeds() {
    std::vector<std::byte> stream{};
    std::vector<std::byte> payload{};
    encode_hosted_hello(payload);
    append_hosted_message(stream, HostedMessageKind::hello, payload);
    HostedFrameRecord frame{};
    frame.slot = 1U;
    frame.width = 64U;
    frame.height = 32U;
    frame.opaque = true;
    frame.row_bytes = 256U;
    frame.damage = ContentPixelRect{1U, 2U, 3U, 4U};
    encode_hosted_frame(payload, frame);
    require(payload.size() == hosted_frame_record_bytes, "a frame record is 40 bytes");
    append_hosted_message(stream, HostedMessageKind::frame, payload);
    const ContentSemantics semantics{"Scene", "Three cubes", ""};
    encode_hosted_semantics(payload, semantics);
    append_hosted_message(stream, HostedMessageKind::semantics, payload);

    HostedMessageReader reader(true);
    HostedMessage message{};
    // Fed one byte at a time, the reader still yields whole messages.
    std::vector<HostedMessageKind> kinds{};
    for (const std::byte value : stream) {
        reader.feed(std::span<const std::byte>(&value, 1U));
        while (reader.next(message) == HostedReadResult::message) {
            kinds.push_back(message.kind);
            if (message.kind == HostedMessageKind::frame) {
                HostedFrameRecord decoded{};
                require(decode_hosted_frame(message.payload, decoded) && decoded.slot == 1U && decoded.width == 64U &&
                            decoded.row_bytes == 256U && decoded.damage == frame.damage && decoded.opaque,
                        "a frame record survives the wire");
            }
            if (message.kind == HostedMessageKind::semantics) {
                ContentSemantics decoded{};
                require(decode_hosted_semantics(message.payload, decoded) && decoded == semantics,
                        "semantic text survives the wire");
            }
        }
    }
    require(kinds.size() == 3U && kinds[0] == HostedMessageKind::hello && kinds[2] == HostedMessageKind::semantics,
            "every message arrives once, in order");
    require(reader.pending_bytes() == 0U, "nothing is left over");

    HostedKeyRecord key{};
    key.physical_key = PhysicalKey::enter;
    key.modifiers = 1U;
    encode_hosted_key(payload, key);
    HostedKeyRecord key_back{};
    require(decode_hosted_key(payload, key_back) && key_back.physical_key == PhysicalKey::enter, "a key round-trips");
    encode_hosted_text(payload, "\xE3\x81\x8B", true);
    std::string text{};
    bool composing = false;
    require(decode_hosted_text(payload, text, composing) && text == "\xE3\x81\x8B" && composing, "text round-trips");
    encode_hosted_focus(payload, true);
    bool focused = false;
    require(decode_hosted_focus(payload, focused) && focused, "a focus change round-trips");
    payload[0] = std::byte{2};
    require(!decode_hosted_focus(payload, focused), "a focus flag other than 0 or 1 is refused");
    require(hosted_message_kind_valid(static_cast<std::uint32_t>(HostedMessageKind::focus), false) &&
                !hosted_message_kind_valid(static_cast<std::uint32_t>(HostedMessageKind::focus), true),
            "only the parent sends focus");
}

void the_reader_refuses_broken_streams() {
    std::vector<std::byte> payload{};
    std::vector<std::byte> stream{};
    append_hosted_message(stream, HostedMessageKind::configure, payload);
    HostedMessageReader child_side(true);
    HostedMessage message{};
    child_side.feed(stream);
    require(child_side.next(message) == HostedReadResult::refused_kind,
            "the parent's reader refuses a kind only a parent sends");
    require(child_side.next(message) == HostedReadResult::refused_kind, "a refusal repeats");

    std::vector<std::byte> garbage(16U, std::byte{0xFF});
    HostedMessageReader magic(true);
    magic.feed(garbage);
    require(magic.next(message) == HostedReadResult::refused_magic, "a wrong magic is refused");

    std::vector<std::byte> oversized{};
    append_hosted_message(oversized, HostedMessageKind::heartbeat, payload);
    // The payload length field (bytes 8..11) claims more than the limit.
    oversized[8] = std::byte{0xFF};
    oversized[9] = std::byte{0xFF};
    oversized[10] = std::byte{0xFF};
    oversized[11] = std::byte{0x7F};
    HostedMessageReader size(true);
    size.feed(oversized);
    require(size.next(message) == HostedReadResult::refused_size, "a payload over the limit is refused unread");

    bool threw = false;
    try {
        const std::vector<std::byte> huge(hosted_maximum_payload_bytes + 1U);
        append_hosted_message(stream, HostedMessageKind::heartbeat, huge);
    } catch (const std::length_error&) {
        threw = true;
    }
    require(threw, "a sender cannot build an oversized message");

    std::vector<std::byte> bad_semantics{};
    const ContentSemantics invalid{"\xFF", "", ""};
    encode_hosted_semantics(bad_semantics, invalid);
    ContentSemantics decoded{};
    require(!decode_hosted_semantics(bad_semantics, decoded), "semantic text that is not UTF-8 is refused");
    HostedFrameRecord record{};
    require(!decode_hosted_frame(std::span<const std::byte>(bad_semantics.data(), 7U), record),
            "a short frame record is refused");
}

void frame_records_are_checked_against_their_slot() {
    constexpr std::uint64_t slot_bytes = 64U * 64U * 4U;
    const std::vector<std::byte> region(static_cast<std::size_t>(slot_bytes * hosted_slot_count));
    HostedFrameRecord record{};
    record.slot = 1U;
    record.width = 64U;
    record.height = 64U;
    record.row_bytes = 256U;
    record.opaque = true;
    HostedFrameView view = view_hosted_frame(record, region, hosted_slot_count, slot_bytes);
    require(view.check == HostedFrameCheck::accepted && view.frame.pixels.data() == region.data() + slot_bytes &&
                view.frame.pixels.size() == slot_bytes,
            "a frame that fills its slot is accepted and views that slot");
    record.slot = 2U;
    view = view_hosted_frame(record, region, hosted_slot_count, slot_bytes);
    require(view.check == HostedFrameCheck::refused_slot, "a slot past the region is refused");
    record.slot = 0U;
    record.height = 65U;
    view = view_hosted_frame(record, region, hosted_slot_count, slot_bytes);
    require(view.check == HostedFrameCheck::refused_frame && view.frame_result == ContentPublishResult::refused_extent,
            "a frame larger than its slot is refused before a pixel is read");
    record.height = 64U;
    record.row_bytes = 100U;
    view = view_hosted_frame(record, region, hosted_slot_count, slot_bytes);
    require(view.check == HostedFrameCheck::refused_frame && view.frame_result == ContentPublishResult::refused_row_bytes,
            "short rows are refused");
    const std::vector<std::byte> short_region(static_cast<std::size_t>(slot_bytes));
    record.row_bytes = 256U;
    record.slot = 1U;
    view = view_hosted_frame(record, short_region, hosted_slot_count, slot_bytes);
    require(view.check == HostedFrameCheck::refused_slot, "a region shorter than its slots is not trusted");
}

void options_are_checked() {
    HostedContentOptions relative{};
    relative.executable = "hosted_child";
    bool threw = false;
    try {
        static_cast<void>(make_hosted_content(relative));
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (const std::runtime_error&) {
        // Windows refuses every program before looking at it.
        threw = hosted_content_support() == HostedContentSupport::not_reported;
    }
    require(threw, "a program not named by an absolute path is refused");
    const bool reported = host::headless_capabilities().supports(HostCapability::hosted_content);
    const bool available = hosted_content_support() == HostedContentSupport::available;
    require(reported == available, "the capability is reported exactly where hosted content exists");
}

#if !defined(_WIN32)

// -- the process ---------------------------------------------------------------

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ContentSurface> surface;
    std::unique_ptr<Window> window;
    HostedInput input{};
};

Fixture start_child(const char* id, const char* mode,
                    std::chrono::milliseconds start_timeout = default_hosted_start_timeout) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.surface = make_control<ContentSurface>(StableId(std::string(id) + ".surface"));
    (*fixture.surface).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*fixture.root).add_child(fixture.surface);
    fixture.window = std::make_unique<Window>(fixture.root, Size{200.0, 120.0});
    (*fixture.window).perform_layout();
    // The watchdog is off unless a case turns it on; the termination grace
    // covers the stop grace and the reap.
    (*fixture.surface).set_stale_interval(FrameInterval::zero());
    (*fixture.surface).set_termination_grace(std::chrono::milliseconds(2000));
    HostedContentOptions options{};
    options.executable = GUI_FORMS_HOSTED_CHILD;
    options.arguments = {mode};
    options.slot_bytes = 1024U * 1024U;
    options.start_timeout = start_timeout;
    options.stop_grace = std::chrono::milliseconds(200);
    HostedContent hosted = make_hosted_content(options);
    fixture.input = hosted.input;
    (*fixture.surface).start_program(std::move(hosted.program), true);
    return fixture;
}

// Drains posted work and checks the surface until `done` holds or `bound`
// passes.
template <typename Condition>
bool settle(Fixture& fixture, Condition done, std::chrono::milliseconds bound = std::chrono::milliseconds(5000)) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + bound;
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>((*fixture.window).drain_posted_work());
        (*fixture.surface).check_watchdog(FrameClock::now());
        if (done(*fixture.surface)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

struct StatusIs final {
    ContentSurfaceStatus wanted{};
    bool operator()(const ContentSurface& surface) const { return surface.status() == wanted; }
};

struct HasFrames final {
    bool operator()(const ContentSurface& surface) const { return surface.statistics().frames_published >= 2U; }
};

struct SemanticTextHas final {
    std::string wanted{};
    bool operator()(const ContentSurface& surface) const {
        return surface.content_semantics().text.find(wanted) != std::string::npos;
    }
};

struct HasRefusal final {
    bool operator()(const ContentSurface& surface) const { return surface.statistics().frames_refused >= 1U; }
};

void frames_and_input_cross_the_process_boundary() {
    Fixture fixture = start_child("hosted.frames", "frames");
    require(settle(fixture, StatusIs{ContentSurfaceStatus::live}), "the hosted program's first frame makes it live");
    require(settle(fixture, HasFrames{}), "frames keep arriving through shared memory");
    require(fixture.input.connected(), "the input channel is connected");
    const KeyEvent key{.action = KeyAction::down, .physical_key = PhysicalKey::enter};
    require(fixture.input.send_key(key), "a key is sent without waiting");
    require(settle(fixture, SemanticTextHas{"kd" + std::to_string(PhysicalKey::enter)}),
            "the program received the key and answered through its semantic text");
    (*fixture.surface).terminate();
    require(settle(fixture, StatusIs{ContentSurfaceStatus::ended}), "a stopped program exits and the surface ends");
    require(!fixture.input.connected() && !fixture.input.send_key(key), "input to an ended program is dropped");
}

// Keeps every key_down for `withheld` from the program, as any subscriber
// may.
struct WithholdKey final {
    std::uint32_t withheld{};
    void operator()(KeyEventArgs& arguments) const {
        if (arguments.physical_key == withheld) arguments.handled = true;
    }
};

// Where `token` stands in a space-separated log, or npos.
std::size_t token_at(const std::string& log, const std::string& token) {
    const std::string padded = " " + log + " ";
    const std::size_t found = padded.find(" " + token + " ");
    return found;
}

void the_surface_forwards_its_input_in_order() {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("hosted.forward.root"));
    fixture.surface = make_control<ContentSurface>(StableId("hosted.forward.surface"));
    std::shared_ptr<Button> other = make_control<Button>(StableId("hosted.forward.other"), "Other");
    (*fixture.surface).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*other).set_requested_bounds({0.0, 90.0, 80.0, 24.0});
    (*fixture.root).add_child(fixture.surface);
    (*fixture.root).add_child(other);
    fixture.window = std::make_unique<Window>(fixture.root, Size{200.0, 120.0});
    (*fixture.window).perform_layout();
    (*fixture.surface).set_stale_interval(FrameInterval::zero());
    (*fixture.surface).set_termination_grace(std::chrono::milliseconds(2000));
    HostedContentOptions options{};
    options.executable = GUI_FORMS_HOSTED_CHILD;
    options.arguments = {"frames"};
    options.slot_bytes = 1024U * 1024U;
    options.stop_grace = std::chrono::milliseconds(200);
    fixture.input = (*fixture.surface).start_hosted_content(options, true);
    require(settle(fixture, StatusIs{ContentSurfaceStatus::live}), "the program runs");
    // A subscriber that handles a key keeps it from the program.
    SubscriptionToken withhold = (*fixture.surface).key_down().subscribe(WithholdKey{PhysicalKey::b});

    Window& window = *fixture.window;
    const Point inside{40.0, 30.0};
    PointerEvent down{PointerAction::down, PointerButton::primary, inside};
    PointerEvent up{PointerAction::up, PointerButton::primary, inside};
    static_cast<void>(window.dispatch_pointer(down));
    static_cast<void>(window.dispatch_pointer(up));
    // The press may have focused it already; either way it has the focus.
    static_cast<void>(window.request_focus(fixture.surface));
    require(window.control_focused(fixture.surface), "the surface has the focus");
    static_cast<void>(window.dispatch_key(KeyEvent{.action = KeyAction::down, .physical_key = PhysicalKey::b}));
    static_cast<void>(window.dispatch_key(KeyEvent{.action = KeyAction::up, .physical_key = PhysicalKey::b}));
    static_cast<void>(window.dispatch_key(KeyEvent{.action = KeyAction::down, .physical_key = PhysicalKey::a}));
    TextInputEvent committed{};
    committed.text_utf8 = "a";
    static_cast<void>(window.dispatch_text(committed));
    static_cast<void>(window.dispatch_key(KeyEvent{.action = KeyAction::up, .physical_key = PhysicalKey::a}));
    require(window.request_focus(other), "the focus moves away");
    require(settle(fixture, SemanticTextHas{"f0"}), "the program heard the focus leave");

    const std::string log = (*fixture.surface).content_semantics().text;
    const std::string key_a = std::to_string(PhysicalKey::a);
    const std::string key_b = std::to_string(PhysicalKey::b);
    const std::size_t pressed = token_at(log, "pd");
    const std::size_t released = token_at(log, "pu");
    const std::size_t focused = token_at(log, "f1");
    const std::size_t key_down = token_at(log, "kd" + key_a);
    const std::size_t text = token_at(log, "ta");
    const std::size_t key_up = token_at(log, "ku" + key_a);
    const std::size_t unfocused = token_at(log, "f0");
    require(pressed != std::string::npos && released != std::string::npos && pressed < released,
            "the press reaches the program before the release");
    require(focused != std::string::npos && focused < key_down, "the focus arrives before the keys");
    require(key_down < text && text < key_up && key_up < unfocused,
            "key down, its text, key up and the focus leaving arrive in the window's order");
    require(token_at(log, "kd" + key_b) == std::string::npos, "a key a subscriber handled is withheld");
    require(token_at(log, "ku" + key_b) != std::string::npos, "its release, which nobody handled, is forwarded");
    (*fixture.surface).close_content();
    require(!(*fixture.surface).hosted_input().connected(), "closing the content ends the forwarding");
}

void a_crash_ends_only_the_program() {
    Fixture fixture = start_child("hosted.crash", "crash");
    require(settle(fixture, StatusIs{ContentSurfaceStatus::failed}), "a crashed program fails the surface");
    require((*fixture.surface).failure().find("crashed") != std::string::npos, "the failure says it crashed");
    // The application goes on: the window lays out and the surface takes a
    // new program.
    (*fixture.window).perform_layout();
    require(settle(fixture, StatusIs{ContentSurfaceStatus::failed}), "the failed status stays");
}

void a_hung_program_goes_stale_and_is_ended() {
    Fixture fixture = start_child("hosted.hang", "hang");
    require(settle(fixture, StatusIs{ContentSurfaceStatus::live}), "the program publishes before it hangs");
    (*fixture.surface).set_stale_interval(std::chrono::milliseconds(100));
    require(settle(fixture, StatusIs{ContentSurfaceStatus::stale}), "the watchdog marks the hung program stale");
    const std::chrono::steady_clock::time_point asked = std::chrono::steady_clock::now();
    (*fixture.surface).terminate();
    require(settle(fixture, StatusIs{ContentSurfaceStatus::ended}), "terminate ends the hung process");
    require(std::chrono::steady_clock::now() - asked < std::chrono::seconds(2),
            "the process was killed after the stop grace, not waited on");
}

void garbage_breaks_the_protocol() {
    Fixture fixture = start_child("hosted.garbage", "garbage");
    require(settle(fixture, StatusIs{ContentSurfaceStatus::failed}), "a program writing garbage is ended as failed");
    require((*fixture.surface).failure().find("protocol") != std::string::npos, "the failure names the protocol");
}

void an_oversized_frame_is_refused_and_counted() {
    Fixture fixture = start_child("hosted.oversize", "oversize");
    require(settle(fixture, HasRefusal{}), "the oversized frame is refused");
    require(settle(fixture, StatusIs{ContentSurfaceStatus::live}), "a refused frame is not fatal; later frames show");
    (*fixture.surface).terminate();
    require(settle(fixture, StatusIs{ContentSurfaceStatus::ended}), "the program ends on request");
}

void exit_status_and_start_timeout() {
    Fixture exited = start_child("hosted.exit", "exit3");
    require(settle(exited, StatusIs{ContentSurfaceStatus::failed}), "a nonzero exit fails the surface");
    require((*exited.surface).failure().find("status 3") != std::string::npos, "the failure carries the status");

    Fixture silent = start_child("hosted.silent", "silent", std::chrono::milliseconds(300));
    require(settle(silent, StatusIs{ContentSurfaceStatus::failed}), "a program that never connects is ended");
    require((*silent.surface).failure().find("hello") != std::string::npos, "the failure names the missing hello");
}

void closing_the_window_ends_the_process() {
    Fixture fixture = start_child("hosted.close", "hang");
    require(settle(fixture, StatusIs{ContentSurfaceStatus::live}), "the program runs");
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    fixture.window.reset();
    fixture.root.reset();
    fixture.surface.reset();
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(3),
            "closing the window waited only the bounded grace");
}

#endif

} // namespace

int main() {
    try {
        messages_round_trip_and_split_feeds();
        the_reader_refuses_broken_streams();
        frame_records_are_checked_against_their_slot();
        options_are_checked();
#if !defined(_WIN32)
        frames_and_input_cross_the_process_boundary();
        the_surface_forwards_its_input_in_order();
        a_crash_ends_only_the_program();
        a_hung_program_goes_stale_and_is_ended();
        garbage_breaks_the_protocol();
        an_oversized_frame_is_refused_and_counted();
        exit_status_and_start_timeout();
        closing_the_window_ends_the_process();
#endif
    } catch (const std::exception& error) {
        std::cerr << "hosted content tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "hosted content tests passed\n";
    return 0;
}
