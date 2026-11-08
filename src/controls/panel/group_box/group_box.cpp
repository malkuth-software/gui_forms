#include "gui_forms/controls/panel/group_box/group_box.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "group_box_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

GroupBox::GroupBox(StableId stable_id, std::string text)
    : Panel(std::move(stable_id)), text_(std::move(text)) {
    border_style = BorderStyle::none;
    // The caption (Windows Forms' GroupBox.Text); it raises no change event.
    define_bindable_property({
        {"text", BindingValueKind::text, "Appearance",
         "Caption shown in the group's frame.", BindingValue{std::string{}},
         Dirty::measure | Dirty::paint | Dirty::semantics},
        detail::BindingMemberGetter<GroupBox, std::string>(*this, &GroupBox::text_),
        detail::ConvertedPropertySetter<GroupBox, std::string>(
            *this, &GroupBox::set_text, BindingValueKind::text,
            "GroupBox.Text binding requires text"),
        {}, {}, {}});

    PropertyDescriptor font;
    font.name = "font";
    font.kind = BindingValueKind::font;
    font.category = "Appearance";
    font.description = "Font used for the caption.";
    font.default_value = BindingValue{FontSpec{FontRole::control, 12.0, 600, false, 0.24}};
    font.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    font.bindable = false;
    define_bindable_property({
        std::move(font),
        detail::BindingMemberGetter<GroupBox, FontSpec>(*this, &GroupBox::font_),
        detail::DirectPropertySetter<GroupBox, FontSpec>(*this, &GroupBox::set_font), {}, {}, {}});
}

void GroupBox::set_text(std::string text) {
    require_mutable();
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void GroupBox::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("GroupBox font specification is invalid");
    }
    set_font_from_code(true);
    if (font_ == font) {
        return;
    }
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void GroupBox::set_use_mnemonic(bool value) {
    require_mutable();
    if (use_mnemonic_ == value) return;
    use_mnemonic_ = value;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void GroupBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const FontSpec font = text_font(font_);
    const std::string caption = caption_text();
    const double caption_height = std::max(16.0, font.size + 4.0);
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
    label.font = font;
    paint_text_part_at(painter, *this, label, {13.0, std::max(font.size, rule_y + font.size * 0.36)},
                       std::max(0.0, gap_right - 13.0));
}

bool GroupBox::mnemonic_matches(char32_t character) const noexcept {
    return use_mnemonic_ && is_mnemonic(character, text_);
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
        ? (use_mnemonic_ ? parse_mnemonic_text(text_).display_text : text_)
        : accessible_name.read();
    descriptor.description = accessible_description.read();
    if (use_mnemonic_) descriptor.access_key = mnemonic_access_key(text_);
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
    return use_mnemonic_ ? parse_mnemonic_text(text_).display_text : text_;
}

Rect GroupBox::caption_bounds() const {
    const Rect bounds = local_bounds();
    const FontSpec font = text_font(font_);
    const double caption_height = std::max(16.0, font.size + 4.0);
    const double caption_width = std::min(
        std::max(0.0, bounds.width - 18.0),
        estimated_text_width(caption_text(), font) + 12.0);
    const Rect caption{9.0, 1.0, caption_width, caption_height};
    return caption;
}

const ControlAnatomy& GroupBox::anatomy() const noexcept {
    return group_box_anatomy;
}

void GroupBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const FontSpec font = text_font(font_);
    const double caption_height = std::max(16.0, font.size + 4.0);
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

} // namespace gui_forms
