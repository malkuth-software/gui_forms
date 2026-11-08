#include "gui_forms/controls/panel/scaled_panel/scaled_panel.hpp"
#include "../../scrollable_control/container_control/scaled_layout_utilities.hpp"
#include "../panel_anatomy.hpp"
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
        set_child_layout(child, scaled_layout_detail::child_bounds(design_size.read(), design, final_bounds));
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

// property_codemod begin ScaledPanel (tools/property_families/scaled_panel.json; generated, do not edit)

namespace {

PropertyDescriptor scaled_panel_design_size_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "design_size";
    descriptor.kind = BindingValueKind::size;
    descriptor.category = "Layout";
    descriptor.description = "The coordinate space the children's design bounds are written in; it scales to the arranged size.";
    descriptor.default_value = BindingValue{Size{1.0, 1.0}};
    descriptor.invalidation_effects = Dirty::layout;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_scaled_panel_design_size() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scaled_panel_design_size_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ScaledPanel::scaled_panel_property_count>& ScaledPanel::scaled_panel_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"design_size", &describe_scaled_panel_design_size,
         &read_cell_entry<ScaledPanel, decltype(&ScaledPanel::design_size), &ScaledPanel::design_size>,
         &write_cell_entry<ScaledPanel, decltype(&ScaledPanel::design_size), &ScaledPanel::design_size>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ScaledPanel's property table names a property twice");
    static const std::array<PropertyEntry, scaled_panel_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ScaledPanel::property_table() const noexcept {
    const std::array<PropertyEntry, scaled_panel_property_count>& entries = scaled_panel_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ScaledPanel::prepare_write(const decltype(design_size)&, Size& incoming) {
    require_mutable();
    scaled_layout_detail::validate_design_size(incoming);
}

void ScaledPanel::written(const decltype(design_size)&) {
    invalidate(Dirty::layout);
}

// property_codemod end ScaledPanel

} // namespace gui_forms
