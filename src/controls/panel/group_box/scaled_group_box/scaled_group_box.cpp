#include "gui_forms/controls/panel/group_box/scaled_group_box/scaled_group_box.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "../../../scrollable_control/container_control/scaled_layout_utilities.hpp"
#include "../group_box_anatomy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

ScaledGroupBox::ScaledGroupBox(StableId stable_id, std::string text,
                               Size design_size)
    : GroupBox(std::move(stable_id), std::move(text)),
      design_size_(design_size) {
    scaled_layout_detail::validate_design_size(design_size_);
    PropertyDescriptor design_size_property;
    design_size_property.name = "design_size";
    design_size_property.kind = BindingValueKind::size;
    design_size_property.category = "Layout";
    design_size_property.description =
        "The coordinate space the children's design bounds are written in; it scales to the arranged size.";
    design_size_property.default_value = BindingValue{Size{1.0, 1.0}};
    design_size_property.invalidation_effects = Dirty::layout;
    design_size_property.bindable = false;
    define_bindable_property({
        std::move(design_size_property),
        detail::BindingMemberGetter<ScaledGroupBox, Size>(*this, &ScaledGroupBox::design_size_),
        detail::DirectPropertySetter<ScaledGroupBox, Size>(*this, &ScaledGroupBox::set_design_size), {}, {}, {}});
}

void ScaledGroupBox::set_design_size(Size size) {
    require_mutable();
    scaled_layout_detail::validate_design_size(size);
    if (design_size_ == size) return;
    design_size_ = size;
    invalidate(Dirty::layout);
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

std::optional<Rect> ScaledGroupBox::design_bounds(const Control& child) const {
    const SlotMap::const_iterator found =
        slots_.find(child.runtime_id().value);
    return found == slots_.end() ? std::nullopt
                                : std::optional<Rect>((*found).second);
}

void ScaledGroupBox::reconcile_slots() {
    scaled_layout_detail::reconcile_slots(*this, slots_);
}

void ScaledGroupBox::arrange(Rect final_bounds) {
    reconcile_slots();
    arrange_self(final_bounds);
    for (const Control::Ptr& child : children()) {
        // A child added without design bounds (a form document's child,
        // added by add_child) is placed by its own authored bounds, read in
        // design coordinates.
        const SlotMap::iterator slot =
            slots_.find((*child).runtime_id().value);
        const Rect design = slot == slots_.end() ? (*child).requested_bounds() : (*slot).second;
        set_child_layout(child, scaled_layout_detail::child_bounds(design_size_, design, final_bounds));
    }
}


namespace {

constexpr ControlAnatomy scaled_group_box_anatomy{
    .control_id = "scaled_group_box",
    .parts = group_box_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ScaledGroupBox::anatomy() const noexcept {
    return scaled_group_box_anatomy;
}

} // namespace gui_forms
