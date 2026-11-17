// Host scroll's damage arithmetic (src/core/host/presentation/host_scroll.hpp,
// docs/user/presentation.md "Host scroll"), as the Windows copy path uses it
// with ScrollWindowEx: an engine scroll copy in device pixels, the offset a
// plan moves by, the judgement of a window system's uncovered region, and
// the shim over a fake window system that moves pixels as ScrollWindowEx
// does, with part of the window covered or not. Only the exposed strip is
// copied after a whole move; the whole area is copied after a partial one.
// Plain C++, tested on every platform; no window system.

#include "../src/core/host/presentation/host_scroll.hpp"
#include "../src/core/host/presentation/presentation_geometry.hpp"
#include "../src/core/host/presentation/presentation_shim.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms::host::presentation;
using gui_forms::Point;
using gui_forms::Rect;
using gui_forms::ScrollCopy;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

PresentRect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
    const PresentRect result{left, top, right, bottom};
    return result;
}

// A BGRA8 image owned by the test, rows packed.
struct Image final {
    PresentExtent extent{};
    std::size_t row_bytes{};
    std::vector<std::byte> pixels{};
};

Image make_image(std::uint32_t width, std::uint32_t height) {
    Image image{};
    image.extent = PresentExtent{width, height};
    image.row_bytes = static_cast<std::size_t>(width) * 4U;
    image.pixels.assign(image.row_bytes * height, std::byte{0});
    return image;
}

// Every pixel made of its position and `seed`, so a pixel in the wrong
// place is seen.
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
    const bool same = a.extent == b.extent && a.pixels == b.pixels;
    return same;
}

PresentSourcePixels source_of(const Image& image) {
    const PresentSourcePixels source{image.pixels.data(), image.row_bytes, image.extent};
    return source;
}

PresentTargetPixels target_of(Image& image) {
    const PresentTargetPixels target{image.pixels.data(), image.row_bytes, image.extent};
    return target;
}

PresentFrame frame_of(const Image& image, std::span<const PresentRect> damage, std::span<const PresentScroll> scrolls) {
    PresentFrame frame{};
    frame.source = source_of(image);
    frame.damage = damage;
    frame.scrolls = scrolls;
    return frame;
}

void test_device_scroll_whole_pixels() {
    const ScrollCopy copy{Rect{10.0, 10.0, 100.0, 80.0}, Point{0.0, -6.0}};
    PresentScroll scroll{};
    require(device_present_scroll(copy, 1.0, scroll), "whole logical pixels at scale 1");
    require(scroll.area == rect(10, 10, 110, 90) && scroll.dx == 0 && scroll.dy == -6, "scale 1 values");
    require(device_present_scroll(copy, 2.0, scroll), "whole device pixels at scale 2");
    require(scroll.area == rect(20, 20, 220, 180) && scroll.dy == -12, "scale 2 values");
    // 6 * 1.25 = 7.5: half a device pixel, refused and left unchanged.
    const PresentScroll before = scroll;
    require(!device_present_scroll(copy, 1.25, scroll), "a fractional device offset is refused");
    require(scroll.area == before.area && scroll.dy == before.dy, "a refusal leaves the scroll as it was");
    // A clip reaching left of the window is clamped at 0.
    const ScrollCopy left_of_window{Rect{-4.0, 0.0, 20.0, 10.0}, Point{3.0, 0.0}};
    require(device_present_scroll(left_of_window, 1.0, scroll), "a clip left of the window converts");
    require(scroll.area == rect(0, 0, 16, 10) && scroll.dx == 3, "the clip is clamped at the window's edge");
    const double infinite = std::numeric_limits<double>::infinity();
    const ScrollCopy unbounded{Rect{0.0, 0.0, 10.0, 10.0}, Point{0.0, infinite}};
    require(!device_present_scroll(unbounded, 1.0, scroll), "a non-finite offset is refused");
    const ScrollCopy huge{Rect{0.0, 0.0, 10.0, 10.0}, Point{0.0, 40000.0}};
    require(!device_present_scroll(huge, 1.0, scroll), "an offset beyond the largest buffer side is refused");
}

void test_offset_from_plan() {
    const PresentExtent extent{100U, 100U};
    const PresentScrollPlan down = plan_present_scroll(PresentScroll{rect(0, 10, 100, 90), 0, 7}, extent);
    const PresentOffset moved = present_scroll_offset(down);
    require(moved.dx == 0 && moved.dy == 7, "the plan's offset is the scroll's");
    const PresentScrollPlan both = plan_present_scroll(PresentScroll{rect(10, 10, 60, 60), -5, 3}, extent);
    const PresentOffset diagonal = present_scroll_offset(both);
    require(diagonal.dx == -5 && diagonal.dy == 3, "a diagonal plan's offset");
    const PresentScrollPlan nothing = plan_present_scroll(PresentScroll{rect(0, 0, 10, 10), 0, 10}, extent);
    const PresentOffset none = present_scroll_offset(nothing);
    require(!nothing.moves() && none.dx == 0 && none.dy == 0, "a plan that moves nothing has no offset");
}

void test_uncovered_region_judgement() {
    const PresentExtent extent{100U, 100U};
    const PresentScrollPlan plan = plan_present_scroll(PresentScroll{rect(10, 10, 90, 90), 4, -6}, extent);
    require(plan.exposed_count == 2U, "a diagonal scroll exposes two bands");
    // Exactly the bands: whole.
    const std::array<PresentRect, 2> bands{plan.exposed[0], plan.exposed[1]};
    require(classify_host_scroll_move(plan, bands) == HostScrollMove::whole, "the bands alone are a whole move");
    // The bands in pieces, with an empty rectangle: still whole.
    const PresentRect rows = plan.exposed[0];
    const std::int32_t middle = (rows.left + rows.right) / 2;
    const std::array<PresentRect, 4> pieces{rect(rows.left, rows.top, middle, rows.bottom),
                                            rect(middle, rows.top, rows.right, rows.bottom), plan.exposed[1],
                                            rect(5, 5, 5, 5)};
    require(classify_host_scroll_move(plan, pieces) == HostScrollMove::whole, "the bands in pieces are whole");
    // Nothing reported: whole (the window system had nothing left to paint).
    require(classify_host_scroll_move(plan, std::span<const PresentRect>{}) == HostScrollMove::whole,
            "an empty region is whole");
    // A covered part of the destination: partial.
    const std::array<PresentRect, 3> covered{plan.exposed[0], plan.exposed[1], rect(40, 40, 50, 50)};
    require(classify_host_scroll_move(plan, covered) == HostScrollMove::partial, "a covered part is partial");
    // A band reaching one pixel past its edge: partial.
    const PresentRect wider{rows.left, rows.top - 1, rows.right, rows.bottom};
    const std::array<PresentRect, 2> past{wider, plan.exposed[1]};
    require(classify_host_scroll_move(plan, past) == HostScrollMove::partial, "a pixel past a band is partial");
    // Outside the area altogether: partial.
    const std::array<PresentRect, 1> outside{rect(0, 0, 5, 5)};
    require(classify_host_scroll_move(plan, outside) == HostScrollMove::partial, "outside the area is partial");
    // A plan that moves nothing cannot be a whole move.
    const PresentScrollPlan nothing = plan_present_scroll(PresentScroll{rect(0, 0, 10, 10), 0, 10}, extent);
    require(classify_host_scroll_move(nothing, std::span<const PresentRect>{}) == HostScrollMove::partial,
            "a plan that moves nothing is partial");
}

// A window system that moves its own pixels as ScrollWindowEx does: inside
// `covered` the window's pixels are not its own (another window lies over
// them), so they neither move nor are moved into, and the region it reports
// left to paint is the exposed bands plus the covered part of the
// destination. The path judges that region as the Windows copy path does.
class FakeScrollingWindow final : public PresentationCopyPath {
public:
    FakeScrollingWindow(std::uint32_t width, std::uint32_t height) : window(make_image(width, height)) {}

    [[nodiscard]] bool scrolls() const noexcept override { return true; }
    bool scroll(const PresentScrollPlan& plan) override {
        ++scroll_calls;
        const Image before = window;
        if (!move_present_pixels(target_of(window), plan)) return false;
        std::vector<PresentRect> uncovered{};
        for (std::size_t band = 0U; band < plan.exposed_count; ++band) uncovered.push_back(plan.exposed[band]);
        const PresentRect hidden = present_rect_intersection(plan.destination, covered);
        if (!hidden.empty()) {
            // The covered pixels keep what they showed.
            for (std::int32_t y = hidden.top; y < hidden.bottom; ++y) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) * window.row_bytes + static_cast<std::size_t>(hidden.left) * 4U;
                const std::size_t bytes = static_cast<std::size_t>(hidden.right - hidden.left) * 4U;
                for (std::size_t index = 0U; index < bytes; ++index) {
                    window.pixels[offset + index] = before.pixels[offset + index];
                }
            }
            uncovered.push_back(hidden);
        }
        const bool whole = classify_host_scroll_move(plan, uncovered) == HostScrollMove::whole;
        return whole;
    }
    PresentOutcome copy(const PresentSourcePixels& source, std::span<const PresentRect> rects) override {
        last_rects.assign(rects.begin(), rects.end());
        const bool copied = copy_present_rects(source, target_of(window), rects);
        return copied ? PresentOutcome::presented : PresentOutcome::failed;
    }

    Image window;
    PresentRect covered{};
    std::vector<PresentRect> last_rects{};
    std::uint32_t scroll_calls{};
};

// One scroll frame: the frame's raster moves as the engine's scroll copy
// moves it and the exposed bands are repainted; the shim presents the bands
// as the damage with the scroll.
PresentReport present_scroll_frame(PresentationShim& shim, Image& frame, const PresentScroll& scroll,
                                   std::byte band_value) {
    const PresentScrollPlan plan = plan_present_scroll(scroll, frame.extent);
    require(move_present_pixels(target_of(frame), plan), "the frame's own move");
    std::vector<PresentRect> damage{};
    for (std::size_t band = 0U; band < plan.exposed_count; ++band) {
        paint_rect(frame, plan.exposed[band], band_value);
        damage.push_back(plan.exposed[band]);
    }
    const std::array<PresentScroll, 1> scrolls{scroll};
    const PresentReport report = shim.present(frame_of(frame, damage, scrolls));
    return report;
}

void test_whole_move_copies_only_the_strip() {
    FakeScrollingWindow window(64U, 48U);
    PresentationShim shim(&window, nullptr, nullptr);
    Image frame = make_image(64U, 48U);
    paint_pattern(frame, 5U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 64, 48)};
    static_cast<void>(shim.present(frame_of(frame, whole, {})));
    require(same_pixels(window.window, frame), "the first frame is shown");
    // A vertical scroll of a viewport (8..56 x 4..44) by 6 rows up.
    const PresentScroll scroll{rect(8, 4, 56, 44), 0, -6};
    const PresentScrollPlan plan = plan_present_scroll(scroll, frame.extent);
    const PresentReport report = present_scroll_frame(shim, frame, scroll, std::byte{0x31});
    require(report.outcome == PresentOutcome::presented, "the scroll frame is presented");
    require(window.scroll_calls == 1U && shim.snapshot().scrolls_moved == 1U, "the window moved its pixels");
    require(report.pixels == plan.exposed[0].area(), "only the exposed strip is copied");
    require(plan.exposed[0] == rect(8, 38, 56, 44), "the strip is the 48 x 6 band the rows left");
    require(same_pixels(window.window, frame), "the window equals the frame");
    // A diagonal scroll: two bands, still only they are copied.
    const PresentScroll diagonal{rect(8, 4, 56, 44), 3, 2};
    const PresentScrollPlan diagonal_plan = plan_present_scroll(diagonal, frame.extent);
    const PresentReport second = present_scroll_frame(shim, frame, diagonal, std::byte{0x52});
    require(second.pixels == diagonal_plan.exposed[0].area() + diagonal_plan.exposed[1].area(),
            "a diagonal scroll copies its two bands only");
    require(same_pixels(window.window, frame), "the window equals the frame after the diagonal scroll");
}

void test_partial_move_falls_back_to_the_area() {
    FakeScrollingWindow window(64U, 48U);
    PresentationShim shim(&window, nullptr, nullptr);
    Image frame = make_image(64U, 48U);
    paint_pattern(frame, 9U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 64, 48)};
    static_cast<void>(shim.present(frame_of(frame, whole, {})));
    // Another window covers part of the viewport: the move is partial, the
    // scroll is refused, and the whole area is copied from the frame.
    window.covered = rect(20, 10, 40, 30);
    const PresentScroll scroll{rect(8, 4, 56, 44), 0, -6};
    const PresentScrollPlan plan = plan_present_scroll(scroll, frame.extent);
    const PresentReport report = present_scroll_frame(shim, frame, scroll, std::byte{0x77});
    require(window.scroll_calls == 1U, "the window system was asked to move");
    require(shim.snapshot().scrolls_moved == 0U && shim.snapshot().scrolls_refused == 1U, "the scroll is refused");
    require(report.pixels == plan.area.area(), "the whole area is copied");
    require(same_pixels(window.window, frame), "the window equals the frame after the fallback");
    // Uncovered again, the next scroll moves.
    window.covered = PresentRect{};
    const PresentReport next = present_scroll_frame(shim, frame, scroll, std::byte{0x78});
    require(shim.snapshot().scrolls_moved == 1U, "the next scroll moves");
    require(next.pixels == plan.exposed[0].area(), "and copies only its strip");
    require(same_pixels(window.window, frame), "the window equals the frame");
}

void test_contents_lost_refuses_the_move() {
    FakeScrollingWindow window(32U, 32U);
    PresentationShim shim(&window, nullptr, nullptr);
    Image frame = make_image(32U, 32U);
    paint_pattern(frame, 2U);
    const std::array<PresentRect, 1> whole{rect(0, 0, 32, 32)};
    static_cast<void>(shim.present(frame_of(frame, whole, {})));
    // The window's contents were lost (an expose Windows did not keep): the
    // scroll's area is copied, and the window system is not asked to move.
    shim.notify_copy_contents_lost();
    const PresentScroll scroll{rect(0, 0, 32, 32), 0, 4};
    const PresentScrollPlan plan = plan_present_scroll(scroll, frame.extent);
    const PresentReport report = present_scroll_frame(shim, frame, scroll, std::byte{0x10});
    require(window.scroll_calls == 0U, "no move after lost contents");
    require(report.pixels == plan.area.area(), "the area is copied");
    require(same_pixels(window.window, frame), "the window equals the frame");
}

} // namespace

int main() {
    try {
        test_device_scroll_whole_pixels();
        test_offset_from_plan();
        test_uncovered_region_judgement();
        test_whole_move_copies_only_the_strip();
        test_partial_move_falls_back_to_the_area();
        test_contents_lost_refuses_the_move();
    } catch (const std::exception& error) {
        std::cerr << "host scroll damage tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "host scroll damage tests passed\n";
    return EXIT_SUCCESS;
}
