#include "gui_forms/controls/panel/color_value_editor/color_value_editor.hpp"
#include "gui_forms/detail/weak_member_callback.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace gui_forms {

ColorValueEditor::ColorValueEditor(StableId stable_id, Color value)
    : Panel(std::move(stable_id)), value_(value) {
    set_paint_plane(PaintPlane::control);
    set_border_style(BorderStyle::none);
}

void ColorValueEditor::initialize_control_tree() {
    if (editor_) return;
    editor_ = make_control<TextBox>(
        StableId(std::string(stable_id().value()) + ".text"),
        format_value(value_));
    (*editor_).set_font({FontRole::monospace, 9.5, 400, false});
    // Enter commits the text (commit_text) and Escape restores the value
    // (cancel); neither goes further.
    (*editor_).set_accepts_enter(true);
    (*editor_).set_accepts_escape(true);
    add_child(editor_);
    const std::weak_ptr<ColorValueEditor> weak =
        std::static_pointer_cast<ColorValueEditor>(shared_from_this());
    committed_ = (*editor_).committed().subscribe(
        *this, detail::WeakMemberCallback<
            void (ColorValueEditor::*)(const std::string&)>(
                weak, &ColorValueEditor::commit_text));
    cancelled_ = (*editor_).cancelled().subscribe(
        *this, detail::WeakMemberCallback<void (ColorValueEditor::*)()>(
            weak, &ColorValueEditor::cancel));
}

std::string ColorValueEditor::format_value(Color value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result{"#00000000"};
    const std::uint8_t channels[]{value.red, value.green, value.blue, value.alpha};
    for (std::size_t index = 0U; index < 4U; ++index) {
        result[1U + index * 2U] = digits[channels[index] >> 4U];
        result[2U + index * 2U] = digits[channels[index] & 0x0fU];
    }
    return result;
}

std::optional<Color> ColorValueEditor::parse_value(std::string_view text) {
    const std::string_view::size_type first =
        text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::string_view::size_type last =
        text.find_last_not_of(" \t\r\n");
    text = text.substr(first, last - first + 1U);
    if ((text.size() != 7U && text.size() != 9U) || text.front() != '#') {
        return {};
    }
    std::uint8_t channels[4]{0U, 0U, 0U, 255U};
    const std::size_t count = text.size() == 9U ? 4U : 3U;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::optional<std::uint8_t> high =
            parse_nibble(text[1U + index * 2U]);
        const std::optional<std::uint8_t> low =
            parse_nibble(text[2U + index * 2U]);
        if (!high || !low) return {};
        channels[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return Color::rgba(channels[0], channels[1], channels[2], channels[3]);
}

std::optional<std::uint8_t> ColorValueEditor::parse_nibble(
    char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    return {};
}

void ColorValueEditor::commit_text(const std::string& text) {
    commit(text);
}

void ColorValueEditor::set_value(Color value) {
    require_mutable();
    if (value_ == value) {
        if (editor_ && (*editor_).text() != format_value(value)) {
            synchronizing_ = true;
            (*editor_).set_text(format_value(value));
            synchronizing_ = false;
        }
        if (editor_) (*editor_).set_visual_status(ControlVisualStatus::normal);
        set_visual_status(ControlVisualStatus::normal);
        invalidate(Dirty::paint | Dirty::semantics);
        return;
    }
    value_ = value;
    if (editor_) {
        synchronizing_ = true;
        (*editor_).set_text(format_value(value_));
        (*editor_).set_visual_status(ControlVisualStatus::normal);
        synchronizing_ = false;
    }
    set_visual_status(ControlVisualStatus::normal);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ColorValueEditor::set_swatch_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width < 12.0 || width > 96.0) {
        throw std::invalid_argument(
            "color editor swatch width must be within [12, 96]");
    }
    if (swatch_width_ == width) return;
    swatch_width_ = width;
    invalidate(Dirty::layout | Dirty::paint | Dirty::hit_test);
}

void ColorValueEditor::commit(std::string_view text) {
    if (synchronizing_) return;
    const std::optional<Color> parsed = parse_value(text);
    if (!parsed) {
        set_visual_status(ControlVisualStatus::invalid);
        if (editor_) (*editor_).set_visual_status(ControlVisualStatus::invalid);
        const PropertyEditorInputError failure{
            std::string(text), "Color must be #RRGGBB or #RRGGBBAA"};
        edit_failed_.emit(failure);
        return;
    }
    const bool changed = *parsed != value_;
    value_ = *parsed;
    set_visual_status(ControlVisualStatus::normal);
    if (editor_) {
        synchronizing_ = true;
        (*editor_).set_text(format_value(value_));
        (*editor_).set_visual_status(ControlVisualStatus::normal);
        synchronizing_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(value_changed_, value_);
}

void ColorValueEditor::cancel() {
    if (!editor_) return;
    synchronizing_ = true;
    (*editor_).set_text(format_value(value_));
    (*editor_).set_visual_status(ControlVisualStatus::normal);
    synchronizing_ = false;
    set_visual_status(ControlVisualStatus::normal);
    invalidate(Dirty::paint | Dirty::semantics);
}

Size ColorValueEditor::measure(Size available) {
    return {std::max(0.0, available.width), 28.0 * effective_text_scale()};
}

void ColorValueEditor::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const double swatch = std::min(
        swatch_width_, std::max(0.0, final_bounds.width));
    if (editor_) {
        set_child_layout(editor_, {swatch, 0.0,
            std::max(0.0, final_bounds.width - swatch), final_bounds.height});
    }
}

// The swatch is the value's colour as content (owner decision W-7) over a
// checkerboard that shows its transparency, inside a well that frames it.
// The checkerboard's squares are content colours too: they are the
// backdrop the value is judged against, the same in every theme.
void ColorValueEditor::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const double swatch_width = std::min(swatch_width_, bounds.width);
    const Rect swatch{2.0, 2.0, std::max(0.0, swatch_width - 5.0),
                      std::max(0.0, bounds.height - 4.0)};
    const AnatomyStateSet states = anatomy_states();
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.states = states;
    well.bounds = swatch;
    well.corner_radius = 0.0;
    paint_part(painter, *this, well);
    const Rect inside{swatch.x + 1.0, swatch.y + 1.0, std::max(0.0, swatch.width - 2.0),
                      std::max(0.0, swatch.height - 2.0)};
    const double half_width = inside.width * 0.5;
    const double half_height = inside.height * 0.5;
    const Color light = Color::rgba(232, 232, 232);
    const Color dark = Color::rgba(178, 178, 178);
    const std::array<Rect, 4> squares{{
        {inside.x, inside.y, half_width, half_height},
        {inside.x + half_width, inside.y, half_width, half_height},
        {inside.x, inside.y + half_height, half_width, half_height},
        {inside.x + half_width, inside.y + half_height, half_width, half_height},
    }};
    const std::array<Color, 4> shades{{light, dark, dark, light}};
    PartPaint fill;
    fill.family = ControlVisualRole::editor;
    fill.role = PartRole::fill;
    fill.states = states;
    for (std::size_t index = 0U; index < squares.size(); ++index) {
        fill.bounds = squares[index];
        fill.content_color = shades[index];
        paint_part(painter, *this, fill);
    }
    fill.bounds = inside;
    fill.content_color = value_;
    paint_part(painter, *this, fill);
}

SemanticDescriptor ColorValueEditor::semantic_descriptor() const {
    SemanticDescriptor result;
    result.role = SemanticRole::group;
    result.name = accessible_name();
    result.value = format_value(value_);
    result.description = accessible_description();
    result.exposed = true;
    if (visual_status() == ControlVisualStatus::invalid) {
        result.states |= SemanticState::invalid;
    }
    return result;
}

void ColorValueEditor::on_dispose() noexcept {
    committed_.disconnect();
    cancelled_.disconnect();
    editor_.reset();
    Control::on_dispose();
}


namespace {

constexpr std::array<PartField<ColorValueEditor::Parts>, 2> color_value_editor_extra_part_fields{{
    {.declaration = {.name = "swatch", .role = PartRole::chip, .composed_from = {}},
     .member = &ColorValueEditor::Parts::swatch},
    {.declaration = {.name = "field", .role = PartRole::well, .composed_from = "text_box"},
     .member = &ColorValueEditor::Parts::field},
}};

constexpr std::array<StateField<ColorValueEditor::States>, 9> color_value_editor_state_fields{{
    {.state = AnatomyState::normal, .member = &ColorValueEditor::States::normal},
    {.state = AnatomyState::pressed, .member = &ColorValueEditor::States::pressed},
    {.state = AnatomyState::captured, .member = &ColorValueEditor::States::captured},
    {.state = AnatomyState::enabled, .member = &ColorValueEditor::States::enabled},
    {.state = AnatomyState::disabled, .member = &ColorValueEditor::States::disabled},
    {.state = AnatomyState::focused, .member = &ColorValueEditor::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ColorValueEditor::States::focus_visible},
    {.state = AnatomyState::invalid, .member = &ColorValueEditor::States::invalid},
    {.state = AnatomyState::pending, .member = &ColorValueEditor::States::pending},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 2>
    color_value_editor_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(color_value_editor_extra_part_fields));
constexpr std::array<AnatomyState, 9> color_value_editor_states =
    state_declarations(color_value_editor_state_fields);

constexpr ControlAnatomy color_value_editor_anatomy{
    .control_id = "color_value_editor",
    .parts = color_value_editor_parts,
    .states = color_value_editor_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ColorValueEditor::anatomy() const noexcept {
    return color_value_editor_anatomy;
}

void ColorValueEditor::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = local_bounds();
    const double swatch_width = std::min(swatch_width_, bounds.width);
    const Rect swatch{2.0, 2.0, std::max(0.0, swatch_width - 5.0),
                      std::max(0.0, bounds.height - 4.0)};
    Parts record;
    record.scrollable = scrollable_parts();
    record.swatch = PartGeometry::from_bounds(swatch);
    if (editor_) {
        record.field = PartGeometry::from_bounds((*editor_).committed_arranged_bounds());
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), color_value_editor_extra_part_fields, record, geometry);
}

ColorValueEditor::Parts ColorValueEditor::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), color_value_editor_extra_part_fields, published.parts(), record);
    return record;
}

AnatomyStateSet ColorValueEditor::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (visual_status() == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (visual_status() == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    }
    return states;
}

ColorValueEditor::States ColorValueEditor::states() const {
    States record;
    read_states(color_value_editor_state_fields, anatomy_states(), record);
    return record;
}

namespace {

// The events ColorValueEditor adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> color_value_editor_interactions{{
    {.name = "value_changed", .payload = "Color", .mutable_payload = false},
    {.name = "edit_failed", .payload = "PropertyEditorInputError", .mutable_payload = false},
}};

} // namespace

void ColorValueEditor::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), color_value_editor_interactions.begin(), color_value_editor_interactions.end());
}

} // namespace gui_forms
