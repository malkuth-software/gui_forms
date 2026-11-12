#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <optional>
#include <span>

#include <cstdint>

namespace gui_forms {

class Control;
class Painter;
class SurfaceRoot;
class Window;
struct PaintGroup;
struct PartPaint;

// What `window` (a window or a sub-surface) is to a rule's media condition (EC3): its presentation
// settings' colour scheme, high contrast and reduced motion, and its scale.
[[nodiscard]] GUI_FORMS_API MediaFacts window_media_facts(const SurfaceRoot& window) noexcept;

// The text role a part of `control` whose cascade gave it `font` has (T3
// P8c): the font's role, or, where it is `inherit` (owner decision D7),
// the parent control's own: the font its rules give the control itself,
// followed up the tree while that inherits too, and ui at the root.
[[nodiscard]] GUI_FORMS_API TextRole resolved_text_role(const Control& control, FontDeclaration font);

// The em of text in `role` on `control` (D5): the role's size in the
// control's theme's typography tokens times its window's text scale.
[[nodiscard]] GUI_FORMS_API double text_role_em(const Control& control, TextRole role) noexcept;

// The side, in device pixels, that paint_material_blocks fits whole periods
// into. With the material's outsets (8 logical units each side) a block of
// one composited pattern stays within the material bake bound
// (maximum_material_bake_effort, at 135 units a pixel) up to a 96-unit
// period at scale 3; a larger period or scale may be refused, and is then
// not drawn.
inline constexpr double maximum_material_block_side = 256.0;

// A material repeated over an area (ThemeEngine::paint_material_blocks):
// - `key` names the part's record (borrowed for the call);
// - `material` is borrowed for the call;
// - `period` is the material's repeat in logical units (a procedural
//   fill's tile), within [minimum_procedural_tile,
//   maximum_procedural_tile];
// - the blocks lie on the lattice of whole periods through `anchor`, so
//   that they stay where the caller anchors them as it pans, and are drawn
//   where they meet `area`, clipped to it (the painter's coordinates).
struct GUI_FORMS_API MaterialBlocks final {
    PartKey key{};
    const SurfaceMaterial* material{};
    double period{};
    Point anchor{};
    Rect area{};
};

// A surface finish over an area, in world coordinates
// (ThemeEngine::paint_finish_tiles; cp31, the node canvas's finish
// texture):
// - `key` names the part's record (borrowed for the call);
// - `finish` is borrowed for the call;
// - `anchor` is world (0, 0) in the painter's coordinates and `zoom` the
//   view units per world unit, so the finish is a property of the world:
//   it pans with it and grows with the zoom;
// - `area` is where it is drawn, clipped to it (the painter's
//   coordinates).
struct GUI_FORMS_API FinishTiles final {
    PartKey key{};
    const FinishFill* finish{};
    Point anchor{};
    double zoom{1.0};
    Rect area{};
};

// The theme engine's entry point (T2 plan, ThemeEngine). paint_part hands it
// every part; controls never call it directly.
//
// Ownership: a Theme owns its engine, and every window showing the theme
// shares it. An application's windows share one theme (ApplicationResources),
// so they share one engine. The engine is immutable once its theme is made:
// it holds the theme's compiled rules (the cascade, E4), below them the
// engine's own built-in rules (built_in_theme_rules, E7), and borrows the
// built-in primitive registry. What changes from frame to frame belongs to
// each window's EnginePartTable and MotionClock.
//
// The engine draws a part with a primitive only when the cascade selects a
// registered primitive for the part, its parameters validate, the cost is
// within budget (GIVEN F1-1) and the presentation is not forced high
// contrast. In every other case the built-in recipe draws it: the body
// paint_part had before the engine existed. A theme with no rules of its
// own draws every part with the built-in recipe except those the built-in
// rules address: the ribbon's command faces and group buttons, which take
// the dithered bulge and its rise, and the docking tiles, which fade in.
//
// A keyed part's transitions run on its window's motion clock (E7): the
// part's states select each machine's state, the property moves toward it,
// and the part repaints within its extent as it moves. Its opacity
// multiplies a primitive's composite in the shared blend; a part the
// built-in recipe draws is drawn as one paint group (Painter::begin_group)
// composited once at that opacity. A part painted without a key stands at
// its machines' targets and its ambient effect's pose.
class GUI_FORMS_API ThemeEngine final {
public:
    // An engine with only the built-in rules.
    ThemeEngine();
    // An engine with `sheet` compiled against the built-in registry.
    // Invalid rules and declarations are dropped with diagnostics; nothing
    // throws for them.
    explicit ThemeEngine(const ThemeRuleSheet& sheet);
    // An engine with `sheet` compiled as `set`: a user theme's has no
    // built-in rules and is consulted beside a program theme's
    // (CompiledThemeRules::compile_user).
    ThemeEngine(const ThemeRuleSheet& sheet, ThemeRuleSet set);

    // The primitives the engine's rules may select.
    [[nodiscard]] const PrimitiveRegistry& registry() const noexcept { return *registry_; }
    [[nodiscard]] const CompiledThemeRules& rules() const noexcept { return rules_; }

    // How the engine draws one part, and why.
    enum class PartDrawing : std::uint8_t {
        // The cascade's primitive draws it.
        primitive,
        // No rule selects a primitive, or the winning rule selects built_in.
        built_in_recipe,
        // Forced high contrast (CascadeLevel::forced_high_contrast): the
        // engine's own pass draws the part with the built-in recipe, whose
        // high-contrast records keep geometry and state composition and draw
        // no gradient, texture, glow or cast shadow. Only an important
        // inline style's primitive draws a part under the pass, with its
        // colours the pass's (high_contrast_colour); its material keeps the
        // border's styles and widths and the corners, its colours remapped
        // and its images, shadows, glows and filters dropped.
        forced_high_contrast,
        // The selected primitive does not paint the part's role.
        role_mismatch,
        // The part's primitive has an animated parameter and would cost more
        // than maximum_animated_operations_per_frame at its device size.
        over_frame_budget,
        // The part's bounds are empty, or the control is in no window.
        not_paintable,
    };

    struct GUI_FORMS_API PartPlan final {
        PartDrawing drawing{PartDrawing::built_in_recipe};
        CompiledThemeRules::Resolution resolution{};
        // The raster's size for a primitive, in device pixels.
        DeviceExtent extent{};
        DeviceScale scale{};
    };

    // The cascade's decision for one part of `control`, without painting.
    [[nodiscard]] PartPlan plan(const Control& control, const PartPaint& part) const noexcept;

    // Paints one part of `control` (see paint_part). A part with a key
    // (PartPaint::key) and paintable bounds, painted by a control attached
    // to a window, has its record in that window's part table made or
    // updated first; a primitive then draws into the record's cached raster.
    // A part without a key leaves no record; a primitive draws it into a
    // raster of its own for this paint. If the primitive refuses the part,
    // the built-in recipe draws it. A part with a blend (blend_property) is
    // drawn inside a paint group of that composite.
    void paint(Painter& painter, const Control& control, const PartPaint& part) const;
    // As paint, with what it draws repeated under each of `clips` (each in
    // a save, clip_rect and restore; none for an empty span), the part
    // planned, noted and baked once (N91): a pointed part's body and the
    // strips of its nose.
    void paint_clipped(Painter& painter, const Control& control, const PartPaint& part,
                       std::span<const Rect> clips) const;

    // Paints `blocks.material` repeated over `blocks.area` of `control`
    // (the node canvas's texture layers; cp31), through the material
    // primitive and the window's caches as a keyed part is:
    // - The block is square, a whole number of periods across: as many as
    //   fit maximum_material_block_side device pixels at the window's
    //   scale, and at least one. The primitive bakes it once into the
    //   record `blocks.key` names, and again only when the material, the
    //   scale or the block changes; panning bakes nothing.
    // - A procedural fill's pattern is generated in an idle frame, never
    //   here (E10): until then the block shows the fill's ground, and the
    //   window repaints the area once the pattern is made.
    // - The raster is then drawn at every block of the lattice that meets
    //   the area, in one save, clip_rect and restore.
    // Draws nothing and returns false for a control in no window, an empty
    // or non-finite area or anchor, a period outside its range, no
    // material, a material the primitive refuses (its bake over the
    // material bound) or no raster. Allocation failure propagates as
    // std::bad_alloc, as paint's does. Not reentrant, as paint is not.
    bool paint_material_blocks(Painter& painter, const Control& control, const MaterialBlocks& blocks) const;

    // Paints `tiles.finish` over `tiles.area` of `control` from the
    // window's world finish tiles (EnginePartTable::finish_tile; cp31):
    // - The finish is evaluated at world device pixels, at the window's
    //   scale times the zoom, on the lattice of finish_tile_side-pixel tiles
    //   through world (0, 0) (snapped to the nearest device pixel, so tiles
    //   land on whole pixels). Each tile is the kernel at those pixels, so
    //   adjacent tiles join exactly: no period, and no seam.
    // - A tile not held is queued for an idle frame and its place left
    //   bare; once the queue is painted the window repaints the area
    //   (the record `tiles.key` names). Panning reuses the tiles held; a
    //   new zoom or scale names new tiles, painted again in idle frames.
    // - Cost: one draw_part_raster per tile meeting the area, inside one
    //   save, clip_rect and restore; nothing is painted inside the paint.
    // Draws nothing and returns false for a control in no window, an empty
    // or non-finite area, anchor or zoom, or no finish; otherwise returns
    // whether every tile was held. Not reentrant, as paint is not.
    bool paint_finish_tiles(Painter& painter, const Control& control, const FinishTiles& tiles) const;

    // The hit region of one part of `control` (E8, T1's hand-off): the
    // part's bounds and the shape its primitive draws there, as plan()
    // decides it. A part the built-in recipe draws, for any reason, has a
    // rectangle. Themes choose a primitive's shape; they never set a hit
    // region (design language §7.4 rule 1). Test a point with part_hit.
    [[nodiscard]] PartHitRegion hit_region(const Control& control, const PartPaint& part) const noexcept;

    // Where one part of `control` lays out its text (E8, T1's hand-off):
    // the part's bounds less its primitive's content insets at the
    // window's scale and text scale, as plan() decides it. Where the insets
    // leave no room across or down, the slot shrinks to the bounds' centre
    // line on that axis, so it never leaves the part. A part the built-in
    // recipe draws has its bounds. Like the hit region, it ignores the
    // presentational scale.
    [[nodiscard]] Rect text_slot(const Control& control, const PartPaint& part) const noexcept;

private:
    // plan() without the control's kept plans; sets `reads_ancestor_states`
    // where the cascade read an ancestor's live states (an inherited font's
    // em).
    [[nodiscard]] PartPlan plan_from_rules(const Control& control, const PartPaint& part,
                                           bool& reads_ancestor_states) const noexcept;
    // paint_clipped(), once the part's plan is decided (N39): the stages
    // below, then the draw under each clip, inside `group` (the part's
    // blend) when there is one.
    struct PartPaintRun;
    void paint_planned(Painter& painter, const Control& control, const PartPaint& part,
                       const PartPlan& decided, std::span<const Rect> clips, const PaintGroup* group) const;
    // The part's record, where it lands and its transitions' sources.
    void note_part_paint(Painter& painter, const Control& control, const PartPaint& part, PartPaintRun& run) const;
    // A part no primitive draws: its transitions; the built-in recipe draws
    // it, in the material its rules author (built_in_part_material).
    void paint_without_primitive(const Control& control, const PartPaint& part, PartPaintRun& run) const;
    // The material the built-in recipe draws a part in where its rules
    // author material fields; null where they author none.
    [[nodiscard]] const SurfaceMaterial* built_in_part_material(const Control& control, const PartPaint& part,
                                                                PartPaintRun& run) const;
    // The raster request and the size the part is shown at; false when the
    // part draws nothing.
    [[nodiscard]] bool prepare_part_request(const Control& control, const PartPaint& part, PartPaintRun& run) const;
    // The material lent for this paint; false when the part draws nothing.
    [[nodiscard]] bool lend_part_material(const Control& control, const PartPaint& part, PartPaintRun& run) const;
    // The backplane borrowed for glass and backplane reflections.
    void borrow_part_backplane(const PartPaint& part, PartPaintRun& run) const;
    // The raster baked or taken from the record, or the built-in fallback.
    void bake_part(const PartPaint& part, PartPaintRun& run) const;
    // Draws what the stages decided; reads the run only.
    void draw_part_paint(Painter& painter, const Control& control, const PartPaint& part,
                         const PartPaintRun& run) const;
    // E7: an ambient part's time_in_state for this paint, starting its
    // effect on the window's clock, or stopping it and returning the pose
    // when the effect is refused for its area (`area_ok` false) or ambient
    // motion is stopped. A part without a record is at its pose.
    [[nodiscard]] static double ambient_seconds(SurfaceRoot& window, EnginePartRecord* record,
                                                const std::optional<PartRecordHandle>& handle,
                                                const AmbientEffect& ambient, bool area_ok);
    // Stops the ambient effect of a part whose paint has none.
    static void stop_ambient(SurfaceRoot& window, EnginePartRecord& record, PartRecordHandle handle) noexcept;
    // E7r: queues for the window's idle frames every keyframe level of
    // each animated parameter in `movable` (bits by parameter position),
    // with the part's other parameters as `request` has them.
    static void queue_keyframes(SurfaceRoot& window, const PartRasterRequest& request, std::uint32_t movable);

    // Borrowed: the built-in registry has static storage.
    const PrimitiveRegistry* registry_;
    CompiledThemeRules rules_{};
};

} // namespace gui_forms
