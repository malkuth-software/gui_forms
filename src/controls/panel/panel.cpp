#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "../basic/basic_control_rendering.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

std::shared_ptr<const PropertyEnumDescriptor> border_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "border_style", {{"none", 0}, {"line", 1}, {"sunken", 2}, {"raised", 3}}, false});
    return value;
}

} // namespace

Panel::Panel(StableId stable_id) : ScrollableControl(std::move(stable_id)) {
    set_paint_plane(PaintPlane::backplane);
    PropertyDescriptor border_style;
    border_style.name = "border_style";
    border_style.kind = BindingValueKind::enumeration;
    border_style.category = "Appearance";
    // A property grid starts with a line, so the descriptor states no default.
    border_style.description = "The panel's border: none, a line, sunken or raised.";
    border_style.invalidation_effects = Dirty::paint;
    border_style.bindable = false;
    border_style.enumeration = border_style_enum();
    define_bindable_property({
        std::move(border_style),
        detail::EnumerationPropertyGetter<Panel, BorderStyle>(*this, &Panel::border_style, border_style_enum()),
        detail::EnumerationPropertySetter<Panel, BorderStyle>(*this, &Panel::set_border_style), {}, {}, {}});
}

std::optional<Color> Panel::face_colour() const noexcept {
    if (background_override_) return background_override_;
    const std::optional<Color> own = back_color.read();
    if (!own) return std::nullopt;
    const Window* const attached = window();
    const bool high_contrast = attached != nullptr && (*attached).presentation_settings().high_contrast;
    if (high_contrast) return std::nullopt;
    return own;
}

void Panel::set_border_style(BorderStyle style) {
    require_mutable();
    switch (style) {
    case BorderStyle::none:
    case BorderStyle::line:
    case BorderStyle::sunken:
    case BorderStyle::raised:
        break;
    default:
        throw std::invalid_argument("Panel border style is invalid");
    }
    if (border_style_ == style) {
        return;
    }
    border_style_ = style;
    invalidate(Dirty::paint | Dirty::semantics);
}

void Panel::set_background(Color color) {
    require_mutable();
    if (background_override_ && *background_override_ == color) return;
    background_override_ = color;
    invalidate(Dirty::paint);
}

Color Panel::background() const noexcept {
    const std::optional<Color> face = face_colour();
    if (face) return *face;
    const ControlVisualRecipe& recipe = effective_theme().resolve(
        visual_role_, visual_context());
    const MaterialFillLayer& fill = recipe.material.fills.front();
    switch (fill.kind) {
    case MaterialFillKind::solid:
        return fill.color;
    case MaterialFillKind::linear_gradient:
    case MaterialFillKind::radial_gradient:
    case MaterialFillKind::conic_gradient:
        if (fill.stops.empty()) return fill.color;
        return fill.stops.front().color;
    case MaterialFillKind::image:
        // An image has no one colour: the layer's colour, as before any
        // image is drawn. (Reading stops.front() here was undefined: an
        // image fill has no stops.)
        return fill.color;
    case MaterialFillKind::procedural:
        return fill.procedural.ground;
    }
    // A kind outside the enumeration: the layer's colour.
    return fill.color;
}

void Panel::clear_background() {
    require_mutable();
    if (!background_override_) return;
    background_override_.reset();
    invalidate(Dirty::style | Dirty::paint);
}

void Panel::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_override_ && *style_override_ == style) return;
    style_override_ = std::move(style);
    invalidate(Dirty::paint | Dirty::semantics);
}

const BasicControlStyle& Panel::style() const noexcept {
    return style_override_ ? *style_override_ : effective_theme().basic_style();
}

void Panel::clear_style() {
    require_mutable();
    if (!style_override_) return;
    style_override_.reset();
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

void Panel::set_visual_role(ControlVisualRole role) {
    require_mutable();
    if (role != ControlVisualRole::window && role != ControlVisualRole::panel &&
        role != ControlVisualRole::card) {
        throw std::invalid_argument(
            "panel visual role must be window, panel, or card");
    }
    if (visual_role_ == role) return;
    visual_role_ = role;
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

Rect Panel::local_bounds() const noexcept {
    const Rect arranged = committed_arranged_bounds();
    return {0.0, 0.0, arranged.width, arranged.height};
}

// The panel's face is a part the theme's engine draws in the panel's
// visual role. A sunken border makes the face the editor's well and a raised
// one the button's face; a line border is four keylines around the face. A
// background or style override supplies the face's ground (T4-1).
void Panel::paint_panel(Painter& painter, Rect bounds) const {
    PartPaint face;
    face.family = visual_role_;
    face.role = PartRole::face;
    face.bounds = bounds;
    if (border_style_ == BorderStyle::sunken) {
        face.family = ControlVisualRole::editor;
        face.role = PartRole::well;
    } else if (border_style_ == BorderStyle::raised) {
        face.family = ControlVisualRole::button;
    }
    // The panel's own colour (face_colour) or a style override supplies the
    // face's ground.
    const std::optional<Color> face_ground = face_colour();
    if (face_ground) face.authored_background = *face_ground;
    if (!face_ground && style_override_) face.authored_background = background();
    paint_part(painter, *this, face);
    if (border_style_ != BorderStyle::line) return;
    PartPaint rule;
    rule.family = visual_role_;
    rule.role = PartRole::keyline;
    const double right = std::max(0.0, bounds.width - 1.0);
    const double bottom = std::max(0.0, bounds.height - 1.0);
    const std::array<Rect, 4> sides{{
        {0.0, 0.0, bounds.width, 1.0},
        {0.0, bottom, bounds.width, 1.0},
        {0.0, 0.0, 1.0, bounds.height},
        {right, 0.0, 1.0, bounds.height},
    }};
    for (const Rect& side : sides) {
        rule.bounds = side;
        paint_part(painter, *this, rule);
    }
}

bool Panel::paint_depends_on_size() const noexcept {
    if (border_style_ != BorderStyle::none || hscroll() || vscroll()) return true;
    if (background_override_ || style_override_) return false;
    const bool invariant = surface_material_size_invariant(
        effective_theme().resolve(visual_role_, visual_context()).material);
    return !invariant;
}

// An authored background keeps the material's border and shadows (T4-1),
// so the outsets are the material's either way.
Insets Panel::visual_outsets() const noexcept {
    return surface_material_visual_outsets(
        effective_theme().resolve(visual_role_, visual_context()).material);
}

void Panel::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    paint_panel(painter, bounds);
    // The face paint_panel drew first: an opaque background colour, or an
    // opaque material without outsets. The border draws over it.
    const bool overridden = face_colour() || style_override_;
    const bool opaque = overridden
        ? background().alpha == 255U
        : surface_material_opaque(effective_theme().resolve(visual_role_, visual_context()).material);
    // A panel its theme declares clear draws its face from the theme's
    // transparent background and covers nothing; an overridden face is the
    // panel's own fill, reported (report_opaque_cover warns of it).
    const bool themed_clear = !overridden && declares_clarity();
    if (opaque && !themed_clear) report_opaque_cover(painter, bounds);
}

SemanticDescriptor Panel::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() ||
                         !descriptor.description.empty();
    return descriptor;
}


namespace {

constexpr ControlAnatomy panel_anatomy{
    .control_id = "panel",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& Panel::anatomy() const noexcept {
    return panel_anatomy;
}

Panel::Parts Panel::panel_parts() const {
    const Rect bounds = local_bounds();
    Parts record;
    record.scrollable = scrollable_parts();
    record.face = PartGeometry::from_bounds(bounds);
    if (border_style_ != BorderStyle::none) {
        record.edge = PartGeometry::from_bounds(bounds);
    }
    return record;
}

void Panel::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = panel_parts();
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), panel_detail::extra_fields, record, geometry);
}

Panel::Parts Panel::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), panel_detail::extra_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
