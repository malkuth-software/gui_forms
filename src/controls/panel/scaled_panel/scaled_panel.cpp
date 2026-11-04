#include "gui_forms/controls/panel/scaled_panel/scaled_panel.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "../../scrollable_control/container_control/scaled_layout_utilities.hpp"
#include "../panel_anatomy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

ScaledPanel::ScaledPanel(StableId stable_id, Size design_size)
    : Panel(std::move(stable_id)), design_size_(design_size) {
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
        detail::BindingMemberGetter<ScaledPanel, Size>(*this, &ScaledPanel::design_size_),
        detail::DirectPropertySetter<ScaledPanel, Size>(*this, &ScaledPanel::set_design_size), {}, {}, {}});
}

void ScaledPanel::set_design_size(Size size) {
    require_mutable();
    scaled_layout_detail::validate_design_size(size);
    if (design_size_ == size) return;
    design_size_ = size;
    invalidate(Dirty::layout);
}

void ScaledPanel::add_at(Control::Ptr child, Rect design_bounds) {
    require_mutable();
    if (!child) throw std::invalid_argument("scaled layout child may not be null");
    scaled_layout_detail::validate_design_bounds(design_bounds);
    const RuntimeId id = (*child).runtime_id();
    add_child(std::move(child));
    slots_[id.value] = design_bounds;
    invalidate(Dirty::layout);
}

void ScaledPanel::set_design_bounds(const Control& child, Rect design_bounds) {
    require_mutable();
    scaled_layout_detail::validate_design_bounds(design_bounds);
    if (child.parent().get() != this) {
        throw std::logic_error("scaled layout child must belong to the panel");
    }
    slots_[child.runtime_id().value] = design_bounds;
    invalidate(Dirty::layout);
}

std::optional<Rect> ScaledPanel::design_bounds(const Control& child) const {
    const SlotMap::const_iterator found =
        slots_.find(child.runtime_id().value);
    return found == slots_.end() ? std::nullopt
                                : std::optional<Rect>((*found).second);
}

void ScaledPanel::reconcile_slots() {
    scaled_layout_detail::reconcile_slots(*this, slots_);
}

void ScaledPanel::arrange(Rect final_bounds) {
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

constexpr ControlAnatomy scaled_panel_anatomy{
    .control_id = "scaled_panel",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ScaledPanel::anatomy() const noexcept {
    return scaled_panel_anatomy;
}

} // namespace gui_forms
