// FlexPanel (stage L, phase L3): the core flex solver over child
// controls. One layout gathers the visible children once into a reused
// list, resolves each child's record and measurements into solver inputs,
// runs the solver's two calls with the cross measurement between them, and
// publishes the slots. Measurements go through measure_for_layout, so a
// child whose constraint is unchanged answers from its cache.

#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include "../../scrollable_anatomy.hpp"
#include "../layout_style_property.hpp"
#include "../out_of_flow.hpp"

#include "gui_forms/binding/property_table/property_table.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

constexpr double unbounded = std::numeric_limits<double>::infinity();

bool horizontal_main(FlexDirection direction) noexcept {
    return direction == FlexDirection::row || direction == FlexDirection::row_reverse;
}

// A definite extent for a measure query, within what a constraint admits.
AvailableExtent query_extent(double extent) noexcept {
    const double bounded = std::clamp(extent, 0.0, practical_unbounded_extent);
    return definite_extent(LogicalLength{bounded});
}

double resolve_length_percentage(const LengthPercentage& value, const FlexAvailableExtent& basis) noexcept {
    const double percentage = basis.definite ? basis.extent * value.percent / 100.0 : 0.0;
    return value.length.value() + percentage;
}

// Whether a size property is definite against `basis`: a length, or a
// percentage of a definite extent (CSS treats a percentage of an
// indefinite one as `auto`).
bool definite_size(const SizeValue& value, const FlexAvailableExtent& basis) noexcept {
    if (value.kind != SizeKind::length_percentage) return false;
    return value.value.percent == 0.0 || basis.definite;
}

double definite_size_value(const SizeValue& value, const FlexAvailableExtent& basis) noexcept {
    return std::max(0.0, resolve_length_percentage(value.value, basis));
}

// A min-width or min-height other than `auto`: a definite length, a
// content keyword, or zero.
double resolve_minimum(const SizeValue& value, const FlexAvailableExtent& basis, double min_content,
                       double max_content) noexcept {
    switch (value.kind) {
    case SizeKind::length_percentage:
        return definite_size(value, basis) ? definite_size_value(value, basis) : 0.0;
    case SizeKind::min_content:
        return min_content;
    case SizeKind::max_content:
        return max_content;
    case SizeKind::fit_content:
    case SizeKind::automatic:
    case SizeKind::none:
    case SizeKind::content:
        return 0.0;
    }
    return 0.0;
}

double resolve_maximum(const SizeValue& value, const FlexAvailableExtent& basis, double min_content,
                       double max_content) noexcept {
    switch (value.kind) {
    case SizeKind::length_percentage:
        return definite_size(value, basis) ? definite_size_value(value, basis) : unbounded;
    case SizeKind::min_content:
        return min_content;
    case SizeKind::max_content:
        return max_content;
    case SizeKind::fit_content:
    case SizeKind::automatic:
    case SizeKind::none:
    case SizeKind::content:
        return unbounded;
    }
    return unbounded;
}

// A control's own maximum: zero means none.
double maximum_or_unbounded(double maximum) noexcept {
    return maximum > 0.0 ? maximum : unbounded;
}

bool admitted_content_alignment(LayoutAlignment alignment) noexcept {
    return alignment != LayoutAlignment::automatic && alignment != LayoutAlignment::baseline;
}

bool admitted_items_alignment(LayoutAlignment alignment) noexcept {
    switch (alignment) {
    case LayoutAlignment::normal:
    case LayoutAlignment::stretch:
    case LayoutAlignment::start:
    case LayoutAlignment::end:
    case LayoutAlignment::flex_start:
    case LayoutAlignment::flex_end:
    case LayoutAlignment::center:
    case LayoutAlignment::baseline:
        return true;
    case LayoutAlignment::automatic:
    case LayoutAlignment::space_between:
    case LayoutAlignment::space_around:
    case LayoutAlignment::space_evenly:
        return false;
    }
    return false;
}

bool admitted_gap(const LengthPercentage& gap) noexcept {
    const double length = gap.length.value();
    return std::isfinite(length) && std::isfinite(gap.percent) && length >= 0.0 && gap.percent >= 0.0;
}

bool admitted_size(const SizeValue& value, bool maximum) noexcept {
    const double length = value.value.length.value();
    if (!std::isfinite(length) || !std::isfinite(value.value.percent)) return false;
    if (value.kind == SizeKind::content) return false;
    if (value.kind == SizeKind::none) return maximum;
    if (value.kind == SizeKind::automatic) return !maximum;
    return true;
}

bool admitted_item_layout(const FlexItemLayout& layout) noexcept {
    const double grow = layout.flex.grow.value();
    const double shrink = layout.flex.shrink.value();
    if (!std::isfinite(grow) || !std::isfinite(shrink) || grow < 0.0 || shrink < 0.0) return false;
    const SizeKind basis = layout.flex.basis.kind;
    const bool basis_admitted = basis == SizeKind::automatic || basis == SizeKind::content ||
                                basis == SizeKind::length_percentage;
    const double basis_length = layout.flex.basis.value.length.value();
    if (!basis_admitted || !std::isfinite(basis_length) || !std::isfinite(layout.flex.basis.value.percent)) {
        return false;
    }
    if (layout.align_self != LayoutAlignment::automatic && !admitted_items_alignment(layout.align_self)) {
        return false;
    }
    return admitted_size(layout.width, false) && admitted_size(layout.height, false) &&
           admitted_size(layout.min_width, false) && admitted_size(layout.min_height, false) &&
           admitted_size(layout.max_width, true) && admitted_size(layout.max_height, true) &&
           out_of_flow::admitted_positioning(layout.positioning);
}

const FlexItemLayout default_item_layout{};

constexpr ControlAnatomy flex_panel_anatomy{
    .control_id = "flex_panel",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

// The item_style child property: a child's flex item settings as one CSS
// inline style. Margin lengths stay the child's own margin, so only `auto`
// margins are admitted.
constexpr std::string_view item_style_name = "FlexPanel.item_style";

const PropertyDescriptor& describe_item_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"item_style", BindingValueKind::text, "Layout",
                           "The child's flex item settings as a CSS inline style: flex-grow, flex-shrink, "
                           "flex-basis, order, align-self, width, height, their minimums and maximums, auto "
                           "margins, position and insets.",
                           BindingValue{std::string{}}, invalidation::bounds},
        PropertyOperations{true, true, true, false});
    return descriptor;
}

std::string item_style_text(const FlexItemLayout& layout) {
    const LayoutStyle base{};
    LayoutStyle style = base;
    style.flex_item = layout.flex;
    style.alignment.align_self = layout.align_self;
    layout_style_property::describe_item_box(layout, style);
    std::string text = layout_style_property::describe(style, base);
    return text;
}

BindingValue read_item_style(const Component& container, const Control& child) {
    const FlexPanel& flex = static_cast<const FlexPanel&>(container);
    std::string text = item_style_text(flex.item_layout(child));
    return BindingValue{std::move(text)};
}

void write_item_style(Component& container, const Control& child, const BindingValue& value) {
    FlexPanel& flex = static_cast<FlexPanel&>(container);
    const LayoutStyle base{};
    const LayoutStyle read = layout_style_property::read(std::get<std::string>(value), base, item_style_name);
    LayoutStyle kept = base;
    FlexItemLayout layout{};
    layout.flex = read.flex_item;
    layout.align_self = read.alignment.align_self;
    kept.flex_item = read.flex_item;
    kept.alignment.align_self = read.alignment.align_self;
    layout_style_property::keep_item_box(read, kept, layout);
    layout_style_property::require_admitted(read, kept, item_style_name,
                                            "the child's margin lengths are its own margin");
    flex.set_item_layout(child, layout);
}

void reset_item_style(Component& container, const Control& child) {
    FlexPanel& flex = static_cast<FlexPanel&>(container);
    flex.set_item_layout(child, FlexItemLayout{});
}

bool item_style_serialized(const Component& container, const Control& child) {
    const FlexPanel& flex = static_cast<const FlexPanel&>(container);
    const bool serialized = !(flex.item_layout(child) == FlexItemLayout{});
    return serialized;
}

} // namespace

FlexPanel::FlexPanel(StableId stable_id) : ContainerControl(std::move(stable_id)) {}

std::span<const ChildPropertyEntry> FlexPanel::child_property_table() const noexcept {
    static constexpr std::array<ChildPropertyEntry, 1U> entries{{
        {"item_style", &describe_item_style, &read_item_style, &write_item_style, &reset_item_style,
         &item_style_serialized},
    }};
    return std::span<const ChildPropertyEntry>(entries);
}

BindingValue FlexPanel::style_property() const {
    const LayoutStyle base{};
    LayoutStyle style = base;
    style.flex.direction = flex_direction.read();
    style.flex.wrap = flex_wrap.read();
    style.alignment.justify_content = justify_content.read();
    style.alignment.align_content = align_content.read();
    style.alignment.align_items = align_items.read();
    style.row_gap = row_gap.read();
    style.column_gap = column_gap.read();
    std::string text = layout_style_property::describe(style, base);
    return BindingValue{std::move(text)};
}

// Every value is checked before the first setter runs, so a refused style
// leaves the panel as it was.
void FlexPanel::set_style_property(const BindingValue& value) {
    require_mutable();
    constexpr std::string_view name = "FlexPanel.style";
    const LayoutStyle base{};
    const LayoutStyle read = layout_style_property::read(std::get<std::string>(value), base, name);
    LayoutStyle kept = base;
    kept.flex = read.flex;
    kept.alignment.justify_content = read.alignment.justify_content;
    kept.alignment.align_content = read.alignment.align_content;
    kept.alignment.align_items = read.alignment.align_items;
    kept.row_gap = read.row_gap;
    kept.column_gap = read.column_gap;
    layout_style_property::require_admitted(read, kept, name, "");
    const bool admitted = read.flex.direction <= FlexDirection::column_reverse &&
                          read.flex.wrap <= FlexWrap::wrap_reverse &&
                          admitted_content_alignment(read.alignment.justify_content) &&
                          read.alignment.justify_content <= LayoutAlignment::space_evenly &&
                          admitted_content_alignment(read.alignment.align_content) &&
                          read.alignment.align_content <= LayoutAlignment::space_evenly &&
                          admitted_items_alignment(read.alignment.align_items) && admitted_gap(read.row_gap) &&
                          admitted_gap(read.column_gap);
    if (!admitted) throw std::invalid_argument("FlexPanel.style holds a value flex containers do not admit");
    flex_direction = read.flex.direction;
    flex_wrap = read.flex.wrap;
    justify_content = read.alignment.justify_content;
    align_content = read.alignment.align_content;
    align_items = read.alignment.align_items;
    row_gap = read.row_gap;
    column_gap = read.column_gap;
}

void FlexPanel::set_item_layout(const Control& child, const FlexItemLayout& layout) {
    require_mutable();
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument("FlexPanel item layout requires a live direct child");
    }
    if (!admitted_item_layout(layout)) {
        throw std::invalid_argument("FlexPanel item layout holds a value flex items do not admit");
    }
    const std::uint64_t id = child.runtime_id().value;
    const ItemMap::iterator found = items_.find(id);
    if (found != items_.end() && (*found).second.layout == layout) return;
    if (found == items_.end() && layout == default_item_layout) return;
    const bool was_positioned = record_for(child).positioning.position != PositionKind::static_position;
    items_[id].layout = layout;
    invalidate(invalidation::bounds);
    // A child that becomes, or stops being, a positioned box changes the
    // containing block of absolute children below it.
    const bool positioned = layout.positioning.position != PositionKind::static_position;
    if (positioned != was_positioned) notify_positioning_changed(child);
}

FlexItemLayout FlexPanel::item_layout(const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return default_item_layout;
    const FlexItemLayout layout = record_for(child);
    return layout;
}

const FlexItemLayout& FlexPanel::record_for(const Control& child) const noexcept {
    const ItemMap::const_iterator found = items_.find(child.runtime_id().value);
    if (found == items_.end()) return default_item_layout;
    return (*found).second.layout;
}

void FlexPanel::forget_departed_children() {
    ++pass_;
    for (const Control::Ptr& child : children()) {
        if (!child) continue;
        const ItemMap::iterator found = items_.find((*child).runtime_id().value);
        if (found != items_.end()) (*found).second.seen_pass = pass_;
    }
    ItemMap::iterator entry = items_.begin();
    while (entry != items_.end()) {
        if ((*entry).second.seen_pass != pass_) {
            entry = items_.erase(entry);
        } else {
            ++entry;
        }
    }
}

void FlexPanel::gather_children() {
    const Control::ChildView all = children();
    children_.clear();
    children_.reserve(all.size());
    absolute_children_.clear();
    for (const Control::Ptr& child : all) {
        if (!child || !is_current_layout_child(child) || !(*child).visible.read()) continue;
        if (record_for(*child).positioning.position == PositionKind::absolute) {
            absolute_children_.push_back(child);
            continue;
        }
        children_.push_back(child);
    }
}

FlexContainerInput FlexPanel::container_input(const SolveRequest& request) const noexcept {
    FlexContainerInput container{};
    container.direction = flex_direction.read();
    container.wrap = flex_wrap.read();
    container.justify_content = justify_content.read();
    container.align_content = align_content.read();
    container.align_items = align_items.read();
    container.row_gap = resolve_length_percentage(row_gap.read(), request.height);
    container.column_gap = resolve_length_percentage(column_gap.read(), request.width);
    container.width = request.width;
    container.height = request.height;
    const LayoutDirection direction = effective_layout_direction();
    container.layout_direction = direction == LayoutDirection::right_to_left ? LayoutDirection::right_to_left
                                                                             : LayoutDirection::left_to_right;
    return container;
}

void FlexPanel::resolve_item_inputs(const SolveRequest& request) {
    const std::size_t count = children_.size();
    inputs_.resize(count);
    contents_.resize(count);
    const bool horizontal = horizontal_main(flex_direction.read());
    const FlexAvailableExtent& main_basis = horizontal ? request.width : request.height;
    const FlexAvailableExtent& cross_basis = horizontal ? request.height : request.width;
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const FlexItemLayout& layout = record_for(child);
        FlexItemInput& input = inputs_[index];
        input = FlexItemInput{};
        input.grow = layout.flex.grow;
        input.shrink = layout.flex.shrink;
        input.order = layout.flex.order;
        input.align_self = layout.align_self;
        const Insets margin = child.margin.read();
        input.margin.top = FlexMargin{margin.top, layout.auto_margin.top};
        input.margin.right = FlexMargin{margin.right, layout.auto_margin.right};
        input.margin.bottom = FlexMargin{margin.bottom, layout.auto_margin.bottom};
        input.margin.left = FlexMargin{margin.left, layout.auto_margin.left};

        const SizeValue& main_size = horizontal ? layout.width : layout.height;
        const SizeValue& cross_size = horizontal ? layout.height : layout.width;
        const SizeValue& minimum_main = horizontal ? layout.min_width : layout.min_height;
        const SizeValue& maximum_main = horizontal ? layout.max_width : layout.max_height;
        const SizeValue& minimum_cross = horizontal ? layout.min_height : layout.min_width;
        const SizeValue& maximum_cross = horizontal ? layout.max_height : layout.max_width;
        const double cross_margins = horizontal ? margin.top + margin.bottom : margin.left + margin.right;
        const Size control_minimum = child.minimum_size.read();
        const Size control_maximum = child.maximum_size.read();
        const double control_minimum_main = horizontal ? control_minimum.width : control_minimum.height;
        const double control_minimum_cross = horizontal ? control_minimum.height : control_minimum.width;
        const double control_maximum_main =
            maximum_or_unbounded(horizontal ? control_maximum.width : control_maximum.height);
        const double control_maximum_cross =
            maximum_or_unbounded(horizontal ? control_maximum.height : control_maximum.width);

        input.definite_cross = definite_size(cross_size, cross_basis);
        const double definite_cross = input.definite_cross ? definite_size_value(cross_size, cross_basis) : 0.0;
        const LayoutAlignment self = layout.align_self == LayoutAlignment::automatic ? align_items.read() : layout.align_self;
        const bool stretched = (self == LayoutAlignment::normal || self == LayoutAlignment::stretch) &&
                               !layout.auto_margin.top && !layout.auto_margin.bottom &&
                               !layout.auto_margin.left && !layout.auto_margin.right;

        // The content query: max-content on the main axis, and on the cross
        // axis the item's definite or stretched cross size when known. Only
        // a single-line container stretches an item to its own cross size
        // (CSS Flexbox 9.4, step 8); a wrapping container's lines take their
        // cross size from their items, which stretch to the line afterwards.
        AvailableExtent cross_query = max_content_extent();
        const bool single_line = flex_wrap.read() == FlexWrap::nowrap;
        if (input.definite_cross) {
            cross_query = query_extent(definite_cross);
        } else if (!horizontal && stretched && single_line && cross_basis.definite) {
            cross_query = query_extent(cross_basis.extent - cross_margins);
        }
        const LayoutConstraint content_constraint = horizontal ? LayoutConstraint{max_content_extent(), cross_query}
                                                               : LayoutConstraint{cross_query, max_content_extent()};
        const Size max_content = child.measure_for_layout(content_constraint);
        if (!is_alive()) return;
        contents_[index].max_content = max_content;
        const double content_main = horizontal ? max_content.width : max_content.height;

        // The flex base size (CSS Flexbox 9.2.3).
        const SizeValue& basis = layout.flex.basis;
        bool base_from_content = true;
        double base = content_main;
        if (basis.kind == SizeKind::length_percentage && (basis.value.percent == 0.0 || main_basis.definite)) {
            base = std::max(0.0, resolve_length_percentage(basis.value, main_basis));
            base_from_content = false;
        } else if (basis.kind == SizeKind::automatic && definite_size(main_size, main_basis)) {
            base = definite_size_value(main_size, main_basis);
            base_from_content = false;
        }
        input.flex_base_size = base;

        // The main size's maximum, then its minimum: the automatic minimum
        // (CSS Flexbox 4.5) is the smaller of the content's minimum and a
        // definite main size, never above the maximum. It is measured only
        // when it can matter: when the item may shrink, or its base did not
        // come from its content (which is never below its minimum).
        double minimum_content_main = content_main;
        const bool automatic_minimum = minimum_main.kind == SizeKind::automatic;
        const bool minimum_needed = automatic_minimum && (input.shrink.value() > 0.0 || !base_from_content);
        if (minimum_needed && horizontal) {
            const LayoutConstraint minimum_constraint{min_content_extent(), cross_query};
            minimum_content_main = child.measure_for_layout(minimum_constraint).width;
            if (!is_alive()) return;
        }
        contents_[index].min_content_main = minimum_content_main;
        const double maximum = std::min(
            resolve_maximum(maximum_main, main_basis, minimum_content_main, content_main), control_maximum_main);
        double minimum = 0.0;
        if (minimum_needed) {
            double suggestion = minimum_content_main;
            if (definite_size(main_size, main_basis)) {
                suggestion = std::min(suggestion, definite_size_value(main_size, main_basis));
            }
            minimum = std::min(suggestion, maximum);
        } else if (!automatic_minimum) {
            minimum = resolve_minimum(minimum_main, main_basis, minimum_content_main, content_main);
        }
        input.minimum_main = std::max(minimum, control_minimum_main);
        input.maximum_main = maximum;
        const double cross_content = horizontal ? max_content.height : max_content.width;
        input.minimum_cross =
            std::max(resolve_minimum(minimum_cross, cross_basis, cross_content, cross_content), control_minimum_cross);
        input.maximum_cross =
            std::min(resolve_maximum(maximum_cross, cross_basis, cross_content, cross_content), control_maximum_cross);
    }
}

void FlexPanel::measure_item_crosses(const SolveRequest& request, const FlexLayoutWorkspace& workspace) {
    const std::size_t count = children_.size();
    crosses_.resize(count);
    const bool horizontal = horizontal_main(flex_direction.read());
    const FlexAvailableExtent& cross_basis = horizontal ? request.height : request.width;
    const BorrowedSpan<FlexItemResult> results = workspace.items();
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const FlexItemLayout& layout = record_for(child);
        const SizeValue& cross_size = horizontal ? layout.height : layout.width;
        FlexItemCross& cross = crosses_[index];
        cross = FlexItemCross{};
        if (horizontal) {
            // A row aligns its items by their first baselines at their main
            // sizes (9.4 step 8); a column's baseline alignment falls back
            // to start, so it needs none.
            const std::optional<double> baseline = child.first_baseline(results[index].main_size);
            if (!is_alive()) return;
            if (baseline) cross.baseline = *baseline;
        }
        if (inputs_[index].definite_cross) {
            cross.hypothetical_cross = definite_size_value(cross_size, cross_basis);
            continue;
        }
        const double main = results[index].main_size;
        const Size content = contents_[index].max_content;
        if (horizontal) {
            // The height at the final width; the max-content answer already
            // is that when the width did not change.
            if (main == content.width) {
                cross.hypothetical_cross = content.height;
                continue;
            }
            const LayoutConstraint constraint{query_extent(main), max_content_extent()};
            cross.hypothetical_cross = child.measure_for_layout(constraint).height;
            if (!is_alive()) return;
            continue;
        }
        // A column's item: its width is fit-content against the line's
        // available cross space (the content query already used a definite
        // width when the item is stretched against a definite container).
        double width = content.width;
        const Insets margin = child.margin.read();
        const double available = cross_basis.extent - (margin.left + margin.right);
        if (cross_basis.definite && width > available) {
            const LayoutConstraint minimum_constraint{min_content_extent(), query_extent(main)};
            const double minimum = child.measure_for_layout(minimum_constraint).width;
            if (!is_alive()) return;
            width = std::max(std::max(0.0, available), minimum);
        }
        cross.hypothetical_cross = width;
    }
}

void FlexPanel::publish_slots(const FlexLayoutWorkspace& workspace, Size content_box) {
    const LayoutDirection direction = effective_layout_direction();
    const BorrowedSpan<FlexItemResult> results = workspace.items();
    const Insets inset = padding.read();
    const Point offset = scroll_position();
    arranged_ids_.clear();
    arranged_ids_.reserve(children_.size());
    for (std::size_t index = 0U; index < children_.size(); ++index) {
        const Control::Ptr& child = children_[index];
        arranged_ids_.push_back((*child).runtime_id());
        if (index >= results.size() || !is_current_layout_child(child)) continue;
        const Rect bounds = results[index].bounds;
        const Point shift = relative_position_offset(record_for(*child).positioning, content_box, direction);
        const Rect slot{inset.left + bounds.x + overflow_shift_.x - offset.x + shift.x,
                        inset.top + bounds.y + overflow_shift_.y - offset.y + shift.y, bounds.width, bounds.height};
        set_child_layout(child, slot);
    }
}

Rect FlexPanel::content_margin_bounds(const FlexLayoutWorkspace& workspace) const noexcept {
    const BorrowedSpan<FlexItemResult> results = workspace.items();
    const std::size_t count = std::min(results.size(), inputs_.size());
    if (count == 0U) return Rect{};
    // Every item counts, an empty one too: its position still extends the
    // content.
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < count; ++index) {
        const Rect bounds = results[index].bounds;
        const BoxSides<FlexMargin>& margin = inputs_[index].margin;
        left = std::min(left, bounds.x - margin.left.length);
        top = std::min(top, bounds.y - margin.top.length);
        right = std::max(right, bounds.x + bounds.width + margin.right.length);
        bottom = std::max(bottom, bounds.y + bounds.height + margin.bottom.length);
    }
    const Rect result{left, top, right - left, bottom - top};
    return result;
}

Size FlexPanel::solve(const SolveRequest& request, FlexLayoutWorkspace& workspace) {
    gather_children();
    const FlexContainerInput container = container_input(request);
    if (children_.size() > maximum_flex_items) {
        // Refused before any child is measured (PLAN section 5): the solver
        // clears the workspace and no slot is published.
        inputs_.resize(children_.size());
        static_cast<void>(resolve_flex_main_sizes(container, inputs_, workspace));
        children_.clear();
        return {};
    }
    resolve_item_inputs(request);
    if (!is_alive()) return {};
    static_cast<void>(resolve_flex_main_sizes(container, inputs_, workspace));
    measure_item_crosses(request, workspace);
    if (!is_alive()) return {};
    static_cast<void>(place_flex_items(container, inputs_, crosses_, workspace));
    if (request.assign) publish_slots(workspace, Size{request.width.extent, request.height.extent});
    const bool horizontal = horizontal_main(flex_direction.read());
    const FlexAvailableExtent& main_space = horizontal ? request.width : request.height;
    const FlexAvailableExtent& cross_space = horizontal ? request.height : request.width;
    const double main = main_space.definite ? main_space.extent : workspace.content_main();
    const double cross = cross_space.definite ? cross_space.extent : workspace.content_cross();
    const Size used = horizontal ? Size{main, cross} : Size{cross, main};
    return used;
}

double FlexPanel::intrinsic_content_width(bool minimum) {
    gather_children();
    const bool horizontal = horizontal_main(flex_direction.read());
    const FlexAvailableExtent indefinite{};
    const double gap = resolve_length_percentage(column_gap.read(), indefinite);
    double sum = 0.0;
    double largest = 0.0;
    std::size_t counted = 0U;
    for (const Control::Ptr& retained : children_) {
        Control& child = *retained;
        const FlexItemLayout& layout = record_for(child);
        double width = 0.0;
        if (definite_size(layout.width, indefinite)) {
            width = definite_size_value(layout.width, indefinite);
        } else {
            const AvailableExtent query = minimum ? min_content_extent() : max_content_extent();
            width = child.measure_for_layout(LayoutConstraint{query, max_content_extent()}).width;
            if (!is_alive()) return 0.0;
        }
        const Size control_minimum = child.minimum_size.read();
        const Size control_maximum = child.maximum_size.read();
        const double low = std::max(resolve_minimum(layout.min_width, indefinite, width, width), control_minimum.width);
        const double high = std::min(resolve_maximum(layout.max_width, indefinite, width, width),
                                     maximum_or_unbounded(control_maximum.width));
        const double clamped = std::max(low, std::min(width, high));
        const Insets margin = child.margin.read();
        const double outer = clamped + margin.left + margin.right;
        sum += outer;
        largest = std::max(largest, outer);
        ++counted;
    }
    if (counted == 0U) return 0.0;
    // A row lays its items side by side (a wrapping row's narrowest is its
    // widest item); a column's width is its widest item.
    if (!horizontal) return largest;
    if (minimum && flex_wrap.read() != FlexWrap::nowrap) return largest;
    const double gaps = gap * static_cast<double>(counted - 1U);
    return sum + gaps;
}

Size FlexPanel::measure_intrinsic(const LayoutConstraint& constraint) {
    const Insets inset = padding.read();
    const double horizontal_padding = inset.left + inset.right;
    const double vertical_padding = inset.top + inset.bottom;
    const Rect requested = requested_bounds();

    // The border-box width: definite; filled or requested when the panel
    // is not auto-sized; otherwise from the content (fit-content for at
    // most).
    double width = 0.0;
    const AvailableExtentKind width_kind = constraint.width.kind;
    if (width_kind == AvailableExtentKind::definite) {
        width = constraint.width.extent.value();
    } else if (width_kind == AvailableExtentKind::at_most && !auto_size.read()) {
        const double available = constraint.width.extent.value();
        width = requested.width > 0.0 ? std::min(requested.width, available) : available;
    } else if (width_kind == AvailableExtentKind::min_content) {
        width = intrinsic_content_width(true) + horizontal_padding;
    } else if (width_kind == AvailableExtentKind::max_content) {
        width = intrinsic_content_width(false) + horizontal_padding;
    } else {
        const double available = constraint.width.extent.value();
        const double largest = intrinsic_content_width(false) + horizontal_padding;
        const double smallest = intrinsic_content_width(true) + horizontal_padding;
        width = std::min(largest, std::max(smallest, available));
    }
    if (!is_alive()) return {};

    double height = 0.0;
    const AvailableExtentKind height_kind = constraint.height.kind;
    if (height_kind == AvailableExtentKind::definite) {
        height = constraint.height.extent.value();
    } else if (height_kind == AvailableExtentKind::at_most && !auto_size.read()) {
        const double available = constraint.height.extent.value();
        height = requested.height > 0.0 ? std::min(requested.height, available) : available;
    } else {
        SolveRequest request{};
        request.width = FlexAvailableExtent{std::max(0.0, width - horizontal_padding), true};
        request.height = FlexAvailableExtent{};
        const Size used = solve(request, workspace_);
        if (!is_alive()) return {};
        height = used.height + vertical_padding;
        if (height_kind == AvailableExtentKind::at_most) {
            height = std::min(height, constraint.height.extent.value());
        }
    }
    const Size minimum = minimum_size.read();
    const Size maximum = maximum_size.read();
    width = std::min(std::max(width, minimum.width), maximum_or_unbounded(maximum.width));
    height = std::min(std::max(height, minimum.height), maximum_or_unbounded(maximum.height));
    const Size measured{width, height};
    return measured;
}

// Legacy measure(Size) callers (the Windows Forms containers, Dock/Anchor)
// ask as an at-most constraint.
Size FlexPanel::measure(Size available) {
    const double width = std::isfinite(available.width) ? available.width : practical_unbounded_extent;
    const double height = std::isfinite(available.height) ? available.height : practical_unbounded_extent;
    const Size bounded{std::clamp(width, 0.0, practical_unbounded_extent),
                       std::clamp(height, 0.0, practical_unbounded_extent)};
    const Size measured = measure_intrinsic(layout_constraint_at_most(bounded));
    return measured;
}

void FlexPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    forget_departed_children();
    const Insets inset = padding.read();
    const Size client{final_bounds.width, final_bounds.height};
    Size available = client;
    // With auto_scroll, the bars a content overflow needs narrow the
    // viewport; lay out against it until the two settle (at most three
    // passes, as FlowLayoutPanelCompat).
    // The scroll extent is every item's margin box, on both axes, whatever
    // the direction and wrap: lines wrapped below the viewport, a nowrap
    // line running past it, and content a reversed axis pushes past the
    // content box's start (moved inside by overflow_shift_).
    overflow_shift_ = Point{};
    if (auto_scroll.read()) {
        for (int pass = 0; pass < 3; ++pass) {
            SolveRequest request{};
            request.width = FlexAvailableExtent{std::max(0.0, available.width - inset.left - inset.right), true};
            request.height = FlexAvailableExtent{std::max(0.0, available.height - inset.top - inset.bottom), true};
            const Size used = solve(request, arranged_);
            if (!is_alive()) return;
            const Rect items = content_margin_bounds(arranged_);
            const double low_x = std::min(0.0, items.x);
            const double low_y = std::min(0.0, items.y);
            const double high_x = std::max(used.width, items.x + items.width);
            const double high_y = std::max(used.height, items.y + items.height);
            overflow_shift_ = Point{-low_x, -low_y};
            const Size extra = auto_scroll_margin.read();
            const Size content{high_x - low_x + inset.left + inset.right + extra.width,
                               high_y - low_y + inset.top + inset.bottom + extra.height};
            arrange_scroll_viewport(client, content);
            const Rect viewport = scroll_snapshot().viewport_rectangle;
            const Size next{viewport.width, viewport.height};
            if (next == available) break;
            available = next;
        }
        // Keep the view on the same content when the shift changes (and on
        // the main-axis start at first): the position moves with the shift,
        // clamped to the range the viewport just set.
        const Point moved{overflow_shift_.x - scrolled_shift_.x, overflow_shift_.y - scrolled_shift_.y};
        scrolled_shift_ = overflow_shift_;
        if (moved != Point{}) {
            const Point position = scroll_position();
            static_cast<void>(scroll_to(Point{position.x + moved.x, position.y + moved.y}));
            if (!is_alive()) return;
        }
    } else {
        scrolled_shift_ = Point{};
        arrange_scroll_viewport(client, client);
    }
    SolveRequest request{};
    request.width = FlexAvailableExtent{std::max(0.0, available.width - inset.left - inset.right), true};
    request.height = FlexAvailableExtent{std::max(0.0, available.height - inset.top - inset.bottom), true};
    request.assign = true;
    static_cast<void>(solve(request, arranged_));
    if (!is_alive()) return;
    place_absolute_children(client);
}

void FlexPanel::place_absolute_children(Size client) {
    if (absolute_children_.empty()) {
        if (registered_dependencies_) set_containing_block_dependencies({});
        registered_dependencies_ = false;
        return;
    }
    const out_of_flow::ContainingBlock block = out_of_flow::resolve_containing_block(*this);
    out_of_flow::fill_dependency_path(*this, block, dependency_path_);
    set_containing_block_dependencies(dependency_path_);
    registered_dependencies_ = true;
    out_of_flow::AbsoluteContainer container{};
    container.padding = padding.read();
    container.size = client;
    container.scroll = scroll_position();
    container.direction = effective_layout_direction();
    for (const Control::Ptr& child : absolute_children_) {
        if (!is_current_layout_child(child)) continue;
        // The block holds its ancestor, so a measure callback that changes
        // the tree leaves it readable; such a change is laid out by the
        // next pass.
        note_containing_block(*child, *block.ancestor);
        const out_of_flow::AbsoluteItem item = out_of_flow::absolute_item(record_for(*child));
        const Rect slot = out_of_flow::place_absolute_child(*child, item, block, container);
        if (!is_alive()) return;
        if (!is_current_layout_child(child)) continue;
        set_child_layout(child, slot);
    }
}

ItemPositioning FlexPanel::item_positioning(const Control& child) const noexcept {
    if (child.parent().get() != this) return ItemPositioning{};
    return record_for(child).positioning;
}

bool FlexPanel::measures_children_in_arrange() const noexcept {
    return true;
}

SemanticDescriptor FlexPanel::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}

const ControlAnatomy& FlexPanel::anatomy() const noexcept {
    return flex_panel_anatomy;
}

namespace {

// The container settings as one CSS inline style text: a hand table entry
// (extra_entries) over the panel's settings.
PropertyDescriptor flex_panel_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "style";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Layout";
    descriptor.description = "The flex container's settings as a CSS inline style: flex-direction, flex-wrap, justify-content, align-content, align-items and the gaps.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_flex_panel_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        flex_panel_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

// property_codemod begin FlexPanel (tools/property_families/flex_panel.json; generated, do not edit)

const std::array<PropertyEntry, FlexPanel::flex_panel_property_count>& FlexPanel::flex_panel_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"style", &describe_flex_panel_style,
         &read_member_entry<FlexPanel, decltype(&FlexPanel::style_property), &FlexPanel::style_property>,
         &write_member_entry<FlexPanel, decltype(&FlexPanel::set_style_property), &FlexPanel::set_style_property>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "FlexPanel's property table names a property twice");
    static const std::array<PropertyEntry, flex_panel_property_count> entries =
        join_property_tables(ScrollableControl::scrollable_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> FlexPanel::property_table() const noexcept {
    const std::array<PropertyEntry, flex_panel_property_count>& entries = flex_panel_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void FlexPanel::prepare_write(const decltype(flex_direction)&, FlexDirection& incoming) {
    require_mutable();
    if (incoming > FlexDirection::column_reverse) throw std::invalid_argument("FlexPanel flex direction is unknown");
}

void FlexPanel::written(const decltype(flex_direction)&) {
    invalidate(invalidation::bounds);
}

void FlexPanel::prepare_write(const decltype(flex_wrap)&, FlexWrap& incoming) {
    require_mutable();
    if (incoming > FlexWrap::wrap_reverse) throw std::invalid_argument("FlexPanel flex wrap is unknown");
}

void FlexPanel::written(const decltype(flex_wrap)&) {
    invalidate(invalidation::bounds);
}

void FlexPanel::prepare_write(const decltype(justify_content)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_content_alignment(incoming) || incoming > LayoutAlignment::space_evenly) throw std::invalid_argument("FlexPanel justify-content admits no such value");
}

void FlexPanel::written(const decltype(justify_content)&) {
    invalidate(invalidation::bounds);
}

void FlexPanel::prepare_write(const decltype(align_content)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_content_alignment(incoming) || incoming > LayoutAlignment::space_evenly) throw std::invalid_argument("FlexPanel align-content admits no such value");
}

void FlexPanel::written(const decltype(align_content)&) {
    invalidate(invalidation::bounds);
}

void FlexPanel::prepare_write(const decltype(align_items)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_items_alignment(incoming)) throw std::invalid_argument("FlexPanel align-items admits no such value");
}

void FlexPanel::written(const decltype(align_items)&) {
    invalidate(invalidation::bounds);
}

void FlexPanel::prepare_write(const decltype(row_gap)&, LengthPercentage& incoming) {
    require_mutable();
    if (!admitted_gap(incoming)) throw std::invalid_argument("FlexPanel row gap must be finite and nonnegative");
}

void FlexPanel::written(const decltype(row_gap)&) {
    invalidate(invalidation::bounds);
}

void FlexPanel::prepare_write(const decltype(column_gap)&, LengthPercentage& incoming) {
    require_mutable();
    if (!admitted_gap(incoming)) throw std::invalid_argument("FlexPanel column gap must be finite and nonnegative");
}

void FlexPanel::written(const decltype(column_gap)&) {
    invalidate(invalidation::bounds);
}

// property_codemod end FlexPanel

} // namespace gui_forms
