// BlockPanel (stage L, phase L5): the core block flow solver over child
// controls. One layout gathers the visible children once into a reused
// list, resolves each child's used width and height (a block item fills the
// panel, an inline item shrinks to fit; heights are measured at the used
// width through measure_for_layout, so a child whose constraint is
// unchanged answers from its cache), asks a nested BlockPanel for the
// margins its content shares with this one, and runs the solver.

#include "gui_forms/controls/block_panel.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include "../../scrollable_anatomy.hpp"
#include "../layout_style_property.hpp"
#include "../out_of_flow.hpp"
#include "gui_forms/binding/property_table/property_table.hpp"
#include "gui_forms/window.hpp"

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

AvailableExtent query_extent(double extent) noexcept {
    const double bounded = std::clamp(extent, 0.0, practical_unbounded_extent);
    return definite_extent(LogicalLength{bounded});
}

double resolve_length(const LengthPercentage& value, double basis) noexcept {
    return value.length.value() + basis * value.percent / 100.0;
}

bool plain_length(const SizeValue& value) noexcept {
    return value.kind == SizeKind::length_percentage && value.value.percent == 0.0;
}

// Whether a size resolves to a length: a plain length always, a
// percentage when its basis is known.
bool definite_size(const SizeValue& value, bool percentage_basis) noexcept {
    if (value.kind != SizeKind::length_percentage) return false;
    return value.value.percent == 0.0 || percentage_basis;
}

double maximum_or_unbounded(double maximum) noexcept {
    return maximum > 0.0 ? maximum : unbounded;
}

// A nonnegative length, or a calc() of a percentage and a length that may
// be negative (`calc(100% - 24px)`); a negative used size is zero.
bool admitted_length(const LengthPercentage& value) noexcept {
    const double length = value.length.value();
    if (!std::isfinite(length) || !std::isfinite(value.percent) || value.percent < 0.0) return false;
    return length >= 0.0 || value.percent > 0.0;
}

bool admitted_size(const SizeValue& value, bool maximum) noexcept {
    if (value.kind == SizeKind::length_percentage) return admitted_length(value.value);
    if (value.kind == SizeKind::none) return maximum;
    if (value.kind == SizeKind::automatic) return !maximum;
    return false;
}

bool admitted_item_layout(const BlockFlowItemLayout& layout) noexcept {
    if (layout.level > BlockFlowLevel::inline_level) return false;
    if (layout.vertical_align > InlineVerticalAlignment::bottom) return false;
    return admitted_size(layout.width, false) && admitted_size(layout.height, false) &&
           admitted_size(layout.min_width, false) && admitted_size(layout.min_height, false) &&
           admitted_size(layout.max_width, true) && admitted_size(layout.max_height, true) &&
           out_of_flow::admitted_positioning(layout.positioning);
}

bool admitted_strut(const LineStrut& strut) noexcept {
    const bool finite = std::isfinite(strut.ascent) && std::isfinite(strut.descent) && std::isfinite(strut.x_height);
    return finite && strut.ascent >= 0.0 && strut.descent >= 0.0 && strut.x_height >= 0.0;
}

// An item's used size bounds on one axis: its min and max properties where
// they resolve, and its control's limits. A minimum wins over a maximum.
struct SizeBounds final {
    double minimum{};
    double maximum{unbounded};
};

SizeBounds size_bounds(const SizeValue& minimum, const SizeValue& maximum, double basis, bool percentage_basis,
                       double control_minimum, double control_maximum) noexcept {
    SizeBounds bounds{};
    const double own_minimum = definite_size(minimum, percentage_basis) ? resolve_length(minimum.value, basis) : 0.0;
    const double own_maximum = definite_size(maximum, percentage_basis) ? resolve_length(maximum.value, basis)
                                                                        : unbounded;
    bounds.minimum = std::max(own_minimum, control_minimum);
    bounds.maximum = std::min(own_maximum, maximum_or_unbounded(control_maximum));
    return bounds;
}

double clamp_size(double size, const SizeBounds& bounds) noexcept {
    const double limited = std::min(size, bounds.maximum);
    return std::max(bounds.minimum, limited);
}

// A control the panel treats as an empty CSS block when its height is
// zero: one with no children that is not a container (a container lays out
// a formatting context of its own).
bool leaf_control(const Control& child) noexcept {
    return dynamic_cast<const ContainerControl*>(&child) == nullptr && child.children().empty();
}

const BlockFlowItemLayout default_item_layout{};

constexpr ControlAnatomy block_panel_anatomy{
    .control_id = "block_panel",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

// The item_style child property: a child's block flow settings as one CSS
// inline style. `display` is block (the default) or inline-block; margin
// lengths stay the child's own margin, so only `auto` margins are admitted.
constexpr std::string_view item_style_name = "BlockPanel.item_style";

LayoutStyle item_base() {
    LayoutStyle base{};
    base.display = DisplayKind::block;
    return base;
}

const PropertyDescriptor& describe_item_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"item_style", BindingValueKind::text, "Layout",
                           "The child's block flow settings as a CSS inline style: display (block or "
                           "inline-block), vertical-align, width, height, their minimums and maximums, auto "
                           "margins, position and insets.",
                           BindingValue{std::string{}}, invalidation::bounds},
        PropertyOperations{true, true, true, false});
    return descriptor;
}

std::string item_style_text(const BlockFlowItemLayout& layout) {
    const LayoutStyle base = item_base();
    LayoutStyle style = base;
    if (layout.level == BlockFlowLevel::inline_level) style.display = DisplayKind::inline_block;
    style.vertical_align = layout.vertical_align;
    layout_style_property::describe_item_box(layout, style);
    std::string text = layout_style_property::describe(style, base);
    return text;
}

BindingValue read_item_style(const Component& container, const Control& child) {
    const BlockPanel& block = static_cast<const BlockPanel&>(container);
    std::string text = item_style_text(block.item_layout(child));
    return BindingValue{std::move(text)};
}

void write_item_style(Component& container, const Control& child, const BindingValue& value) {
    BlockPanel& block = static_cast<BlockPanel&>(container);
    const LayoutStyle base = item_base();
    const LayoutStyle read = layout_style_property::read(std::get<std::string>(value), base, item_style_name);
    LayoutStyle kept = base;
    BlockFlowItemLayout layout{};
    if (read.display == DisplayKind::inline_block) {
        layout.level = BlockFlowLevel::inline_level;
        kept.display = DisplayKind::inline_block;
    }
    layout.vertical_align = read.vertical_align;
    kept.vertical_align = read.vertical_align;
    layout_style_property::keep_item_box(read, kept, layout);
    layout_style_property::require_admitted(read, kept, item_style_name,
                                            "display is block or inline-block, and the child's margin lengths "
                                            "are its own margin");
    block.set_item_layout(child, layout);
}

void reset_item_style(Component& container, const Control& child) {
    BlockPanel& block = static_cast<BlockPanel&>(container);
    block.set_item_layout(child, BlockFlowItemLayout{});
}

bool item_style_serialized(const Component& container, const Control& child) {
    const BlockPanel& block = static_cast<const BlockPanel&>(container);
    const bool serialized = !(block.item_layout(child) == BlockFlowItemLayout{});
    return serialized;
}

} // namespace

BlockPanel::BlockPanel(StableId stable_id) : ContainerControl(std::move(stable_id)) {}

std::span<const ChildPropertyEntry> BlockPanel::child_property_table() const noexcept {
    static constexpr std::array<ChildPropertyEntry, 1U> entries{{
        {"item_style", &describe_item_style, &read_item_style, &write_item_style, &reset_item_style,
         &item_style_serialized},
    }};
    return std::span<const ChildPropertyEntry>(entries);
}

BindingValue BlockPanel::style_property() const {
    const LayoutStyle base{};
    LayoutStyle style = base;
    style.text_align = text_align.read();
    std::string text = layout_style_property::describe(style, base);
    return BindingValue{std::move(text)};
}

void BlockPanel::set_style_property(const BindingValue& value) {
    require_mutable();
    constexpr std::string_view name = "BlockPanel.style";
    const LayoutStyle base{};
    const LayoutStyle read = layout_style_property::read(std::get<std::string>(value), base, name);
    LayoutStyle kept = base;
    kept.text_align = read.text_align;
    layout_style_property::require_admitted(read, kept, name, "the line strut and font are their own properties");
    text_align = read.text_align;
}

LineStrut BlockPanel::used_line_strut() const {
    const std::optional<LineStrut>& own = line_strut.read();
    if (own) return *own;
    const FontSpec used = text_font(resolved_font());
    const ResolvedTextLayout primary = resolve_text_layout_utf8(" ", used);
    const LineBoxMetrics line = line_box_metrics(primary, std::nullopt);
    const LineStrut strut{line.baseline, std::max(0.0, line.height - line.baseline), line.x_height};
    return strut;
}

FontSpec BlockPanel::resolved_font() const noexcept {
    const std::optional<FontSpec>& own = font.read();
    if (own) return *own;
    return effective_theme().structure().typography.field;
}

void BlockPanel::prepare_write(const decltype(font)&, std::optional<FontSpec>& incoming) {
    require_mutable();
    if (incoming && !valid_font_spec(*incoming)) throw std::invalid_argument("BlockPanel font is invalid");
    set_font_from_code(incoming.has_value());
}

// The font feeds only the strut, so a set strut makes a font change moot.
void BlockPanel::written(const decltype(font)&) {
    if (!line_strut.read()) invalidate(invalidation::bounds);
}

void BlockPanel::set_item_layout(const Control& child, const BlockFlowItemLayout& layout) {
    require_mutable();
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument("BlockPanel item layout requires a live direct child");
    }
    if (!admitted_item_layout(layout)) {
        throw std::invalid_argument("BlockPanel item layout holds a value block flow items do not admit");
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
    // A nested panel's own layout reads its record here (whether it shares
    // this panel's margins, whether its height is definite), so its
    // measures are stale too.
    if (dynamic_cast<const BlockPanel*>(&child) == nullptr) return;
    for (const Control::Ptr& candidate : children()) {
        if (candidate.get() == &child) (*candidate).invalidate(invalidation::bounds);
    }
}

BlockFlowItemLayout BlockPanel::item_layout(const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return default_item_layout;
    const BlockFlowItemLayout layout = record_for(child);
    return layout;
}

const BlockFlowItemLayout& BlockPanel::record_for(const Control& child) const noexcept {
    const ItemMap::const_iterator found = items_.find(child.runtime_id().value);
    if (found == items_.end()) return default_item_layout;
    return (*found).second.layout;
}

void BlockPanel::forget_departed_children() {
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

bool BlockPanel::shares_parent_flow() const noexcept {
    if (auto_scroll.read()) return false;
    const Control::Ptr held = parent();
    const BlockPanel* const flow = dynamic_cast<const BlockPanel*>(held.get());
    if (flow == nullptr) return false;
    const BlockFlowItemLayout& record = (*flow).record_for(*this);
    return record.level == BlockFlowLevel::block && record.positioning.position != PositionKind::absolute;
}

bool BlockPanel::percentage_heights_definite() const noexcept {
    if (!shares_parent_flow()) return true;
    const Control::Ptr held = parent();
    const BlockPanel& flow = static_cast<const BlockPanel&>(*held);
    return plain_length(flow.record_for(*this).height);
}

void BlockPanel::gather_children() {
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

void BlockPanel::resolve_item_inputs(const SolveRequest& request) {
    const std::size_t count = children_.size();
    inputs_.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const BlockFlowItemLayout& layout = record_for(child);
        BlockFlowItemInput& input = inputs_[index];
        input = BlockFlowItemInput{};
        input.level = layout.level;
        input.vertical_align = layout.vertical_align;
        const bool block = layout.level == BlockFlowLevel::block;
        const Insets margin = child.margin.read();
        input.auto_margin_left = block && layout.auto_margin.left;
        input.auto_margin_right = block && layout.auto_margin.right;
        input.margin.top = layout.auto_margin.top ? 0.0 : margin.top;
        input.margin.bottom = layout.auto_margin.bottom ? 0.0 : margin.bottom;
        input.margin.left = layout.auto_margin.left ? 0.0 : margin.left;
        input.margin.right = layout.auto_margin.right ? 0.0 : margin.right;
        const double margins = input.margin.left + input.margin.right;
        const Size control_minimum = child.minimum_size.read();
        const Size control_maximum = child.maximum_size.read();

        const SizeBounds widths = size_bounds(layout.min_width, layout.max_width, request.width, true,
                                              control_minimum.width, control_maximum.width);
        double width = 0.0;
        if (definite_size(layout.width, true)) {
            width = resolve_length(layout.width.value, request.width);
        } else if (block) {
            width = request.width - margins;
        } else {
            // Shrink-to-fit (CSS 2.1 10.3.9).
            const LayoutConstraint minimum_constraint{min_content_extent(), max_content_extent()};
            const double smallest = child.measure_for_layout(minimum_constraint).width;
            if (!is_alive()) return;
            const LayoutConstraint maximum_constraint{max_content_extent(), max_content_extent()};
            const double largest = child.measure_for_layout(maximum_constraint).width;
            if (!is_alive()) return;
            width = std::min(std::max(smallest, request.width - margins), largest);
        }
        width = std::max(0.0, clamp_size(width, widths));
        input.width = width;

        const SizeBounds heights = size_bounds(layout.min_height, layout.max_height, request.height,
                                               request.height_definite, control_minimum.height,
                                               control_maximum.height);
        const bool definite_height = definite_size(layout.height, request.height_definite);
        double content_height = 0.0;
        if (definite_height) {
            content_height = resolve_length(layout.height.value, request.height);
        } else {
            const LayoutConstraint constraint{query_extent(width), max_content_extent()};
            content_height = child.measure_for_layout(constraint).height;
            if (!is_alive()) return;
        }
        const double height = std::max(0.0, clamp_size(content_height, heights));
        input.height = height;
        if (!block) {
            // An inline item sits on the line by its last line box's
            // baseline, as an inline-block does.
            const std::optional<double> baseline = child.last_baseline(width);
            if (!is_alive()) return;
            if (baseline) input.baseline = *baseline;
            continue;
        }

        BlockPanel* const nested = dynamic_cast<BlockPanel*>(&child);
        if (nested != nullptr) {
            const BlockFlowSharedMargins shared = (*nested).shared_margins(width);
            if (!is_alive()) return;
            input.leading_inner = shared.leading;
            // The trailing margins pass only through an auto height the
            // item's own bounds left alone (Chromium drops them otherwise).
            if (!definite_height && height == content_height) input.trailing_inner = shared.trailing;
            input.collapses_through = shared.open && height == 0.0;
        } else {
            input.collapses_through = height == 0.0 && leaf_control(child);
        }
    }
}

void BlockPanel::publish_slots(const BlockFlowLayoutWorkspace& workspace, Size content_box) {
    const LayoutDirection direction = effective_layout_direction();
    const BorrowedSpan<BlockFlowItemResult> results = workspace.items();
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
        const Rect slot{inset.left + bounds.x - offset.x + shift.x, inset.top + bounds.y - offset.y + shift.y,
                        bounds.width, bounds.height};
        set_child_layout(child, slot);
    }
}

BlockPanel::SolveResult BlockPanel::solve(const SolveRequest& request, BlockFlowLayoutWorkspace& workspace) {
    gather_children();
    SolveResult result{};
    const Insets inset = padding.read();
    const bool shares = shares_parent_flow();
    BlockFlowContainerInput container{};
    container.width = request.width;
    container.text_align = text_align.read();
    container.strut = used_line_strut();
    container.shares_leading_margin = shares && inset.top == 0.0;
    const LayoutDirection direction = effective_layout_direction();
    container.layout_direction = direction == LayoutDirection::right_to_left ? LayoutDirection::right_to_left
                                                                             : LayoutDirection::left_to_right;
    if (children_.size() > maximum_block_flow_items) {
        // Refused before any child is measured (PLAN section 5): the solver
        // clears the workspace and no slot is published.
        inputs_.resize(children_.size());
        result.outcome = solve_block_flow(container, inputs_, workspace);
        children_.clear();
        return result;
    }
    resolve_item_inputs(request);
    if (!is_alive()) return result;
    result.outcome = solve_block_flow(container, inputs_, workspace);
    if (result.outcome != BlockFlowSolveOutcome::solved) return result;
    if (request.assign) publish_slots(workspace, Size{request.width, request.height});
    double content = workspace.content_height();
    if (container.shares_leading_margin) result.shared.leading = workspace.leading_margin();
    result.shared.open = workspace.leading_open();
    if (shares && inset.bottom == 0.0) {
        result.shared.trailing = workspace.trailing_margin();
    } else {
        content += workspace.trailing_margin().collapsed();
    }
    result.content_height = std::max(0.0, content);
    return result;
}

double BlockPanel::intrinsic_content_width(BlockFlowIntrinsicKind kind) {
    gather_children();
    const std::size_t count = children_.size();
    if (count > maximum_block_flow_items) return 0.0;
    inputs_.resize(count);
    const AvailableExtent extent =
        kind == BlockFlowIntrinsicKind::min_content ? min_content_extent() : max_content_extent();
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const BlockFlowItemLayout& layout = record_for(child);
        BlockFlowItemInput& input = inputs_[index];
        input = BlockFlowItemInput{};
        input.level = layout.level;
        const Insets margin = child.margin.read();
        input.margin.left = layout.auto_margin.left ? 0.0 : margin.left;
        input.margin.right = layout.auto_margin.right ? 0.0 : margin.right;
        // Percentages have no basis here and read as `auto` (CSS Sizing 3,
        // 5.2.1's cyclic percentages).
        const SizeBounds widths = size_bounds(layout.min_width, layout.max_width, 0.0, false,
                                              child.minimum_size.read().width, child.maximum_size.read().width);
        double width = 0.0;
        if (plain_length(layout.width)) {
            width = layout.width.value.length.value();
        } else {
            const LayoutConstraint constraint{extent, max_content_extent()};
            width = child.measure_for_layout(constraint).width;
            if (!is_alive()) return 0.0;
        }
        input.width = std::max(0.0, clamp_size(width, widths));
    }
    const double width = block_flow_intrinsic_width(inputs_, kind);
    return width;
}

BlockFlowSharedMargins BlockPanel::shared_margins(double width) {
    if (!shares_parent_flow()) return BlockFlowSharedMargins{};
    if (shared_entry_.valid && shared_entry_.width == width && shared_entry_.generation == measure_generation()) {
        return shared_entry_.margins;
    }
    const Insets inset = padding.read();
    SolveRequest request{};
    request.width = std::max(0.0, width - inset.left - inset.right);
    const SolveResult solved = solve(request, workspace_);
    if (!is_alive()) return BlockFlowSharedMargins{};
    BlockFlowSharedMargins margins = solved.shared;
    const double height = solved.content_height + inset.top + inset.bottom;
    const double bounded = std::min(std::max(height, minimum_size.read().height), maximum_or_unbounded(maximum_size.read().height));
    if (bounded != height) margins.trailing = CollapsedMargin{};
    shared_entry_ = SharedMarginsEntry{width, measure_generation(), margins, true};
    return margins;
}

Size BlockPanel::measure_intrinsic(const LayoutConstraint& constraint) {
    const Insets inset = padding.read();
    const double horizontal_padding = inset.left + inset.right;
    const double vertical_padding = inset.top + inset.bottom;
    const Rect requested = requested_bounds();

    // The border-box width: definite; filled or requested when the panel
    // is not auto-sized; otherwise its content's min-content or
    // max-content width (fit-content for at most).
    double width = 0.0;
    const AvailableExtentKind width_kind = constraint.width.kind;
    if (width_kind == AvailableExtentKind::definite) {
        width = constraint.width.extent.value();
    } else if (width_kind == AvailableExtentKind::at_most && !auto_size.read()) {
        const double available = constraint.width.extent.value();
        width = requested.width > 0.0 ? std::min(requested.width, available) : available;
    } else if (width_kind == AvailableExtentKind::min_content) {
        width = intrinsic_content_width(BlockFlowIntrinsicKind::min_content) + horizontal_padding;
    } else if (width_kind == AvailableExtentKind::max_content) {
        width = intrinsic_content_width(BlockFlowIntrinsicKind::max_content) + horizontal_padding;
    } else {
        const double available = constraint.width.extent.value();
        const double largest = intrinsic_content_width(BlockFlowIntrinsicKind::max_content) + horizontal_padding;
        if (!is_alive()) return {};
        const double smallest = intrinsic_content_width(BlockFlowIntrinsicKind::min_content) + horizontal_padding;
        width = std::min(largest, std::max(smallest, available));
    }
    if (!is_alive()) return {};

    const Size minimum = minimum_size.read();
    const Size maximum = maximum_size.read();
    double height = 0.0;
    const AvailableExtentKind height_kind = constraint.height.kind;
    if (height_kind == AvailableExtentKind::definite) {
        height = constraint.height.extent.value();
    } else if (height_kind == AvailableExtentKind::at_most && !auto_size.read()) {
        const double available = constraint.height.extent.value();
        height = requested.height > 0.0 ? std::min(requested.height, available) : available;
    } else {
        SolveRequest request{};
        request.width = std::max(0.0, width - horizontal_padding);
        const SolveResult solved = solve(request, workspace_);
        if (!is_alive()) return {};
        height = solved.content_height + vertical_padding;
        BlockFlowSharedMargins margins = solved.shared;
        const double bounded = std::min(std::max(height, minimum.height), maximum_or_unbounded(maximum.height));
        if (bounded != height) margins.trailing = CollapsedMargin{};
        if (shares_parent_flow()) shared_entry_ = SharedMarginsEntry{width, measure_generation(), margins, true};
        if (height_kind == AvailableExtentKind::at_most) {
            height = std::min(height, constraint.height.extent.value());
        }
    }
    width = std::min(std::max(width, minimum.width), maximum_or_unbounded(maximum.width));
    height = std::min(std::max(height, minimum.height), maximum_or_unbounded(maximum.height));
    const Size measured{width, height};
    return measured;
}

// Legacy measure(Size) callers (the Windows Forms containers, Dock/Anchor)
// ask as an at-most constraint.
Size BlockPanel::measure(Size available) {
    const double width = std::isfinite(available.width) ? available.width : practical_unbounded_extent;
    const double height = std::isfinite(available.height) ? available.height : practical_unbounded_extent;
    const Size bounded{std::clamp(width, 0.0, practical_unbounded_extent),
                       std::clamp(height, 0.0, practical_unbounded_extent)};
    const Size measured = measure_intrinsic(layout_constraint_at_most(bounded));
    return measured;
}

void BlockPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    forget_departed_children();
    const Insets inset = padding.read();
    const Size client{final_bounds.width, final_bounds.height};
    const bool percentage_basis = percentage_heights_definite();
    Size available = client;
    // With auto_scroll, the bars a content overflow needs narrow the
    // viewport; lay out against it until the two settle (at most three
    // passes, as FlowLayoutPanelCompat).
    if (auto_scroll.read()) {
        for (int pass = 0; pass < 3; ++pass) {
            SolveRequest request{};
            request.width = std::max(0.0, available.width - inset.left - inset.right);
            request.height = std::max(0.0, available.height - inset.top - inset.bottom);
            request.height_definite = percentage_basis;
            const SolveResult solved = solve(request, arranged_);
            if (!is_alive()) return;
            double content_width = 0.0;
            for (const BlockFlowItemResult& item : arranged_.items()) {
                content_width = std::max(content_width, item.bounds.x + item.bounds.width);
            }
            const Size extra = auto_scroll_margin.read();
            const Size content{content_width + inset.left + inset.right + extra.width,
                               solved.content_height + inset.top + inset.bottom + extra.height};
            arrange_scroll_viewport(client, content);
            const Rect viewport = scroll_snapshot().viewport_rectangle;
            const Size next{viewport.width, viewport.height};
            if (next == available) break;
            available = next;
        }
    } else {
        arrange_scroll_viewport(client, client);
    }
    SolveRequest request{};
    request.width = std::max(0.0, available.width - inset.left - inset.right);
    request.height = std::max(0.0, available.height - inset.top - inset.bottom);
    request.height_definite = percentage_basis;
    request.assign = true;
    const SolveResult solved = solve(request, arranged_);
    arranged_outcome_ = solved.outcome;
    if (solved.outcome != BlockFlowSolveOutcome::solved) arranged_ids_.clear();
    if (!is_alive()) return;
    place_absolute_children(client);
}

void BlockPanel::place_absolute_children(Size client) {
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

ItemPositioning BlockPanel::item_positioning(const Control& child) const noexcept {
    if (child.parent().get() != this) return ItemPositioning{};
    return record_for(child).positioning;
}

bool BlockPanel::measures_children_in_arrange() const noexcept {
    return true;
}

SemanticDescriptor BlockPanel::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}

const ControlAnatomy& BlockPanel::anatomy() const noexcept {
    return block_panel_anatomy;
}

namespace {

// The container settings as one CSS inline style text: a hand table entry
// (extra_entries) over the panel's settings.
PropertyDescriptor block_panel_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "style";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Layout";
    descriptor.description = "The block container's settings as a CSS inline style: text-align.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_block_panel_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        block_panel_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

// property_codemod begin BlockPanel (tools/property_families/block_panel.json; generated, do not edit)

const std::array<PropertyEntry, BlockPanel::block_panel_property_count>& BlockPanel::block_panel_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"style", &describe_block_panel_style,
         &read_member_entry<BlockPanel, decltype(&BlockPanel::style_property), &BlockPanel::style_property>,
         &write_member_entry<BlockPanel, decltype(&BlockPanel::set_style_property), &BlockPanel::set_style_property>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "BlockPanel's property table names a property twice");
    static const std::array<PropertyEntry, block_panel_property_count> entries =
        join_property_tables(ScrollableControl::scrollable_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> BlockPanel::property_table() const noexcept {
    const std::array<PropertyEntry, block_panel_property_count>& entries = block_panel_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void BlockPanel::prepare_write(const decltype(text_align)&, TextAlignment& incoming) {
    require_mutable();
    if (incoming > TextAlignment::center) throw std::invalid_argument("BlockPanel text-align is unknown");
}

void BlockPanel::written(const decltype(text_align)&) {
    invalidate(invalidation::bounds);
}

void BlockPanel::prepare_write(const decltype(line_strut)&, std::optional<LineStrut>& incoming) {
    require_mutable();
    if (incoming && !admitted_strut(*incoming)) {
        throw std::invalid_argument("BlockPanel line strut values must be finite and nonnegative");
    }
}

void BlockPanel::written(const decltype(line_strut)&) {
    invalidate(invalidation::bounds);
}

// property_codemod end BlockPanel

} // namespace gui_forms
