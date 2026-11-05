#include "gui_forms/gui_forms.hpp"
#include "../src/core/display/recording_painter/recording_painter.hpp"
#include "../src/core/display/replay/replay_display_chunk.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class OrderPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point,
                        std::string_view text,
                        FontSpec,
                        Color) override {
        order.emplace_back(text);
    }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> order;
};

class ChunkControl final : public Control {
public:
    ChunkControl(StableId id, std::string label)
        : Control(std::move(id)), label_(std::move(label)) {}

    void on_paint(Painter& painter, Rect) override {
        ++paint_callbacks;
        painter.draw_text_utf8({2.0, 12.0}, label_, {}, Color::rgba(20, 30, 40));
    }

    std::uint64_t paint_callbacks{};

private:
    std::string label_;
};

struct Fixture {
    Fixture() {
        (*root).set_paint_plane(PaintPlane::backplane);
        (*root).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
        (*overlay).set_paint_plane(PaintPlane::overlay);
        (*overlay).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
        (*control).set_requested_bounds({10.0, 10.0, 80.0, 40.0});

        // Overlay precedes the ordinary child in tree order. Plane ordering,
        // not insertion order, must still place it last.
        (*root).add_child(overlay);
        (*root).add_child(control);
        window = std::make_unique<Window>(root, Size{200.0, 120.0});
        (*window).perform_layout();
        static_cast<void>((*window).take_damage());
        (*window).paint(painter, {0.0, 0.0, 200.0, 120.0});
        (*window).reset_activity_metrics();
        painter.order.clear();
    }

    std::shared_ptr<ChunkControl> root =
        make_control<ChunkControl>(StableId("chunk.root"), "backplane");
    std::shared_ptr<ChunkControl> overlay =
        make_control<ChunkControl>(StableId("chunk.overlay"), "overlay");
    std::shared_ptr<ChunkControl> control =
        make_control<ChunkControl>(StableId("chunk.control"), "control");
    std::unique_ptr<Window> window;
    OrderPainter painter;
};

void test_plane_order_and_exposure_reuse() {
    Fixture fixture;
    const std::uint64_t root_callbacks = (*fixture.root).paint_callbacks;
    const std::uint64_t control_callbacks = (*fixture.control).paint_callbacks;
    const std::uint64_t overlay_callbacks = (*fixture.overlay).paint_callbacks;

    (*fixture.window).paint(fixture.painter, {0.0, 0.0, 200.0, 120.0});
    require(fixture.painter.order ==
                std::vector<std::string>{"backplane", "control", "overlay"},
            "display replay must order backplane, controls, then overlays");
    require((*fixture.root).paint_callbacks == root_callbacks &&
                (*fixture.control).paint_callbacks == control_callbacks &&
                (*fixture.overlay).paint_callbacks == overlay_callbacks,
            "exposure paint must replay chunks without invoking control paint callbacks");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.display_chunks_rebuilt == 0 && metrics.display_chunks_reused == 3 &&
                metrics.display_commands_replayed == 3,
            "exposure metrics must report three reused one-command chunks");
}

void test_local_rebuild_and_generation() {
    Fixture fixture;
    const std::optional<DisplayChunkInfo> root_before = (*fixture.root).display_chunk_info();
    const std::optional<DisplayChunkInfo> control_before = (*fixture.control).display_chunk_info();
    const std::optional<DisplayChunkInfo> overlay_before = (*fixture.overlay).display_chunk_info();
    require(root_before && control_before && overlay_before,
            "initial paint must populate every display chunk");

    (*fixture.control).invalidate(invalidation::paint_only);
    require((*fixture.window).take_damage(PaintPlane::backplane).empty() &&
                (*fixture.window).take_damage(PaintPlane::overlay).empty(),
            "control invalidation must not damage backplane or overlay planes");
    DamageRegion damage = (*fixture.window).take_damage(PaintPlane::control);
    require(!damage.empty(), "control invalidation must damage the control plane");
    (*fixture.window).paint(fixture.painter, damage.bounds());

    const std::optional<DisplayChunkInfo> root_after = (*fixture.root).display_chunk_info();
    const std::optional<DisplayChunkInfo> control_after = (*fixture.control).display_chunk_info();
    const std::optional<DisplayChunkInfo> overlay_after = (*fixture.overlay).display_chunk_info();
    require((*root_after).generation == (*root_before).generation &&
                (*overlay_after).generation == (*overlay_before).generation &&
                (*control_after).generation > (*control_before).generation,
            "only the invalidated control chunk may advance generation");
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    require(metrics.display_chunks_rebuilt == 1 && metrics.display_chunks_reused == 2 &&
                metrics.paint_invalidations_consumed == 1,
            "localized paint must rebuild one chunk and reuse two plane peers");
}

void test_plane_migration_damages_old_and_new_planes() {
    Fixture fixture;
    (*fixture.control).set_paint_plane(PaintPlane::overlay);
    require(!(*fixture.window).take_damage(PaintPlane::control).empty(),
            "paint-plane migration must clear the old plane");
    require(!(*fixture.window).take_damage(PaintPlane::overlay).empty(),
            "paint-plane migration must damage the new plane");
    require(!(*fixture.control).display_chunk_info().has_value(),
            "paint-plane migration must discard an incompatible cached chunk");
}

void test_arrangement_recomposes_every_plane() {
    Fixture fixture;
    (*fixture.control).set_requested_bounds({30.0, 20.0, 80.0, 40.0});
    (*fixture.window).perform_layout();
    require(!(*fixture.window).take_damage(PaintPlane::backplane).empty(),
            "moved control must expose and redraw its backplane");
    require(!(*fixture.window).take_damage(PaintPlane::control).empty(),
            "moved control must redraw its control plane");
    require(!(*fixture.window).take_damage(PaintPlane::overlay).empty(),
            "moved control must recompose overlays crossing its old or new bounds");
}

void test_invalid_plane_rejection_preserves_state() {
    Fixture fixture;
    const std::optional<DisplayChunkInfo> before = (*fixture.control).display_chunk_info();
    bool setter_rejected = false;
    try {
        (*fixture.control).set_paint_plane(static_cast<PaintPlane>(99));
    } catch (const std::invalid_argument&) {
        setter_rejected = true;
    }
    require(setter_rejected && (*fixture.control).paint_plane() == PaintPlane::control &&
                (*(*fixture.control).display_chunk_info()).generation == (*before).generation,
            "invalid paint-plane mutation must be rejected without changing cache state");

    (*fixture.control).invalidate(invalidation::paint_only);
    bool damage_rejected = false;
    try {
        static_cast<void>((*fixture.window).take_damage(static_cast<PaintPlane>(99)));
    } catch (const std::invalid_argument&) {
        damage_rejected = true;
    }
    require(damage_rejected &&
                !(*fixture.window).take_damage(PaintPlane::control).empty(),
            "invalid damage-plane access must not consume valid pending damage");
}

void test_disposal_removes_cache_entry_and_returns_idle() {
    Fixture fixture;
    (*fixture.control).dispose();
    DamageRegion damage = (*fixture.window).take_damage();
    require(!damage.empty(), "disposing a cached control must damage its old pixels");
    (*fixture.window).paint(fixture.painter, damage.bounds());
    (*fixture.window).reset_activity_metrics();
    require((*fixture.window).metrics_snapshot().display_cache_entries == 2,
            "disposed control must leave exactly two retained cache entries");
    require(!(*fixture.window).needs_frame() && !(*fixture.window).next_wake().has_value(),
            "completed disposal paint must return the window to idle");
}

// Records every fill with its rectangle and colour, in replay order.
class FillPainter final : public Painter {
public:
    struct Fill final {
        Rect rect;
        Color color;
    };

    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color color) override { fills.push_back({rect, color}); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    // Fills of the whole window in a colour other than `face`: the backplane's.
    [[nodiscard]] std::size_t backplane_fills(Rect window, Color face) const {
        std::size_t count = 0;
        for (const Fill& fill : fills) {
            if (fill.rect == window && !(fill.color == face)) ++count;
        }
        return count;
    }

    std::vector<Fill> fills;
};

// Paints only text: it never reports a cover, though Panel's background is set.
class TextPanel final : public Panel {
public:
    using Panel::Panel;
    void on_paint(Painter& painter, Rect) override {
        painter.draw_text_utf8({2.0, 12.0}, "text", {}, Color::rgba(20, 30, 40));
    }
};

// Invalidates `target` from its first paint callback. Observing: the
// target is its ancestor, which owns it.
class InvalidatingControl final : public Control {
public:
    using Control::Control;
    void on_paint(Painter&, Rect) override {
        const Control::Ptr observed = target.lock();
        if (observed && !invalidated) {
            invalidated = true;
            (*observed).invalidate(invalidation::paint_only);
        }
    }
    std::weak_ptr<Control> target;
    bool invalidated{};
};

constexpr Color face_color = Color::rgba(1, 2, 3);
constexpr Rect window_rect{0.0, 0.0, 200.0, 120.0};

// Paints its whole bounds as one engine face of `role`, in an authored
// colour: an opaque engine face reports its cover (owner, 2026-11-05).
class FacePanel final : public Panel {
public:
    using Panel::Panel;
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = role;
        part.bounds = local_bounds();
        part.family = ControlVisualRole::panel;
        part.authored_background = colour;
        part.corner_radius = 0.0;
        paint_part(painter, *this, part);
    }
    PartRole role{PartRole::well};
    Color colour{face_color};
};

struct PanelFixture {
    explicit PanelFixture(std::shared_ptr<Panel> panel) : root(std::move(panel)) {
        (*root).set_requested_bounds(window_rect);
        window = std::make_unique<Window>(root, Size{window_rect.width, window_rect.height});
        (*window).perform_layout();
        static_cast<void>((*window).take_damage());
    }

    // Paints the whole window twice: the second replays current chunks,
    // which is when a cover is known.
    FillPainter paint_twice() {
        FillPainter first;
        (*window).paint(first, window_rect);
        FillPainter second;
        (*window).paint(second, window_rect);
        return second;
    }

    std::shared_ptr<Panel> root;
    std::unique_ptr<Window> window;
};

void test_opaque_root_panel_skips_the_backplane() {
    PanelFixture fixture(make_control<Panel>(StableId("cover.root")));
    (*fixture.root).set_background(face_color);
    const FillPainter painted = fixture.paint_twice();
    require(painted.backplane_fills(window_rect, face_color) == 0U && !painted.fills.empty() &&
                painted.fills.front().color == face_color,
            "an opaque root panel must start the paint at its own face");
}

// Declared clarity (`background: transparent`, owner decision 2026-11-05):
// a panel declared clear reports no cover, so the backplane (where a
// window's backdrop lies) is still drawn beneath it, though its own face is
// opaque (W405 is written once); without the declaration the face covers.
void test_clear_panel_keeps_the_backplane() {
    PanelFixture clear(make_control<Panel>(StableId("cover.clear")));
    (*clear.root).set_background(face_color);
    const InlineStyleResult styled = (*clear.root).set_inline_style("background: transparent");
    require(styled.succeeded() && (*clear.root).declares_clarity(), "background: transparent declares the panel clear");
    require(clear.paint_twice().backplane_fills(window_rect, face_color) == 1U,
            "a clear panel must keep the backplane beneath its face");
    const InlineStyleResult colour_only = (*clear.root).set_inline_style("background-color: transparent");
    require(colour_only.succeeded() && !(*clear.root).declares_clarity(),
            "a transparent colour over the floor's layers is not clear");
    const InlineStyleResult removed = (*clear.root).set_inline_style("");
    require(removed.succeeded() && !(*clear.root).declares_clarity(), "without a style the panel is not clear");
    require(clear.paint_twice().backplane_fills(window_rect, face_color) == 0U,
            "then its opaque face covers the backplane again");
}

void test_translucent_or_overridden_panels_keep_the_backplane() {
    PanelFixture translucent(make_control<Panel>(StableId("cover.translucent")));
    (*translucent.root).set_background(Color::rgba(1, 2, 3, 128));
    require(translucent.paint_twice().backplane_fills(window_rect, Color::rgba(1, 2, 3, 128)) == 1U,
            "a translucent panel must keep the backplane beneath it");
    PanelFixture text(make_control<TextPanel>(StableId("cover.text")));
    (*text.root).set_background(face_color);
    require(text.paint_twice().backplane_fills(window_rect, face_color) == 1U,
            "a panel subclass that paints its own way must keep the backplane");
}

void test_opaque_engine_faces_report_their_cover() {
    PanelFixture opaque(make_control<FacePanel>(StableId("cover.face")));
    require(opaque.paint_twice().backplane_fills(window_rect, face_color) == 0U,
            "an opaque square engine face must start the paint at itself");
    std::shared_ptr<FacePanel> translucent_face = make_control<FacePanel>(StableId("cover.face.translucent"));
    (*translucent_face).colour = Color::rgba(1, 2, 3, 128);
    PanelFixture translucent(translucent_face);
    require(translucent.paint_twice().backplane_fills(window_rect, Color::rgba(1, 2, 3, 128)) == 1U,
            "a translucent engine face must keep the backplane beneath it");
    std::shared_ptr<FacePanel> rule = make_control<FacePanel>(StableId("cover.face.rule"));
    (*rule).role = PartRole::divider;
    PanelFixture divider(rule);
    require(divider.paint_twice().backplane_fills(window_rect, face_color) == 1U,
            "a part that fills no surface must keep the backplane beneath it");
}

void test_a_cover_lost_during_paint_restores_what_it_skipped() {
    PanelFixture fixture(make_control<Panel>(StableId("cover.lost")));
    (*fixture.root).set_background(face_color);
    const std::shared_ptr<InvalidatingControl> child =
        make_control<InvalidatingControl>(StableId("cover.lost.child"));
    (*child).set_requested_bounds({0.0, 0.0, 40.0, 40.0});
    // In the root panel's plane, so that it paints between the rectangles' checks.
    (*child).set_paint_plane((*fixture.root).paint_plane());
    (*fixture.root).add_child(child);
    (*fixture.window).perform_layout();
    static_cast<void>(fixture.paint_twice());
    // The child paints in the first rectangle and invalidates the root, so
    // the second rectangle's cover is gone when its plane is reached.
    (*child).target = fixture.root;
    (*child).invalidate(invalidation::paint_only);
    const std::array<Rect, 2> rectangles{Rect{0.0, 0.0, 40.0, 40.0}, Rect{100.0, 60.0, 50.0, 40.0}};
    FillPainter painted;
    (*fixture.window).paint(painted, rectangles);
    std::size_t backplane = 0;
    for (const FillPainter::Fill& fill : painted.fills) {
        if (fill.rect == window_rect && !(fill.color == face_color)) ++backplane;
    }
    require((*child).invalidated && backplane == 1U,
            "a rectangle whose cover is lost before its plane must get its backplane back");
}

// The opaque cover the chunk `recorder` finishes keeps.
std::optional<Rect> finished_cover(detail::RecordingPainter& recorder, const Rect bounds) {
    const std::shared_ptr<const detail::DisplayChunk> chunk = recorder.finish(1U, PaintPlane::control, bounds);
    const std::optional<Rect> cover = (*chunk).opaque_cover();
    return cover;
}

// Records fills and text in replay order.
class SequencePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override {
        sequence.push_back("fill " + std::to_string(color.red) + "," + std::to_string(color.green));
    }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { sequence.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> sequence;
};

void test_a_nested_cover_starts_the_paint_and_later_siblings_follow() {
    PanelFixture fixture(make_control<Panel>(StableId("nested.root")));
    (*fixture.root).set_background(Color::rgba(10, 11, 12));
    const std::shared_ptr<ChunkControl> before = make_control<ChunkControl>(StableId("nested.before"), "before");
    const std::shared_ptr<Panel> face = make_control<Panel>(StableId("nested.face"));
    const std::shared_ptr<ChunkControl> after = make_control<ChunkControl>(StableId("nested.after"), "after");
    (*before).set_requested_bounds({20.0, 20.0, 60.0, 30.0});
    (*face).set_requested_bounds({10.0, 10.0, 120.0, 80.0});
    (*face).set_background(Color::rgba(20, 21, 22));
    (*after).set_requested_bounds({30.0, 30.0, 60.0, 30.0});
    // All in the root panel's plane, so that tree order alone decides.
    (*before).set_paint_plane((*fixture.root).paint_plane());
    (*face).set_paint_plane((*fixture.root).paint_plane());
    (*after).set_paint_plane((*fixture.root).paint_plane());
    (*fixture.root).add_child(before);
    (*fixture.root).add_child(face);
    (*fixture.root).add_child(after);
    (*fixture.window).perform_layout();
    static_cast<void>(fixture.paint_twice());
    SequencePainter painted;
    (*fixture.window).paint(painted, Rect{40.0, 40.0, 20.0, 10.0});
    require(painted.sequence == std::vector<std::string>{"fill 20,21", "after"},
            "a covered rectangle must start at the covering panel and still paint what follows it");
    SequencePainter outside;
    (*fixture.window).paint(outside, Rect{140.0, 95.0, 20.0, 10.0});
    require(outside.sequence == std::vector<std::string>{"fill 10,11"},
            "a rectangle outside the nested panel must start at the root");
}

void test_recorded_covers_follow_the_painter_state() {
    using detail::RecordingPainter;
    const Rect face{0.0, 0.0, 20.0, 10.0};
    RecordingPainter plain;
    plain.fill_rect(face, face_color);
    plain.mark_opaque_cover(face);
    PaintGroup over{};
    over.bounds = face;
    plain.begin_group(over);
    plain.end_group();
    require(finished_cover(plain, face) == std::optional<Rect>{face}, "a top-level mark must be kept");

    RecordingPainter saved;
    saved.save();
    saved.mark_opaque_cover(face);
    saved.restore();
    RecordingPainter clipped;
    clipped.clip_rect(face);
    clipped.mark_opaque_cover(face);
    RecordingPainter moved;
    moved.translate({1.0, 0.0});
    moved.mark_opaque_cover(face);
    RecordingPainter copied;
    copied.mark_opaque_cover(face);
    PaintGroup copy{};
    copy.bounds = face;
    copy.composite = PaintGroupComposite::copy;
    copied.begin_group(copy);
    copied.end_group();
    require(!finished_cover(saved, face) && !finished_cover(clipped, face) && !finished_cover(moved, face) && !finished_cover(copied, face),
            "a mark inside a save, under a clip or translation, or before a copy group must be dropped");
}

// Keeps the per-corner rounded clips it is given.
class CornerClipPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii) override {
        boxes.push_back(rect);
        corners.push_back(radii);
    }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    std::vector<Rect> boxes;
    std::vector<PaintCornerRadii> corners;
};

void test_a_per_corner_clip_replays_its_radii() {
    detail::RecordingPainter recorder;
    const Rect box{1.0, 2.0, 30.0, 12.0};
    const PaintCornerRadii radii{1.0, 2.0, 3.0, 4.0};
    recorder.save();
    recorder.clip_rounded_rect_corners(box, radii);
    recorder.fill_rect(box, face_color);
    recorder.restore();
    const std::shared_ptr<const detail::DisplayChunk> chunk = recorder.finish(1U, PaintPlane::control, box);
    CornerClipPainter replayed;
    static_cast<void>(detail::replay_display_chunk(*chunk, replayed));
    require(replayed.boxes == std::vector<Rect>{box} && replayed.corners == std::vector<PaintCornerRadii>{radii},
            "a per-corner rounded clip must replay with each corner's radius in place");
}

} // namespace

int main() {
    try {
        test_plane_order_and_exposure_reuse();
        test_local_rebuild_and_generation();
        test_plane_migration_damages_old_and_new_planes();
        test_arrangement_recomposes_every_plane();
        test_invalid_plane_rejection_preserves_state();
        test_disposal_removes_cache_entry_and_returns_idle();
        test_opaque_root_panel_skips_the_backplane();
        test_clear_panel_keeps_the_backplane();
        test_translucent_or_overridden_panels_keep_the_backplane();
        test_opaque_engine_faces_report_their_cover();
        test_a_cover_lost_during_paint_restores_what_it_skipped();
        test_a_nested_cover_starts_the_paint_and_later_siblings_follow();
        test_recorded_covers_follow_the_painter_state();
        test_a_per_corner_clip_replays_its_radii();
        std::cout << "gui_forms_display_chunk_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_display_chunk_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
