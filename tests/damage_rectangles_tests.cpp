// Stage DP: a paint pass over a set of damage rectangles paints each one
// under its own clip. Two distant changes cost the sum of their areas, not
// their bounding box, and nothing is drawn between them.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "damage rectangles failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

bool near(double left, double right) {
    const double difference = left - right;
    return difference < 1.0e-6 && difference > -1.0e-6;
}

// Tracks the effective clip in window coordinates and records it for every
// draw, so a test can check where pixels could have been touched.
class ClipTrackingPainter final : public Painter {
public:
    struct State final {
        Point offset{};
        Rect clip{};
    };

    explicit ClipTrackingPainter(Rect window) {
        State initial;
        initial.clip = window;
        states_.push_back(initial);
    }

    void save() override { states_.push_back(states_.back()); }
    void restore() override {
        if (states_.size() > 1U) states_.pop_back();
    }
    void translate(Point delta) override {
        states_.back().offset.x += delta.x;
        states_.back().offset.y += delta.y;
    }
    void clip_rect(Rect local) override {
        const Rect window{local.x + states_.back().offset.x, local.y + states_.back().offset.y,
                          local.width, local.height};
        states_.back().clip = Rect::intersection(states_.back().clip, window);
    }
    void clip_rounded_rect(Rect local, double) override { clip_rect(local); }
    void fill_rect(Rect, Color) override { record(); }
    void fill_rounded_rect(Rect, double, Color) override { record(); }
    void stroke_rect(Rect, Color, double) override { record(); }
    void stroke_rounded_rect(Rect, double, Color, double) override { record(); }
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override { record(); }
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override { record(); }
    void draw_box_shadow(Rect, double, Point, double, double, Color) override { record(); }
    void draw_line(Point, Point, Color, double) override { record(); }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { record(); }
    void draw_image(ImageId, Rect, double) override { record(); }

    // The clip of every draw since the last reset; an empty clip draws nothing.
    std::vector<Rect> drawn_clips;

private:
    void record() {
        if (!states_.back().clip.empty()) drawn_clips.push_back(states_.back().clip);
    }

    std::vector<State> states_;
};

bool inside_any(Rect clip, std::span<const Rect> allowed) {
    for (std::size_t index = 0; index < allowed.size(); ++index) {
        if (allowed[index].contains(clip)) return true;
    }
    return false;
}

struct Scene final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Panel> top_left;
    std::shared_ptr<Panel> bottom_right;
};

Scene make_scene() {
    Scene scene;
    scene.root = make_control<Panel>(StableId("dp.root"));
    scene.top_left = make_control<Panel>(StableId("dp.top-left"));
    scene.bottom_right = make_control<Panel>(StableId("dp.bottom-right"));
    (*scene.top_left).set_bounds({10.0, 10.0, 40.0, 30.0});
    (*scene.bottom_right).set_bounds({340.0, 250.0, 40.0, 30.0});
    (*scene.root).add_child(scene.top_left);
    (*scene.root).add_child(scene.bottom_right);
    return scene;
}

void distant_rectangles_paint_their_sum_and_nothing_between() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    window.perform_layout();
    ClipTrackingPainter first(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(first));
    static_cast<void>(window.take_damage());

    (*scene.top_left).set_background(Color::rgba(200, 40, 40));
    (*scene.bottom_right).set_background(Color::rgba(40, 40, 200));
    window.flush();
    // A host takes the damage and hands the rectangle set back, as the
    // Windows host does with its pending damage and update region.
    const DamageRegion damage = window.take_damage();
    expect(damage.rectangle_count() == 2U, "two distant mutations keep two rectangles");
    const std::span<const Rect> rectangles = damage.rectangles();
    double expected_area = 0.0;
    for (std::size_t index = 0; index < rectangles.size(); ++index) {
        expected_area += rectangles[index].area();
    }

    const MetricsSnapshot before = window.metrics_snapshot();
    ClipTrackingPainter painter(Rect{0.0, 0.0, 400.0, 300.0});
    const std::optional<PaintReceipt> receipt = window.paint(painter, rectangles);
    expect(receipt.has_value(), "one lease produces one receipt");
    const MetricsSnapshot after = window.metrics_snapshot();

    expect(after.paint_passes == before.paint_passes + 1U, "the set paints in one pass");
    expect(after.damage_rectangles_painted == before.damage_rectangles_painted + 2U,
           "both rectangles are painted separately");
    const double painted = after.painted_damage_area - before.painted_damage_area;
    const double bounding = after.paint_bounding_area - before.paint_bounding_area;
    expect(near(painted, expected_area), "painted area equals the rectangles' sum");
    expect(bounding > painted * 10.0, "the bounding box would have cost far more");
    expect(!painter.drawn_clips.empty(), "the pass draws");
    for (std::size_t index = 0; index < painter.drawn_clips.size(); ++index) {
        expect(inside_any(painter.drawn_clips[index], rectangles),
               "no draw reaches outside the damaged rectangles");
    }
}

void nearly_touching_rectangles_merge() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    window.perform_layout();
    ClipTrackingPainter first(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(first));

    const std::array<Rect, 2> adjacent{Rect{100.0, 100.0, 50.0, 20.0},
                                       Rect{151.0, 100.0, 50.0, 20.0}};
    const MetricsSnapshot before = window.metrics_snapshot();
    ClipTrackingPainter painter(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(painter, adjacent));
    const MetricsSnapshot after = window.metrics_snapshot();
    expect(after.damage_rectangles_painted == before.damage_rectangles_painted + 1U,
           "rectangles a unit apart paint as one");
}

void single_rectangle_paint_is_unchanged() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    window.perform_layout();
    ClipTrackingPainter first(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(first));

    (*scene.top_left).set_background(Color::rgba(200, 40, 40));
    (*scene.bottom_right).set_background(Color::rgba(40, 40, 200));
    window.flush();
    const MetricsSnapshot before = window.metrics_snapshot();
    ClipTrackingPainter painter(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(painter));
    const MetricsSnapshot after = window.metrics_snapshot();
    const double painted = after.painted_damage_area - before.painted_damage_area;
    const double bounding = after.paint_bounding_area - before.paint_bounding_area;
    expect(after.damage_rectangles_painted == before.damage_rectangles_painted + 1U &&
               near(painted, bounding),
           "the single-rectangle overload still paints the bounding box");
}

// A clip that contains a rectangle on an axis returns that axis as it was:
// 41.19375 + 40 - 41.19375 is not 40 in doubles, and a row of exactly 40
// must stay 40 after clipping (TreeListView's virtual rows under a 22.6
// header). Partial overlaps and disjoint rectangles clip as before.
void intersection_keeps_contained_extents() {
    const Rect row{0.0, 41.19375, 383.0, 40.0};
    const Rect clip{0.0, 22.6, 383.0, 260.0};
    const Rect kept = Rect::intersection(row, clip);
    const Rect kept_reversed = Rect::intersection(clip, row);
    expect(kept.y == row.y && kept.height == 40.0 && kept_reversed.height == 40.0 && kept.width == 383.0,
           "a contained rectangle keeps its extent bit for bit");
    const Rect partial = Rect::intersection(Rect{0.0, 270.0, 100.0, 40.0}, clip);
    expect(partial.y == 270.0 && partial.height == (22.6 + 260.0) - 270.0, "a partial overlap clips to the edge");
    const Rect apart = Rect::intersection(Rect{0.0, 400.0, 10.0, 10.0}, clip);
    expect(apart.empty() && apart.height == 0.0, "disjoint rectangles intersect empty");
}

void accumulated_damage_paints_as_rectangles() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    window.perform_layout();
    ClipTrackingPainter first(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(first));

    (*scene.top_left).set_background(Color::rgba(200, 40, 40));
    (*scene.bottom_right).set_background(Color::rgba(40, 40, 200));
    window.flush();
    const MetricsSnapshot before = window.metrics_snapshot();
    ClipTrackingPainter painter(Rect{0.0, 0.0, 400.0, 300.0});
    const std::span<const Rect> none{};
    static_cast<void>(window.paint(painter, none));
    const MetricsSnapshot after = window.metrics_snapshot();
    expect(after.damage_rectangles_painted == before.damage_rectangles_painted + 2U,
           "an empty set paints the accumulated rectangles, not their bounds");
    expect(window.take_damage().empty(), "the painted rectangles consume their damage");
}

// Hosts report what each present copied to the screen, retained and live
// apart, so the copy cost is a counter on every host.
void presentation_counters_record_copied_area() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    window.perform_layout();
    ClipTrackingPainter painter(Rect{0.0, 0.0, 400.0, 300.0});
    const std::optional<PaintReceipt> receipt = window.paint(painter);
    expect(receipt.has_value(), "the first frame paints");
    PresentedArea retained;
    retained.device_pixels = 2400U;
    retained.rectangles = 2U;
    retained.copy_nanoseconds = 300U;
    expect(window.notify_presented(*receipt, 1000U, retained), "the receipt is accepted");
    // A repeated receipt (a tick that only presented live frames) is refused,
    // but its copy still happened and still counts.
    PresentedArea repeated;
    repeated.copy_nanoseconds = 100U;
    expect(!window.notify_presented(*receipt, 1000U, repeated), "a repeated receipt is refused");
    PresentedArea live;
    live.device_pixels = 10000U;
    live.rectangles = 1U;
    window.notify_live_presented(500U, live);
    window.notify_live_presented(500U, live);
    const MetricsSnapshot metrics = window.metrics_snapshot();
    expect(metrics.frames_presented == 1U && metrics.retained_pixels_presented == 2400U &&
               metrics.retained_rectangles_presented == 2U,
           "a retained present records its copied pixels and rectangles");
    expect(metrics.live_frames_presented == 2U && metrics.live_pixels_presented == 20000U &&
               metrics.live_rectangles_presented == 2U &&
               metrics.live_present_duration_nanoseconds == 1000U,
           "live frames are counted apart from retained presents");
    expect(metrics.screen_copy_nanoseconds == 400U,
           "copy time counts every copy, accepted receipt or not");
    const std::string json = metrics.to_json();
    expect(json.find("\"live_pixels_presented\":20000") != std::string::npos &&
               json.find("\"retained_pixels_presented\":2400") != std::string::npos,
           "the counters appear in the metrics JSON");
}

// A host declares the paint contract it meets and the extensions it offers;
// one that cannot meet the contract cannot attach.
void availability_record_reports_the_declaration() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    expect(window.presentation_availability().contract_version == 0U,
           "a window no host has declared reports no contract");
    PresentationDeclaration outdated;
    bool rejected = false;
    try {
        window.declare_presentation(outdated);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "a host below the paint contract fails attachment");
    PresentationDeclaration without_rectangles;
    without_rectangles.contract_version = paint_contract_version;
    rejected = false;
    try {
        window.declare_presentation(without_rectangles);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "contract version 2 requires presenting rectangle sets");
    PresentationDeclaration declared;
    declared.contract_version = paint_contract_version;
    declared.extensions.partial_present = true;
    window.declare_presentation(declared);
    const PresentationAvailability availability = window.presentation_availability();
    expect(availability.contract_version == paint_contract_version &&
               availability.extensions.partial_present &&
               !availability.extensions.zero_copy_import &&
               availability.lease.surface_epoch == window.paint_lease_snapshot().surface_epoch,
           "the record carries the declaration and the live lease");
    const std::string json = availability.to_json();
    expect(json.find("\"partial_present\":true") != std::string::npos &&
               json.find("\"contract_version\":2") != std::string::npos,
           "the record serializes for diagnostics");
}


// A one-pixel RGBA PNG.
constexpr std::array<std::uint8_t, 70> one_pixel_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// Counts paint wakes: each one is a frame the window asks its host for.
struct WakeCounter final {
    std::shared_ptr<int> wakes = std::make_shared<int>(0);
    void operator()() const { ++(*wakes); }
};

// Replacing a shared image damages every plane of the window without
// dirtying any control's paint. Once the host takes all of that damage,
// nothing is owed: the window must not keep asking for a frame. (A host
// with no damage to draw never paints, so such a request stays pending
// while the host is idle.)
void taking_all_damage_leaves_no_frame_pending() {
    Scene scene = make_scene();
    Window window(scene.root, Size{400.0, 300.0});
    const std::span<const std::byte, 70> png = std::as_bytes(std::span<const std::uint8_t, 70>(one_pixel_png));
    const ImageLoadResult loaded = window.load_png(png);
    expect(static_cast<bool>(loaded), "the fixture PNG loads");
    window.perform_layout();
    ClipTrackingPainter first(Rect{0.0, 0.0, 400.0, 300.0});
    static_cast<void>(window.paint(first));
    static_cast<void>(window.take_damage());
    expect(!window.needs_frame(), "a painted window starts idle");

    const ImageLoadResult replaced = window.replace_png(loaded.image, png);
    expect(static_cast<bool>(replaced), "the image is replaced");
    expect(window.needs_frame(), "the replacement asks for a frame");
    const DamageRegion damage = window.take_damage();
    expect(!damage.empty(), "the replacement leaves damage to take");
    expect(!window.needs_frame(), "no frame is needed once every plane's damage is taken");
    const WakeCounter counter;
    window.set_paint_wake_handler(counter);
    expect(*counter.wakes == 0, "installing a wake handler requests no frame");
}


// A control outside the client area changes in the same revision as a
// visible one. Its damage is clipped away, so the paint never reaches it and
// its own paint-dirty mark stays (it rebuilds when it is next painted). Once
// the visible change is rendered and presented, nothing is owed: the lease is
// clean and no frame is needed, although that control is still marked.
void an_unreachable_dirty_control_owes_no_frame() {
    Scene scene = make_scene();
    std::shared_ptr<Panel> outside = make_control<Panel>(StableId("dp.outside"));
    (*outside).set_bounds({500.0, 10.0, 40.0, 30.0});
    (*scene.root).add_child(outside);
    Window window(scene.root, Size{400.0, 300.0});
    window.perform_layout();
    ClipTrackingPainter first(Rect{0.0, 0.0, 400.0, 300.0});
    const std::optional<PaintReceipt> initial = window.paint(first);
    expect(initial.has_value(), "the first paint produces a receipt");
    expect(window.notify_presented(*initial, 1000U), "the first receipt is presented");
    expect(!window.needs_frame(), "a presented window starts idle");

    (*scene.top_left).set_background(Color::rgba(200, 40, 40));
    (*outside).set_background(Color::rgba(40, 200, 40));
    window.flush();
    expect(window.needs_frame(), "the visible change asks for a frame");
    const DamageRegion damage = window.take_damage();
    ClipTrackingPainter painter(Rect{0.0, 0.0, 400.0, 300.0});
    const std::optional<PaintReceipt> receipt = window.paint(painter, damage.rectangles());
    expect(receipt.has_value(), "the visible change paints");
    expect(window.notify_presented(*receipt, 1000U), "the receipt is presented");

    const PaintLeaseSnapshot lease = window.paint_lease_snapshot();
    expect(lease.content_revision == lease.rendered_revision &&
               lease.rendered_revision == lease.presented_revision,
           "the change is rendered and presented");
    expect(lease.state == PaintLeaseState::clean, "nothing owed leaves the lease clean");
    expect(!window.needs_frame(), "nothing owed needs no frame");
    const WakeCounter counter;
    window.set_paint_wake_handler(counter);
    expect(*counter.wakes == 0, "nothing owed requests no wake");
}

} // namespace

int main() {
    distant_rectangles_paint_their_sum_and_nothing_between();
    nearly_touching_rectangles_merge();
    single_rectangle_paint_is_unchanged();
    intersection_keeps_contained_extents();
    accumulated_damage_paints_as_rectangles();
    presentation_counters_record_copied_area();
    availability_record_reports_the_declaration();
    taking_all_damage_leaves_no_frame_pending();
    an_unreachable_dirty_control_owes_no_frame();
    std::cout << "gui_forms_damage_rectangles_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
