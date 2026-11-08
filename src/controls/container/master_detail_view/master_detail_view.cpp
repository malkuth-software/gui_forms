#include "gui_forms/controls/container/master_detail_view/master_detail_view.hpp"
#include "../../scrollable_control/scrollable_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

bool valid_master_detail_extent(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 16384.0;
}

bool valid_layout(const MasterDetailLayout& layout) noexcept {
    const double values[] = {layout.master_extent, layout.splitter_width,
                             layout.splitter_hit_width, layout.master_minimum,
                             layout.detail_minimum, layout.compact_threshold};
    if (!std::all_of(std::begin(values), std::end(values),
                     valid_master_detail_extent)) {
        return false;
    }
    return layout.splitter_width > 0.0 &&
           layout.splitter_hit_width >= layout.splitter_width &&
           layout.master_extent >= layout.master_minimum;
}

MasterDetailLayout themed_master_detail_layout(const Theme& theme) noexcept {
    const ThemeGeometryTokens& geometry = theme.structure().geometry;
    return {Orientation::vertical,
            geometry.navigation_extent,
            geometry.splitter_width,
            geometry.splitter_hit_width,
            geometry.navigation_minimum,
            geometry.content_minimum,
            geometry.compact_breakpoint,
            true};
}

// Collapses and expands `split`'s panels to present `mode`, recording
// `origin`; false for automatic, which is no presentation of its own.
bool collapse_for_mode(SplitContainer& split, MasterDetailDisplayMode mode,
                       SplitCollapseOrigin origin) {
    switch (mode) {
    case MasterDetailDisplayMode::side_by_side:
        if (split.first_collapsed()) split.set_first_collapsed(false, origin);
        if (split.second_collapsed()) split.set_second_collapsed(false, origin);
        return true;
    case MasterDetailDisplayMode::master_only:
        if (split.first_collapsed()) split.set_first_collapsed(false, origin);
        if (!split.second_collapsed()) split.set_second_collapsed(true, origin);
        return true;
    case MasterDetailDisplayMode::detail_only:
        if (split.second_collapsed()) split.set_second_collapsed(false, origin);
        if (!split.first_collapsed()) split.set_first_collapsed(true, origin);
        return true;
    case MasterDetailDisplayMode::automatic:
        // Resolved to one of the others before it is applied.
        return false;
    }
    // A mode outside the enumeration presents nothing.
    return false;
}

} // namespace

MasterDetailView::MasterDetailView(StableId stable_id)
    : ContainerControl(std::move(stable_id)),
      split_(make_control<SplitContainer>(
          StableId(std::string((*this).stable_id().value()) + ".split"))) {
    configure_split();
}

void MasterDetailView::initialize_control_tree() {
    if (initialized_) return;
    add_child(split_);
    initialized_ = true;
}

Control::Ptr MasterDetailView::replace_role(
    Control::Ptr& slot, const std::shared_ptr<SplitterPanel>& panel,
    Control::Ptr replacement) {
    require_mutable();
    if (replacement && (replacement == master_ || replacement == detail_)) {
        if (replacement == slot) return {};
        throw std::logic_error(
            "one control cannot occupy both master/detail roles");
    }
    if (replacement &&
        (replacement.get() == this || replacement == split_ ||
         replacement == (*split_).first_panel() ||
         replacement == (*split_).second_panel() ||
         replacement == (*split_).splitter_control())) {
        throw std::logic_error(
            "master/detail role cannot own its composition infrastructure");
    }
    if (replacement && (*replacement).parent()) {
        throw std::logic_error("master/detail role control already has a parent");
    }
    Control::Ptr previous = slot;
    if (replacement) {
        (*replacement).dock = DockStyle::fill;
        (*panel).add_child(replacement);
    }
    if (slot) previous = (*panel).remove_child((*slot).runtime_id());
    slot = std::move(replacement);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
    return previous;
}

Control::Ptr MasterDetailView::set_master(Control::Ptr control) {
    return replace_role(master_, (*split_).first_panel(), std::move(control));
}

Control::Ptr MasterDetailView::set_detail(Control::Ptr control) {
    return replace_role(detail_, (*split_).second_panel(), std::move(control));
}

void MasterDetailView::configure_split() {
    const MasterDetailLayout layout = effective_master_detail_layout();
    (*split_).orientation = layout.orientation;
    (*split_).splitter_width = layout.splitter_width;
    (*split_).splitter_hit_width = layout.splitter_hit_width;
    (*split_).first_minimum = layout.master_minimum;
    (*split_).second_minimum = layout.detail_minimum;
    (*split_).splitter_fixed = !layout.resizable;
    (*split_).fixed_panel = SplitFixedPanel::first;
    (*split_).splitter_distance = layout.master_extent;
}

MasterDetailLayout MasterDetailView::effective_master_detail_layout() const noexcept {
    return uses_theme_layout_
        ? themed_master_detail_layout(effective_theme())
        : layout_;
}

void MasterDetailView::set_master_detail_layout(MasterDetailLayout layout) {
    require_mutable();
    if (!valid_layout(layout)) {
        throw std::invalid_argument(
            "master/detail layout values must be finite, ordered, and bounded");
    }
    if (!uses_theme_layout_ && layout_ == layout) return;
    layout_ = layout;
    uses_theme_layout_ = false;
    configure_split();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void MasterDetailView::reset_master_detail_layout_to_theme() {
    require_mutable();
    if (uses_theme_layout_) return;
    uses_theme_layout_ = true;
    configure_split();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void MasterDetailView::show_master() {
    compact_detail_visible = false;
}

void MasterDetailView::show_detail() {
    compact_detail_visible = true;
}

MasterDetailDisplayMode MasterDetailView::resolve_display_mode(
    Size available) const noexcept {
    if (display_mode.read() != MasterDetailDisplayMode::automatic) {
        return display_mode.read();
    }
    const MasterDetailLayout layout = effective_master_detail_layout();
    const double axis = layout.orientation == Orientation::vertical
        ? available.width : available.height;
    const double required = std::max(
        layout.compact_threshold,
        layout.master_minimum + layout.splitter_width +
            layout.detail_minimum);
    if (axis >= required) return MasterDetailDisplayMode::side_by_side;
    return compact_detail_visible.read() ? MasterDetailDisplayMode::detail_only
                                   : MasterDetailDisplayMode::master_only;
}

void MasterDetailView::apply_display_mode(MasterDetailDisplayMode mode,
                                          bool automatic) {
    if (effective_display_mode_ == mode) return;
    const MasterDetailDisplayMode previous = effective_display_mode_;
    const SplitCollapseOrigin origin = automatic
        ? SplitCollapseOrigin::automatic_accommodation : SplitCollapseOrigin::programmatic;
    if (!collapse_for_mode(*split_, mode, origin)) return;
    effective_display_mode_ = mode;
    publish_change(presentation_changed_,
                   MasterDetailPresentationChange{previous, mode, automatic});
}

Size MasterDetailView::measure(Size available) {
    initialize_control_tree();
    configure_split();
    const MasterDetailLayout layout = effective_master_detail_layout();
    const MasterDetailDisplayMode mode = resolve_display_mode(available);
    const Control::Ptr retained_master = master_;
    const Control::Ptr retained_detail = detail_;
    Size master_desired;
    Size detail_desired;
    if (retained_master && (*retained_master).is_alive() &&
        (*retained_master).parent() == (*split_).first_panel()) {
        master_desired = (*retained_master).measure(available);
        if (!is_alive()) return {};
        if (master_ != retained_master || !(*retained_master).is_alive() ||
            (*retained_master).parent() != (*split_).first_panel()) {
            master_desired = {};
            invalidate(Dirty::measure | Dirty::arrange);
        }
    }
    if (retained_detail && (*retained_detail).is_alive() &&
        (*retained_detail).parent() == (*split_).second_panel()) {
        detail_desired = (*retained_detail).measure(available);
        if (!is_alive()) return {};
        if (detail_ != retained_detail || !(*retained_detail).is_alive() ||
            (*retained_detail).parent() != (*split_).second_panel()) {
            detail_desired = {};
            invalidate(Dirty::measure | Dirty::arrange);
        }
    }
    if (mode == MasterDetailDisplayMode::master_only) return master_desired;
    if (mode == MasterDetailDisplayMode::detail_only) return detail_desired;
    if (layout.orientation == Orientation::vertical) {
        return {master_desired.width + layout.splitter_width +
                    detail_desired.width,
                std::max(master_desired.height, detail_desired.height)};
    }
    return {std::max(master_desired.width, detail_desired.width),
            master_desired.height + layout.splitter_width +
                detail_desired.height};
}

void MasterDetailView::arrange(Rect final_bounds) {
    initialize_control_tree();
    configure_split();
    arrange_self(final_bounds);
    apply_display_mode(resolve_display_mode(
                           {final_bounds.width, final_bounds.height}),
                       display_mode.read() == MasterDetailDisplayMode::automatic);
    set_child_layout(split_,
                     {0.0, 0.0, final_bounds.width, final_bounds.height});
}

SemanticDescriptor MasterDetailView::semantic_descriptor() const {
    SemanticDescriptor descriptor = ContainerControl::semantic_descriptor();
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.value = effective_display_mode_ ==
            MasterDetailDisplayMode::side_by_side
        ? "side-by-side"
        : effective_display_mode_ == MasterDetailDisplayMode::master_only
            ? "master"
            : "detail";
    descriptor.exposed = !descriptor.name.empty() ||
                         !descriptor.description.empty();
    return descriptor;
}

namespace {

// Its panes and members are child controls, which publish their own anatomies.
constexpr ControlAnatomy master_detail_view_anatomy{
    .control_id = "master_detail_view",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& MasterDetailView::anatomy() const noexcept {
    return master_detail_view_anatomy;
}

namespace {

// The events MasterDetailView adds to those of ContainerControl, in declaration order.
constexpr std::array<InteractionDeclaration, 1> master_detail_view_interactions{{
    {.name = "presentation_changed", .payload = "MasterDetailPresentationChange", .mutable_payload = false},
}};

} // namespace

void MasterDetailView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ContainerControl::append_interactions(interactions);
    interactions.insert(interactions.end(), master_detail_view_interactions.begin(), master_detail_view_interactions.end());
}

// property_codemod begin MasterDetailView (tools/property_families/master_detail_view.json; generated, do not edit)

void MasterDetailView::prepare_write(const decltype(display_mode)&, MasterDetailDisplayMode& incoming) {
    require_mutable();
    switch (incoming) {
    case MasterDetailDisplayMode::automatic:
    case MasterDetailDisplayMode::side_by_side:
    case MasterDetailDisplayMode::master_only:
    case MasterDetailDisplayMode::detail_only:
        break;
    default:
        throw std::invalid_argument("master/detail display mode is invalid");
    }
}

void MasterDetailView::written(const decltype(display_mode)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MasterDetailView::prepare_write(const decltype(compact_detail_visible)&, bool&) {
    require_mutable();
}

void MasterDetailView::written(const decltype(compact_detail_visible)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end MasterDetailView

} // namespace gui_forms
