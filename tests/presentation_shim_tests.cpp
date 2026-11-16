// The presentation shim (src/core/host/presentation/, docs/user/
// presentation.md): its geometry (damage clipping, host scroll plans, the
// damage history of buffer ages, the copy and move kernels), the normative
// trace order, and the shim's paths over in-memory fakes: copy only, a
// direct path with one, two and three buffers, fallback on every refusal
// and on loss, the pause and resume after a refusal, host scroll on both
// paths, and producer mode (the window manager's). Plain C++, tested on
// every platform; no window system.

#include "../src/core/host/presentation/presentation_geometry.hpp"
#include "../src/core/host/presentation/presentation_shim.hpp"
#include "../src/core/host/presentation/presentation_trace.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms::host::presentation;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

PresentRect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
    const PresentRect result{left, top, right, bottom};
    return result;
}

// A BGRA8 image owned by the test, rows packed with `padding` spare bytes.
struct Image final {
    PresentExtent extent{};
    std::size_t row_bytes{};
    std::vector<std::byte> pixels{};
};

Image make_image(std::uint32_t width, std::uint32_t height, std::size_t padding) {
    Image image{};
    image.extent = PresentExtent{width, height};
    image.row_bytes = static_cast<std::size_t>(width) * 4U + padding;
    image.pixels.assign(image.row_bytes * height, std::byte{0});
    return image;
}

// Fills every pixel with a value made of its position and `seed`, so that
// a pixel in the wrong place is seen.
void paint_pattern(Image& image, std::uint32_t seed) {
    for (std::uint32_t y = 0U; y < image.extent.height; ++y) {
        for (std::uint32_t x = 0U; x < image.extent.width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * image.row_bytes + static_cast<std::size_t>(x) * 4U;
            image.pixels[offset] = static_cast<std::byte>((x + seed) & 0xffU);
            image.pixels[offset + 1U] = static_cast<std::byte>((y + 3U * seed) & 0xffU);
            image.pixels[offset + 2U] = static_cast<std::byte>((x * 7U + y) & 0xffU);
            image.pixels[offset + 3U] = std::byte{0xff};
        }
    }
}

void paint_rect(Image& image, PresentRect area, std::byte value) {
    for (std::int32_t y = area.top; y < area.bottom; ++y) {
        for (std::int32_t x = area.left; x < area.right; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * image.row_bytes + static_cast<std::size_t>(x) * 4U;
            for (std::size_t channel = 0U; channel < 4U; ++channel) image.pixels[offset + channel] = value;
        }
    }
}

bool same_pixels(const Image& a, const Image& b) {
    if (a.extent != b.extent) return false;
    for (std::uint32_t y = 0U; y < a.extent.height; ++y) {
        for (std::uint32_t x = 0U; x < a.extent.width * 4U; ++x) {
            const std::size_t left = static_cast<std::size_t>(y) * a.row_bytes + x;
            const std::size_t right = static_cast<std::size_t>(y) * b.row_bytes + x;
            if (a.pixels[left] != b.pixels[right]) return false;
        }
    }
    return true;
}

PresentSourcePixels source_of(const Image& image) {
    const PresentSourcePixels source{image.pixels.data(), image.row_bytes, image.extent};
    return source;
}

PresentTargetPixels target_of(Image& image) {
    const PresentTargetPixels target{image.pixels.data(), image.row_bytes, image.extent};
    return target;
}

std::vector<PresentTraceRecord> records_of(const PresentTrace& trace) {
    std::vector<PresentTraceRecord> records(trace.size());
    const std::size_t count = trace.fill(records);
    records.resize(count);
    return records;
}

// The events of `records` in order, for compact comparisons.
std::vector<PresentTraceEvent> events_of(const std::vector<PresentTraceRecord>& records) {
    std::vector<PresentTraceEvent> events{};
    events.reserve(records.size());
    for (std::size_t index = 0U; index < records.size(); ++index) events.push_back(records[index].event);
    return events;
}

void require_trace_valid(const PresentTrace& trace, const std::string& what) {
    const std::vector<PresentTraceRecord> records = records_of(trace);
    const PresentTraceCheck check = check_present_trace(records);
    require(check.passed(), what + ": trace violates the normative order at record " + std::to_string(check.index) +
                                " (violation " + std::to_string(static_cast<int>(check.violation)) + ")");
}

// The copy path's fake: a window's storage, written by copy and moved by
// scroll, with scripted outcomes.
class FakeCopyPath final : public PresentationCopyPath {
public:
    FakeCopyPath(std::uint32_t width, std::uint32_t height, bool scrolls)
        : window(make_image(width, height, 0U)), scrolls_(scrolls) {}

    [[nodiscard]] bool scrolls() const noexcept override { return scrolls_; }
    bool scroll(const PresentScrollPlan& plan) override {
        ++scroll_calls;
        const bool moved = move_present_pixels(target_of(window), plan);
        return moved;
    }
    PresentOutcome copy(const PresentSourcePixels& source, std::span<const PresentRect> rects) override {
        ++copy_calls;
        last_rects.assign(rects.begin(), rects.end());
        if (!outcomes.empty()) {
            const PresentOutcome scripted = outcomes.front();
            outcomes.pop_front();
            if (scripted != PresentOutcome::presented) return scripted;
        }
        const bool copied = copy_present_rects(source, target_of(window), rects);
        return copied ? PresentOutcome::presented : PresentOutcome::failed;
    }

    Image window;
    std::deque<PresentOutcome> outcomes{};
    std::vector<PresentRect> last_rects{};
    std::uint32_t copy_calls{};
    std::uint32_t scroll_calls{};

private:
    bool scrolls_;
};

// The direct path's fake: `count` buffers used in turn, each remembering
// the present it was last shown at so that it reports its age; the buffer
// shown is `shown`. Scripted refusals and outcomes are taken in order.
class FakeDirectPath final : public PresentationDirectPath {
public:
    FakeDirectPath(std::uint32_t width, std::uint32_t height, std::size_t count) {
        for (std::size_t index = 0U; index < count; ++index) {
            buffers.push_back(make_image(width, height, 8U));
            shown_at.push_back(0U);
        }
    }

    DirectAcquire acquire(PresentExtent extent) override {
        ++acquire_calls;
        if (!refusals.empty()) {
            const DirectRefusal refusal = refusals.front();
            refusals.pop_front();
            return DirectAcquire::refused(refusal);
        }
        if (released) {
            // Released buffers come back with unknown contents.
            for (std::size_t index = 0U; index < shown_at.size(); ++index) shown_at[index] = 0U;
            released = false;
        }
        Image& buffer = buffers[next];
        if (buffer.extent != extent) return DirectAcquire::refused(DirectRefusal::extent);
        held = next;
        DirectBuffer granted{};
        granted.pixels = buffer.pixels.data();
        granted.row_bytes = buffer.row_bytes;
        granted.extent = buffer.extent;
        granted.age = shown_at[next] == 0U ? 0U : static_cast<std::uint32_t>(presents - shown_at[next] + 1U);
        return DirectAcquire::granted(granted);
    }
    PresentOutcome present(std::span<const PresentRect> damage) override {
        last_damage.assign(damage.begin(), damage.end());
        if (!outcomes.empty()) {
            const PresentOutcome scripted = outcomes.front();
            outcomes.pop_front();
            if (scripted != PresentOutcome::presented) return scripted;
        }
        ++presents;
        shown_at[held] = presents;
        shown = held;
        next = (next + 1U) % buffers.size();
        return PresentOutcome::presented;
    }
    void release() noexcept override {
        ++release_calls;
        released = true;
    }

    std::vector<Image> buffers{};
    std::vector<std::uint64_t> shown_at{};
    std::deque<DirectRefusal> refusals{};
    std::deque<PresentOutcome> outcomes{};
    std::vector<PresentRect> last_damage{};
    std::size_t next{};
    std::size_t held{};
    std::size_t shown{};
    std::uint64_t presents{};
    std::uint32_t acquire_calls{};
    std::uint32_t release_calls{};
    bool released{};
};

PresentFrame frame_of(const Image& image, std::span<const PresentRect> damage,
                      std::span<const PresentScroll> scrolls) {
    PresentFrame frame{};
    frame.source = source_of(image);
    frame.damage = damage;
    frame.scrolls = scrolls;
    return frame;
}

// ---- geometry ----

void test_scroll_plan_partitions_the_area() {
    const PresentExtent extent{100U, 80U};
    const std::array<std::array<std::int32_t, 2>, 9> offsets{
        {{0, 10}, {0, -10}, {10, 0}, {-10, 0}, {7, 9}, {-7, 9}, {7, -9}, {-7, -9}, {0, 0}}};
    const PresentRect area = rect(10, 5, 70, 65);
    for (std::size_t index = 0U; index < offsets.size(); ++index) {
        const PresentScroll scroll{area, offsets[index][0], offsets[index][1]};
        const PresentScrollPlan plan = plan_present_scroll(scroll, extent);
        require(plan.area == area, "the area is kept when inside the buffer");
        std::uint64_t covered = plan.destination.area();
        for (std::size_t band = 0U; band < plan.exposed_count; ++band) {
            covered += plan.exposed[band].area();
            require(present_rect_contains(area, plan.exposed[band]), "an exposed band lies in the area");
            require(present_rect_intersection(plan.exposed[band], plan.destination).empty(),
                    "an exposed band does not overlap the destination");
        }
        if (plan.exposed_count == 2U)
            require(present_rect_intersection(plan.exposed[0], plan.exposed[1]).empty(), "the bands are disjoint");
        require(covered == area.area(), "destination and bands partition the area");
        require(plan.source.left == plan.destination.left - scroll.dx &&
                    plan.source.top == plan.destination.top - scroll.dy,
                "source is destination less the offset");
        require(present_rect_contains(area, plan.source), "the source lies in the area");
    }
    const PresentScrollPlan down = plan_present_scroll(PresentScroll{area, 0, 10}, extent);
    require(down.exposed_count == 1U && down.exposed[0] == rect(10, 5, 70, 15), "moving down exposes the top band");
    const PresentScrollPlan left = plan_present_scroll(PresentScroll{area, -10, 0}, extent);
    require(left.exposed_count == 1U && left.exposed[0] == rect(60, 5, 70, 65), "moving left exposes the right band");
}

void test_scroll_plan_edges() {
    const PresentExtent extent{50U, 40U};
    const PresentScrollPlan far = plan_present_scroll(PresentScroll{rect(0, 0, 50, 40), 0, 40}, extent);
    require(!far.moves() && far.exposed_count == 1U && far.exposed[0] == rect(0, 0, 50, 40),
            "an offset of the whole height exposes the whole area");
    const PresentScrollPlan huge = plan_present_scroll(PresentScroll{rect(0, 0, 50, 40), -2147483647 - 1, 0}, extent);
    require(!huge.moves() && huge.exposed_count == 1U, "the most negative offset is handled without overflow");
    const PresentScrollPlan clipped = plan_present_scroll(PresentScroll{rect(-20, 30, 70, 90), 0, -3}, extent);
    require(clipped.area == rect(0, 30, 50, 40), "the area is clipped to the buffer");
    require(clipped.exposed_count == 1U && clipped.exposed[0] == rect(0, 37, 50, 40), "the clipped area's band");
    const PresentScrollPlan outside = plan_present_scroll(PresentScroll{rect(60, 0, 80, 10), 1, 1}, extent);
    require(outside.area.empty() && outside.exposed_count == 0U && !outside.moves(), "an area outside moves nothing");
}

void test_damage_clipping_and_overflow() {
    const PresentExtent extent{20U, 10U};
    const std::array<PresentRect, 4> damage{rect(-5, -5, 5, 5), rect(30, 0, 40, 5), rect(15, 8, 25, 12),
                                            rect(3, 3, 3, 9)};
    std::array<PresentRect, 4> out{};
    const std::size_t count = clip_present_damage(damage, extent, out);
    require(count == 2U && out[0] == rect(0, 0, 5, 5) && out[1] == rect(15, 8, 20, 10),
            "clipping drops the outside and the empty");
    std::array<PresentRect, 1> small{};
    const std::size_t collapsed = clip_present_damage(damage, extent, small);
    require(collapsed == 1U && small[0] == rect(0, 0, 20, 10), "an overflow keeps the bounding rectangle");
    const std::size_t none = clip_present_damage(damage, extent, std::span<PresentRect>{});
    require(none == 0U, "no output storage writes nothing");
}

void test_damage_history_ages() {
    const PresentExtent extent{100U, 100U};
    PresentDamageHistory history{};
    const std::array<PresentRect, 1> first{rect(0, 0, 10, 10)};
    const std::array<PresentRect, 1> second{rect(20, 20, 30, 30)};
    const std::array<PresentRect, 1> current{rect(50, 50, 60, 60)};
    history.record(first);
    history.record(second);
    std::array<PresentRect, 8> out{};
    std::size_t count = 0U;
    require(history.accumulate(1U, current, extent, out, count) && count == 1U && out[0] == current[0],
            "age 1 writes only the current damage");
    require(history.accumulate(2U, current, extent, out, count) && count == 2U && out[1] == second[0],
            "age 2 adds the frame before");
    require(history.accumulate(3U, current, extent, out, count) && count == 3U && out[2] == first[0],
            "age 3 adds the two frames before");
    require(!history.accumulate(4U, current, extent, out, count), "an age older than the history writes everything");
    require(!history.accumulate(0U, current, extent, out, count), "age 0 writes everything");
    for (std::size_t index = 0U; index < PresentDamageHistory::frames + 2U; ++index) history.record(current);
    require(history.recorded() == PresentDamageHistory::frames, "the history keeps a fixed number of frames");
    history.reset();
    require(!history.accumulate(2U, current, extent, out, count), "a reset forgets every frame");
    std::array<PresentRect, PresentDamageHistory::rects_per_frame + 1U> many{};
    for (std::size_t index = 0U; index < many.size(); ++index) {
        const std::int32_t left = static_cast<std::int32_t>(index * 4U);
        many[index] = rect(left, 0, left + 2, 2);
    }
    history.record(many);
    require(history.accumulate(2U, std::span<const PresentRect>{}, extent, out, count) && count == 1U &&
                out[0] == rect(0, 0, 66, 2),
            "an overfull frame is kept as its bounding rectangle");
}

void test_hold_count() {
    const std::array<bool, 0> none{};
    PresentHoldCount count = count_present_holds(none);
    require(count.displaced == 0U && count.held == 0U, "no presents hold nothing");
    const std::array<bool, 3> waiting{false, false, false};
    count = count_present_holds(waiting);
    require(count.displaced == 0U && count.held == 3U, "presents not yet displayed are all held");
    const std::array<bool, 4> replaced{true, false, true, false};
    count = count_present_holds(replaced);
    require(count.displaced == 2U && count.held == 2U,
            "the newest displayed present replaces the earlier ones; it and later ones are held");
    const std::array<bool, 2> shown{false, true};
    count = count_present_holds(shown);
    require(count.displaced == 1U && count.held == 1U, "the one on screen stays held");
}

// What a buffer of each age must be written with, proved on pixels: a
// buffer last written k presents ago holds that frame, and the union of the
// damage since (overlapping and clipped) brings it to the current frame
// exactly; unknown contents (age 0, or a route whose mapping may lose them)
// take the whole frame.
void test_damage_union_since_last_written() {
    const PresentExtent extent{32U, 24U};
    PresentDamageHistory history{};
    std::vector<Image> frames{};
    Image frame = make_image(32U, 24U, 0U);
    paint_pattern(frame, 9U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 32, 24)};
    history.record(whole);
    frames.push_back(frame);
    // Damage that overlaps, nests and runs past the extent.
    const std::array<std::array<PresentRect, 2>, 3> damage{{
        {rect(2, 2, 12, 10), rect(20, 4, 40, 8)},
        {rect(4, 4, 8, 8), rect(10, 14, 30, 30)},
        {rect(0, 20, 6, 24), rect(6, 6, 22, 18)},
    }};
    for (std::size_t step = 0U; step < damage.size(); ++step) {
        for (const PresentRect area : damage[step]) {
            const PresentRect clipped = present_rect_intersection(area, present_extent_rect(extent));
            paint_rect(frame, clipped, static_cast<std::byte>(0x30U + step));
        }
        if (step + 1U < damage.size()) history.record(damage[step]);
        frames.push_back(frame);
    }
    // frames[k] is the frame of present k + 1; the current frame (present 4)
    // carries damage[2], not yet recorded.
    const Image& current = frames.back();
    for (std::uint32_t age = 1U; age <= 3U; ++age) {
        Image buffer = frames[frames.size() - 1U - age];
        std::array<PresentRect, 16> out{};
        std::size_t count = 0U;
        require(history.accumulate(age, damage[2], extent, out, count), "a known age has a union");
        require(copy_present_rects(source_of(current), target_of(buffer), std::span<const PresentRect>(out.data(), count)),
                "the union copies");
        require(same_pixels(buffer, current), "the union since the buffer was last written restores the frame");
    }
    std::array<PresentRect, 16> out{};
    std::size_t count = 0U;
    require(!history.accumulate(0U, damage[2], extent, out, count), "unknown contents have no union");
    Image lost = make_image(32U, 24U, 0U);
    require(copy_present_rects(source_of(current), target_of(lost), whole) && same_pixels(lost, current),
            "unknown contents take the whole frame");
}

void test_copy_kernel_validates_first() {
    Image source = make_image(16U, 8U, 4U);
    Image target = make_image(16U, 8U, 12U);
    paint_pattern(source, 5U);
    const std::array<PresentRect, 2> rects{rect(1, 1, 4, 3), rect(10, 5, 16, 8)};
    require(copy_present_rects(source_of(source), target_of(target), rects), "a valid copy succeeds");
    Image expected = make_image(16U, 8U, 12U);
    for (std::size_t index = 0U; index < rects.size(); ++index) {
        for (std::int32_t y = rects[index].top; y < rects[index].bottom; ++y) {
            for (std::int32_t x = rects[index].left * 4; x < rects[index].right * 4; ++x) {
                expected.pixels[static_cast<std::size_t>(y) * expected.row_bytes + static_cast<std::size_t>(x)] =
                    source.pixels[static_cast<std::size_t>(y) * source.row_bytes + static_cast<std::size_t>(x)];
            }
        }
    }
    require(same_pixels(target, expected), "only the rectangles are copied");
    Image untouched = make_image(16U, 8U, 12U);
    const std::array<PresentRect, 2> outside{rect(0, 0, 2, 2), rect(15, 7, 17, 8)};
    require(!copy_present_rects(source_of(source), target_of(untouched), outside), "a rectangle outside fails");
    require(same_pixels(untouched, make_image(16U, 8U, 12U)), "a failed copy writes nothing, not even the first");
    const PresentTargetPixels overlapping{source.pixels.data() + 4, source.row_bytes, PresentExtent{8U, 8U}};
    require(!copy_present_rects(source_of(source), overlapping, rects), "overlapping buffers are refused");
}

void test_move_kernel_matches_a_copy() {
    const std::array<std::array<std::int32_t, 2>, 6> offsets{{{0, 3}, {0, -3}, {5, 0}, {-5, 0}, {4, 2}, {-3, -4}}};
    for (std::size_t index = 0U; index < offsets.size(); ++index) {
        Image moved = make_image(32U, 24U, 4U);
        paint_pattern(moved, 9U);
        const Image original = moved;
        const PresentScroll scroll{rect(2, 2, 30, 22), offsets[index][0], offsets[index][1]};
        const PresentScrollPlan plan = plan_present_scroll(scroll, moved.extent);
        require(move_present_pixels(target_of(moved), plan), "a valid move succeeds");
        // The reference: each destination pixel read from the original.
        Image expected = original;
        for (std::int32_t y = plan.destination.top; y < plan.destination.bottom; ++y) {
            for (std::int32_t x = plan.destination.left; x < plan.destination.right; ++x) {
                const std::size_t to = static_cast<std::size_t>(y) * expected.row_bytes + static_cast<std::size_t>(x) * 4U;
                const std::size_t from = static_cast<std::size_t>(y - scroll.dy) * original.row_bytes +
                                         static_cast<std::size_t>(x - scroll.dx) * 4U;
                for (std::size_t channel = 0U; channel < 4U; ++channel)
                    expected.pixels[to + channel] = original.pixels[from + channel];
            }
        }
        require(same_pixels(moved, expected), "the in-place move equals a copy from the original");
    }
    Image still = make_image(8U, 8U, 0U);
    const PresentScrollPlan nothing = plan_present_scroll(PresentScroll{rect(0, 0, 8, 8), 0, 8}, still.extent);
    require(!move_present_pixels(target_of(still), nothing), "a plan that moves nothing is refused");
}

// ---- the normative trace ----

PresentTraceRecord entry(std::uint64_t frame, PresentTraceEvent event, PresentPath path, std::uint8_t detail = 0U) {
    PresentTraceRecord record{};
    record.frame = frame;
    record.event = event;
    record.path = path;
    record.detail = detail;
    return record;
}

void test_trace_order_checks() {
    using E = PresentTraceEvent;
    const std::uint8_t presented = static_cast<std::uint8_t>(PresentOutcome::presented);
    const std::uint8_t lost = static_cast<std::uint8_t>(PresentOutcome::lost);
    const std::vector<PresentTraceRecord> good{
        entry(1U, E::acquire, PresentPath::direct), entry(1U, E::scroll, PresentPath::direct),
        entry(1U, E::damage, PresentPath::direct), entry(1U, E::present, PresentPath::direct, presented),
        entry(2U, E::acquire, PresentPath::direct), entry(2U, E::damage, PresentPath::direct),
        entry(2U, E::present, PresentPath::direct, lost), entry(2U, E::lost, PresentPath::direct),
        entry(2U, E::release, PresentPath::direct), entry(2U, E::fallback, PresentPath::none),
        entry(2U, E::damage, PresentPath::copy), entry(2U, E::present, PresentPath::copy, presented),
        entry(3U, E::scroll_refused, PresentPath::copy), entry(3U, E::damage, PresentPath::copy),
        entry(3U, E::present, PresentPath::copy, presented), entry(3U, E::lost, PresentPath::direct),
        entry(4U, E::fallback, PresentPath::none), entry(4U, E::damage, PresentPath::copy),
        entry(4U, E::present, PresentPath::copy, presented)};
    require(check_present_trace(good).passed(), "a well-formed trace passes");
    const std::vector<PresentTraceRecord> present_first{entry(1U, E::present, PresentPath::copy, presented)};
    require(check_present_trace(present_first).violation == PresentTraceViolation::out_of_order,
            "a present before its damage is out of order");
    const std::vector<PresentTraceRecord> uncaused{entry(1U, E::fallback, PresentPath::none),
                                                  entry(1U, E::damage, PresentPath::copy)};
    require(check_present_trace(uncaused).violation == PresentTraceViolation::fallback_without_cause,
            "a fallback needs a cause");
    const std::vector<PresentTraceRecord> backwards{entry(2U, E::damage, PresentPath::copy),
                                                   entry(2U, E::present, PresentPath::copy, presented),
                                                   entry(1U, E::damage, PresentPath::copy)};
    require(check_present_trace(backwards).violation == PresentTraceViolation::frame_decreased,
            "frames never decrease");
    const std::vector<PresentTraceRecord> unfinished{entry(1U, E::acquire, PresentPath::direct),
                                                    entry(2U, E::damage, PresentPath::copy)};
    const PresentTraceCheck incomplete = check_present_trace(unfinished);
    require(incomplete.violation == PresentTraceViolation::incomplete_frame && incomplete.index == 1U,
            "a frame ending inside a direct present is incomplete");
    const std::vector<PresentTraceRecord> wrong{entry(1U, E::acquire, PresentPath::copy)};
    require(check_present_trace(wrong).violation == PresentTraceViolation::wrong_path, "acquire runs on direct");
    const std::vector<PresentTraceRecord> lost_without_fallback{
        entry(1U, E::acquire, PresentPath::direct), entry(1U, E::damage, PresentPath::direct),
        entry(1U, E::present, PresentPath::direct, lost)};
    require(check_present_trace(lost_without_fallback).violation == PresentTraceViolation::incomplete_frame,
            "a lost present must fall back");
    std::vector<PresentTraceRecord> other_ages = good;
    other_ages[0].detail = 2U;
    other_ages[2].rect_count = 7U;
    require(present_traces_equivalent(good, other_ages), "ages and counts do not change equivalence");
    other_ages[3].detail = static_cast<std::uint8_t>(PresentOutcome::pending);
    require(!present_traces_equivalent(good, other_ages), "an outcome does");
}

void test_trace_ring() {
    PresentTrace trace{};
    trace.record(entry(1U, PresentTraceEvent::damage, PresentPath::copy));
    require(trace.size() == 0U, "a disabled trace records nothing");
    trace.set_enabled(true);
    for (std::uint64_t frame = 1U; frame <= PresentTrace::capacity + 10U; ++frame)
        trace.record(entry(frame, PresentTraceEvent::damage, PresentPath::copy));
    require(trace.size() == PresentTrace::capacity && trace.total() == PresentTrace::capacity + 10U,
            "the ring keeps its capacity and counts every record");
    const std::vector<PresentTraceRecord> records = records_of(trace);
    require(records.front().frame == 11U && records.back().frame == PresentTrace::capacity + 10U,
            "the oldest records are dropped first");
}

// ---- the shim ----

void test_copy_only_is_todays_path() {
    FakeCopyPath copy(40U, 30U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    PresentationShim shim(&copy, nullptr, &trace);
    Image frame = make_image(40U, 30U, 4U);
    paint_pattern(frame, 1U);
    const std::array<PresentRect, 2> damage{rect(0, 0, 40, 30), rect(50, 50, 60, 60)};
    const PresentReport first = shim.present(frame_of(frame, damage, {}));
    require(first.outcome == PresentOutcome::presented && first.path == PresentPath::copy, "copy presents");
    require(copy.last_rects.size() == 1U && copy.last_rects[0] == rect(0, 0, 40, 30),
            "the copy receives the clipped damage only");
    require(same_pixels(copy.window, frame), "the window shows the frame");
    const PresentReport idle = shim.present(frame_of(frame, std::span<const PresentRect>{}, {}));
    require(idle.outcome == PresentOutcome::presented && copy.copy_calls == 1U, "no damage copies nothing");
    const std::vector<PresentTraceEvent> events = events_of(records_of(trace));
    const std::vector<PresentTraceEvent> expected{PresentTraceEvent::damage, PresentTraceEvent::present};
    require(events == expected, "copy only traces damage then present");
    require_trace_valid(trace, "copy only");
    require(!shim.direct_available() && shim.snapshot().next_path == PresentPath::copy, "no direct path");
}

// Runs `frames` frames of changing pixels and damage through a shim over a
// direct path with `buffers` buffers, and checks after each that the buffer
// shown equals the frame everywhere: the age accumulation writes exactly
// enough.
void run_direct_frames(std::size_t buffers) {
    FakeCopyPath copy(48U, 32U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(48U, 32U, buffers);
    FakeDirectPath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(48U, 32U, 0U);
    paint_pattern(frame, 2U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 48, 32)};
    const PresentReport first = shim.present(frame_of(frame, whole, {}));
    require(first.path == PresentPath::direct && first.outcome == PresentOutcome::presented, "direct presents");
    for (std::uint32_t step = 0U; step < 9U; ++step) {
        const std::int32_t left = static_cast<std::int32_t>((step * 5U) % 40U);
        const std::int32_t top = static_cast<std::int32_t>((step * 3U) % 24U);
        const PresentRect changed = rect(left, top, left + 8, top + 8);
        paint_rect(frame, changed, static_cast<std::byte>(0x10U + step));
        const std::array<PresentRect, 1> damage{changed};
        const PresentReport report = shim.present(frame_of(frame, damage, {}));
        require(report.path == PresentPath::direct, "the direct path keeps presenting");
        require(same_pixels(direct.buffers[direct.shown], frame),
                "the buffer shown equals the frame with " + std::to_string(buffers) + " buffers, step " +
                    std::to_string(step));
        require(direct.last_damage.size() == 1U && direct.last_damage[0] == changed,
                "the window system is told only what changed on screen");
        if (step > 2U) require(report.pixels < 48U * 32U, "a buffer of known age is not rewritten whole");
    }
    require(copy.copy_calls == 0U, "the copy path is not used");
    require_trace_valid(trace, "direct frames");
}

void test_direct_buffers_by_age() {
    run_direct_frames(1U);
    run_direct_frames(2U);
    run_direct_frames(3U);
}

void test_unsupported_refusal_falls_back_for_good() {
    FakeCopyPath copy(20U, 20U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(20U, 20U, 2U);
    FakeDirectPath& direct = *owned;
    direct.refusals.push_back(DirectRefusal::unsupported);
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(20U, 20U, 0U);
    paint_pattern(frame, 3U);
    const std::array<PresentRect, 1> damage{rect(0, 0, 20, 20)};
    const PresentReport first = shim.present(frame_of(frame, damage, {}));
    require(first.path == PresentPath::copy && first.outcome == PresentOutcome::presented,
            "a refused frame is presented by the copy path");
    require(same_pixels(copy.window, frame), "with the same pixels");
    for (std::uint32_t index = 0U; index < PresentationShim::retry_frames + 5U; ++index)
        static_cast<void>(shim.present(frame_of(frame, damage, {})));
    require(direct.acquire_calls == 1U, "unsupported is never asked again");
    require(shim.snapshot().direct_disabled && !shim.direct_available(), "the direct path is off for good");
    const std::vector<PresentTraceRecord> records = records_of(trace);
    const std::vector<PresentTraceEvent> head{PresentTraceEvent::refuse, PresentTraceEvent::fallback,
                                              PresentTraceEvent::damage, PresentTraceEvent::present};
    const std::vector<PresentTraceEvent> events = events_of(records);
    require(std::vector<PresentTraceEvent>(events.begin(), events.begin() + 4) == head,
            "refuse, fallback, then the copy");
    require(records[0].detail == static_cast<std::uint8_t>(DirectRefusal::unsupported), "the refusal is traced");
    require_trace_valid(trace, "unsupported refusal");
}

void test_busy_refusal_pauses_then_resumes() {
    FakeCopyPath copy(20U, 20U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(20U, 20U, 2U);
    FakeDirectPath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(20U, 20U, 0U);
    paint_pattern(frame, 4U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 20, 20)};
    require(shim.present(frame_of(frame, whole, {})).path == PresentPath::direct, "direct first");
    direct.refusals.push_back(DirectRefusal::busy);
    const std::array<PresentRect, 1> small{rect(2, 2, 4, 4)};
    paint_rect(frame, small[0], std::byte{0x77});
    const PresentReport refused = shim.present(frame_of(frame, small, {}));
    require(refused.path == PresentPath::copy, "busy falls back");
    require(copy.last_rects.size() == 1U && copy.last_rects[0] == whole[0],
            "after the direct path showed, the copy writes the whole frame");
    require(direct.release_calls == 1U, "the direct buffers are released so the copy shows");
    require(same_pixels(copy.window, frame), "the copy shows the same pixels");
    const std::uint32_t asked = direct.acquire_calls;
    for (std::uint32_t index = 0U; index + 1U < PresentationShim::retry_frames; ++index) {
        const PresentReport paused = shim.present(frame_of(frame, small, {}));
        require(paused.path == PresentPath::copy, "paused frames copy");
    }
    require(direct.acquire_calls == asked, "the direct path rests while paused");
    const PresentReport resumed = shim.present(frame_of(frame, small, {}));
    require(resumed.path == PresentPath::direct, "the direct path is tried again after the pause");
    require(same_pixels(direct.buffers[direct.shown], frame), "returning writes the whole frame");
    bool saw_resume = false;
    const std::vector<PresentTraceRecord> records = records_of(trace);
    for (std::size_t index = 0U; index < records.size(); ++index)
        if (records[index].event == PresentTraceEvent::resume) saw_resume = true;
    require(saw_resume, "the resume is traced");
    require_trace_valid(trace, "busy refusal");
}

void test_extent_refusal_waits_for_a_new_size() {
    FakeCopyPath copy(30U, 30U, false);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(20U, 20U, 2U);
    FakeDirectPath& direct = *owned;
    PresentTrace trace{};
    trace.set_enabled(true);
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image large = make_image(30U, 30U, 0U);
    paint_pattern(large, 6U);
    const std::array<PresentRect, 1> damage{rect(0, 0, 30, 30)};
    require(shim.present(frame_of(large, damage, {})).path == PresentPath::copy, "a refused size copies");
    static_cast<void>(shim.present(frame_of(large, damage, {})));
    require(direct.acquire_calls == 1U, "the same size is not asked again");
    Image fitting = make_image(20U, 20U, 0U);
    paint_pattern(fitting, 7U);
    const std::array<PresentRect, 1> resized{rect(0, 0, 20, 20)};
    require(shim.present(frame_of(fitting, resized, {})).path == PresentPath::direct, "a new size is tried again");
    require_trace_valid(trace, "extent refusal");
}

void test_loss_falls_back_and_disables_after_repeats() {
    FakeCopyPath copy(16U, 16U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(16U, 16U, 2U);
    FakeDirectPath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(16U, 16U, 0U);
    paint_pattern(frame, 8U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 16, 16)};
    const std::array<PresentRect, 1> small{rect(1, 1, 3, 3)};
    require(shim.present(frame_of(frame, whole, {})).path == PresentPath::direct, "direct first");
    for (std::uint32_t loss = 0U; loss < PresentationShim::maximum_consecutive_losses; ++loss) {
        direct.outcomes.push_back(PresentOutcome::lost);
        const PresentReport report = shim.present(frame_of(frame, small, {}));
        require(report.path == PresentPath::copy && report.outcome == PresentOutcome::presented,
                "a lost present is shown by the copy path");
        require(copy.last_rects.size() == 1U && copy.last_rects[0] == whole[0], "the whole frame is copied");
        // The pause, short of its last frame: the next loss's frame is the
        // one that resumes, so no successful present breaks the row.
        if (loss + 1U < PresentationShim::maximum_consecutive_losses) {
            for (std::uint32_t index = 0U; index + 1U < PresentationShim::retry_frames; ++index)
                static_cast<void>(shim.present(frame_of(frame, small, {})));
        }
    }
    require(shim.snapshot().direct_disabled, "repeated losses turn the direct path off");
    require(shim.snapshot().losses == PresentationShim::maximum_consecutive_losses, "each loss is counted");
    require_trace_valid(trace, "losses");
}

void test_loss_between_frames() {
    FakeCopyPath copy(16U, 16U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(16U, 16U, 2U);
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(16U, 16U, 0U);
    paint_pattern(frame, 10U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 16, 16)};
    const std::array<PresentRect, 1> small{rect(4, 4, 6, 6)};
    require(shim.present(frame_of(frame, whole, {})).path == PresentPath::direct, "direct first");
    shim.notify_lost();
    const PresentReport report = shim.present(frame_of(frame, small, {}));
    require(report.path == PresentPath::copy && copy.last_rects[0] == whole[0],
            "after a loss between frames the copy writes the whole frame");
    const std::vector<PresentTraceRecord> records = records_of(trace);
    bool fallback_after_loss = false;
    for (std::size_t index = 1U; index < records.size(); ++index) {
        if (records[index].event == PresentTraceEvent::fallback &&
            records[index].detail == static_cast<std::uint8_t>(FallbackReason::lost))
            fallback_after_loss = true;
    }
    require(fallback_after_loss, "the fallback names the loss");
    require_trace_valid(trace, "loss between frames");
}

void test_copy_path_host_scroll() {
    FakeCopyPath copy(40U, 40U, true);
    PresentTrace trace{};
    trace.set_enabled(true);
    PresentationShim shim(&copy, nullptr, &trace);
    Image frame = make_image(40U, 40U, 0U);
    paint_pattern(frame, 11U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 40, 40)};
    const PresentScroll scroll{rect(0, 10, 40, 40), 0, -6};
    const std::array<PresentScroll, 1> scrolls{scroll};
    // The first frame cannot move pixels: nothing was presented before.
    static_cast<void>(shim.present(frame_of(frame, whole, scrolls)));
    require(copy.scroll_calls == 0U, "no move before a presented frame");
    // The frame scrolls: its raster moves (as the engine's scroll copy
    // does) and the exposed band is repainted.
    const PresentScrollPlan plan = plan_present_scroll(scroll, frame.extent);
    require(move_present_pixels(target_of(frame), plan), "the frame's own move");
    paint_rect(frame, plan.exposed[0], std::byte{0x42});
    const std::array<PresentRect, 1> exposed{plan.exposed[0]};
    const PresentReport report = shim.present(frame_of(frame, exposed, scrolls));
    require(copy.scroll_calls == 1U, "the window system moves the presented pixels");
    require(report.pixels == plan.exposed[0].area(), "only the exposed band is copied");
    require(same_pixels(copy.window, frame), "the window equals the frame after the move and the band");
    shim.notify_copy_contents_lost();
    static_cast<void>(shim.present(frame_of(frame, exposed, scrolls)));
    require(copy.scroll_calls == 1U, "after an expose the scroll's area is copied instead");
    require_trace_valid(trace, "copy host scroll");
    require(shim.snapshot().scrolls_moved == 1U && shim.snapshot().scrolls_refused == 2U, "scroll counters");
}

void test_direct_host_scroll_in_place() {
    FakeCopyPath copy(40U, 40U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(40U, 40U, 1U);
    FakeDirectPath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(40U, 40U, 0U);
    paint_pattern(frame, 12U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 40, 40)};
    static_cast<void>(shim.present(frame_of(frame, whole, {})));
    const PresentScroll scroll{rect(5, 5, 35, 35), 3, 4};
    const std::array<PresentScroll, 1> scrolls{scroll};
    const PresentScrollPlan plan = plan_present_scroll(scroll, frame.extent);
    require(move_present_pixels(target_of(frame), plan), "the frame's own move");
    for (std::size_t band = 0U; band < plan.exposed_count; ++band) paint_rect(frame, plan.exposed[band], std::byte{9});
    const std::array<PresentRect, 2> exposed{plan.exposed[0], plan.exposed[1]};
    const PresentReport report = shim.present(frame_of(frame, exposed, scrolls));
    require(report.path == PresentPath::direct && shim.snapshot().scrolls_moved == 1U,
            "a buffer of age 1 moves in place");
    require(report.pixels == plan.exposed[0].area() + plan.exposed[1].area(), "only the bands are written");
    require(same_pixels(direct.buffers[direct.shown], frame), "the buffer equals the frame");
    require(direct.last_damage.size() == 1U && direct.last_damage[0] == plan.area,
            "the window system is told the whole area changed");
    require_trace_valid(trace, "direct host scroll");
}

void test_failed_copy_keeps_scroll_off() {
    FakeCopyPath copy(20U, 20U, true);
    PresentationShim shim(&copy, nullptr, nullptr);
    Image frame = make_image(20U, 20U, 0U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 20, 20)};
    static_cast<void>(shim.present(frame_of(frame, whole, {})));
    copy.outcomes.push_back(PresentOutcome::failed);
    const std::array<PresentRect, 1> small{rect(0, 0, 2, 2)};
    require(shim.present(frame_of(frame, small, {})).outcome == PresentOutcome::failed, "a failed copy reports it");
    const std::array<PresentScroll, 1> scrolls{PresentScroll{rect(0, 0, 20, 20), 0, 2}};
    static_cast<void>(shim.present(frame_of(frame, small, scrolls)));
    require(copy.scroll_calls == 0U, "no move after a failed copy: the window is not the frame before");
}

void test_producer_mode() {
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(24U, 24U, 1U);
    FakeDirectPath& direct = *owned;
    PresentationShim shim(nullptr, std::move(owned), &trace);
    const DirectAcquire first = shim.acquire_for_writing(PresentExtent{24U, 24U});
    require(first.buffer() != nullptr && (*first.buffer()).age == 0U, "a new buffer's contents are unknown");
    const std::array<PresentRect, 1> whole{rect(0, 0, 24, 24)};
    require(shim.present_written(whole) == PresentOutcome::presented, "producer presents");
    const DirectAcquire second = shim.acquire_for_writing(PresentExtent{24U, 24U});
    require(second.buffer() != nullptr && (*second.buffer()).age == 1U, "the same buffer is one present old");
    const PresentScrollResult moved = shim.scroll_written(PresentScroll{rect(0, 0, 24, 24), 0, 5});
    require(moved.moved && moved.plan.exposed_count == 1U, "a scroll moves the held buffer");
    shim.decline_scroll(PresentScroll{rect(0, 0, 12, 12), 0, 2});
    require(shim.snapshot().scrolls_refused == 1U, "a declined scroll is counted and traced");
    direct.outcomes.push_back(PresentOutcome::pending);
    require(shim.present_written(whole) == PresentOutcome::pending, "a pending present");
    const DirectAcquire blocked = shim.acquire_for_writing(PresentExtent{24U, 24U});
    require(blocked.refusal() == std::optional<DirectRefusal>(DirectRefusal::busy), "pending refuses as busy");
    shim.notify_released();
    require(shim.acquire_for_writing(PresentExtent{24U, 24U}).buffer() != nullptr, "released, it is granted");
    require(shim.present_written(whole) == PresentOutcome::presented, "presented again");
    direct.refusals.push_back(DirectRefusal::resources);
    require(shim.acquire_for_writing(PresentExtent{24U, 24U}).refusal().has_value(), "a refusal reaches the caller");
    require(shim.acquire_for_writing(PresentExtent{24U, 24U}).buffer() != nullptr,
            "producer mode does not pause: the caller retries");
    require(shim.present_written(whole) == PresentOutcome::presented, "and presents");
    require_trace_valid(trace, "producer mode");
}

// A direct path that never waits for a buffer (the Metal route's rule):
// before the call that could block, it counts the buffers the window system
// still holds, and refuses busy when the pool is all held.
class NonWaitingPath final : public PresentationDirectPath {
public:
    static constexpr std::size_t pool = 3U;

    NonWaitingPath() : buffer(make_image(16U, 16U, 0U)) {}

    DirectAcquire acquire(PresentExtent extent) override {
        const PresentHoldCount count = count_present_holds(displayed);
        displayed.erase(displayed.begin(), displayed.begin() + static_cast<std::ptrdiff_t>(count.displaced));
        if (count.held >= pool) return DirectAcquire::refused(DirectRefusal::busy);
        // Stands for nextDrawable, which may wait while the pool is held.
        ++blocking_calls;
        DirectBuffer granted{};
        granted.pixels = buffer.pixels.data();
        granted.row_bytes = buffer.row_bytes;
        granted.extent = extent;
        granted.age = 0U;
        return DirectAcquire::granted(granted);
    }
    PresentOutcome present(std::span<const PresentRect> damage) override {
        static_cast<void>(damage);
        displayed.push_back(false);
        return PresentOutcome::presented;
    }
    void release() noexcept override { ++release_calls; }

    Image buffer;
    std::vector<bool> displayed{};
    std::uint32_t blocking_calls{};
    std::uint32_t release_calls{};
};

void test_no_buffer_ready_falls_back_without_waiting() {
    FakeCopyPath copy(16U, 16U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<NonWaitingPath> owned = std::make_unique<NonWaitingPath>();
    NonWaitingPath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(16U, 16U, 0U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 16, 16)};
    // The window system displays nothing: every present stays held.
    for (std::size_t index = 0U; index < NonWaitingPath::pool; ++index) {
        paint_pattern(frame, static_cast<std::uint32_t>(index));
        require(shim.present(frame_of(frame, whole, {})).path == PresentPath::direct, "the pool fills");
    }
    require(direct.blocking_calls == NonWaitingPath::pool, "one obtain per buffer");
    paint_pattern(frame, 21U);
    const std::array<PresentRect, 1> small{rect(3, 3, 9, 9)};
    const PresentReport report = shim.present(frame_of(frame, small, {}));
    require(direct.blocking_calls == NonWaitingPath::pool, "no buffer ready: the blocking obtain is never called");
    require(report.path == PresentPath::copy && report.outcome == PresentOutcome::presented,
            "the frame is presented by the copy path at once");
    require(same_pixels(copy.window, frame), "with identical pixels (the whole frame after a direct present)");
    require(direct.release_calls == 1U, "the direct buffers are released so the copy shows");
    require_trace_valid(trace, "no buffer ready");
}

// A server error the X11 paths absorb (A-gf-present-01-F1) reaches the shim
// as a refusal (a pixmap refused while acquiring: resources) or a lost
// present (a present rejected): either way the frame is presented by the
// copy path in the same frame, with identical pixels.
void test_refused_pixmap_or_present_falls_back() {
    FakeCopyPath copy(16U, 16U, false);
    PresentTrace trace{};
    trace.set_enabled(true);
    std::unique_ptr<FakeDirectPath> owned = std::make_unique<FakeDirectPath>(16U, 16U, 2U);
    FakeDirectPath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), &trace);
    Image frame = make_image(16U, 16U, 0U);
    paint_pattern(frame, 31U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 16, 16)};
    // The pixmap is refused at the first acquire: nothing direct was shown.
    direct.refusals.push_back(DirectRefusal::resources);
    const PresentReport refused = shim.present(frame_of(frame, whole, {}));
    require(refused.path == PresentPath::copy && refused.outcome == PresentOutcome::presented,
            "a refused pixmap presents on the copy path");
    require(same_pixels(copy.window, frame), "with identical pixels");
    // After the pause the direct path presents, then a present is rejected.
    for (std::uint32_t index = 0U; index + 1U < PresentationShim::retry_frames; ++index)
        static_cast<void>(shim.present(frame_of(frame, whole, {})));
    require(shim.present(frame_of(frame, whole, {})).path == PresentPath::direct, "direct again after the pause");
    const std::array<PresentRect, 1> small{rect(5, 5, 9, 9)};
    paint_rect(frame, small[0], std::byte{0x5a});
    direct.outcomes.push_back(PresentOutcome::lost);
    const PresentReport rejected = shim.present(frame_of(frame, small, {}));
    require(rejected.path == PresentPath::copy && rejected.outcome == PresentOutcome::presented,
            "a rejected present presents on the copy path in the same frame");
    require(copy.last_rects.size() == 1U && copy.last_rects[0] == whole[0],
            "the copy writes the whole frame after the direct path showed");
    require(same_pixels(copy.window, frame), "with identical pixels");
    require_trace_valid(trace, "refused pixmap or present");
}

// A direct path that grants one buffer of any extent and always claims age
// 1, as a path that recycles a buffer across a resize could
// (A-gf-present-01-F3): the shim must not trust that age at a new size.
class ResizeAgePath final : public PresentationDirectPath {
public:
    DirectAcquire acquire(PresentExtent extent) override {
        if (buffer.extent != extent) {
            buffer = make_image(extent.width, extent.height, 4U);
            // Contents never written by the shim.
            paint_rect(buffer, rect(0, 0, static_cast<std::int32_t>(extent.width),
                                    static_cast<std::int32_t>(extent.height)), std::byte{0xee});
        }
        DirectBuffer granted{};
        granted.pixels = buffer.pixels.data();
        granted.row_bytes = buffer.row_bytes;
        granted.extent = extent;
        granted.age = presents == 0U ? 0U : 1U;
        return DirectAcquire::granted(granted);
    }
    PresentOutcome present(std::span<const PresentRect> damage) override {
        static_cast<void>(damage);
        ++presents;
        return PresentOutcome::presented;
    }
    void release() noexcept override {}

    Image buffer{};
    std::uint32_t presents{};
};

void test_new_extent_forgets_ages() {
    FakeCopyPath copy(24U, 24U, false);
    std::unique_ptr<ResizeAgePath> owned = std::make_unique<ResizeAgePath>();
    ResizeAgePath& direct = *owned;
    PresentationShim shim(&copy, std::move(owned), nullptr);
    Image small = make_image(16U, 16U, 0U);
    paint_pattern(small, 2U);
    const std::array<PresentRect, 1> small_whole{rect(0, 0, 16, 16)};
    require(shim.present(frame_of(small, small_whole, {})).path == PresentPath::direct, "direct at the first size");
    Image large = make_image(24U, 24U, 0U);
    paint_pattern(large, 3U);
    const std::array<PresentRect, 1> corner{rect(0, 0, 4, 4)};
    require(shim.present(frame_of(large, corner, {})).path == PresentPath::direct, "direct at the new size");
    require(same_pixels(direct.buffer, large), "a new extent writes the whole frame whatever age is claimed");
}

void test_policy_from_environment() {
    // Only the default is checked: the variable is the process's, and a
    // test does not set it.
    if (std::getenv("GUI_FORMS_PRESENT") == nullptr) {
        const PresentPolicy policy = present_policy_from_environment();
        require(!policy.direct && !policy.host_scroll, "copy only by default");
        require(!present_policy_names("metal"), "no route named by default");
    }
}

void test_policy_list_words() {
    require(present_list_names("direct,metal", "metal"), "a later word");
    require(present_list_names("direct", "direct"), "a single word");
    require(!present_list_names("direct,metals", "metal"), "whole words only");
    require(!present_list_names("", "direct"), "an empty list names nothing");
    require(present_list_names(",scroll,", "scroll"), "empty words are skipped");
    require(!present_list_names("dri3", ""), "the empty word is not named by a word");
}

} // namespace

int main() {
    try {
        test_scroll_plan_partitions_the_area();
        test_scroll_plan_edges();
        test_damage_clipping_and_overflow();
        test_damage_history_ages();
        test_hold_count();
        test_damage_union_since_last_written();
        test_copy_kernel_validates_first();
        test_move_kernel_matches_a_copy();
        test_trace_order_checks();
        test_trace_ring();
        test_copy_only_is_todays_path();
        test_direct_buffers_by_age();
        test_unsupported_refusal_falls_back_for_good();
        test_busy_refusal_pauses_then_resumes();
        test_extent_refusal_waits_for_a_new_size();
        test_loss_falls_back_and_disables_after_repeats();
        test_loss_between_frames();
        test_copy_path_host_scroll();
        test_direct_host_scroll_in_place();
        test_failed_copy_keeps_scroll_off();
        test_producer_mode();
        test_policy_from_environment();
        test_policy_list_words();
        test_no_buffer_ready_falls_back_without_waiting();
        test_refused_pixmap_or_present_falls_back();
        test_new_extent_forgets_ages();
    } catch (const std::exception& error) {
        std::cerr << "presentation shim tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "presentation shim tests passed\n";
    return EXIT_SUCCESS;
}
