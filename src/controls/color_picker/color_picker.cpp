#include "gui_forms/controls/color_picker.hpp"

#include "color_picker_gradients.hpp"
#include "gui_forms/controls/color_value_editor.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The checkerboard's square in logical units.
constexpr double checker_cell = 5.0;
// The field thumb's side, and the track thumbs' height beyond the track.
constexpr double field_thumb_extent = 10.0;
constexpr double track_thumb_height = 6.0;
constexpr double track_thumb_overhang = 2.0;
// The keyboard steps.
constexpr double fine_step = 0.01;
constexpr double coarse_step = 0.1;
constexpr double hue_step = 5.0;
constexpr double hue_coarse_step = 30.0;
constexpr int alpha_step = 16;

double clamp_unit(double value) noexcept {
    const double result = std::clamp(value, 0.0, 1.0);
    return result;
}

// A hue in [0, 360): 360 is 0, and steps past either end wrap.
double wrap_hue(double hue) noexcept {
    double wrapped = std::fmod(hue, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    if (wrapped >= 360.0) wrapped = 0.0;
    return wrapped;
}

std::uint8_t channel(double unit) noexcept {
    const double scaled = clamp_unit(unit) * 255.0;
    const std::uint8_t result = static_cast<std::uint8_t>(std::lround(scaled));
    return result;
}

constexpr std::array<AnatomyState, 2> color_picker_thumb_states{{
    AnatomyState::pressed, AnatomyState::captured}};

constexpr std::array<PartField<ColorPicker::Parts>, 9> color_picker_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ColorPicker::Parts::region},
    {.declaration = {.name = "field", .role = PartRole::well, .composed_from = {}},
     .member = &ColorPicker::Parts::field},
    {.declaration = {.name = "field_thumb", .role = PartRole::thumb, .composed_from = {},
                     .states = color_picker_thumb_states},
     .member = &ColorPicker::Parts::field_thumb},
    {.declaration = {.name = "hue_track", .role = PartRole::track, .composed_from = {}},
     .member = &ColorPicker::Parts::hue_track},
    {.declaration = {.name = "hue_thumb", .role = PartRole::thumb, .composed_from = {},
                     .states = color_picker_thumb_states},
     .member = &ColorPicker::Parts::hue_thumb},
    {.declaration = {.name = "alpha_track", .role = PartRole::track, .composed_from = {}},
     .member = &ColorPicker::Parts::alpha_track},
    {.declaration = {.name = "alpha_thumb", .role = PartRole::thumb, .composed_from = {},
                     .states = color_picker_thumb_states},
     .member = &ColorPicker::Parts::alpha_thumb},
    {.declaration = {.name = "swatch", .role = PartRole::chip, .composed_from = {}},
     .member = &ColorPicker::Parts::swatch},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ColorPicker::Parts::focus},
}};

constexpr std::array<StateField<ColorPicker::States>, 7> color_picker_state_fields{{
    {.state = AnatomyState::normal, .member = &ColorPicker::States::normal},
    {.state = AnatomyState::pressed, .member = &ColorPicker::States::pressed},
    {.state = AnatomyState::captured, .member = &ColorPicker::States::captured},
    {.state = AnatomyState::enabled, .member = &ColorPicker::States::enabled},
    {.state = AnatomyState::disabled, .member = &ColorPicker::States::disabled},
    {.state = AnatomyState::focused, .member = &ColorPicker::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ColorPicker::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 9> color_picker_parts = part_declarations(color_picker_part_fields);
constexpr std::array<AnatomyState, 7> color_picker_states = state_declarations(color_picker_state_fields);

constexpr ControlAnatomy color_picker_anatomy{
    .control_id = "color_picker",
    .parts = color_picker_parts,
    .states = color_picker_states,
    .custom_states = {},
};

// The events ColorPicker adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> color_picker_interactions{{
    {.name = "value_changed", .payload = "Color", .mutable_payload = false},
    {.name = "value_committed", .payload = "Color", .mutable_payload = false},
}};

} // namespace

ColorPicker::ColorPicker(StableId stable_id, Color initial_value) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::hand;
    value = initial_value;
    // The write above runs written only when the colour differs from the
    // cell's initial white; place the thumbs either way.
    hsv_ = hsv_of(value.read(), ColorPickerHsv{});
}

// -- colour model ---------------------------------------------------------------

Color ColorPicker::color_of(ColorPickerHsv hsv, std::uint8_t alpha) noexcept {
    const double hue = wrap_hue(hsv.hue);
    const double saturation = clamp_unit(hsv.saturation);
    const double brightness = clamp_unit(hsv.brightness);
    const double chroma = brightness * saturation;
    const double sector = hue / 60.0;
    const double second = chroma * (1.0 - std::fabs(std::fmod(sector, 2.0) - 1.0));
    const double base = brightness - chroma;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    switch (static_cast<int>(std::floor(sector))) {
    case 0: red = chroma; green = second; break;
    case 1: red = second; green = chroma; break;
    case 2: green = chroma; blue = second; break;
    case 3: green = second; blue = chroma; break;
    case 4: red = second; blue = chroma; break;
    default: red = chroma; blue = second; break;
    }
    const Color result = Color::rgba(channel(red + base), channel(green + base), channel(blue + base), alpha);
    return result;
}

ColorPickerHsv ColorPicker::hsv_of(Color colour, ColorPickerHsv previous) noexcept {
    const double red = static_cast<double>(colour.red) / 255.0;
    const double green = static_cast<double>(colour.green) / 255.0;
    const double blue = static_cast<double>(colour.blue) / 255.0;
    const double largest = std::max({red, green, blue});
    const double smallest = std::min({red, green, blue});
    const double chroma = largest - smallest;
    ColorPickerHsv result = previous;
    result.brightness = largest;
    // Black keeps the saturation and the hue; a grey keeps the hue.
    if (largest <= 0.0) return result;
    result.saturation = chroma / largest;
    if (chroma <= 0.0) return result;
    double hue = 0.0;
    if (largest == red) {
        hue = 60.0 * std::fmod((green - blue) / chroma, 6.0);
    } else if (largest == green) {
        hue = 60.0 * ((blue - red) / chroma + 2.0);
    } else {
        hue = 60.0 * ((red - green) / chroma + 4.0);
    }
    result.hue = wrap_hue(hue);
    return result;
}

void ColorPicker::set_hsv(ColorPickerHsv incoming) {
    require_mutable();
    if (!std::isfinite(incoming.hue) || !std::isfinite(incoming.saturation) ||
        !std::isfinite(incoming.brightness)) {
        throw std::invalid_argument("ColorPicker HSV components must be finite");
    }
    if (incoming.hue < 0.0 || incoming.hue > 360.0 || incoming.saturation < 0.0 || incoming.saturation > 1.0 ||
        incoming.brightness < 0.0 || incoming.brightness > 1.0) {
        throw std::invalid_argument("ColorPicker HSV components are out of range");
    }
    incoming.hue = wrap_hue(incoming.hue);
    apply(incoming, value.read().alpha);
}

void ColorPicker::apply(ColorPickerHsv next, std::uint8_t alpha) {
    const bool moved = !(next == hsv_);
    hsv_ = next;
    picking_ = true;
    try {
        value = color_of(next, alpha);
    } catch (...) {
        picking_ = false;
        throw;
    }
    picking_ = false;
    // An HSV move that lands on the same colour (a hue change of a grey)
    // still moves a thumb.
    if (moved) invalidate(Dirty::paint | Dirty::semantics);
}

// -- layout ---------------------------------------------------------------------

Size ColorPicker::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size preferred{requested.width > 0.0 ? requested.width : 220.0,
                         requested.height > 0.0 ? requested.height : 180.0};
    const Size result{std::min(std::max(0.0, available.width), preferred.width),
                      std::min(std::max(0.0, available.height), preferred.height)};
    return result;
}

ColorPicker::Layout ColorPicker::layout_parts() const noexcept {
    const Rect bounds = local_bounds();
    const double tracks = alpha_enabled.read() ? 2.0 : 1.0;
    const double inner_width = std::max(0.0, bounds.width - inset * 2.0);
    const double inner_height = std::max(0.0, bounds.height - inset * 2.0);
    const double top_height = std::max(0.0, inner_height - swatch_height - part_gap);
    const double field_width = std::max(0.0, inner_width - tracks * (track_width + part_gap));
    Layout layout{};
    layout.field = Rect{inset, inset, field_width, top_height};
    layout.hue_track = Rect{inset + field_width + part_gap, inset, track_width, top_height};
    if (alpha_enabled.read()) {
        layout.alpha_track = Rect{layout.hue_track.x + track_width + part_gap, inset, track_width, top_height};
    }
    layout.swatch = Rect{inset, inset + top_height + part_gap, inner_width, std::min(swatch_height, inner_height)};
    return layout;
}

Rect ColorPicker::field_thumb_bounds(const Layout& layout) const noexcept {
    const double x = layout.field.x + hsv_.saturation * layout.field.width;
    const double y = layout.field.y + (1.0 - hsv_.brightness) * layout.field.height;
    const double half = field_thumb_extent * 0.5;
    const Rect result{x - half, y - half, field_thumb_extent, field_thumb_extent};
    return result;
}

Rect ColorPicker::hue_thumb_bounds(const Layout& layout) const noexcept {
    const double y = layout.hue_track.y + hsv_.hue / 360.0 * layout.hue_track.height;
    const Rect result{layout.hue_track.x - track_thumb_overhang, y - track_thumb_height * 0.5,
                      layout.hue_track.width + track_thumb_overhang * 2.0, track_thumb_height};
    return result;
}

Rect ColorPicker::alpha_thumb_bounds(const Layout& layout) const noexcept {
    if (!alpha_enabled.read()) return {};
    const double opacity = static_cast<double>(value.read().alpha) / 255.0;
    const double y = layout.alpha_track.y + (1.0 - opacity) * layout.alpha_track.height;
    const Rect result{layout.alpha_track.x - track_thumb_overhang, y - track_thumb_height * 0.5,
                      layout.alpha_track.width + track_thumb_overhang * 2.0, track_thumb_height};
    return result;
}

ColorPickerPart ColorPicker::part_at(const Layout& layout, Point local) const noexcept {
    if (layout.field.contains(local)) return ColorPickerPart::field;
    if (layout.hue_track.contains(local)) return ColorPickerPart::hue;
    if (alpha_enabled.read() && layout.alpha_track.contains(local)) return ColorPickerPart::alpha;
    return ColorPickerPart::none;
}

// -- input ----------------------------------------------------------------------

void ColorPicker::pick(ColorPickerPart part, Point local) {
    const Layout layout = layout_parts();
    ColorPickerHsv next = hsv_;
    std::uint8_t alpha = value.read().alpha;
    switch (part) {
    case ColorPickerPart::field: {
        const double width = std::max(1.0, layout.field.width);
        const double height = std::max(1.0, layout.field.height);
        next.saturation = clamp_unit((local.x - layout.field.x) / width);
        next.brightness = 1.0 - clamp_unit((local.y - layout.field.y) / height);
        break;
    }
    case ColorPickerPart::hue: {
        const double height = std::max(1.0, layout.hue_track.height);
        // The bottom end is 360, which is the top's red; keep it just below.
        next.hue = std::min(clamp_unit((local.y - layout.hue_track.y) / height) * 360.0, 359.999);
        break;
    }
    case ColorPickerPart::alpha: {
        const double height = std::max(1.0, layout.alpha_track.height);
        alpha = channel(1.0 - (local.y - layout.alpha_track.y) / height);
        break;
    }
    default:
        return;
    }
    apply(next, alpha);
}

void ColorPicker::on_mouse_down(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary && enabled_to_use()) {
        const ColorPickerPart part = part_at(layout_parts(), arguments.location);
        if (part != ColorPickerPart::none) {
            dragged_ = part;
            invalidate(Dirty::paint);
            pick(part, arguments.location);
            arguments.handled = true;
        }
    }
    Control::on_mouse_down(arguments);
}

void ColorPicker::on_mouse_move(MouseEventArgs& arguments) {
    if (dragged_ != ColorPickerPart::none) {
        pick(dragged_, arguments.location);
        arguments.handled = true;
    }
    Control::on_mouse_move(arguments);
}

void ColorPicker::on_mouse_up(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary && dragged_ != ColorPickerPart::none) {
        const ColorPickerPart part = dragged_;
        dragged_ = ColorPickerPart::none;
        pick(part, arguments.location);
        invalidate(Dirty::paint);
        arguments.handled = true;
        value_committed_.emit(value.read());
    }
    Control::on_mouse_up(arguments);
}

void ColorPicker::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !enabled_to_use()) return;
    const bool shift = has_modifier(arguments.modifiers, Modifier::shift);
    const double step = shift ? coarse_step : fine_step;
    ColorPickerHsv next = hsv_;
    int alpha = static_cast<int>(value.read().alpha);
    bool recognized = true;
    switch (arguments.physical_key) {
    case PhysicalKey::left:
        next.saturation = clamp_unit(next.saturation - step);
        break;
    case PhysicalKey::right:
        next.saturation = clamp_unit(next.saturation + step);
        break;
    case PhysicalKey::up:
        next.brightness = clamp_unit(next.brightness + step);
        break;
    case PhysicalKey::down:
        next.brightness = clamp_unit(next.brightness - step);
        break;
    case PhysicalKey::page_up:
        next.hue = wrap_hue(next.hue + (shift ? hue_coarse_step : hue_step));
        break;
    case PhysicalKey::page_down:
        next.hue = wrap_hue(next.hue - (shift ? hue_coarse_step : hue_step));
        break;
    case PhysicalKey::home:
        recognized = alpha_enabled.read();
        alpha = shift ? 0 : std::max(0, alpha - alpha_step);
        break;
    case PhysicalKey::end:
        recognized = alpha_enabled.read();
        alpha = shift ? 255 : std::min(255, alpha + alpha_step);
        break;
    default:
        recognized = false;
        break;
    }
    if (!recognized) return;
    apply(next, static_cast<std::uint8_t>(alpha));
    arguments.handled = true;
    value_committed_.emit(value.read());
}

void ColorPicker::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) dragged_ = ColorPickerPart::none;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- paint ----------------------------------------------------------------------

// The wells, thumbs and focus cue are theme parts in the control's states;
// the gradients and the swatch's colour are content (color_picker_gradients).
void ColorPicker::on_paint(Painter& painter, Rect) {
    const Layout layout = layout_parts();
    const AnatomyStateSet states = anatomy_states();
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.states = states;
    well.corner_radius = 0.0;
    well.bounds = layout.field;
    well.key = PartKey{"field", {}, {}};
    paint_part(painter, *this, well);
    color_picker_detail::paint_field(painter, layout.field, hsv_.hue);
    well.bounds = layout.hue_track;
    well.key = PartKey{"hue_track", {}, {}};
    paint_part(painter, *this, well);
    color_picker_detail::paint_hue_track(painter, layout.hue_track);
    if (alpha_enabled.read()) {
        well.bounds = layout.alpha_track;
        well.key = PartKey{"alpha_track", {}, {}};
        paint_part(painter, *this, well);
        color_picker_detail::paint_alpha_track(painter, layout.alpha_track, value.read(), checker_cell);
    }
    well.bounds = layout.swatch;
    well.key = PartKey{"swatch", {}, {}};
    paint_part(painter, *this, well);
    color_picker_detail::paint_swatch(painter, layout.swatch, value.read(), checker_cell);

    PartPaint thumb;
    thumb.family = ControlVisualRole::button;
    thumb.role = PartRole::thumb;
    thumb.states = states;
    thumb.bounds = field_thumb_bounds(layout);
    thumb.key = PartKey{"field_thumb", {}, {}};
    paint_part(painter, *this, thumb);
    thumb.bounds = hue_thumb_bounds(layout);
    thumb.key = PartKey{"hue_thumb", {}, {}};
    paint_part(painter, *this, thumb);
    if (alpha_enabled.read()) {
        thumb.bounds = alpha_thumb_bounds(layout);
        thumb.key = PartKey{"alpha_thumb", {}, {}};
        paint_part(painter, *this, thumb);
    }
    if (focused_) {
        PartPaint focus;
        focus.family = ControlVisualRole::editor;
        focus.role = PartRole::focus;
        focus.states = states;
        focus.states.insert(AnatomyState::focus_visible);
        focus.bounds = layout.field;
        focus.corner_radius = 0.0;
        focus.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, focus);
    }
}

// -- semantics and anatomy -------------------------------------------------------

SemanticDescriptor ColorPicker::semantic_descriptor() const {
    SemanticDescriptor result;
    result.role = SemanticRole::group;
    result.name = accessible_name.read();
    result.description = accessible_description.read();
    result.value = ColorValueEditor::format_value(value.read());
    result.actions = {SemanticAction::focus};
    result.exposed = true;
    return result;
}

const ControlAnatomy& ColorPicker::anatomy() const noexcept {
    return color_picker_anatomy;
}

AnatomyStateSet ColorPicker::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (dragged_ != ColorPickerPart::none) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    return states;
}

void ColorPicker::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Layout layout = layout_parts();
    Parts record;
    record.region = PartGeometry::from_bounds(local_bounds());
    record.field = PartGeometry::from_bounds(layout.field);
    record.field_thumb = PartGeometry::from_bounds(field_thumb_bounds(layout));
    record.hue_track = PartGeometry::from_bounds(layout.hue_track);
    record.hue_thumb = PartGeometry::from_bounds(hue_thumb_bounds(layout));
    record.alpha_track = PartGeometry::from_bounds(layout.alpha_track);
    record.alpha_thumb = PartGeometry::from_bounds(alpha_thumb_bounds(layout));
    record.swatch = PartGeometry::from_bounds(layout.swatch);
    PartGeometry* dragged_thumb = nullptr;
    switch (dragged_) {
    case ColorPickerPart::field: dragged_thumb = &record.field_thumb; break;
    case ColorPickerPart::hue: dragged_thumb = &record.hue_thumb; break;
    case ColorPickerPart::alpha: dragged_thumb = &record.alpha_thumb; break;
    default: break;
    }
    if (dragged_thumb != nullptr) {
        (*dragged_thumb).states.insert(AnatomyState::pressed);
        (*dragged_thumb).states.insert(AnatomyState::captured);
    }
    // The picker shows focus around its field.
    record.focus = PartGeometry::from_bounds(layout.field);
    write_parts(anatomy(), color_picker_part_fields, record, geometry);
}

ColorPicker::Parts ColorPicker::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), color_picker_part_fields, published.parts(), record);
    return record;
}

ColorPicker::States ColorPicker::states() const {
    States record;
    read_states(color_picker_state_fields, anatomy_states(), record);
    return record;
}

void ColorPicker::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), color_picker_interactions.begin(), color_picker_interactions.end());
}

// -- properties -----------------------------------------------------------------

void ColorPicker::prepare_write(const decltype(value)&, Color&) {
    require_mutable();
}

void ColorPicker::written(const decltype(value)&) {
    if (!picking_) hsv_ = hsv_of(value.read(), hsv_);
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(value_changed_, value.read());
}

// property_codemod begin ColorPicker (tools/property_families/color_picker.json; generated, do not edit)

namespace {

PropertyDescriptor color_picker_value_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "value";
    descriptor.kind = BindingValueKind::color;
    descriptor.category = "Data";
    descriptor.description = "The picked colour.";
    descriptor.default_value = BindingValue{Color::rgba(255, 255, 255)};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.binding_direction = PropertyBindingDirection::two_way;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_color_picker_value() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        color_picker_value_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor color_picker_alpha_enabled_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "alpha_enabled";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the alpha track is shown and the alpha can be picked.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_color_picker_alpha_enabled() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        color_picker_alpha_enabled_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ColorPicker::color_picker_property_count>& ColorPicker::color_picker_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 2U> own{{
        {"value", &describe_color_picker_value,
         &read_cell_entry<ColorPicker, decltype(&ColorPicker::value), &ColorPicker::value>,
         &write_cell_entry<ColorPicker, decltype(&ColorPicker::value), &ColorPicker::value>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<ColorPicker, decltype(&ColorPicker::value_changed_), &ColorPicker::value_changed_>},
        {"alpha_enabled", &describe_color_picker_alpha_enabled,
         &read_cell_entry<ColorPicker, decltype(&ColorPicker::alpha_enabled), &ColorPicker::alpha_enabled>,
         &write_cell_entry<ColorPicker, decltype(&ColorPicker::alpha_enabled), &ColorPicker::alpha_enabled>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ColorPicker's property table names a property twice");
    static const std::array<PropertyEntry, color_picker_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ColorPicker::property_table() const noexcept {
    const std::array<PropertyEntry, color_picker_property_count>& entries = color_picker_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ColorPicker::prepare_write(const decltype(alpha_enabled)&, bool&) {
    require_mutable();
}

void ColorPicker::written(const decltype(alpha_enabled)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end ColorPicker

} // namespace gui_forms
