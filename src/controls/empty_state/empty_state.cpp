#include "gui_forms/controls/empty_state/empty_state.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/text/types/text_types.hpp"
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

// The column's metrics, in logical units: the picture's side, the gap after
// the picture and after the text, and the space between the title's line
// and the description's.
constexpr double image_side = 48.0;
constexpr double gap = 12.0;
constexpr double line_gap = 4.0;

constexpr std::array<PartField<EmptyState::Parts>, 5> empty_state_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &EmptyState::Parts::region},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &EmptyState::Parts::icon},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &EmptyState::Parts::caption},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &EmptyState::Parts::label},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &EmptyState::Parts::content},
}};

constexpr std::array<PartDeclaration, 5> empty_state_parts = part_declarations(empty_state_part_fields);

constexpr std::array<AnatomyState, 3> empty_state_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
};

constexpr ControlAnatomy empty_state_anatomy{
    .control_id = "empty_state",
    .parts = empty_state_parts,
    .states = empty_state_states,
    .custom_states = {},
};

// A rectangle of `size` centred across a column `width` wide, at `top`.
Rect centred(double width, double top, Size size) noexcept {
    const double shown = std::min(size.width, width);
    const Rect placed{std::floor((width - shown) * 0.5), top, shown, size.height};
    return placed;
}

} // namespace

EmptyState::EmptyState(StableId stable_id) : Control(std::move(stable_id)) {}

Control::Ptr EmptyState::set_action(Control::Ptr control) {
    require_mutable();
    if (control && control == action_) return {};
    if (control && (*control).parent()) {
        throw std::logic_error("EmptyState action control already has a parent");
    }
    Control::Ptr previous = action_;
    if (control) add_child(control);
    if (action_) previous = remove_child((*action_).runtime_id());
    action_ = std::move(control);
    action_size_ = Size{};
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return previous;
}

FontSpec EmptyState::title_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.heading);
    return font;
}

FontSpec EmptyState::description_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.control);
    return font;
}

// The column's pieces stacked in order, each present only when it has
// something to show, with a gap after the picture and before the action;
// the whole column is centred in `size` on both axes.
EmptyState::Placement EmptyState::placement(Size size) const {
    Placement placed;
    const bool pictured = image.read().value != 0U;
    const std::string& heading = title.read();
    const std::string& body = description.read();
    Size heading_size{};
    if (!heading.empty()) heading_size = measure_text_extent_utf8(heading, title_font()).logical_size;
    Size body_size{};
    if (!body.empty()) body_size = measure_text_extent_utf8(body, description_font()).logical_size;
    heading_size = Size{std::ceil(heading_size.width), std::ceil(heading_size.height)};
    body_size = Size{std::ceil(body_size.width), std::ceil(body_size.height)};
    const bool texted = !heading.empty() || !body.empty();
    const bool acting = action_ && action_size_.width > 0.0 && action_size_.height > 0.0;

    double height{};
    if (pictured) height += image_side;
    if (pictured && (texted || acting)) height += gap;
    height += heading_size.height + body_size.height;
    if (!heading.empty() && !body.empty()) height += line_gap;
    if (texted && acting) height += gap;
    if (acting) height += action_size_.height;
    double width = std::max(heading_size.width, body_size.width);
    if (pictured) width = std::max(width, image_side);
    if (acting) width = std::max(width, action_size_.width);
    placed.natural = Size{width, height};

    double top = std::floor(std::max(0.0, (size.height - height) * 0.5));
    if (pictured) {
        placed.icon = centred(size.width, top, Size{image_side, image_side});
        top += image_side;
        if (texted || acting) top += gap;
    }
    if (!heading.empty()) {
        placed.caption = Rect{0.0, top, size.width, heading_size.height};
        top += heading_size.height;
        if (!body.empty()) top += line_gap;
    }
    if (!body.empty()) {
        placed.label = Rect{0.0, top, size.width, body_size.height};
        top += body_size.height;
    }
    if (acting) {
        if (texted) top += gap;
        placed.content = centred(size.width, top, action_size_);
    }
    return placed;
}

// The column's natural size, within the space offered; a requested size
// wins on either axis. An unbounded axis takes the natural size.
Size EmptyState::measure(Size available) {
    const Rect requested = requested_bounds();
    action_size_ = Size{};
    const Control::Ptr retained = action_;
    if (retained && is_current_layout_child(retained)) {
        const Size desired = (*retained).measure(available);
        if (!is_alive()) return Size{};
        if (action_ != retained || !is_current_layout_child(retained)) {
            invalidate(Dirty::measure | Dirty::arrange);
        } else {
            action_size_ = desired;
        }
    }
    const Placement placed = placement(Size{});
    double width = placed.natural.width;
    double height = placed.natural.height;
    if (requested.width > 0.0) width = requested.width;
    if (requested.height > 0.0) height = requested.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// The action is measured again against the arranged size, since a parent
// that places the control by its requested bounds never measures it.
void EmptyState::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Control::Ptr retained = action_;
    if (!retained || !is_current_layout_child(retained)) return;
    const Rect client = client_rectangle();
    const Size desired = (*retained).measure(Size{client.width, client.height});
    if (!is_alive()) return;
    if (action_ != retained || !is_current_layout_child(retained)) {
        invalidate(Dirty::measure | Dirty::arrange);
        return;
    }
    action_size_ = desired;
    const Placement placed = placement(Size{client.width, client.height});
    set_child_layout(retained, placed.content);
}

void EmptyState::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    const ImageId picture = image.read();
    if (picture.value != 0U) {
        PartPaint icon{.role = PartRole::icon, .bounds = placed.icon, .states = states, .family = ControlVisualRole::panel};
        icon.image = picture;
        paint_part(painter, *this, icon);
    }
    const std::string& heading = title.read();
    if (!heading.empty()) {
        PartPaint caption{.role = PartRole::caption, .bounds = placed.caption, .states = states,
                          .family = ControlVisualRole::panel};
        caption.text = heading;
        caption.font = title_font();
        caption.alignment = PartTextAlignment::center;
        paint_part(painter, *this, caption);
    }
    const std::string& body = description.read();
    if (!body.empty()) {
        PartPaint label{.role = PartRole::label, .bounds = placed.label, .states = states,
                        .family = ControlVisualRole::panel};
        label.text = body;
        label.font = description_font();
        label.alignment = PartTextAlignment::center;
        label.tone = PartTextTone::secondary;
        paint_part(painter, *this, label);
    }
}

// The control itself takes no pointer input; its action does.
bool EmptyState::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor EmptyState::semantic_descriptor() const {
    SemanticDescriptor semantics;
    semantics.role = SemanticRole::group;
    semantics.name = accessible_name.read().empty() ? title.read() : accessible_name.read();
    semantics.description = accessible_description.read().empty() ? description.read() : accessible_description.read();
    semantics.exposed = true;
    return semantics;
}

const ControlAnatomy& EmptyState::anatomy() const noexcept {
    return empty_state_anatomy;
}

void EmptyState::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.icon = PartGeometry::from_bounds(placed.icon);
    record.caption = PartGeometry::from_bounds(placed.caption);
    record.label = PartGeometry::from_bounds(placed.label);
    record.content = PartGeometry::from_bounds(placed.content);
    write_parts(anatomy(), empty_state_part_fields, record, geometry);
}

EmptyState::Parts EmptyState::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), empty_state_part_fields, published.parts(), record);
    return record;
}

// property_codemod begin EmptyState (tools/property_families/empty_state.json; generated, do not edit)

namespace {

PropertyDescriptor empty_state_image_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "image";
    descriptor.kind = BindingValueKind::image;
    descriptor.category = "Appearance";
    descriptor.description = "The picture above the title.";
    descriptor.default_value = BindingValue{ImageId{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_empty_state_image() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        empty_state_image_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor empty_state_title_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "title";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The heading that says what is empty.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_empty_state_title() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        empty_state_title_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor empty_state_description_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "description";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The line that says why it is empty or what to do next.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_empty_state_description() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        empty_state_description_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, EmptyState::empty_state_property_count>& EmptyState::empty_state_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"image", &describe_empty_state_image,
         &read_cell_entry<EmptyState, decltype(&EmptyState::image), &EmptyState::image>,
         &write_cell_entry<EmptyState, decltype(&EmptyState::image), &EmptyState::image>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"title", &describe_empty_state_title,
         &read_cell_entry<EmptyState, decltype(&EmptyState::title), &EmptyState::title>,
         &write_cell_entry<EmptyState, decltype(&EmptyState::title), &EmptyState::title>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"description", &describe_empty_state_description,
         &read_cell_entry<EmptyState, decltype(&EmptyState::description), &EmptyState::description>,
         &write_cell_entry<EmptyState, decltype(&EmptyState::description), &EmptyState::description>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "EmptyState's property table names a property twice");
    static const std::array<PropertyEntry, empty_state_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> EmptyState::property_table() const noexcept {
    const std::array<PropertyEntry, empty_state_property_count>& entries = empty_state_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void EmptyState::prepare_write(const decltype(image)&, ImageId&) {
    require_mutable();
}

void EmptyState::written(const decltype(image)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void EmptyState::prepare_write(const decltype(title)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("EmptyState title must be valid UTF-8");
    }
}

void EmptyState::written(const decltype(title)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void EmptyState::prepare_write(const decltype(description)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("EmptyState description must be valid UTF-8");
    }
}

void EmptyState::written(const decltype(description)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

// property_codemod end EmptyState

} // namespace gui_forms
