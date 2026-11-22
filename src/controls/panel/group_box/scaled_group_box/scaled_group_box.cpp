#include "gui_forms/controls/scaled_group_box.hpp"
#include "../../../scrollable_control/container_control/scaled_aspect_property.hpp"
#include "../../../scrollable_control/container_control/scaled_layout_utilities.hpp"
#include "../group_box_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

ScaledGroupBox::ScaledGroupBox(StableId stable_id, std::string initial_text, Size initial_design_size)
    : GroupBox(std::move(stable_id), std::move(initial_text)) {
    design_size = initial_design_size;
}

void ScaledGroupBox::add_at(Control::Ptr child, Rect design_bounds) {
    require_mutable();
    if (!child) throw std::invalid_argument("scaled layout child may not be null");
    scaled_layout_detail::validate_design_bounds(design_bounds);
    const RuntimeId id = (*child).runtime_id();
    add_child(std::move(child));
    slots_[id.value] = design_bounds;
    invalidate(Dirty::layout);
}

void ScaledGroupBox::set_design_bounds(const Control& child, Rect design_bounds) {
    require_mutable();
    scaled_layout_detail::validate_design_bounds(design_bounds);
    if (child.parent().get() != this) {
        throw std::logic_error("scaled layout child must belong to the group");
    }
    slots_[child.runtime_id().value] = design_bounds;
    invalidate(Dirty::layout);
}

stxlib::Optional<Rect> ScaledGroupBox::design_bounds(const Control& child) const {
    const SlotMap::const_iterator found =
        slots_.find(child.runtime_id().value);
    if (found == slots_.end()) return stxlib::Optional<Rect>::none();
    const stxlib::Optional<Rect> result = stxlib::Optional<Rect>::of((*found).second);
    return result;
}

void ScaledGroupBox::reconcile_slots() {
    scaled_layout_detail::reconcile_slots(*this, slots_);
}

void ScaledGroupBox::arrange(Rect final_bounds) {
    reconcile_slots();
    arrange_self(final_bounds);
    const Size design_space = scaled_layout_detail::effective_design_size(design_size.read());
    const scaled_layout_detail::DesignMap map = scaled_layout_detail::design_map(
        design_space, final_bounds, aspect.read(), viewport_fit.read().alignment);
    for (const Control::Ptr& child : children()) {
        // A child added without design bounds (a form document's child,
        // added by add_child) is placed by its own authored bounds, read in
        // design coordinates.
        const SlotMap::iterator slot =
            slots_.find((*child).runtime_id().value);
        const Rect design = slot == slots_.end() ? (*child).requested_bounds() : (*slot).second;
        set_child_layout(child, scaled_layout_detail::child_bounds(map, design));
    }
}

namespace {

constexpr ControlAnatomy scaled_group_box_anatomy{
    .control_id = "scaled_group_box",
    .parts = group_box_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = {},
};

} // namespace

const ControlAnatomy& ScaledGroupBox::anatomy() const noexcept {
    return scaled_group_box_anatomy;
}

stxlib::Optional<double> ScaledGroupBox::applied_viewport_scale() const noexcept {
    const stxlib::Optional<double> scale =
        scaled_layout_detail::stretch_scale(design_size.read(), committed_arranged_bounds());
    return scale;
}

// property_codemod begin ScaledGroupBox (tools/property_families/scaled_group_box.json; generated, do not edit)

namespace {

PropertyDescriptor scaled_group_box_aspect_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "aspect";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Layout";
    descriptor.description = "How the design space maps onto the arranged size: stretch or uniform.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"scaled_aspect", "stretch", 0}};
    descriptor.invalidation_effects = Dirty::layout;
    descriptor.bindable = false;
    descriptor.enumeration = scaled_layout_detail::scaled_aspect_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_scaled_group_box_aspect() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scaled_group_box_aspect_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ScaledGroupBox::scaled_group_box_property_count>& ScaledGroupBox::scaled_group_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"aspect", &describe_scaled_group_box_aspect,
         &read_cell_entry<ScaledGroupBox, decltype(&ScaledGroupBox::aspect), &ScaledGroupBox::aspect>,
         &write_cell_entry<ScaledGroupBox, decltype(&ScaledGroupBox::aspect), &ScaledGroupBox::aspect>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ScaledGroupBox's property table names a property twice");
    static const std::array<PropertyEntry, scaled_group_box_property_count> entries =
        join_property_tables(GroupBox::group_box_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ScaledGroupBox::property_table() const noexcept {
    const std::array<PropertyEntry, scaled_group_box_property_count>& entries = scaled_group_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ScaledGroupBox::prepare_write(const decltype(aspect)&, ScaledAspect& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(ScaledAspect::uniform)) throw std::invalid_argument("ScaledGroupBox aspect is not a ScaledAspect");
}

void ScaledGroupBox::written(const decltype(aspect)&) {
    invalidate(Dirty::layout);
}

// property_codemod end ScaledGroupBox

} // namespace gui_forms
