// GridPanel (stage L, phase L4): the core grid solver over child
// controls. One layout gathers the visible children once into a reused
// list, copies each child's placement record for the pass, and runs the
// solver's four calls with the measurements between them: the items' width
// contributions before the columns are sized, and their heights at their
// column widths before the rows are. Measurements go through
// measure_for_layout, so a child whose constraint is unchanged answers
// from its cache, and an item stretched over fixed columns is asked only
// its height.

#include "gui_forms/controls/grid_panel.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include "../../scrollable_anatomy.hpp"
#include "../layout_style_property.hpp"
#include "../out_of_flow.hpp"

#include "gui_forms/binding/property_table/property_table.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

namespace {

constexpr double unbounded = std::numeric_limits<double>::infinity();

// A definite extent for a measure query, within what a constraint admits.
AvailableExtent query_extent(double extent) noexcept {
    const double bounded = std::clamp(extent, 0.0, practical_unbounded_extent);
    return definite_extent(LogicalLength{bounded});
}

double resolve_gap(const LengthPercentage& gap, const GridAvailableExtent& basis) noexcept {
    const double percentage = basis.kind == GridSizingKind::definite ? basis.extent * gap.percent / 100.0 : 0.0;
    return gap.length.value() + percentage;
}

// A size property as a length with no percentage: what a contribution can
// use before the item's grid area is known (a percentage of the area is
// `auto` until then).
bool plain_length(const SizeValue& value) noexcept {
    return value.kind == SizeKind::length_percentage && value.value.percent == 0.0;
}

double plain_length_value(const SizeValue& value) noexcept {
    return std::max(0.0, value.value.length.value());
}

// A control's own maximum: zero means none.
double maximum_or_unbounded(double maximum) noexcept {
    return maximum > 0.0 ? maximum : unbounded;
}

bool admitted_length(const LengthPercentage& value) noexcept {
    const double length = value.length.value();
    return std::isfinite(length) && std::isfinite(value.percent) && length >= 0.0 && value.percent >= 0.0;
}

bool admitted_content_alignment(LayoutAlignment alignment) noexcept {
    return alignment != LayoutAlignment::automatic && alignment != LayoutAlignment::baseline &&
           alignment <= LayoutAlignment::space_evenly;
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

bool admitted_self_alignment(LayoutAlignment alignment) noexcept {
    return alignment == LayoutAlignment::automatic || admitted_items_alignment(alignment);
}

bool admitted_breadth(const TrackBreadth& breadth, bool maximum) noexcept {
    switch (breadth.kind) {
    case TrackBreadthKind::length_percentage:
        return admitted_length(breadth.length);
    case TrackBreadthKind::fraction: {
        // `fr` is a maximum only (a lone `<n>fr` is minmax(auto, <n>fr)).
        const double fraction = breadth.fraction.value();
        return maximum && std::isfinite(fraction) && fraction >= 0.0;
    }
    case TrackBreadthKind::min_content:
    case TrackBreadthKind::max_content:
    case TrackBreadthKind::automatic:
        return true;
    }
    return false;
}

bool admitted_track_size(const GridTrackSize& size) noexcept {
    if (size.fit_content) return admitted_length(size.fit_content_limit);
    return admitted_breadth(size.minimum, false) && admitted_breadth(size.maximum, true);
}

// CSS Grid's <fixed-size>: a fixed breadth at one end at least.
bool fixed_track_size(const GridTrackSize& size) noexcept {
    if (size.fit_content) return false;
    return size.minimum.kind == TrackBreadthKind::length_percentage ||
           size.maximum.kind == TrackBreadthKind::length_percentage;
}

bool admitted_track_sizes(std::span<const GridTrackSize> sizes) noexcept {
    return std::all_of(sizes.begin(), sizes.end(), admitted_track_size);
}

bool admitted_track_list(const GridTrackList& list) noexcept {
    const std::size_t counted = list.tracks.size() + list.auto_repeat_tracks.size();
    if (counted > maximum_grid_tracks) return false;
    if (!admitted_track_sizes(list.tracks) || !admitted_track_sizes(list.auto_repeat_tracks)) return false;
    if (list.auto_repeat == AutoRepeatKind::none) {
        return list.auto_repeat_tracks.empty() && list.auto_repeat_index == 0U;
    }
    if (list.auto_repeat > AutoRepeatKind::auto_fit || list.auto_repeat_tracks.empty()) return false;
    if (list.auto_repeat_index > list.tracks.size()) return false;
    // CSS's <auto-track-list>: with an automatic repeat, every track is
    // fixed.
    const bool repeated_fixed =
        std::all_of(list.auto_repeat_tracks.begin(), list.auto_repeat_tracks.end(), fixed_track_size);
    const bool others_fixed = std::all_of(list.tracks.begin(), list.tracks.end(), fixed_track_size);
    return repeated_fixed && others_fixed;
}

bool admitted_template_areas(const GridTemplateAreas& areas) {
    if (areas.rows == 0U || areas.columns == 0U) {
        return areas.rows == 0U && areas.columns == 0U && areas.areas.empty();
    }
    if (areas.rows > maximum_grid_tracks || areas.columns > maximum_grid_tracks) return false;
    std::unordered_set<std::string> names{};
    names.reserve(areas.areas.size());
    for (const GridNamedArea& area : areas.areas) {
        if (area.name.empty() || !names.insert(area.name).second) return false;
        if (area.row_start >= area.row_end || area.row_end > areas.rows) return false;
        if (area.column_start >= area.column_end || area.column_end > areas.columns) return false;
    }
    return true;
}

// One line reference: a line number other than zero within the track
// limit, a span of at least one within it, or an area's name.
bool admitted_line(const GridLine& line) noexcept {
    const std::int64_t limit = static_cast<std::int64_t>(maximum_grid_tracks);
    const std::int64_t number = line.number;
    switch (line.kind) {
    case GridLineKind::automatic:
        return true;
    case GridLineKind::line:
        return number != 0 && number >= -limit && number <= limit;
    case GridLineKind::span:
        return number >= 1 && number <= limit;
    case GridLineKind::area:
        return !line.area.empty();
    }
    return false;
}

// Width and height: auto or a length-percentage; a minimum also `auto`; a
// maximum `none` instead. The solver reads no content keywords.
bool admitted_size(const SizeValue& value, bool maximum) noexcept {
    if (value.kind == SizeKind::length_percentage) return admitted_length(value.value);
    if (value.kind == SizeKind::none) return maximum;
    if (value.kind == SizeKind::automatic) return !maximum;
    return false;
}

bool admitted_item_layout(const GridItemLayout& layout) noexcept {
    const GridItemStyle& placement = layout.placement;
    if (!admitted_line(placement.column_start) || !admitted_line(placement.column_end) ||
        !admitted_line(placement.row_start) || !admitted_line(placement.row_end)) {
        return false;
    }
    if (!admitted_self_alignment(layout.justify_self) || !admitted_self_alignment(layout.align_self)) return false;
    return admitted_size(layout.width, false) && admitted_size(layout.height, false) &&
           admitted_size(layout.min_width, false) && admitted_size(layout.min_height, false) &&
           admitted_size(layout.max_width, true) && admitted_size(layout.max_height, true) &&
           out_of_flow::admitted_positioning(layout.positioning);
}

// Whether every track of `range` has a fixed size at both ends against the
// sizing extent: then the item's contributions do not change its column.
bool fixed_tracks(std::span<const GridTrack> tracks, std::uint32_t first, std::uint32_t end, bool definite) noexcept {
    for (std::uint32_t index = first; index < end; ++index) {
        const GridTrackSize& size = tracks[index].sizing;
        if (size.fit_content || size.minimum.kind != TrackBreadthKind::length_percentage ||
            size.maximum.kind != TrackBreadthKind::length_percentage) {
            return false;
        }
        if (!definite && (size.minimum.length.percent != 0.0 || size.maximum.length.percent != 0.0)) return false;
    }
    return true;
}

// Whether the item fills its area's width whatever its content (CSS Box
// Alignment's stretch of an auto-sized item without auto margins).
bool stretched_width(const GridItemLayout& layout, LayoutAlignment justify_items) noexcept {
    if (layout.width.kind != SizeKind::automatic) return false;
    if (layout.auto_margin.left || layout.auto_margin.right) return false;
    const LayoutAlignment used = layout.justify_self == LayoutAlignment::automatic ? justify_items : layout.justify_self;
    return used == LayoutAlignment::normal || used == LayoutAlignment::stretch;
}

// The item's own size bounds on one axis, as lengths: its min and max
// properties where they are plain lengths, and its control's limits.
struct ContentBounds final {
    double minimum{};
    double maximum{unbounded};
};

ContentBounds content_bounds(const SizeValue& minimum, const SizeValue& maximum, double control_minimum,
                             double control_maximum) noexcept {
    ContentBounds bounds{};
    bounds.minimum = std::max(plain_length(minimum) ? plain_length_value(minimum) : 0.0, control_minimum);
    bounds.maximum = std::min(plain_length(maximum) ? plain_length_value(maximum) : unbounded,
                              maximum_or_unbounded(control_maximum));
    return bounds;
}

double clamp_content(double size, const ContentBounds& bounds) noexcept {
    const double limited = std::min(size, bounds.maximum);
    return std::max(bounds.minimum, limited);
}

// The used min and max as the solver reads them: the item's own, with its
// control's limits applied where its own is a plain length or `none`.
SizeValue used_minimum(const SizeValue& minimum, double control_minimum) noexcept {
    if (!plain_length(minimum) || control_minimum <= plain_length_value(minimum)) return minimum;
    const SizeValue raised{SizeKind::length_percentage, LengthPercentage{LogicalLength{control_minimum}, 0.0}};
    return raised;
}

SizeValue used_maximum(const SizeValue& maximum, double control_maximum) noexcept {
    if (control_maximum <= 0.0) return maximum;
    if (maximum.kind == SizeKind::none || (plain_length(maximum) && control_maximum < plain_length_value(maximum))) {
        const SizeValue lowered{SizeKind::length_percentage, LengthPercentage{LogicalLength{control_maximum}, 0.0}};
        return lowered;
    }
    return maximum;
}

static_assert(maximum_grid_span_extent == maximum_grid_tracks, "spans end within the grid's track limit");

const GridItemLayout default_item_layout{};
const GridPlacement automatic_placement{};

// Whether every placement line of `lines` is automatic: the item layout
// does not place the child.
bool lines_automatic(const GridItemStyle& lines) noexcept {
    return lines.column_start.kind == GridLineKind::automatic && lines.column_end.kind == GridLineKind::automatic &&
           lines.row_start.kind == GridLineKind::automatic && lines.row_end.kind == GridLineKind::automatic;
}

const char* span_refusal_text(GridSpanOutcome outcome) noexcept {
    switch (outcome) {
    case GridSpanOutcome::extent_invalid:
        return "GridPanel template areas hold an area past the grid's limit";
    case GridSpanOutcome::overlaps:
        return "GridPanel template areas overlap";
    case GridSpanOutcome::name_repeated:
        return "GridPanel template areas must be distinct named rectangles within it";
    case GridSpanOutcome::covers_placed_child:
        return "GridPanel template areas cover a cell a child is placed at";
    case GridSpanOutcome::in_use:
        return "GridPanel template areas remove an area a child is placed into";
    case GridSpanOutcome::applied:
    case GridSpanOutcome::not_found:
    default:
        // Not refusals of a template-areas list (and an unknown outcome):
        // the general text below.
        break;
    }
    return "GridPanel template areas are refused";
}

void set_area_line(GridLine& line, const std::string& area) {
    line.kind = GridLineKind::area;
    line.number = 0;
    line.area = area;
}

void set_number_line(GridLine& line, GridLineKind kind, std::uint32_t number) {
    line.kind = kind;
    line.number = static_cast<std::int32_t>(number);
    line.area.clear();
}

// The child properties GridPanel provides (study 10.9): two views of one
// placement.
PropertyDescriptor area_descriptor() {
    PropertyDescriptor descriptor{"area", BindingValueKind::text, "Layout",
                                  "The span the child is placed into by name; empty when it is not placed by area.",
                                  BindingValue{std::string{}}, invalidation::bounds};
    // The write is the child view's assignment (generated forms assign it).
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_area() {
    static const PropertyDescriptor descriptor =
        property_table_descriptor(area_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

BindingValue read_area(const Component& container, const Control& child) {
    const GridPanel& grid = static_cast<const GridPanel&>(container);
    std::string value = grid.area[child].read();
    return BindingValue{std::move(value)};
}

void write_area(Component& container, const Control& child, const BindingValue& value) {
    GridPanel& grid = static_cast<GridPanel&>(container);
    grid.area[child] = std::get<std::string>(value);
}

// Reset clears only a placement by area; one by cell stays.
void reset_area(Component& container, const Control& child) {
    GridPanel& grid = static_cast<GridPanel&>(container);
    if (grid.placement(child).form != GridPlacement::Form::area) return;
    grid.area[child] = std::string{};
}

// A cell has no default: {0, 0} is a placement, not the automatic one.
const PropertyDescriptor& describe_cell() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"cell", BindingValueKind::point, "Layout",
                           "The cell the child is placed at, row then column; a span's anchor places it into "
                           "the span.",
                           std::nullopt, invalidation::bounds},
        PropertyOperations{true, true, true, false});
    return descriptor;
}

BindingValue read_cell(const Component& container, const Control& child) {
    const GridPanel& grid = static_cast<const GridPanel&>(container);
    const GridCell cell = grid.cell[child].read();
    const BindingValue value = PropertyValueTraits<GridCell>::to_binding(cell);
    return value;
}

// A coordinate of a cell's point form: a whole number in [0, maximum).
bool cell_coordinate(double value, std::uint32_t& coordinate) noexcept {
    if (!std::isfinite(value) || value < 0.0 || value >= static_cast<double>(maximum_grid_span_extent)) return false;
    if (std::floor(value) != value) return false;
    coordinate = static_cast<std::uint32_t>(value);
    return true;
}

void write_cell(Component& container, const Control& child, const BindingValue& value) {
    GridPanel& grid = static_cast<GridPanel&>(container);
    const Point point = std::get<Point>(value);
    GridCell cell{};
    if (!cell_coordinate(point.x, cell.row) || !cell_coordinate(point.y, cell.column)) {
        throw std::invalid_argument("GridPanel cell needs a row and a column, whole numbers from 0 to 9999");
    }
    grid.cell[child] = cell;
}

void reset_cell(Component& container, const Control& child) {
    GridPanel& grid = static_cast<GridPanel&>(container);
    if (grid.placement(child).form != GridPlacement::Form::cell) return;
    grid.area[child] = std::string{};
}

bool area_serialized(const Component& container, const Control& child) {
    const GridPanel& grid = static_cast<const GridPanel&>(container);
    const bool serialized = grid.placement(child).form == GridPlacement::Form::area;
    return serialized;
}

bool cell_serialized(const Component& container, const Control& child) {
    const GridPanel& grid = static_cast<const GridPanel&>(container);
    const bool serialized = grid.placement(child).form == GridPlacement::Form::cell;
    return serialized;
}

// The item_style child property: a child's item settings as one CSS inline
// style. Placement stays the area and cell properties (and spans the span
// blocks), and margin lengths the child's own margin, so those are refused.
constexpr std::string_view item_style_name = "GridPanel.item_style";
constexpr std::string_view item_style_guidance =
    "place the child with its area or cell property and define spans with span blocks; its margin lengths "
    "are its own margin";

const PropertyDescriptor& describe_item_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"item_style", BindingValueKind::text, "Layout",
                           "The child's grid item settings as a CSS inline style: justify-self, align-self, "
                           "width, height, their minimums and maximums, auto margins, position and insets.",
                           BindingValue{std::string{}}, invalidation::bounds},
        PropertyOperations{true, true, true, false});
    return descriptor;
}

GridItemLayout item_layout_from(const LayoutStyle& read, const GridItemLayout& current) {
    const LayoutStyle base{};
    LayoutStyle kept = base;
    GridItemLayout layout{};
    layout.placement = current.placement;
    layout.justify_self = read.alignment.justify_self;
    layout.align_self = read.alignment.align_self;
    kept.alignment.justify_self = read.alignment.justify_self;
    kept.alignment.align_self = read.alignment.align_self;
    layout_style_property::keep_item_box(read, kept, layout);
    layout_style_property::require_admitted(read, kept, item_style_name, item_style_guidance);
    return layout;
}

std::string item_style_text(const GridItemLayout& layout) {
    const LayoutStyle base{};
    LayoutStyle style = base;
    style.alignment.justify_self = layout.justify_self;
    style.alignment.align_self = layout.align_self;
    layout_style_property::describe_item_box(layout, style);
    std::string text = layout_style_property::describe(style, base);
    return text;
}

BindingValue read_item_style(const Component& container, const Control& child) {
    const GridPanel& grid = static_cast<const GridPanel&>(container);
    std::string text = item_style_text(grid.item_layout(child));
    return BindingValue{std::move(text)};
}

void write_item_style(Component& container, const Control& child, const BindingValue& value) {
    GridPanel& grid = static_cast<GridPanel&>(container);
    const LayoutStyle read = layout_style_property::read(std::get<std::string>(value), LayoutStyle{}, item_style_name);
    const GridItemLayout layout = item_layout_from(read, grid.item_layout(child));
    grid.set_item_layout(child, layout);
}

void reset_item_style(Component& container, const Control& child) {
    GridPanel& grid = static_cast<GridPanel&>(container);
    GridItemLayout layout{};
    layout.placement = grid.item_layout(child).placement;
    grid.set_item_layout(child, layout);
}

bool item_style_serialized(const Component& container, const Control& child) {
    const GridPanel& grid = static_cast<const GridPanel&>(container);
    const bool serialized = !item_style_text(grid.item_layout(child)).empty();
    return serialized;
}

constexpr ControlAnatomy grid_panel_anatomy{
    .control_id = "grid_panel",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

GridPanel::GridPanel(StableId stable_id) : ContainerControl(std::move(stable_id)) {}

BindingValue GridPanel::style_property() const {
    const LayoutStyle base{};
    LayoutStyle style = base;
    style.grid.template_columns = style_.template_columns;
    style.grid.template_rows = style_.template_rows;
    style.grid.auto_flow = style_.auto_flow;
    style.grid.auto_columns = style_.auto_columns;
    style.grid.auto_rows = style_.auto_rows;
    style.alignment.justify_content = justify_content.read();
    style.alignment.align_content = align_content.read();
    style.alignment.justify_items = justify_items.read();
    style.alignment.align_items = align_items.read();
    style.row_gap = row_gap.read();
    style.column_gap = column_gap.read();
    std::string text = layout_style_property::describe(style, base);
    return BindingValue{std::move(text)};
}

// Every value is checked before the first setter runs, so a refused style
// leaves the grid as it was.
void GridPanel::set_style_property(const BindingValue& value) {
    require_mutable();
    constexpr std::string_view name = "GridPanel.style";
    const LayoutStyle base{};
    const LayoutStyle read = layout_style_property::read(std::get<std::string>(value), base, name);
    LayoutStyle kept = base;
    kept.grid.template_columns = read.grid.template_columns;
    kept.grid.template_rows = read.grid.template_rows;
    kept.grid.auto_flow = read.grid.auto_flow;
    kept.grid.auto_columns = read.grid.auto_columns;
    kept.grid.auto_rows = read.grid.auto_rows;
    kept.alignment.justify_content = read.alignment.justify_content;
    kept.alignment.align_content = read.alignment.align_content;
    kept.alignment.justify_items = read.alignment.justify_items;
    kept.alignment.align_items = read.alignment.align_items;
    kept.row_gap = read.row_gap;
    kept.column_gap = read.column_gap;
    layout_style_property::require_admitted(
        read, kept, name, "define spans with span blocks and place children with their area and cell properties");
    const GridContainerStyle& grid = read.grid;
    const bool admitted = admitted_track_list(grid.template_columns) && admitted_track_list(grid.template_rows) &&
                          grid.auto_flow <= GridAutoFlow::column_dense && !grid.auto_columns.empty() &&
                          grid.auto_columns.size() <= maximum_grid_tracks && admitted_track_sizes(grid.auto_columns) &&
                          !grid.auto_rows.empty() && grid.auto_rows.size() <= maximum_grid_tracks &&
                          admitted_track_sizes(grid.auto_rows) &&
                          admitted_content_alignment(read.alignment.justify_content) &&
                          admitted_content_alignment(read.alignment.align_content) &&
                          admitted_items_alignment(read.alignment.justify_items) &&
                          admitted_items_alignment(read.alignment.align_items) && admitted_length(read.row_gap) &&
                          admitted_length(read.column_gap);
    if (!admitted) throw std::invalid_argument("GridPanel.style holds a value grids do not admit");
    template_columns = grid.template_columns;
    template_rows = grid.template_rows;
    auto_flow = grid.auto_flow;
    auto_columns = grid.auto_columns;
    auto_rows = grid.auto_rows;
    justify_content = read.alignment.justify_content;
    align_content = read.alignment.align_content;
    justify_items = read.alignment.justify_items;
    align_items = read.alignment.align_items;
    row_gap = read.row_gap;
    column_gap = read.column_gap;
}

const std::array<ChildPropertyEntry, GridPanel::grid_child_property_count>&
GridPanel::grid_child_property_entries() noexcept {
    static constexpr std::array<ChildPropertyEntry, grid_child_property_count> entries{{
        {"area", &describe_area, &read_area, &write_area, &reset_area, &area_serialized},
        {"cell", &describe_cell, &read_cell, &write_cell, &reset_cell, &cell_serialized},
        {"item_style", &describe_item_style, &read_item_style, &write_item_style, &reset_item_style,
         &item_style_serialized},
    }};
    static_assert(property_names_distinct(entries), "GridPanel's child table names a property twice");
    return entries;
}

std::span<const ChildPropertyEntry> GridPanel::child_property_table() const noexcept {
    const std::array<ChildPropertyEntry, grid_child_property_count>& entries = grid_child_property_entries();
    return std::span<const ChildPropertyEntry>(entries);
}

GridSpanOutcome GridPanel::check_spans(std::span<const GridSpan> candidate) const {
    for (const GridSpan& span : candidate) {
        if (!grid_span_extent_valid(span)) return GridSpanOutcome::extent_invalid;
    }
    // Pairwise: span lists are short (a table's fused regions).
    for (std::size_t first = 0U; first < candidate.size(); ++first) {
        for (std::size_t second = first + 1U; second < candidate.size(); ++second) {
            const GridSpan& left = candidate[first];
            const GridSpan& right = candidate[second];
            if (!left.name.empty() && left.name == right.name) return GridSpanOutcome::name_repeated;
            if (grid_spans_overlap(left, right)) return GridSpanOutcome::overlaps;
        }
    }
    for (const ItemMap::value_type& entry : items_) {
        const GridPlacement& placed = entry.second.placement;
        if (placed.form == GridPlacement::Form::cell) {
            for (const GridSpan& span : candidate) {
                if (grid_span_interior(span, placed.cell)) return GridSpanOutcome::covers_placed_child;
            }
        }
    }
    for (const ItemMap::value_type& entry : items_) {
        const GridPlacement& placed = entry.second.placement;
        if (placed.form != GridPlacement::Form::area) continue;
        bool named = false;
        for (const GridSpan& span : candidate) {
            if (span.name == placed.area) named = true;
        }
        if (!named) return GridSpanOutcome::in_use;
    }
    return GridSpanOutcome::applied;
}

void GridPanel::adopt_spans(std::vector<GridSpan> candidate) {
    spans_ = std::move(candidate);
    regenerate_template_areas();
    invalidate(invalidation::bounds);
}

void GridPanel::regenerate_template_areas() {
    GridTemplateAreas areas{};
    std::uint32_t rows = area_extent_.row;
    std::uint32_t columns = area_extent_.column;
    for (const GridSpan& span : spans_) {
        if (span.name.empty()) continue;
        rows = std::max(rows, span.anchor.row + span.rows);
        columns = std::max(columns, span.anchor.column + span.columns);
        areas.areas.push_back(GridNamedArea{span.name, span.anchor.row, span.anchor.row + span.rows,
                                            span.anchor.column, span.anchor.column + span.columns});
    }
    // Both zero is `none`; an extent of zero on one axis with no named
    // span is too.
    if (rows != 0U && columns != 0U) {
        areas.rows = rows;
        areas.columns = columns;
    }
    style_.template_areas = std::move(areas);
}

const GridSpan* GridPanel::find_span(std::string_view name) const noexcept {
    if (name.empty()) return nullptr;
    for (const GridSpan& span : spans_) {
        if (span.name == name) return &span;
    }
    return nullptr;
}

const GridSpan* GridPanel::find_anchored_span(GridCell anchor) const noexcept {
    for (const GridSpan& span : spans_) {
        if (span.anchor == anchor) return &span;
    }
    return nullptr;
}

GridSpanOutcome GridPanel::define_span(const GridSpan& span) {
    require_mutable();
    std::vector<GridSpan> candidate{};
    candidate.reserve(spans_.size() + 1U);
    candidate.assign(spans_.begin(), spans_.end());
    candidate.push_back(span);
    const GridSpanOutcome outcome = check_spans(candidate);
    if (outcome != GridSpanOutcome::applied) return outcome;
    adopt_spans(std::move(candidate));
    return GridSpanOutcome::applied;
}

GridSpanOutcome GridPanel::remove_span_at(std::size_t index) {
    std::vector<GridSpan> candidate = spans_;
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
    const GridSpanOutcome outcome = check_spans(candidate);
    if (outcome != GridSpanOutcome::applied) return outcome;
    adopt_spans(std::move(candidate));
    return GridSpanOutcome::applied;
}

GridSpanOutcome GridPanel::remove_span(std::string_view name) {
    require_mutable();
    const GridSpan* const found = find_span(name);
    if (found == nullptr) return GridSpanOutcome::not_found;
    const std::size_t removed = static_cast<std::size_t>(found - spans_.data());
    const GridSpanOutcome outcome = remove_span_at(removed);
    return outcome;
}

GridSpanOutcome GridPanel::remove_span(GridCell anchor) {
    require_mutable();
    const GridSpan* const found = find_anchored_span(anchor);
    if (found == nullptr) return GridSpanOutcome::not_found;
    const std::size_t removed = static_cast<std::size_t>(found - spans_.data());
    const GridSpanOutcome outcome = remove_span_at(removed);
    return outcome;
}

GridSpanOutcome GridPanel::set_spans(std::span<const GridSpan> spans) {
    require_mutable();
    // Copied before the check: `spans` may borrow spans_ itself.
    std::vector<GridSpan> candidate(spans.begin(), spans.end());
    const GridSpanOutcome outcome = check_spans(candidate);
    if (outcome != GridSpanOutcome::applied) return outcome;
    if (candidate == spans_) return GridSpanOutcome::applied;
    adopt_spans(std::move(candidate));
    return GridSpanOutcome::applied;
}

GridPlacement GridPanel::placement(const Control& child) const {
    require_direct_child(child);
    const GridPlacement placed = placement_for(child);
    return placed;
}

const GridPlacement& GridPanel::placement_for(const Control& child) const noexcept {
    const ItemMap::const_iterator found = items_.find(child.runtime_id().value);
    if (found == items_.end()) return automatic_placement;
    return (*found).second.placement;
}

void GridPanel::prepare_write(const ChildPropertyView<std::string, GridAreaCapability<GridPanel>>& property,
                              const Control& child, std::string& incoming) const {
    static_cast<void>(property);
    require_mutable();
    require_direct_child(child);
    if (!incoming.empty() && find_span(incoming) == nullptr) {
        throw std::invalid_argument("GridPanel area names no span: " + incoming);
    }
}

void GridPanel::prepare_write(const ChildPropertyView<GridCell, GridCellCapability<GridPanel>>& property,
                              const Control& child, GridCell& incoming) const {
    static_cast<void>(property);
    require_mutable();
    require_direct_child(child);
    if (!grid_cell_admitted(incoming)) throw std::invalid_argument("GridPanel cell lies past the grid's limit");
    for (const GridSpan& span : spans_) {
        if (grid_span_interior(span, incoming)) {
            throw std::invalid_argument("GridPanel cell lies inside a span; place the child at its anchor");
        }
    }
}

void GridPanel::written(const Control& child) {
    static_cast<void>(child);
    invalidate(invalidation::bounds);
}

bool GridPanel::replace_area(const Control& child, std::string area) {
    GridPlacement placed{};
    if (!area.empty()) {
        placed.form = GridPlacement::Form::area;
        placed.area = std::move(area);
    }
    const bool replaced = replace_placement(child, std::move(placed));
    return replaced;
}

bool GridPanel::replace_cell(const Control& child, GridCell cell) {
    GridPlacement placed{};
    placed.form = GridPlacement::Form::cell;
    placed.cell = cell;
    const bool replaced = replace_placement(child, std::move(placed));
    return replaced;
}

bool GridPanel::replace_placement(const Control& child, GridPlacement placement) {
    const std::uint64_t id = child.runtime_id().value;
    const ItemMap::iterator found = items_.find(id);
    const GridPlacement& current = found == items_.end() ? automatic_placement : (*found).second.placement;
    if (current == placement) return false;
    if (placement.form == GridPlacement::Form::automatic) {
        // Only an area or cell placement changes here, so a record exists.
        (*found).second.placement = std::move(placement);
        if ((*found).second.layout == default_item_layout) items_.erase(found);
        return true;
    }
    ItemRecord& record = items_[id];
    record.placement = std::move(placement);
    // Writing either view replaces the item layout's lines.
    record.layout.placement = GridItemStyle{};
    return true;
}

std::string GridPanel::stored_area(const Control& child) const {
    require_direct_child(child);
    const GridPlacement& placed = placement_for(child);
    if (placed.form != GridPlacement::Form::area) return std::string{};
    std::string area = placed.area;
    return area;
}

GridCell GridPanel::stored_cell(const Control& child) const {
    require_direct_child(child);
    const GridPlacement& placed = placement_for(child);
    if (placed.form == GridPlacement::Form::cell) return placed.cell;
    if (placed.form != GridPlacement::Form::area) return GridCell{};
    const GridSpan* const span = find_span(placed.area);
    if (span == nullptr) return GridCell{};
    return (*span).anchor;
}

void GridPanel::effective_lines(const ItemRecord& record, GridItemStyle& lines) const {
    const GridPlacement& placed = record.placement;
    switch (placed.form) {
    case GridPlacement::Form::area:
        set_area_line(lines.column_start, placed.area);
        set_area_line(lines.column_end, placed.area);
        set_area_line(lines.row_start, placed.area);
        set_area_line(lines.row_end, placed.area);
        return;
    case GridPlacement::Form::cell: {
        // At a span's anchor the child takes the whole region; elsewhere one
        // cell. Grid lines are one-based.
        const GridSpan* const span = find_anchored_span(placed.cell);
        const std::uint32_t rows = span == nullptr ? 1U : (*span).rows;
        const std::uint32_t columns = span == nullptr ? 1U : (*span).columns;
        set_number_line(lines.row_start, GridLineKind::line, placed.cell.row + 1U);
        set_number_line(lines.row_end, GridLineKind::span, rows);
        set_number_line(lines.column_start, GridLineKind::line, placed.cell.column + 1U);
        set_number_line(lines.column_end, GridLineKind::span, columns);
        return;
    }
    case GridPlacement::Form::automatic:
    default:
        // Automatic, and an unknown form: the item layout's lines below.
        break;
    }
    // Automatic, and the fallback: the item layout's own lines.
    lines = record.layout.placement;
}

void GridPanel::child_leaving(const Control& child) noexcept {
    items_.erase(child.runtime_id().value);
}

void GridPanel::write_view(const decltype(template_columns)&, GridTrackList incoming) {
    require_mutable();
    if (!admitted_track_list(incoming)) {
        throw std::invalid_argument("GridPanel template columns hold a track list grids do not admit");
    }
    if (style_.template_columns == incoming) return;
    style_.template_columns = std::move(incoming);
    invalidate(invalidation::bounds);
}

GridTrackList GridPanel::read_view(const decltype(template_columns)&) const {
    GridTrackList tracks = style_.template_columns;
    return tracks;
}

void GridPanel::write_view(const decltype(template_rows)&, GridTrackList incoming) {
    require_mutable();
    if (!admitted_track_list(incoming)) {
        throw std::invalid_argument("GridPanel template rows hold a track list grids do not admit");
    }
    if (style_.template_rows == incoming) return;
    style_.template_rows = std::move(incoming);
    invalidate(invalidation::bounds);
}

GridTrackList GridPanel::read_view(const decltype(template_rows)&) const {
    GridTrackList tracks = style_.template_rows;
    return tracks;
}

void GridPanel::set_template_areas(const GridTemplateAreas& areas) {
    require_mutable();
    if (!admitted_template_areas(areas)) {
        throw std::invalid_argument("GridPanel template areas must be distinct named rectangles within it");
    }
    std::vector<GridSpan> candidate{};
    candidate.reserve(areas.areas.size());
    for (const GridNamedArea& area : areas.areas) {
        candidate.push_back(GridSpan{area.name, GridCell{area.row_start, area.column_start},
                                     area.row_end - area.row_start, area.column_end - area.column_start});
    }
    const GridSpanOutcome outcome = check_spans(candidate);
    if (outcome != GridSpanOutcome::applied) throw std::invalid_argument(span_refusal_text(outcome));
    const GridCell extent{areas.rows, areas.columns};
    if (candidate == spans_ && extent == area_extent_) return;
    area_extent_ = extent;
    adopt_spans(std::move(candidate));
}

void GridPanel::write_view(const decltype(auto_flow)&, GridAutoFlow incoming) {
    require_mutable();
    if (incoming > GridAutoFlow::column_dense) throw std::invalid_argument("GridPanel auto flow is unknown");
    if (style_.auto_flow == incoming) return;
    style_.auto_flow = incoming;
    invalidate(invalidation::bounds);
}

GridAutoFlow GridPanel::read_view(const decltype(auto_flow)&) const {
    return style_.auto_flow;
}

void GridPanel::write_view(const decltype(auto_columns)&, std::vector<GridTrackSize> incoming) {
    require_mutable();
    if (incoming.empty() || incoming.size() > maximum_grid_tracks || !admitted_track_sizes(incoming)) {
        throw std::invalid_argument("GridPanel auto columns need one to 10000 admitted track sizes");
    }
    if (style_.auto_columns == incoming) return;
    style_.auto_columns = std::move(incoming);
    invalidate(invalidation::bounds);
}

std::vector<GridTrackSize> GridPanel::read_view(const decltype(auto_columns)&) const {
    std::vector<GridTrackSize> sizes = style_.auto_columns;
    return sizes;
}

void GridPanel::write_view(const decltype(auto_rows)&, std::vector<GridTrackSize> incoming) {
    require_mutable();
    if (incoming.empty() || incoming.size() > maximum_grid_tracks || !admitted_track_sizes(incoming)) {
        throw std::invalid_argument("GridPanel auto rows need one to 10000 admitted track sizes");
    }
    if (style_.auto_rows == incoming) return;
    style_.auto_rows = std::move(incoming);
    invalidate(invalidation::bounds);
}

std::vector<GridTrackSize> GridPanel::read_view(const decltype(auto_rows)&) const {
    std::vector<GridTrackSize> sizes = style_.auto_rows;
    return sizes;
}

void GridPanel::set_item_layout(const Control& child, const GridItemLayout& layout) {
    require_mutable();
    if (child.parent().get() != this || !child.is_alive()) {
        throw std::invalid_argument("GridPanel item layout requires a live direct child");
    }
    if (!admitted_item_layout(layout)) {
        throw std::invalid_argument("GridPanel item layout holds a value grid items do not admit");
    }
    const std::uint64_t id = child.runtime_id().value;
    const ItemMap::iterator found = items_.find(id);
    if (found != items_.end() && (*found).second.layout == layout) return;
    if (found == items_.end() && layout == default_item_layout) return;
    const bool was_positioned = record_for(child).positioning.position != PositionKind::static_position;
    ItemRecord& record = items_[id];
    record.layout = layout;
    // Lines that place the child replace an area or cell placement.
    if (!lines_automatic(layout.placement)) record.placement = GridPlacement{};
    invalidate(invalidation::bounds);
    // A child that becomes, or stops being, a positioned box changes the
    // containing block of absolute children below it.
    const bool positioned = layout.positioning.position != PositionKind::static_position;
    if (positioned != was_positioned) notify_positioning_changed(child);
}

GridItemLayout GridPanel::item_layout(const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return default_item_layout;
    const GridItemLayout layout = record_for(child);
    return layout;
}

const GridItemLayout& GridPanel::record_for(const Control& child) const noexcept {
    const ItemMap::const_iterator found = items_.find(child.runtime_id().value);
    if (found == items_.end()) return default_item_layout;
    return (*found).second.layout;
}

void GridPanel::forget_departed_children() {
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

void GridPanel::gather_children() {
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

GridContainerInput GridPanel::container_input(const SolveRequest& request) const noexcept {
    GridContainerInput container{};
    container.justify_content = justify_content.read();
    container.align_content = align_content.read();
    container.justify_items = justify_items.read();
    container.align_items = align_items.read();
    container.column_gap = resolve_gap(column_gap.read(), request.width);
    container.row_gap = resolve_gap(row_gap.read(), request.height);
    container.width = request.width;
    container.height = request.height;
    const LayoutDirection direction = effective_layout_direction();
    container.layout_direction = direction == LayoutDirection::right_to_left ? LayoutDirection::right_to_left
                                                                             : LayoutDirection::left_to_right;
    return container;
}

void GridPanel::resolve_item_inputs() {
    const std::size_t count = children_.size();
    // Sized first, so the pointers below are taken from settled storage;
    // copy-assignment reuses each record's string capacity.
    placements_.resize(count);
    inputs_.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const ItemMap::const_iterator found = items_.find(child.runtime_id().value);
        const bool recorded = found != items_.end();
        const GridItemLayout& layout = recorded ? (*found).second.layout : default_item_layout;
        if (recorded) {
            effective_lines((*found).second, placements_[index]);
        } else {
            placements_[index] = default_item_layout.placement;
        }
        GridItemInput& input = inputs_[index];
        input = GridItemInput{};
        input.placement = &placements_[index];
        input.justify_self = layout.justify_self;
        input.align_self = layout.align_self;
        const Insets margin = child.margin.read();
        input.margin.top = GridMargin{layout.auto_margin.top ? 0.0 : margin.top, layout.auto_margin.top};
        input.margin.right = GridMargin{layout.auto_margin.right ? 0.0 : margin.right, layout.auto_margin.right};
        input.margin.bottom = GridMargin{layout.auto_margin.bottom ? 0.0 : margin.bottom, layout.auto_margin.bottom};
        input.margin.left = GridMargin{layout.auto_margin.left ? 0.0 : margin.left, layout.auto_margin.left};
        const Size control_minimum = child.minimum_size.read();
        const Size control_maximum = child.maximum_size.read();
        input.width = layout.width;
        input.height = layout.height;
        input.min_width = used_minimum(layout.min_width, control_minimum.width);
        input.min_height = used_minimum(layout.min_height, control_minimum.height);
        input.max_width = used_maximum(layout.max_width, control_maximum.width);
        input.max_height = used_maximum(layout.max_height, control_maximum.height);
    }
}

void GridPanel::measure_column_contributions(const SolveRequest& request, const GridLayoutWorkspace& workspace) {
    const std::size_t count = children_.size();
    column_contributions_.resize(count);
    const BorrowedSpan<GridItemResult> results = workspace.items();
    const BorrowedSpan<GridTrack> columns = workspace.columns();
    const bool definite = request.width.kind == GridSizingKind::definite;
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const GridItemLayout& layout = record_for(child);
        GridItemContribution& contribution = column_contributions_[index];
        contribution = GridItemContribution{};
        const GridItemArea& area = results[index].area;
        const bool fixed = fixed_tracks(columns, area.column_start, area.column_end, definite);
        const bool definite_width = plain_length(layout.width);
        // Fixed columns ignore contributions, and a stretched item takes
        // its area whatever its content: nothing to measure.
        if (fixed && !definite_width && stretched_width(layout, justify_items.read())) continue;

        const Insets margin = child.margin.read();
        const double margins = (layout.auto_margin.left ? 0.0 : margin.left) +
                               (layout.auto_margin.right ? 0.0 : margin.right);
        const ContentBounds bounds = content_bounds(layout.min_width, layout.max_width, child.minimum_size.read().width,
                                                    child.maximum_size.read().width);
        double smallest = 0.0;
        double largest = 0.0;
        if (definite_width) {
            smallest = plain_length_value(layout.width);
            largest = smallest;
        } else {
            const LayoutConstraint minimum_constraint{min_content_extent(), max_content_extent()};
            smallest = child.measure_for_layout(minimum_constraint).width;
            if (!is_alive()) return;
            const LayoutConstraint maximum_constraint{max_content_extent(), max_content_extent()};
            largest = child.measure_for_layout(maximum_constraint).width;
            if (!is_alive()) return;
        }
        contribution.min_content = clamp_content(smallest, bounds) + margins;
        contribution.max_content = clamp_content(largest, bounds) + margins;
        if (definite_width) {
            // CSS Grid 12.3: with a definite preferred size, the minimum
            // contribution is the min-content contribution.
            contribution.minimum = contribution.min_content;
        } else if (layout.min_width.kind == SizeKind::automatic) {
            // CSS Grid 6.6's content-based minimum: the min-content size,
            // within the maximum.
            contribution.automatic_minimum = true;
            contribution.minimum = contribution.min_content;
        } else {
            contribution.minimum = bounds.minimum + margins;
        }
    }
}

void GridPanel::measure_row_contributions(const GridContainerInput& container,
                                                const GridLayoutWorkspace& workspace) {
    const std::size_t count = children_.size();
    row_contributions_.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        Control& child = *children_[index];
        const GridItemLayout& layout = record_for(child);
        GridItemContribution& contribution = row_contributions_[index];
        contribution = GridItemContribution{};
        const Insets margin = child.margin.read();
        const double margins = (layout.auto_margin.top ? 0.0 : margin.top) +
                               (layout.auto_margin.bottom ? 0.0 : margin.bottom);
        const ContentBounds bounds = content_bounds(layout.min_height, layout.max_height,
                                                    child.minimum_size.read().height, child.maximum_size.read().height);
        double height = 0.0;
        const bool definite_height = plain_length(layout.height);
        const double width =
            grid_item_column_size(container, inputs_[index], column_contributions_[index], workspace, index);
        if (definite_height) {
            height = plain_length_value(layout.height);
        } else {
            const LayoutConstraint constraint{query_extent(width), max_content_extent()};
            height = child.measure_for_layout(constraint).height;
            if (!is_alive()) return;
        }
        // Its first baseline at its column size, for baseline alignment in
        // its row.
        const std::optional<double> baseline = child.first_baseline(width);
        if (!is_alive()) return;
        if (baseline) contribution.baseline = *baseline;
        const double outer = clamp_content(height, bounds) + margins;
        contribution.min_content = outer;
        contribution.max_content = outer;
        if (definite_height) {
            contribution.minimum = outer;
        } else if (layout.min_height.kind == SizeKind::automatic) {
            contribution.automatic_minimum = true;
            contribution.minimum = outer;
        } else {
            contribution.minimum = bounds.minimum + margins;
        }
    }
}

void GridPanel::publish_slots(const GridLayoutWorkspace& workspace, Size content_box) {
    const LayoutDirection direction = effective_layout_direction();
    const BorrowedSpan<GridItemResult> results = workspace.items();
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

GridPanel::SolveResult GridPanel::solve(const SolveRequest& request, GridLayoutWorkspace& workspace) {
    gather_children();
    const GridContainerInput container = container_input(request);
    SolveResult result{};
    if (children_.size() > maximum_grid_items) {
        // Refused before any child is measured (PLAN section 5): the solver
        // clears the workspace and no slot is published.
        inputs_.resize(children_.size());
        result.outcome = place_grid_items(style_, container, inputs_, workspace);
        children_.clear();
        return result;
    }
    resolve_item_inputs();
    result.outcome = place_grid_items(style_, container, inputs_, workspace);
    if (result.outcome != GridSolveOutcome::solved) {
        children_.clear();
        return result;
    }
    measure_column_contributions(request, workspace);
    if (!is_alive()) return result;
    result.outcome = size_grid_tracks(GridAxis::columns, container, inputs_, column_contributions_, workspace);
    if (result.outcome != GridSolveOutcome::solved) return result;
    const double width = request.width.kind == GridSizingKind::definite ? request.width.extent
                                                                         : workspace.content_width();
    if (request.columns_only) {
        result.used = Size{width, 0.0};
        return result;
    }
    measure_row_contributions(container, workspace);
    if (!is_alive()) return result;
    result.outcome = size_grid_tracks(GridAxis::rows, container, inputs_, row_contributions_, workspace);
    if (result.outcome != GridSolveOutcome::solved) return result;
    result.outcome = arrange_grid_items(container, inputs_, column_contributions_, row_contributions_, workspace);
    if (result.outcome != GridSolveOutcome::solved) return result;
    if (request.assign) publish_slots(workspace, Size{request.width.extent, request.height.extent});
    const double height = request.height.kind == GridSizingKind::definite ? request.height.extent
                                                                           : workspace.content_height();
    result.used = Size{width, height};
    return result;
}

Size GridPanel::measure_intrinsic(const LayoutConstraint& constraint) {
    const Insets inset = padding.read();
    const double horizontal_padding = inset.left + inset.right;
    const double vertical_padding = inset.top + inset.bottom;
    const Rect requested = requested_bounds();

    // The border-box width: definite; filled or requested when the panel
    // is not auto-sized; otherwise its columns under a min-content or
    // max-content constraint (fit-content for at most).
    SolveRequest columns{};
    columns.columns_only = true;
    columns.height = GridAvailableExtent{0.0, GridSizingKind::max_content};
    double width = 0.0;
    const AvailableExtentKind width_kind = constraint.width.kind;
    if (width_kind == AvailableExtentKind::definite) {
        width = constraint.width.extent.value();
    } else if (width_kind == AvailableExtentKind::at_most && !auto_size.read()) {
        const double available = constraint.width.extent.value();
        width = requested.width > 0.0 ? std::min(requested.width, available) : available;
    } else if (width_kind == AvailableExtentKind::min_content) {
        columns.width = GridAvailableExtent{0.0, GridSizingKind::min_content};
        width = solve(columns, workspace_).used.width + horizontal_padding;
    } else if (width_kind == AvailableExtentKind::max_content) {
        columns.width = GridAvailableExtent{0.0, GridSizingKind::max_content};
        width = solve(columns, workspace_).used.width + horizontal_padding;
    } else {
        const double available = constraint.width.extent.value();
        columns.width = GridAvailableExtent{0.0, GridSizingKind::max_content};
        const double largest = solve(columns, workspace_).used.width + horizontal_padding;
        if (!is_alive()) return {};
        columns.width = GridAvailableExtent{0.0, GridSizingKind::min_content};
        const double smallest = solve(columns, workspace_).used.width + horizontal_padding;
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
        request.width = GridAvailableExtent{std::max(0.0, width - horizontal_padding), GridSizingKind::definite};
        request.height = GridAvailableExtent{0.0, GridSizingKind::max_content};
        const SolveResult solved = solve(request, workspace_);
        if (!is_alive()) return {};
        height = solved.used.height + vertical_padding;
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
Size GridPanel::measure(Size available) {
    const double width = std::isfinite(available.width) ? available.width : practical_unbounded_extent;
    const double height = std::isfinite(available.height) ? available.height : practical_unbounded_extent;
    const Size bounded{std::clamp(width, 0.0, practical_unbounded_extent),
                       std::clamp(height, 0.0, practical_unbounded_extent)};
    const Size measured = measure_intrinsic(layout_constraint_at_most(bounded));
    return measured;
}

void GridPanel::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    forget_departed_children();
    const Insets inset = padding.read();
    const Size client{final_bounds.width, final_bounds.height};
    Size available = client;
    // With auto_scroll, the bars a content overflow needs narrow the
    // viewport; lay out against it until the two settle (at most three
    // passes, as FlowLayoutPanelCompat).
    if (auto_scroll.read()) {
        for (int pass = 0; pass < 3; ++pass) {
            SolveRequest request{};
            request.width = GridAvailableExtent{std::max(0.0, available.width - inset.left - inset.right),
                                                GridSizingKind::definite};
            request.height = GridAvailableExtent{std::max(0.0, available.height - inset.top - inset.bottom),
                                                 GridSizingKind::definite};
            const SolveResult solved = solve(request, arranged_);
            if (!is_alive()) return;
            const Size extra = auto_scroll_margin.read();
            const Size content{
                std::max(arranged_.content_width(), solved.used.width) + inset.left + inset.right + extra.width,
                std::max(arranged_.content_height(), solved.used.height) + inset.top + inset.bottom + extra.height};
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
    request.width =
        GridAvailableExtent{std::max(0.0, available.width - inset.left - inset.right), GridSizingKind::definite};
    request.height =
        GridAvailableExtent{std::max(0.0, available.height - inset.top - inset.bottom), GridSizingKind::definite};
    request.assign = true;
    const SolveResult solved = solve(request, arranged_);
    arranged_outcome_ = solved.outcome;
    if (solved.outcome != GridSolveOutcome::solved) arranged_ids_.clear();
    if (!is_alive()) return;
    place_absolute_children(client);
}

void GridPanel::place_absolute_children(Size client) {
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

ItemPositioning GridPanel::item_positioning(const Control& child) const noexcept {
    if (child.parent().get() != this) return ItemPositioning{};
    return record_for(child).positioning;
}

bool GridPanel::measures_children_in_arrange() const noexcept {
    return true;
}

SemanticDescriptor GridPanel::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}

const ControlAnatomy& GridPanel::anatomy() const noexcept {
    return grid_panel_anatomy;
}

namespace {

// The container settings as one CSS inline style text: a hand table entry
// (extra_entries) over the panel's settings.
PropertyDescriptor grid_panel_style_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "style";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Layout";
    descriptor.description = "The grid's container settings as a CSS inline style: grid-template-columns and -rows, grid-auto-flow, grid-auto-columns and -rows, the content and item alignments and the gaps. Spans are span blocks.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = invalidation::bounds;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_grid_panel_style() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        grid_panel_style_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

// property_codemod begin GridPanel (tools/property_families/grid_panel.json; generated, do not edit)

const std::array<PropertyEntry, GridPanel::grid_panel_property_count>& GridPanel::grid_panel_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"style", &describe_grid_panel_style,
         &read_member_entry<GridPanel, decltype(&GridPanel::style_property), &GridPanel::style_property>,
         &write_member_entry<GridPanel, decltype(&GridPanel::set_style_property), &GridPanel::set_style_property>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "GridPanel's property table names a property twice");
    static const std::array<PropertyEntry, grid_panel_property_count> entries =
        join_property_tables(ScrollableControl::scrollable_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> GridPanel::property_table() const noexcept {
    const std::array<PropertyEntry, grid_panel_property_count>& entries = grid_panel_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void GridPanel::prepare_write(const decltype(justify_content)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_content_alignment(incoming)) throw std::invalid_argument("GridPanel justify-content admits no such value");
}

void GridPanel::written(const decltype(justify_content)&) {
    invalidate(invalidation::bounds);
}

void GridPanel::prepare_write(const decltype(align_content)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_content_alignment(incoming)) throw std::invalid_argument("GridPanel align-content admits no such value");
}

void GridPanel::written(const decltype(align_content)&) {
    invalidate(invalidation::bounds);
}

void GridPanel::prepare_write(const decltype(justify_items)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_items_alignment(incoming)) throw std::invalid_argument("GridPanel justify-items admits no such value");
}

void GridPanel::written(const decltype(justify_items)&) {
    invalidate(invalidation::bounds);
}

void GridPanel::prepare_write(const decltype(align_items)&, LayoutAlignment& incoming) {
    require_mutable();
    if (!admitted_items_alignment(incoming)) throw std::invalid_argument("GridPanel align-items admits no such value");
}

void GridPanel::written(const decltype(align_items)&) {
    invalidate(invalidation::bounds);
}

void GridPanel::prepare_write(const decltype(row_gap)&, LengthPercentage& incoming) {
    require_mutable();
    if (!admitted_length(incoming)) throw std::invalid_argument("GridPanel row gap must be finite and nonnegative");
}

void GridPanel::written(const decltype(row_gap)&) {
    invalidate(invalidation::bounds);
}

void GridPanel::prepare_write(const decltype(column_gap)&, LengthPercentage& incoming) {
    require_mutable();
    if (!admitted_length(incoming)) throw std::invalid_argument("GridPanel column gap must be finite and nonnegative");
}

void GridPanel::written(const decltype(column_gap)&) {
    invalidate(invalidation::bounds);
}

// property_codemod end GridPanel

} // namespace gui_forms
