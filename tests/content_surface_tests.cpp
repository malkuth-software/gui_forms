// The content surface (owner decisions 2026-11-13, "What this asks of
// GUI.Forms", items 1 and 4): a consumer publishes finished frames from its
// own thread through a ContentFrameSink; every frame and size is validated
// before use; a missed-frame watchdog marks the surface stale and keeps the
// last good frame; the consumer supplies the surface's semantic text.

#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/content/watchdog/content_watchdog.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/content_surface.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/window/window.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

// Throws on failure, so the check runs in every build type.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A frame's pixels, filled with one premultiplied colour.
struct PixelBuffer final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t row_bytes{};
    std::vector<std::byte> bytes{};
};

PixelBuffer make_pixels(std::uint32_t width, std::uint32_t height, std::uint8_t value) {
    PixelBuffer buffer;
    buffer.width = width;
    buffer.height = height;
    buffer.row_bytes = static_cast<std::uint64_t>(width) * 4U;
    const std::size_t size = static_cast<std::size_t>(buffer.row_bytes) * height;
    buffer.bytes.assign(size, static_cast<std::byte>(value));
    return buffer;
}

ContentFrame frame_of(const PixelBuffer& buffer) {
    ContentFrame frame;
    frame.width = buffer.width;
    frame.height = buffer.height;
    frame.row_bytes = buffer.row_bytes;
    frame.opaque = true;
    frame.pixels = std::span<const std::byte>(buffer.bytes.data(), buffer.bytes.size());
    return frame;
}

// A surface in a window, laid out at 200 by 100.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ContentSurface> surface;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.surface = make_control<ContentSurface>(StableId(std::string(id) + ".surface"));
    (*fixture.surface).set_requested_bounds({10.0, 10.0, 200.0, 100.0});
    (*fixture.root).add_child(fixture.surface);
    fixture.window = std::make_unique<Window>(fixture.root, Size{320.0, 240.0});
    (*fixture.window).perform_layout();
    return fixture;
}

// Runs a publication on a thread of its own, as a consumer would.
struct PublishOnThread final {
    ContentFrameSink sink;
    ContentFrame frame;
    ContentPublishResult* result{};
    void operator()() { *result = sink.publish(frame); }
};

struct SemanticsOnThread final {
    ContentFrameSink sink;
    ContentSemantics semantics;
    ContentSemanticsResult* result{};
    void operator()() { *result = sink.publish_semantics(semantics); }
};

ContentPublishResult publish_from_thread(const ContentFrameSink& sink, const ContentFrame& frame) {
    ContentPublishResult result = ContentPublishResult::revoked;
    const PublishOnThread task{sink, frame, &result};
    std::thread producer(task);
    producer.join();
    return result;
}

struct StatusLog final {
    std::vector<ContentSurfaceStatus>* seen{};
    void operator()(ContentSurfaceStatus status) const { (*seen).push_back(status); }
};

// The node with `stable_id`, searched with an explicit stack (no recursion
// over a runtime-shaped tree); null when there is none.
const SemanticNode* find_node(const SemanticSnapshot& snapshot, const std::string& stable_id) {
    std::vector<const SemanticNode*> pending;
    for (const SemanticNode& root : snapshot.roots) pending.push_back(&root);
    while (!pending.empty()) {
        const SemanticNode* const node = pending.back();
        pending.pop_back();
        if ((*node).stable_id == stable_id) return node;
        for (const SemanticNode& child : (*node).children) pending.push_back(&child);
    }
    return nullptr;
}
const SemanticNode* find_node(const SemanticSnapshot&& snapshot, const std::string& stable_id) = delete;

void drain(Window& window) {
    for (int turn = 0; turn < 8; ++turn) static_cast<void>(window.drain_posted_work());
}

// -- validation ----------------------------------------------------------------

void frame_validation_refusals() {
    const PixelBuffer good = make_pixels(4U, 3U, 0x80U);
    require(validate_content_frame(frame_of(good)) == ContentPublishResult::published, "a whole frame is accepted");

    ContentFrame empty = frame_of(good);
    empty.width = 0U;
    require(validate_content_frame(empty) == ContentPublishResult::refused_empty, "a zero width is refused");
    empty = frame_of(good);
    empty.height = 0U;
    require(validate_content_frame(empty) == ContentPublishResult::refused_empty, "a zero height is refused");

    ContentFrame wide = frame_of(good);
    wide.width = maximum_content_frame_extent + 1U;
    require(validate_content_frame(wide) == ContentPublishResult::refused_too_large, "an axis past the limit is refused");
    ContentFrame square = frame_of(good);
    square.width = maximum_content_frame_extent;
    square.height = maximum_content_frame_extent;
    require(validate_content_frame(square) == ContentPublishResult::refused_too_large,
            "16384 squared pixels exceed the byte limit");

    ContentFrame narrow = frame_of(good);
    narrow.row_bytes = 15U;
    require(validate_content_frame(narrow) == ContentPublishResult::refused_row_bytes,
            "rows shorter than width * 4 bytes are refused");

    ContentFrame short_span = frame_of(good);
    short_span.pixels = short_span.pixels.first(short_span.pixels.size() - 1U);
    require(validate_content_frame(short_span) == ContentPublishResult::refused_extent,
            "a span one byte short of the frame is refused");
    ContentFrame no_padding = frame_of(good);
    no_padding.row_bytes = 20U;
    // 20 * 2 + 16 = 56 bytes needed, 48 held.
    require(validate_content_frame(no_padding) == ContentPublishResult::refused_extent,
            "padding the span does not hold is refused");
    ContentFrame huge_stride = frame_of(good);
    huge_stride.row_bytes = std::numeric_limits<std::uint64_t>::max() / 2U;
    require(validate_content_frame(huge_stride) == ContentPublishResult::refused_extent,
            "a stride whose extent overflows is refused, not wrapped");
    ContentFrame null_pixels = frame_of(good);
    null_pixels.pixels = std::span<const std::byte>{};
    require(validate_content_frame(null_pixels) == ContentPublishResult::refused_extent, "no pixels is refused");

    ContentFrame strange = frame_of(good);
    strange.pixel_format = static_cast<LiveSurfacePixelFormat>(7U);
    require(validate_content_frame(strange) == ContentPublishResult::refused_format, "an unknown format is refused");

    ContentFrame damaged = frame_of(good);
    damaged.damage = ContentPixelRect{2U, 1U, 2U, 2U};
    require(validate_content_frame(damaged) == ContentPublishResult::published, "damage inside the frame is accepted");
    damaged.damage = ContentPixelRect{3U, 0U, 2U, 1U};
    require(validate_content_frame(damaged) == ContentPublishResult::refused_damage, "damage past the right is refused");
    damaged.damage = ContentPixelRect{std::numeric_limits<std::uint32_t>::max(), 0U, 2U, 1U};
    require(validate_content_frame(damaged) == ContentPublishResult::refused_damage,
            "damage whose right edge wraps 32 bits is refused");

    require(content_frame_extent_bytes(4U, 3U, 16U) == 48U, "the extent of a packed frame");
    require(content_frame_extent_bytes(4U, 3U, 20U) == 56U, "the last row carries no padding");
    require(content_frame_extent_bytes(0U, 3U, 16U) == 0U, "an empty frame has no extent");
    require(content_frame_extent_bytes(4U, 3U, std::numeric_limits<std::uint64_t>::max()) == 0U,
            "an overflowing extent is zero");
    require(content_publish_result_name(ContentPublishResult::refused_extent) == "refused_extent", "result names");
}

void semantics_validation() {
    ContentSemantics plain{"Scene", "Teapot, three lights", ""};
    require(validate_content_semantics(plain) == ContentSemanticsResult::published, "UTF-8 text is accepted");
    ContentSemantics broken{"Scene", std::string("\xC3\x28", 2U), ""};
    require(validate_content_semantics(broken) == ContentSemanticsResult::refused_invalid_utf8,
            "malformed UTF-8 is refused");
    ContentSemantics large{"", std::string(maximum_content_semantic_text_bytes, 'a'), "b"};
    require(validate_content_semantics(large) == ContentSemanticsResult::refused_too_long,
            "text past the limit in all three fields is refused");
}

// -- the watchdog -------------------------------------------------------------

void watchdog_turns_stale_and_recovers() {
    const FrameTime start = FrameTime{} + std::chrono::seconds(100);
    ContentWatchdog watchdog(std::chrono::milliseconds(400));
    require(!watchdog.watching(), "a watchdog watches only after start");
    require(watchdog.observe(start, 0U) == ContentWatchdogVerdict::unchanged, "an unstarted watchdog decides nothing");
    watchdog.start(start, 5U);
    require(watchdog.observe(start + std::chrono::milliseconds(400), 5U) == ContentWatchdogVerdict::unchanged,
            "exactly the interval is not yet stale");
    require(watchdog.observe(start + std::chrono::milliseconds(401), 5U) == ContentWatchdogVerdict::became_stale,
            "past the interval without activity is stale");
    require(watchdog.stale(), "the watchdog reports stale");
    require(watchdog.observe(start + std::chrono::seconds(5), 5U) == ContentWatchdogVerdict::unchanged,
            "stale is reported once");
    require(watchdog.observe(start + std::chrono::seconds(6), 6U) == ContentWatchdogVerdict::recovered,
            "activity recovers stale content");
    require(watchdog.observe(start + std::chrono::milliseconds(6300), 6U) == ContentWatchdogVerdict::unchanged,
            "the interval restarts at the movement");
    require(watchdog.observe(start + std::chrono::seconds(1), 6U) == ContentWatchdogVerdict::unchanged,
            "a time earlier than the last check changes nothing");
    require(watchdog.observe(start + std::chrono::milliseconds(6401), 6U) == ContentWatchdogVerdict::became_stale,
            "stale again past the restarted interval");

    ContentWatchdog disabled(FrameInterval::zero());
    disabled.start(start, 0U);
    require(!disabled.enabled() && disabled.observe(start + std::chrono::hours(1), 0U) ==
                                      ContentWatchdogVerdict::unchanged,
            "a zero interval disables the watchdog");
    require(disabled.check_period() == FrameInterval::zero(), "a disabled watchdog needs no checks");
    ContentWatchdog tiny(std::chrono::milliseconds(1));
    require(tiny.interval() == minimum_content_stale_interval, "short intervals are raised to the minimum");
    ContentWatchdog negative(std::chrono::milliseconds(-5));
    require(!negative.enabled(), "a negative interval disables");
}

// -- the surface ----------------------------------------------------------------

void frames_from_a_consumer_thread() {
    Fixture fixture = make_fixture("content.frames");
    ContentSurface& surface = *fixture.surface;
    require(surface.status() == ContentSurfaceStatus::disconnected, "a new surface has no content");
    std::vector<ContentSurfaceStatus> seen;
    SubscriptionToken token = surface.status_changed().subscribe(StatusLog{&seen});
    ContentFrameSink sink = surface.open_content(true);
    require(surface.status() == ContentSurfaceStatus::waiting && seen.size() == 1U, "open content waits");
    const ContentRequest request = sink.request();
    require(request.size.width == 200U && request.size.height == 100U && request.generation != 0U,
            "the sink tells the content the region's device size");

    const PixelBuffer pixels = make_pixels(200U, 100U, 0x40U);
    require(publish_from_thread(sink, frame_of(pixels)) == ContentPublishResult::published,
            "a valid frame from the consumer's thread is published");
    drain(*fixture.window);
    require(surface.status() == ContentSurfaceStatus::live, "the first frame's notice makes the surface live");
    const std::shared_ptr<LiveSurface> live = surface.live_surface();
    const LiveSurfaceSnapshot first = (*live).snapshot();
    require(first.has_frame && first.description.width == 200U, "the frame is the surface's newest");

    ContentFrame short_frame = frame_of(pixels);
    short_frame.pixels = short_frame.pixels.first(64U);
    require(publish_from_thread(sink, short_frame) == ContentPublishResult::refused_extent,
            "a frame its span does not hold is refused on the consumer's thread");
    const LiveSurfaceSnapshot after = (*live).snapshot();
    require(after.published_generation == first.published_generation && after.has_frame,
            "a refusal leaves the last good frame");
    const ContentSurfaceStatistics statistics = surface.statistics();
    require(statistics.frames_published == 1U && statistics.frames_refused == 1U &&
                statistics.last_refusal == ContentPublishResult::refused_extent,
            "the counters record the publication and the refusal");

    const PixelBuffer smaller = make_pixels(64U, 32U, 0x20U);
    require(sink.publish(frame_of(smaller)) == ContentPublishResult::published, "a frame of another size reshapes");
    require((*live).snapshot().description.width == 64U, "the buffers follow the frame's size");

    surface.close_content();
    require(sink.revoked() && sink.publish(frame_of(smaller)) == ContentPublishResult::revoked,
            "a closed surface's sink reports revoked");
    require(surface.status() == ContentSurfaceStatus::disconnected && !surface.live_surface(),
            "closing clears the region");
    ContentFrameSink empty_sink;
    require(empty_sink.publish(frame_of(smaller)) == ContentPublishResult::revoked, "an empty sink is revoked");
}

void reopening_revokes_the_old_sink() {
    Fixture fixture = make_fixture("content.reopen");
    ContentSurface& surface = *fixture.surface;
    ContentFrameSink first = surface.open_content(false);
    ContentFrameSink second = surface.open_content(false);
    const PixelBuffer pixels = make_pixels(8U, 8U, 0x10U);
    require(first.publish(frame_of(pixels)) == ContentPublishResult::revoked, "the earlier channel is revoked");
    require(second.publish(frame_of(pixels)) == ContentPublishResult::published, "the new channel publishes");
    require(first.statistics().after_revocation == 1U, "the old channel counts the late publication");
}

void stale_frames_keep_the_last_good_frame() {
    Fixture fixture = make_fixture("content.stale");
    ContentSurface& surface = *fixture.surface;
    std::vector<ContentSurfaceStatus> seen;
    SubscriptionToken token = surface.status_changed().subscribe(StatusLog{&seen});
    surface.set_stale_interval(std::chrono::milliseconds(200));
    ContentFrameSink sink = surface.open_content(true);
    const PixelBuffer pixels = make_pixels(200U, 100U, 0x40U);
    require(sink.publish(frame_of(pixels)) == ContentPublishResult::published, "one frame");
    drain(*fixture.window);
    require(surface.status() == ContentSurfaceStatus::live, "live after the frame");
    const std::uint64_t shown = (*surface.live_surface()).snapshot().published_generation;

    // The first check absorbs the frame's activity; silence counts from it.
    const FrameTime now = FrameClock::now();
    surface.check_watchdog(now);
    const FrameTime later = now + std::chrono::seconds(2);
    surface.check_watchdog(later);
    require(surface.status() == ContentSurfaceStatus::stale, "no frame for longer than the interval is stale");
    require(surface.statistics().stale_transitions == 1U, "the transition is counted");
    require((*surface.live_surface()).snapshot().published_generation == shown &&
                (*surface.live_surface()).snapshot().has_frame,
            "a stale surface keeps its last good frame");
    const SemanticDescriptor busy = surface.semantic_descriptor();
    require(has_semantic_state(busy.states, SemanticState::busy), "a stale surface publishes busy");
    require(surface.status_caption(ContentSurfaceStatus::stale) == "Not responding", "the default stale caption");

    sink.heartbeat();
    surface.check_watchdog(later + std::chrono::milliseconds(10));
    require(surface.status() == ContentSurfaceStatus::live, "a heartbeat recovers the surface");
    require(surface.statistics().recoveries == 1U, "the recovery is counted");
    const SemanticDescriptor calm = surface.semantic_descriptor();
    require(!has_semantic_state(calm.states, SemanticState::busy), "a live surface is not busy");

    surface.check_watchdog(later + std::chrono::seconds(5));
    require(surface.status() == ContentSurfaceStatus::stale, "silence makes it stale again");
    require(publish_from_thread(sink, frame_of(pixels)) == ContentPublishResult::published, "a late frame");
    surface.check_watchdog(later + std::chrono::seconds(6));
    require(surface.status() == ContentSurfaceStatus::live, "a frame recovers the surface");
    require(seen.size() == 6U && seen[0] == ContentSurfaceStatus::waiting && seen[2] == ContentSurfaceStatus::stale &&
                seen[5] == ContentSurfaceStatus::live,
            "every status change is raised in order");

    surface.set_stale_interval(FrameInterval::zero());
    surface.check_watchdog(later + std::chrono::hours(1));
    require(surface.status() == ContentSurfaceStatus::live, "a disabled watchdog never calls content stale");
}

void waiting_content_turns_stale_too() {
    Fixture fixture = make_fixture("content.never");
    ContentSurface& surface = *fixture.surface;
    surface.set_stale_interval(std::chrono::milliseconds(100));
    ContentFrameSink sink = surface.open_content(false);
    surface.check_watchdog(FrameClock::now() + std::chrono::seconds(1));
    require(surface.status() == ContentSurfaceStatus::stale, "content that never publishes turns stale");
    sink.heartbeat();
    surface.check_watchdog(FrameClock::now() + std::chrono::seconds(2));
    require(surface.status() == ContentSurfaceStatus::waiting, "a heartbeat without a frame returns to waiting");
}

void semantic_text_from_the_consumer() {
    Fixture fixture = make_fixture("content.semantics");
    ContentSurface& surface = *fixture.surface;
    ContentFrameSink sink = surface.open_content(true);
    ContentSemanticsResult result = ContentSemanticsResult::revoked;
    const SemanticsOnThread task{sink, ContentSemantics{"Scene", "Teapot, three lights", "A rendered teapot"}, &result};
    std::thread producer(task);
    producer.join();
    require(result == ContentSemanticsResult::published, "the consumer's text is accepted");
    drain(*fixture.window);
    require(surface.content_semantics().name == "Scene", "the notice delivers the text on the UI thread");
    const SemanticSnapshot snapshot = (*fixture.window).semantic_snapshot();
    const SemanticNode* const node = find_node(snapshot, "content.semantics.surface");
    const bool found = node != nullptr && (*node).role == SemanticRole::figure && (*node).name == "Scene" &&
                       (*node).value == "Teapot, three lights";
    require(found, "the window's snapshot publishes the surface as a figure with the consumer's text");

    ContentSemantics broken{"Scene", std::string("\xFF", 1U), ""};
    require(sink.publish_semantics(broken) == ContentSemanticsResult::refused_invalid_utf8,
            "malformed text from the consumer is refused");
    require(surface.statistics().semantics_refused == 1U, "the refusal is counted");
    bool threw = false;
    try {
        surface.set_content_semantics(broken);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && surface.content_semantics().name == "Scene", "the UI-thread setter refuses it and keeps the text");
    surface.set_content_semantics(ContentSemantics{"Chart", "", ""});
    require(surface.semantic_descriptor().name == "Chart", "the UI thread may set the text");
}

void the_request_follows_the_region() {
    Fixture fixture = make_fixture("content.request");
    ContentSurface& surface = *fixture.surface;
    ContentFrameSink sink = surface.open_content(true);
    const ContentRequest first = sink.request();
    (*fixture.surface).set_requested_bounds({10.0, 10.0, 120.0, 60.0});
    (*fixture.window).perform_layout();
    const ContentRequest changed = sink.wait_for_request(first.generation, std::chrono::milliseconds(0));
    require(changed.generation > first.generation && changed.size.width == 120U && changed.size.height == 60U,
            "a new size reaches the content with a new generation");
    surface.close_content();
    const ContentRequest after = sink.wait_for_request(changed.generation, std::chrono::milliseconds(10000));
    require(after.generation == changed.generation, "a revoked channel ends the wait at once");
}

} // namespace

int main() {
    try {
        frame_validation_refusals();
        semantics_validation();
        watchdog_turns_stale_and_recovers();
        frames_from_a_consumer_thread();
        reopening_revokes_the_old_sink();
        stale_frames_keep_the_last_good_frame();
        waiting_content_turns_stale_too();
        semantic_text_from_the_consumer();
        the_request_follows_the_region();
    } catch (const std::exception& error) {
        std::cerr << "content surface tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "content surface tests passed\n";
    return 0;
}
