#pragma once

#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <optional>

#include <cstdint>

namespace gui_forms {

class Control;
class Painter;
class SurfaceRoot;
class Window;
struct PartPaint;

// What `window` (a window or a sub-surface) is to a rule's media condition (EC3): its presentation
// settings' colour scheme, high contrast and reduced motion, and its scale.
[[nodiscard]] MediaFacts window_media_facts(const SurfaceRoot& window) noexcept;

// The text role a part of `control` whose cascade gave it `font` has (T3
// P8c): the font's role, or, where it is `inherit` (owner decision D7),
// the parent control's own: the font its rules give the control itself,
// followed up the tree while that inherits too, and ui at the root.
[[nodiscard]] TextRole resolved_text_role(const Control& control, FontDeclaration font);

// The em of text in `role` on `control` (D5): the role's size in the
// control's theme's typography tokens times its window's text scale.
[[nodiscard]] double text_role_em(const Control& control, TextRole role) noexcept;

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
class ThemeEngine final {
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

    struct PartPlan final {
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
    // paint(), once the part's plan is decided and its blend group begun.
    void paint_planned(Painter& painter, const Control& control, const PartPaint& part,
                       const PartPlan& decided) const;
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
