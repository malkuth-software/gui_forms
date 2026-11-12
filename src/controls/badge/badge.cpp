#include "gui_forms/controls/badge.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// The BadgeMode enumeration, numbered as declared. Static storage; any
// thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> badge_mode_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "badge_mode", {{"count", 0}, {"dot", 1}}, false});
    return value;
}

// The chip's metrics, in logical units: the space between the text and the
// pill's ends and edges, and a dot's side.
constexpr double chip_inline_padding = 5.0;
constexpr double chip_block_padding = 1.0;
constexpr double dot_side = 8.0;

constexpr std::array<PartField<Badge::Parts>, 3> badge_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Badge::Parts::region},
    {.declaration = {.name = "chip", .role = PartRole::chip, .composed_from = {}},
     .member = &Badge::Parts::chip},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &Badge::Parts::label},
}};

constexpr std::array<PartDeclaration, 3> badge_parts = part_declarations(badge_part_fields);

constexpr std::array<AnatomyState, 5> badge_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled, AnatomyState::focused,
    AnatomyState::focus_visible,
};

// "dot" names dot mode for a theme's selectors (`:family(badge):state(dot)`).
constexpr std::array<CustomStateDeclaration, 1> badge_custom_states{{
    {.name = "dot"},
}};
constexpr CustomStateIndex dot_state{0U};

constexpr ControlAnatomy badge_anatomy{
    .control_id = "badge",
    .parts = badge_parts,
    .states = badge_states,
    .custom_states = badge_custom_states,
};

} // namespace

Badge::Badge(StableId stable_id) : Control(std::move(stable_id)) {}

std::string Badge::display_text() const {
    if (!showing() || mode.read() == BadgeMode::dot) return std::string{};
    const std::uint32_t shown = count.read();
    const std::uint32_t limit = maximum.read();
    if (shown > limit) {
        std::string capped = std::to_string(limit);
        capped.push_back('+');
        return capped;
    }
    std::string text = std::to_string(shown);
    return text;
}

bool Badge::showing() const noexcept {
    if (mode.read() == BadgeMode::dot) return true;
    const bool shown = count.read() != 0U || show_zero.read();
    return shown;
}

FontSpec Badge::chip_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.caption);
    return font;
}

Size Badge::chip_size(const std::string& shown) const {
    if (!showing()) return Size{};
    if (mode.read() == BadgeMode::dot) return Size{dot_side, dot_side};
    const ResolvedTextLayout layout = resolve_text_layout_utf8(shown, chip_font());
    const double height = layout.logical_size.height + 2.0 * chip_block_padding;
    const double width = std::max(height, layout.logical_size.width + 2.0 * chip_inline_padding);
    const Size size{std::ceil(width), std::ceil(height)};
    return size;
}

Rect Badge::chip_bounds(const std::string& shown) const {
    const Rect client = client_rectangle();
    const Size size = chip_size(shown);
    const double top = client.y + std::max(0.0, (client.height - size.height) * 0.5);
    const Rect placed{client.x, std::floor(top), std::min(size.width, client.width), std::min(size.height, client.height)};
    return placed;
}

// The chip's size, or the requested size where one is set; a hidden badge
// measures empty, so a layout closes up around it.
Size Badge::measure(Size available) {
    const Rect requested = requested_bounds();
    const Size chip = chip_size(display_text());
    const double width = requested.width > 0.0 ? requested.width : chip.width;
    const double height = requested.height > 0.0 ? requested.height : chip.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void Badge::on_paint(Painter& painter, Rect) {
    if (!showing()) return;
    const std::string shown = display_text();
    const Rect chip = chip_bounds(shown);
    const AnatomyStateSet states = anatomy_states();
    PartPaint face{.role = PartRole::chip, .bounds = chip, .states = states, .family = ControlVisualRole::badge};
    face.corner_radius = std::min(chip.width, chip.height) * 0.5;
    paint_part(painter, *this, face);
    if (shown.empty()) return;
    PartPaint label{.role = PartRole::label, .bounds = chip, .states = states, .family = ControlVisualRole::badge};
    label.text = shown;
    label.font = chip_font();
    label.alignment = PartTextAlignment::center;
    paint_part(painter, *this, label);
}

// A badge takes no input: the pointer reaches whatever lies beneath it.
bool Badge::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor Badge::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = accessible_name.read().empty() ? display_text() : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = showing() && !descriptor.name.empty();
    return descriptor;
}

const ControlAnatomy& Badge::anatomy() const noexcept {
    return badge_anatomy;
}

AnatomyStateSet Badge::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (mode.read() == BadgeMode::dot) states.insert(dot_state);
    return states;
}

void Badge::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    if (showing()) {
        const std::string shown = display_text();
        const Rect chip = chip_bounds(shown);
        record.chip = PartGeometry::from_bounds(chip);
        if (!shown.empty()) record.label = PartGeometry::from_bounds(chip);
    }
    write_parts(anatomy(), badge_part_fields, record, geometry);
}

Badge::Parts Badge::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), badge_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin Badge (tools/property_families/badge.json; generated, do not edit)

namespace {

PropertyDescriptor badge_count_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "count";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Appearance";
    descriptor.description = "The number the badge shows.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_badge_count() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        badge_count_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor badge_maximum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "maximum";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Appearance";
    descriptor.description = "The largest count shown as it is; a larger count shows it followed by +.";
    descriptor.default_value = BindingValue{std::uint64_t{99U}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_badge_maximum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        badge_maximum_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor badge_mode_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "mode";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the badge shows a count or a dot.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"badge_mode", "count", 0}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = badge_mode_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_badge_mode() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        badge_mode_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor badge_show_zero_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_zero";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether a zero count still shows its chip.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_badge_show_zero() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        badge_show_zero_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<BadgeMode>::type() {
    return PropertyType{BindingValueKind::enumeration, false, badge_mode_enum()};
}

BindingValue PropertyValueTraits<BadgeMode>::to_binding(BadgeMode value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = badge_mode_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

BadgeMode PropertyValueTraits<BadgeMode>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<BadgeMode>(choice.value);
}

const std::array<PropertyEntry, Badge::badge_property_count>& Badge::badge_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"count", &describe_badge_count,
         &read_cell_entry<Badge, decltype(&Badge::count), &Badge::count>,
         &write_cell_entry<Badge, decltype(&Badge::count), &Badge::count>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"maximum", &describe_badge_maximum,
         &read_cell_entry<Badge, decltype(&Badge::maximum), &Badge::maximum>,
         &write_cell_entry<Badge, decltype(&Badge::maximum), &Badge::maximum>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"mode", &describe_badge_mode,
         &read_cell_entry<Badge, decltype(&Badge::mode), &Badge::mode>,
         &write_cell_entry<Badge, decltype(&Badge::mode), &Badge::mode>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_zero", &describe_badge_show_zero,
         &read_cell_entry<Badge, decltype(&Badge::show_zero), &Badge::show_zero>,
         &write_cell_entry<Badge, decltype(&Badge::show_zero), &Badge::show_zero>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Badge's property table names a property twice");
    static const std::array<PropertyEntry, badge_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Badge::property_table() const noexcept {
    const std::array<PropertyEntry, badge_property_count>& entries = badge_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void Badge::prepare_write(const decltype(count)&, std::uint32_t&) {
    require_mutable();
}

void Badge::written(const decltype(count)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Badge::prepare_write(const decltype(maximum)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming == 0U) throw std::invalid_argument("Badge maximum must be at least 1");
}

void Badge::written(const decltype(maximum)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Badge::prepare_write(const decltype(mode)&, BadgeMode&) {
    require_mutable();
}

void Badge::written(const decltype(mode)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Badge::prepare_write(const decltype(show_zero)&, bool&) {
    require_mutable();
}

void Badge::written(const decltype(show_zero)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end Badge

} // namespace gui_forms
