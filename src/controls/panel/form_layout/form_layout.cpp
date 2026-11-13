#include "gui_forms/controls/form_layout.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

namespace {

std::shared_ptr<const PropertyEnumDescriptor> horizontal_alignment_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "horizontal_alignment", {{"near", 0}, {"center", 1}, {"far", 2}}, false});
    return value;
}

constexpr ControlAnatomy form_layout_anatomy{
    .control_id = "form_layout",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

FormLayout::FormLayout(StableId stable_id) : Panel(std::move(stable_id)) {}

std::shared_ptr<Label> FormLayout::add_row(std::string caption, Control::Ptr control, double height) {
    require_mutable();
    if (!control) throw std::invalid_argument("FormLayout row control may not be null");
    if ((*control).parent()) throw std::invalid_argument("FormLayout row control already has a parent");
    if (!std::isfinite(height) || height < 0.0) {
        throw std::invalid_argument("FormLayout row height must be finite and not negative");
    }
    const std::string caption_id = std::string((*control).stable_id().value()) + ".caption";
    std::shared_ptr<Label> label = make_control<Label>(StableId(caption_id), caption);
    (*label).alignment = caption_alignment.read();
    add_child(label);
    add_child(control);
    if ((*control).accessible_name.read().empty()) (*control).accessible_name = caption;
    Row row{};
    row.caption = label;
    row.control = control;
    row.height = height;
    rows_.push_back(std::move(row));
    invalidate(invalidation::bounds);
    return label;
}

Control::Ptr FormLayout::row_control(const Row& row) const {
    Control::Ptr control = row.control.lock();
    if (!control) return {};
    if ((*control).parent().get() != this) return {};
    return control;
}

Control::Ptr FormLayout::shown_control(const Row& row) const {
    Control::Ptr control = row_control(row);
    if (!control || !(*control).visible.read()) return {};
    return control;
}

void FormLayout::erase_row(std::size_t index) {
    const Row row = rows_[index];
    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(index));
    const Control::Ptr control = row_control(row);
    if (control) (void)remove_child((*control).runtime_id());
    if (row.caption && (*row.caption).parent().get() == this) {
        (void)remove_child((*row.caption).runtime_id());
    }
    invalidate(invalidation::bounds);
}

bool FormLayout::remove_row(const Control& control) {
    require_mutable();
    for (std::size_t index = 0U; index < rows_.size(); ++index) {
        const Control::Ptr held = rows_[index].control.lock();
        if (held.get() != &control) continue;
        erase_row(index);
        return true;
    }
    return false;
}

std::size_t FormLayout::forget_empty_rows() {
    require_mutable();
    std::size_t removed = 0U;
    std::size_t index = 0U;
    while (index < rows_.size()) {
        if (row_control(rows_[index])) {
            ++index;
            continue;
        }
        erase_row(index);
        ++removed;
    }
    return removed;
}

std::shared_ptr<Label> FormLayout::caption_of(const Control& control) const {
    for (const Row& row : rows_) {
        const Control::Ptr held = row.control.lock();
        if (held.get() == &control) return row.caption;
    }
    return {};
}

double FormLayout::caption_column(Size available) const {
    const double fixed = caption_width.read();
    if (fixed > 0.0) return fixed;
    double widest = 0.0;
    for (const Row& row : rows_) {
        if (!shown_control(row)) continue;
        const Size measured = (*row.caption).measure(available);
        widest = std::max(widest, measured.width);
    }
    return widest;
}

double FormLayout::row_height(const Row& row, double width) const {
    const Control::Ptr control = shown_control(row);
    if (!control) return 0.0;
    const double unbounded = std::numeric_limits<double>::infinity();
    const Size caption_size = (*row.caption).measure({unbounded, unbounded});
    double control_height = row.height;
    if (control_height <= 0.0) {
        const Size measured = (*control).measure({width, unbounded});
        control_height = measured.height;
    }
    const double result = std::max(caption_size.height, control_height);
    return result;
}

// The layout is as wide as it is offered (a form fills its column) and as
// tall as its rows, their spacing and the padding.
Size FormLayout::measure(Size available) {
    const Insets inset = padding.read();
    const double unbounded = std::numeric_limits<double>::infinity();
    const double caption = caption_column({unbounded, unbounded});
    const double offered = std::isfinite(available.width) ? available.width : caption;
    const double inner = std::max(0.0, offered - inset.left - inset.right);
    const double control_width = std::max(0.0, inner - caption - column_spacing.read());
    double height = 0.0;
    std::size_t shown = 0U;
    for (const Row& row : rows_) {
        if (!shown_control(row)) continue;
        if (shown > 0U) height += row_spacing.read();
        height += row_height(row, control_width);
        ++shown;
    }
    const Size result{std::max(0.0, offered), height + inset.top + inset.bottom};
    return result;
}

void FormLayout::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Insets inset = padding.read();
    const double unbounded = std::numeric_limits<double>::infinity();
    const double inner = std::max(0.0, final_bounds.width - inset.left - inset.right);
    const double caption = std::min(caption_column({unbounded, unbounded}), inner);
    used_caption_width_ = caption;
    const double control_x = inset.left + std::min(inner, caption + column_spacing.read());
    const double control_width = std::max(0.0, inset.left + inner - control_x);
    double y = inset.top;
    std::size_t shown = 0U;
    for (const Row& row : rows_) {
        const Control::Ptr control = shown_control(row);
        if (!control) {
            if ((*row.caption).parent().get() == this) set_child_layout(row.caption, Rect{});
            continue;
        }
        if (shown > 0U) y += row_spacing.read();
        const double height = row_height(row, control_width);
        set_child_layout(row.caption, Rect{inset.left, y, caption, height});
        set_child_layout(control, Rect{control_x, y, control_width, height});
        y += height;
        ++shown;
    }
}

const ControlAnatomy& FormLayout::anatomy() const noexcept {
    return form_layout_anatomy;
}

void FormLayout::prepare_write(const decltype(caption_alignment)&, HorizontalAlignment& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(HorizontalAlignment::far)) {
        throw std::invalid_argument("FormLayout caption alignment is invalid");
    }
}

void FormLayout::written(const decltype(caption_alignment)&) {
    for (const Row& row : rows_) (*row.caption).alignment = caption_alignment.read();
    invalidate(Dirty::paint);
}

// property_codemod begin FormLayout (tools/property_families/form_layout.json; generated, do not edit)

namespace {

PropertyDescriptor form_layout_caption_width_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "caption_width";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The caption column's width in logical units, or 0 for the widest caption's.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_form_layout_caption_width() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        form_layout_caption_width_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor form_layout_caption_alignment_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "caption_alignment";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Layout";
    descriptor.description = "Where each caption sits in the caption column: near, center or far.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"horizontal_alignment", "near", 0}};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.bindable = false;
    descriptor.enumeration = horizontal_alignment_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_form_layout_caption_alignment() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        form_layout_caption_alignment_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor form_layout_row_spacing_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "row_spacing";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The space between rows in logical units.";
    descriptor.default_value = BindingValue{6.0};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_form_layout_row_spacing() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        form_layout_row_spacing_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor form_layout_column_spacing_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "column_spacing";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "The space between the caption column and the controls in logical units.";
    descriptor.default_value = BindingValue{8.0};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_form_layout_column_spacing() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        form_layout_column_spacing_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<HorizontalAlignment>::type() {
    return PropertyType{BindingValueKind::enumeration, false, horizontal_alignment_enum()};
}

BindingValue PropertyValueTraits<HorizontalAlignment>::to_binding(HorizontalAlignment value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = horizontal_alignment_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

HorizontalAlignment PropertyValueTraits<HorizontalAlignment>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<HorizontalAlignment>(choice.value);
}

const std::array<PropertyEntry, FormLayout::form_layout_property_count>& FormLayout::form_layout_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"caption_width", &describe_form_layout_caption_width,
         &read_cell_entry<FormLayout, decltype(&FormLayout::caption_width), &FormLayout::caption_width>,
         &write_cell_entry<FormLayout, decltype(&FormLayout::caption_width), &FormLayout::caption_width>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"caption_alignment", &describe_form_layout_caption_alignment,
         &read_cell_entry<FormLayout, decltype(&FormLayout::caption_alignment), &FormLayout::caption_alignment>,
         &write_cell_entry<FormLayout, decltype(&FormLayout::caption_alignment), &FormLayout::caption_alignment>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"row_spacing", &describe_form_layout_row_spacing,
         &read_cell_entry<FormLayout, decltype(&FormLayout::row_spacing), &FormLayout::row_spacing>,
         &write_cell_entry<FormLayout, decltype(&FormLayout::row_spacing), &FormLayout::row_spacing>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"column_spacing", &describe_form_layout_column_spacing,
         &read_cell_entry<FormLayout, decltype(&FormLayout::column_spacing), &FormLayout::column_spacing>,
         &write_cell_entry<FormLayout, decltype(&FormLayout::column_spacing), &FormLayout::column_spacing>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "FormLayout's property table names a property twice");
    static const std::array<PropertyEntry, form_layout_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> FormLayout::property_table() const noexcept {
    const std::array<PropertyEntry, form_layout_property_count>& entries = form_layout_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void FormLayout::prepare_write(const decltype(caption_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 4096.0) {
        throw std::invalid_argument("FormLayout caption width must be within [0, 4096]");
    }
}

void FormLayout::written(const decltype(caption_width)&) {
    invalidate(invalidation::bounds);
}

void FormLayout::prepare_write(const decltype(row_spacing)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 256.0) {
        throw std::invalid_argument("FormLayout row spacing must be within [0, 256]");
    }
}

void FormLayout::written(const decltype(row_spacing)&) {
    invalidate(invalidation::bounds);
}

void FormLayout::prepare_write(const decltype(column_spacing)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 256.0) {
        throw std::invalid_argument("FormLayout column spacing must be within [0, 256]");
    }
}

void FormLayout::written(const decltype(column_spacing)&) {
    invalidate(invalidation::bounds);
}

// property_codemod end FormLayout

} // namespace gui_forms
