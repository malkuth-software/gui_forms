#include "gui_forms/controls/button_base/link_label/link_label.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

LinkLabel::LinkLabel(StableId stable_id, std::string text)
    : ButtonBase(std::move(stable_id), std::move(text)) {}

void LinkLabel::set_visited(bool visited_value) {
    require_mutable();
    if (visited_ == visited_value) {
        return;
    }
    visited_ = visited_value;
    invalidate(Dirty::paint | Dirty::semantics);
}

namespace {

constexpr std::array<PartField<LinkLabel::Parts>, 4> link_label_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &LinkLabel::Parts::region},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &LinkLabel::Parts::label},
    {.declaration = {.name = "underline", .role = PartRole::glyph, .composed_from = {}},
     .member = &LinkLabel::Parts::underline},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &LinkLabel::Parts::focus},
}};

constexpr std::array<StateField<LinkLabel::States>, 9> link_label_state_fields{{
    {.state = AnatomyState::normal, .member = &LinkLabel::States::normal},
    {.state = AnatomyState::hot, .member = &LinkLabel::States::hot},
    {.state = AnatomyState::pressed, .member = &LinkLabel::States::pressed},
    {.state = AnatomyState::enabled, .member = &LinkLabel::States::enabled},
    {.state = AnatomyState::disabled, .member = &LinkLabel::States::disabled},
    {.state = AnatomyState::focused, .member = &LinkLabel::States::focused},
    {.state = AnatomyState::focus_visible, .member = &LinkLabel::States::focus_visible},
    {.state = AnatomyState::open, .member = &LinkLabel::States::open},
    {.state = AnatomyState::closed, .member = &LinkLabel::States::closed},
}};

constexpr std::array<PartDeclaration, 4> link_label_parts =
    part_declarations(link_label_part_fields);
constexpr std::array<AnatomyState, 9> link_label_states =
    state_declarations(link_label_state_fields);

// "visited" has no counterpart in the design language's state set, so the
// link declares it as its own finite state.
constexpr std::array<CustomStateDeclaration, 1> link_label_custom_states{{
    {.name = "visited"},
}};

constexpr CustomStateIndex visited_state{0U};

constexpr ControlAnatomy link_label_anatomy{
    .control_id = "link_label",
    .parts = link_label_parts,
    .states = link_label_states,
    .custom_states = link_label_custom_states,
};

} // namespace

const ControlAnatomy& LinkLabel::anatomy() const noexcept {
    return link_label_anatomy;
}

double LinkLabel::text_baseline() const noexcept {
    const Rect bounds = local_bounds();
    const double baseline = std::max(font.read().size,
        (bounds.height + font.read().size) * 0.5 - 1.0);
    return baseline;
}

// The text's measured advance: the window's text metrics, which the host
// sets to its raster painter's, so the underline ends where the drawn text
// does (sweep defect D5: the width estimate ran past the text). A detached
// label estimates.
double LinkLabel::underline_width() const {
    const Rect bounds = local_bounds();
    const std::string text = display_text();
    const ResolvedTextLayout measured = window() != nullptr
        ? (*window()).resolve_text_layout_utf8(text, font.read())
        : estimate_text_layout_utf8(text, font.read());
    const double width = std::min(bounds.width - 4.0, measured.logical_size.width);
    return std::max(0.0, width);
}

void LinkLabel::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const double baseline = text_baseline();
    const double width = underline_width();
    // The label spans one font size above the baseline; the underline is the
    // one-unit stroke two units below it.
    const Rect label{2.0, baseline - font.read().size, width, font.read().size};
    const Rect underline{2.0, baseline + 1.5, width, 1.0};
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.label = PartGeometry::from_bounds(label);
    record.underline = PartGeometry::from_bounds(underline);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), link_label_part_fields, record, geometry);
}

AnatomyStateSet LinkLabel::anatomy_states() const {
    AnatomyStateSet states = button_anatomy_states();
    if (visited_) {
        states.insert(visited_state);
    }
    return states;
}

LinkLabel::Parts LinkLabel::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), link_label_part_fields, published.parts(), record);
    return record;
}

LinkLabel::States LinkLabel::states() const {
    States record;
    const AnatomyStateSet active = anatomy_states();
    read_states(link_label_state_fields, active, record);
    record.visited = active.contains(visited_state);
    return record;
}

// The text and underline take the link family's text colour (owner
// decision W-Q5): `visited_link` once visited, else `link`, in the state
// the control's anatomy states select. A compatibility style set on the
// control (set_style) keeps its own link colours outside high contrast, as
// a Button's recipe override does. The underline part is the glyph role's
// underline mark (owner decision, 2026-10-28), drawn in the text's colour.
void LinkLabel::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const AnatomyStateSet states = anatomy_states();
    const ControlVisualContext context = part_visual_context(*this, states);
    const ControlVisualRole family = visited_ ? ControlVisualRole::visited_link : ControlVisualRole::link;
    ControlVisualRecipe recipe = effective_theme().resolve(family, context);
    if (has_style_override() && !context.high_contrast) {
        const BasicControlStyle& colors = style();
        // A disabled link keeps the theme's disabled text (T4-1).
        if (enabled.read()) recipe.text = visited_ ? colors.visited_link : colors.link;
    }
    const Color foreground = recipe.text;
    const double baseline = text_baseline();
    // The label part centres its line box on its bounds, so the bounds are
    // that box, placed on the baseline. The text and the underline are
    // drawn in the link's colour as authored colour (T4-1): a disabled link
    // takes the theme's disabled ink.
    const std::string display = display_text();
    const FontSpec text_font = font.read();
    const Size measured = painter.measure_text_utf8(display, text_font);
    const double box_height = std::max(measured.height, text_font.size * 1.2);
    const double box_ascent = std::max(text_font.size, box_height * 0.82);
    PartPaint label;
    label.role = PartRole::label;
    label.family = family;
    label.states = states;
    label.text = display;
    label.font = text_font;
    label.bounds = {2.0, baseline - box_ascent, std::max(measured.width, bounds.width - 2.0), box_height};
    label.authored_foreground = foreground;
    paint_part(painter, *this, label);
    PartPaint underline;
    underline.role = PartRole::glyph;
    underline.family = family;
    underline.mark = PartMark::underline;
    underline.states = states;
    underline.bounds = {2.0, baseline + 1.5, underline_width(), 1.0};
    underline.authored_foreground = foreground;
    paint_part(painter, *this, underline);
    if (focus_cue_visible()) {
        PartPaint ring;
        ring.role = PartRole::focus;
        ring.family = family;
        ring.states = states;
        ring.states.insert(AnatomyState::focus_visible);
        ring.bounds = bounds;
        paint_part(painter, *this, ring);
    }
}

void LinkLabel::on_activate() {
    set_visited(true);
    if (is_alive()) {
        ButtonBase::on_activate();
    }
}

SemanticDescriptor LinkLabel::semantic_descriptor() const {
    SemanticDescriptor descriptor = ButtonBase::semantic_descriptor();
    descriptor.role = SemanticRole::link;
    descriptor.value = visited_ ? "visited" : "unvisited";
    return descriptor;
}

} // namespace gui_forms
