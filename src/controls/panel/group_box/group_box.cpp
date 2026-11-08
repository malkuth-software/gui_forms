#include "gui_forms/controls/panel/group_box/group_box.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "group_box_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

GroupBox::GroupBox(StableId stable_id, std::string initial_text) : Panel(std::move(stable_id)) {
    border_style = BorderStyle::none;
    text = std::move(initial_text);
}

void GroupBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const FontSpec caption_font = text_font(font.read());
    const std::string caption = caption_text();
    const double caption_height = std::max(16.0, caption_font.size + 4.0);
    const double rule_y = std::max(10.5, caption_height * 0.66);
    // The frame is keylines broken by the caption, and the caption a part,
    // drawn by the theme's engine; a background override supplies a ground
    // under them (T4-1), and without one the parent shows through. The
    // ground fills the frame only: above the rule the parent's ground
    // shows, as in Windows Forms (sweep defect D4).
    if (has_background_override() || has_style_override()) {
        PartPaint ground;
        ground.family = ControlVisualRole::panel;
        ground.role = PartRole::fill;
        ground.bounds = {0.0, rule_y, bounds.width, std::max(0.0, bounds.height - rule_y)};
        ground.corner_radius = 0.0;
        ground.authored_background = background();
        paint_part(painter, *this, ground);
    } else if (face_selected_by_rule()) {
        // A theme rule that selects the face (G3) draws it over the frame,
        // where the override's ground would be.
        PartPaint face;
        face.family = ControlVisualRole::panel;
        face.role = PartRole::face;
        face.key = PartKey{"face", {}, {}};
        face.bounds = {0.0, rule_y, bounds.width, std::max(0.0, bounds.height - rule_y)};
        paint_part(painter, *this, face);
    }
    const Rect gap = caption_bounds();
    const double top = rule_y - 0.5;
    const double right = std::max(0.0, bounds.width - 1.0);
    const double bottom = std::max(top, bounds.height - 1.0);
    const double gap_right = gap.x + gap.width;
    const std::array<Rect, 5> sides{{
        {0.0, top, std::max(0.0, gap.x), 1.0},
        {gap_right, top, std::max(0.0, bounds.width - gap_right), 1.0},
        {0.0, bottom, bounds.width, 1.0},
        {0.0, top, 1.0, std::max(0.0, bounds.height - top)},
        {right, top, 1.0, std::max(0.0, bounds.height - top)},
    }};
    PartPaint rule;
    rule.family = ControlVisualRole::panel;
    rule.role = PartRole::keyline;
    for (const Rect& side : sides) {
        rule.bounds = side;
        paint_part(painter, *this, rule);
    }
    PartPaint label;
    label.family = ControlVisualRole::panel;
    label.role = PartRole::caption;
    label.text = caption;
    label.font = caption_font;
    const Point caption_origin{13.0, std::max(caption_font.size, rule_y + caption_font.size * 0.36)};
    paint_text_part_at(painter, *this, label, caption_origin, std::max(0.0, gap_right - 13.0));
}

bool GroupBox::mnemonic_matches(char32_t character) const noexcept {
    return use_mnemonic.read() && is_mnemonic(character, text.read());
}

bool GroupBox::process_mnemonic_self(char32_t character) {
    if (!mnemonic_matches(character)) return false;
    static_cast<void>(focus_next_after_self());
    return true;
}

SemanticDescriptor GroupBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty()
        ? (use_mnemonic.read() ? parse_mnemonic_text(text.read()).display_text : text.read())
        : accessible_name.read();
    descriptor.description = accessible_description.read();
    if (use_mnemonic.read()) descriptor.access_key = mnemonic_access_key(text.read());
    descriptor.exposed = true;
    return descriptor;
}

namespace {

constexpr ControlAnatomy group_box_anatomy{
    .control_id = "group_box",
    .parts = group_box_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

std::string GroupBox::caption_text() const {
    return use_mnemonic.read() ? parse_mnemonic_text(text.read()).display_text : text.read();
}

Rect GroupBox::caption_bounds() const {
    const Rect bounds = local_bounds();
    const FontSpec caption_font = text_font(font.read());
    const double caption_height = std::max(16.0, caption_font.size + 4.0);
    const double caption_width = std::min(
        std::max(0.0, bounds.width - 18.0),
        estimated_text_width(caption_text(), caption_font) + 12.0);
    const Rect caption{9.0, 1.0, caption_width, caption_height};
    return caption;
}

const ControlAnatomy& GroupBox::anatomy() const noexcept {
    return group_box_anatomy;
}

void GroupBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const FontSpec caption_font = text_font(font.read());
    const double caption_height = std::max(16.0, caption_font.size + 4.0);
    const double rule_y = std::max(10.5, caption_height * 0.66);
    // The one-unit frame stroked half a unit inside the rule line.
    const Rect frame{0.0, rule_y - 0.5, bounds.width,
                     std::max(0.0, bounds.height - rule_y + 0.5)};
    Parts record;
    record.scrollable = scrollable_parts();
    record.face = PartGeometry::from_bounds(bounds);
    record.edge = PartGeometry::from_bounds(frame);
    record.caption = PartGeometry::from_bounds(caption_bounds());
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), group_box_detail::extra_fields, record, geometry);
}

GroupBox::Parts GroupBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), group_box_detail::extra_fields, published.parts(), record);
    return record;
}

// property_codemod begin GroupBox (tools/property_families/group_box.json; generated, do not edit)

namespace {

PropertyDescriptor group_box_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "Caption shown in the group's frame.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    return descriptor;
}

const PropertyDescriptor& describe_group_box_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        group_box_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor group_box_font_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "font";
    descriptor.kind = BindingValueKind::font;
    descriptor.category = "Appearance";
    descriptor.description = "Font used for the caption.";
    descriptor.default_value = BindingValue{FontSpec{FontRole::control, 12.0, 600, false, 0.24}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_group_box_font() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        group_box_font_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, GroupBox::group_box_property_count>& GroupBox::group_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 2U> own{{
        {"text", &describe_group_box_text,
         &read_cell_entry<GroupBox, decltype(&GroupBox::text), &GroupBox::text>,
         &write_cell_entry<GroupBox, decltype(&GroupBox::text), &GroupBox::text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"font", &describe_group_box_font,
         &read_cell_entry<GroupBox, decltype(&GroupBox::font), &GroupBox::font>,
         &write_cell_entry<GroupBox, decltype(&GroupBox::font), &GroupBox::font>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "GroupBox's property table names a property twice");
    static const std::array<PropertyEntry, group_box_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> GroupBox::property_table() const noexcept {
    const std::array<PropertyEntry, group_box_property_count>& entries = group_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void GroupBox::prepare_write(const decltype(text)&, std::string& incoming) {
    require_mutable();
    coerce_text(incoming);
}

void GroupBox::written(const decltype(text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    on_text_changed();
}

void GroupBox::on_text_changed() {}

void GroupBox::coerce_text(std::string&) {}

void GroupBox::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("GroupBox font specification is invalid");
    set_font_from_code(true);
    coerce_font(incoming);
}

void GroupBox::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    on_font_changed();
}

void GroupBox::on_font_changed() {}

void GroupBox::coerce_font(FontSpec&) {}

void GroupBox::prepare_write(const decltype(use_mnemonic)&, bool&) {
    require_mutable();
}

void GroupBox::written(const decltype(use_mnemonic)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end GroupBox

} // namespace gui_forms
