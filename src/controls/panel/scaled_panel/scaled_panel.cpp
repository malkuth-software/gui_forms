#include "gui_forms/controls/scaled_panel.hpp"
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

ScaledPanel::ScaledPanel(StableId stable_id, Size initial_design_size) : Panel(std::move(stable_id)) {
    design_size = initial_design_size;
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

stxlib::Optional<Rect> ScaledPanel::design_bounds(const Control& child) const {
    const SlotMap::const_iterator found =
        slots_.find(child.runtime_id().value);
    if (found == slots_.end()) return stxlib::Optional<Rect>::none();
    const stxlib::Optional<Rect> result = stxlib::Optional<Rect>::of((*found).second);
    return result;
}

void ScaledPanel::reconcile_slots() {
    scaled_layout_detail::reconcile_slots(*this, slots_);
}

void ScaledPanel::arrange(Rect final_bounds) {
    reconcile_slots();
    arrange_self(final_bounds);
    const Size design_space = scaled_layout_detail::effective_design_size(design_size.read());
    for (const Control::Ptr& child : children()) {
        // A child added without design bounds (a form document's child,
        // added by add_child) is placed by its own authored bounds, read in
        // design coordinates.
        const SlotMap::iterator slot =
            slots_.find((*child).runtime_id().value);
        const Rect design = slot == slots_.end() ? (*child).requested_bounds() : (*slot).second;
        set_child_layout(child, scaled_layout_detail::child_bounds(design_space, design, final_bounds));
    }
}

namespace {

constexpr ControlAnatomy scaled_panel_anatomy{
    .control_id = "scaled_panel",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = {},
};

} // namespace

const ControlAnatomy& ScaledPanel::anatomy() const noexcept {
    return scaled_panel_anatomy;
}

stxlib::Optional<double> ScaledPanel::applied_viewport_scale() const noexcept {
    const stxlib::Optional<double> scale =
        scaled_layout_detail::stretch_scale(design_size.read(), committed_arranged_bounds());
    return scale;
}

} // namespace gui_forms
