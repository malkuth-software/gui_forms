#include "gui_forms/controls/scrollable_control/container_control/table_layout_panel_compat/table_layout_panel_compat.hpp"
#include "../container_layout_utilities.hpp"
#include "../text_collection_property.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "../../scrollable_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {
constexpr std::size_t maximum_layout_tracks = 64U;

std::shared_ptr<const PropertyEnumDescriptor> grow_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "table_layout_grow_style", {{"fixed_size", 0}, {"add_rows", 1}, {"add_columns", 2}}, false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> cell_border_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "table_cell_border_style", {{"none", 0}, {"single", 1}, {"inset", 2}, {"outset", 3}}, false});
    return value;
}

// One style's text: "absolute 140", "percent 1" or "auto".
std::string style_text(TableLayoutStyle style) {
    std::string text;
    switch (style.size_mode) {
    case TableSizeMode::absolute:
        text = "absolute ";
        break;
    case TableSizeMode::percent:
        text = "percent ";
        break;
    case TableSizeMode::auto_size:
        text = "auto";
        return text;
    default:
        // A style outside the enumeration cannot be stored (validate_style
        // checks the size, the setters only take the enumerators); written
        // as auto so a read never fails.
        text = "auto";
        return text;
    }
    std::array<char, 32U> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), style.size);
    text.append(buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data()));
    return text;
}

// One style read from its text; throws std::invalid_argument for another
// shape. The size's range is validate_style's.
TableLayoutStyle style_from_text(std::string_view item) {
    const std::vector<std::string_view> words = text_collection_property::words_of(item);
    if (words.size() == 1U && words[0] == "auto") return TableLayoutStyle{TableSizeMode::auto_size, 0.0};
    if (words.size() == 2U && (words[0] == "absolute" || words[0] == "percent")) {
        double size{};
        const char* const end = words[1].data() + words[1].size();
        const std::from_chars_result parsed = std::from_chars(words[1].data(), end, size);
        if (parsed.ec == std::errc{} && parsed.ptr == end) {
            const TableSizeMode mode = words[0] == "absolute" ? TableSizeMode::absolute : TableSizeMode::percent;
            return TableLayoutStyle{mode, size};
        }
    }
    throw std::invalid_argument("TableLayoutPanelCompat style is \"absolute <size>\", \"percent <weight>\" or \"auto\"");
}

std::vector<std::string> styles_text(std::span<const TableLayoutStyle> styles) {
    std::vector<std::string> items{};
    items.reserve(styles.size());
    for (const TableLayoutStyle style : styles) items.push_back(style_text(style));
    return items;
}

std::vector<TableLayoutStyle> styles_from(const BindingValue& value, std::string_view property) {
    const std::vector<std::string> items = text_collection_property::items_of(value, property);
    std::vector<TableLayoutStyle> styles{};
    styles.reserve(items.size());
    for (const std::string& item : items) styles.push_back(style_from_text(item));
    return styles;
}

// A count property's value: an unsigned integer; larger than the track
// limit is out of range (the setter's own check covers zero).
std::size_t count_from(const BindingValue& value) {
    const std::uint64_t count = std::get<std::uint64_t>(value);
    if (count > maximum_layout_tracks) throw std::out_of_range("TableLayoutPanelCompat track count must be 1 through 64");
    return static_cast<std::size_t>(count);
}

const PropertyDescriptor& describe_cell() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"cell", BindingValueKind::point, "Layout",
                           "The child's cell, row then column; without one it is placed automatically.",
                           std::nullopt, invalidation::bounds},
        PropertyOperations{true, true, true, false});
    return descriptor;
}

const PropertyDescriptor& describe_column_span() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"column_span", BindingValueKind::unsigned_integer, "Layout",
                           "How many columns the child's cell spans; a span needs a cell.",
                           BindingValue{std::uint64_t{1U}}, invalidation::bounds},
        PropertyOperations{true, true, false, false});
    return descriptor;
}

const PropertyDescriptor& describe_row_span() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        PropertyDescriptor{"row_span", BindingValueKind::unsigned_integer, "Layout",
                           "How many rows the child's cell spans; a span needs a cell.",
                           BindingValue{std::uint64_t{1U}}, invalidation::bounds},
        PropertyOperations{true, true, false, false});
    return descriptor;
}

BindingValue read_cell(const Component& container, const Control& child) {
    const TableLayoutPanelCompat& table = static_cast<const TableLayoutPanelCompat&>(container);
    const GridCell cell = table.cell[child].read();
    const BindingValue value = PropertyValueTraits<GridCell>::to_binding(cell);
    return value;
}

// A coordinate of a cell's point form: a whole number of tracks below the limit.
bool cell_coordinate(double value, std::uint32_t& coordinate) noexcept {
    if (!std::isfinite(value) || value < 0.0 || value >= static_cast<double>(maximum_layout_tracks)) return false;
    if (std::floor(value) != value) return false;
    coordinate = static_cast<std::uint32_t>(value);
    return true;
}

void write_cell(Component& container, const Control& child, const BindingValue& value) {
    TableLayoutPanelCompat& table = static_cast<TableLayoutPanelCompat&>(container);
    const Point point = std::get<Point>(value);
    GridCell cell{};
    if (!cell_coordinate(point.x, cell.row) || !cell_coordinate(point.y, cell.column)) {
        throw std::invalid_argument("TableLayoutPanelCompat cell needs a row and a column, whole numbers from 0 to 63");
    }
    table.cell[child] = cell;
}

void reset_cell(Component& container, const Control& child) {
    TableLayoutPanelCompat& table = static_cast<TableLayoutPanelCompat&>(container);
    table.clear_cell(child);
}

bool cell_serialized(const Component& container, const Control& child) {
    const TableLayoutPanelCompat& table = static_cast<const TableLayoutPanelCompat&>(container);
    const bool serialized = table.has_cell(child);
    return serialized;
}

// A span's extent: an unsigned count no larger than the track limit (the
// setter refuses 0); a larger value cannot fit 32 bits and is refused here.
std::uint32_t span_extent(const BindingValue& value) {
    const std::uint64_t extent = std::get<std::uint64_t>(value);
    if (extent > maximum_layout_tracks) throw std::out_of_range("TableLayoutPanelCompat span must be 1 through 64");
    return static_cast<std::uint32_t>(extent);
}

BindingValue read_column_span(const Component& container, const Control& child) {
    const TableLayoutPanelCompat& table = static_cast<const TableLayoutPanelCompat&>(container);
    const std::uint32_t extent = table.column_span[child].read();
    return BindingValue{std::uint64_t{extent}};
}

void write_column_span(Component& container, const Control& child, const BindingValue& value) {
    TableLayoutPanelCompat& table = static_cast<TableLayoutPanelCompat&>(container);
    table.column_span[child] = span_extent(value);
}

BindingValue read_row_span(const Component& container, const Control& child) {
    const TableLayoutPanelCompat& table = static_cast<const TableLayoutPanelCompat&>(container);
    const std::uint32_t extent = table.row_span[child].read();
    return BindingValue{std::uint64_t{extent}};
}

void write_row_span(Component& container, const Control& child, const BindingValue& value) {
    TableLayoutPanelCompat& table = static_cast<TableLayoutPanelCompat&>(container);
    table.row_span[child] = span_extent(value);
}

// The occupancy bitmap's row stride and capacity (the track limit).
constexpr std::size_t occupancy_stride = maximum_layout_tracks;
constexpr std::size_t occupancy_capacity = maximum_layout_tracks * maximum_layout_tracks;

// Whether the cells [column, column + column_span) by [row, row + row_span)
// lie inside the live grid and are all free.
bool region_free(std::span<const std::uint8_t> occupied, std::size_t columns, std::size_t rows,
                 std::size_t column, std::size_t row, std::size_t column_span, std::size_t row_span) noexcept {
    if (column + column_span > columns || row + row_span > rows) return false;
    for (std::size_t y = row; y < row + row_span; ++y) {
        for (std::size_t x = column; x < column + column_span; ++x) {
            if (occupied[y * occupancy_stride + x] != 0U) return false;
        }
    }
    return true;
}

void occupy(std::span<std::uint8_t> occupied, std::size_t column, std::size_t row, std::size_t column_span,
            std::size_t row_span) noexcept {
    for (std::size_t y = row; y < row + row_span; ++y) {
        for (std::size_t x = column; x < column + column_span; ++x) {
            occupied[y * occupancy_stride + x] = 1U;
        }
    }
}

// Grows the live grid to hold a positioned item, as grow_style allows. The
// bitmap's cells outside the live grid are free, so growing is a count.
bool grow_to_fit(std::size_t& columns, std::size_t& rows, TableLayoutCellPosition position,
                 std::size_t column_span, std::size_t row_span, TableLayoutGrowStyle grow_style) noexcept {
    const std::size_t required_columns = position.column + column_span;
    const std::size_t required_rows = position.row + row_span;
    if (required_columns > maximum_layout_tracks || required_rows > maximum_layout_tracks) return false;
    if (required_columns > columns) {
        if (grow_style != TableLayoutGrowStyle::add_columns) return false;
        columns = required_columns;
    }
    if (required_rows > rows) {
        if (grow_style != TableLayoutGrowStyle::add_rows) return false;
        rows = required_rows;
    }
    return true;
}

// The grid's rules, each a `rule` part (its role and family set by the
// caller) one unit across, centred on the line the grid draws.
void draw_table_grid(Painter& painter, const Control& control,
                     std::span<const double> column_widths,
                     std::span<const double> row_heights,
                     Insets inset, Size track_spacing,
                     double width, double height,
                     PartPaint rule, double offset) {
    double x = inset.left;
    rule.bounds = {x + offset - 0.5, inset.top, 1.0, height};
    paint_part(painter, control, rule);
    for (std::size_t index = 0U; index < column_widths.size(); ++index) {
        x += column_widths[index];
        const bool has_gap = index + 1U < column_widths.size();
        const double line_x = x + (has_gap ? track_spacing.width * 0.5 : 0.0);
        rule.bounds = {line_x + offset - 0.5, inset.top, 1.0, height};
        paint_part(painter, control, rule);
        if (has_gap) x += track_spacing.width;
    }
    double y = inset.top;
    rule.bounds = {inset.left, y + offset - 0.5, width, 1.0};
    paint_part(painter, control, rule);
    for (std::size_t index = 0U; index < row_heights.size(); ++index) {
        y += row_heights[index];
        const bool has_gap = index + 1U < row_heights.size();
        const double line_y = y + (has_gap ? track_spacing.height * 0.5 : 0.0);
        rule.bounds = {inset.left, line_y + offset - 0.5, width, 1.0};
        paint_part(painter, control, rule);
        if (has_gap) y += track_spacing.height;
    }
}

} // namespace

using namespace container_layout_detail;

double TableLayoutPanelCompat::resolve_tracks(
    std::span<const TableLayoutStyle> styles,
    std::span<double> minimum,
    std::span<const TrackDemand> spans,
    double available,
    std::span<double> actual) noexcept {
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        if (styles[index].size_mode == TableSizeMode::absolute) {
            minimum[index] = styles[index].size;
        }
    }
    for (const TrackDemand& demand : spans) {
        const std::size_t end = std::min(styles.size(), demand.start + demand.span);
        double current = 0.0;
        double total_weight = 0.0;
        for (std::size_t index = demand.start; index < end; ++index) {
            current += minimum[index];
            if (styles[index].size_mode != TableSizeMode::absolute) {
                total_weight += styles[index].size_mode == TableSizeMode::percent
                    ? styles[index].size : 1.0;
            }
        }
        const double deficit = std::max(0.0, demand.required - current);
        if (deficit <= 0.0 || total_weight <= 0.0) continue;
        for (std::size_t index = demand.start; index < end; ++index) {
            if (styles[index].size_mode == TableSizeMode::absolute) continue;
            const double weight = styles[index].size_mode == TableSizeMode::percent
                ? styles[index].size : 1.0;
            minimum[index] += deficit * weight / total_weight;
        }
    }

    double fixed_extent = 0.0;
    double percent_weight = 0.0;
    double desired_percent_extent = 0.0;
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        if (styles[index].size_mode == TableSizeMode::percent) {
            percent_weight += styles[index].size;
        } else {
            fixed_extent += minimum[index];
        }
    }
    if (percent_weight > 0.0) {
        for (std::size_t index = 0U; index < styles.size(); ++index) {
            if (styles[index].size_mode != TableSizeMode::percent) continue;
            desired_percent_extent = std::max(
                desired_percent_extent,
                minimum[index] * percent_weight / styles[index].size);
        }
    }

    const double remaining = std::max(0.0, available - fixed_extent);
    for (std::size_t index = 0U; index < styles.size(); ++index) {
        actual[index] = styles[index].size_mode == TableSizeMode::percent
            ? (percent_weight > 0.0
                   ? remaining * styles[index].size / percent_weight
                   : 0.0)
            : minimum[index];
    }
    const double desired = fixed_extent + desired_percent_extent;
    return desired;
}


TableLayoutPanelCompat::TableLayoutPanelCompat(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {
    register_panel_properties();
}

void TableLayoutPanelCompat::register_panel_properties() {
    PropertyDescriptor column_count_descriptor{"column_count", BindingValueKind::unsigned_integer, "Layout",
                                               "The number of authored column tracks, 1 through 64.",
                                               BindingValue{std::uint64_t{1U}}, invalidation::bounds};
    column_count_descriptor.bindable = false;
    define_bindable_property({std::move(column_count_descriptor),
                              detail::BoundMemberFunction<BindingValue (TableLayoutPanelCompat::*)() const>(
                                  *this, &TableLayoutPanelCompat::column_count_property),
                              detail::BoundMemberFunction<void (TableLayoutPanelCompat::*)(const BindingValue&)>(
                                  *this, &TableLayoutPanelCompat::set_column_count_property),
                              {}, {}, {}});
    PropertyDescriptor row_count_descriptor{"row_count", BindingValueKind::unsigned_integer, "Layout",
                                            "The number of authored row tracks, 1 through 64.",
                                            BindingValue{std::uint64_t{1U}}, invalidation::bounds};
    row_count_descriptor.bindable = false;
    define_bindable_property({std::move(row_count_descriptor),
                              detail::BoundMemberFunction<BindingValue (TableLayoutPanelCompat::*)() const>(
                                  *this, &TableLayoutPanelCompat::row_count_property),
                              detail::BoundMemberFunction<void (TableLayoutPanelCompat::*)(const BindingValue&)>(
                                  *this, &TableLayoutPanelCompat::set_row_count_property),
                              {}, {}, {}});
    PropertyDescriptor grow_descriptor{"grow_style", BindingValueKind::enumeration, "Layout",
                                       "What a child that does not fit the authored grid does: add rows, add "
                                       "columns, or stay out (fixed size).",
                                       std::nullopt, invalidation::bounds};
    grow_descriptor.bindable = false;
    grow_descriptor.enumeration = grow_style_enum();
    grow_descriptor.default_value = BindingValue{std::int64_t{1}};
    define_bindable_property({std::move(grow_descriptor),
                              detail::EnumerationPropertyGetter<TableLayoutPanelCompat, TableLayoutGrowStyle>(
                                  *this, &TableLayoutPanelCompat::grow_style, grow_style_enum()),
                              detail::EnumerationPropertySetter<TableLayoutPanelCompat, TableLayoutGrowStyle>(
                                  *this, &TableLayoutPanelCompat::set_grow_style),
                              {}, {}, {}});
    PropertyDescriptor border_descriptor{"cell_border_style", BindingValueKind::enumeration, "Appearance",
                                         "The lines around and between the cells: none, single, inset or outset.",
                                         std::nullopt, Dirty::paint};
    border_descriptor.bindable = false;
    border_descriptor.enumeration = cell_border_style_enum();
    border_descriptor.default_value = BindingValue{std::int64_t{0}};
    define_bindable_property({std::move(border_descriptor),
                              detail::EnumerationPropertyGetter<TableLayoutPanelCompat, TableCellBorderStyle>(
                                  *this, &TableLayoutPanelCompat::cell_border_style, cell_border_style_enum()),
                              detail::EnumerationPropertySetter<TableLayoutPanelCompat, TableCellBorderStyle>(
                                  *this, &TableLayoutPanelCompat::set_cell_border_style),
                              {}, {}, {}});
    PropertyDescriptor spacing_descriptor{"track_spacing", BindingValueKind::size, "Layout",
                                          "The space between neighbouring columns (width) and rows (height).",
                                          BindingValue{Size{}}, invalidation::bounds};
    spacing_descriptor.bindable = false;
    define_bindable_property({std::move(spacing_descriptor),
                              detail::BindingNoexceptMethodGetter<TableLayoutPanelCompat, Size>(
                                  *this, &TableLayoutPanelCompat::track_spacing),
                              detail::DirectPropertySetter<TableLayoutPanelCompat, Size>(
                                  *this, &TableLayoutPanelCompat::set_track_spacing),
                              {}, {}, {}});
    const std::array<std::string, 1U> automatic{std::string{"auto"}};
    PropertyDescriptor column_styles_descriptor{"column_styles", BindingValueKind::collection, "Layout",
                                                "Each column's sizing, one item per column: \"absolute 140\", "
                                                "\"percent 1\" or \"auto\"; the count follows.",
                                                text_collection_property::value_of(automatic), invalidation::bounds};
    column_styles_descriptor.bindable = false;
    define_bindable_property({std::move(column_styles_descriptor),
                              detail::BoundMemberFunction<BindingValue (TableLayoutPanelCompat::*)() const>(
                                  *this, &TableLayoutPanelCompat::column_styles_property),
                              detail::BoundMemberFunction<void (TableLayoutPanelCompat::*)(const BindingValue&)>(
                                  *this, &TableLayoutPanelCompat::set_column_styles_property),
                              {}, {}, {}});
    PropertyDescriptor row_styles_descriptor{"row_styles", BindingValueKind::collection, "Layout",
                                             "Each row's sizing, one item per row, as column_styles.",
                                             text_collection_property::value_of(automatic), invalidation::bounds};
    row_styles_descriptor.bindable = false;
    define_bindable_property({std::move(row_styles_descriptor),
                              detail::BoundMemberFunction<BindingValue (TableLayoutPanelCompat::*)() const>(
                                  *this, &TableLayoutPanelCompat::row_styles_property),
                              detail::BoundMemberFunction<void (TableLayoutPanelCompat::*)(const BindingValue&)>(
                                  *this, &TableLayoutPanelCompat::set_row_styles_property),
                              {}, {}, {}});
}

BindingValue TableLayoutPanelCompat::column_count_property() const {
    return BindingValue{std::uint64_t{column_count_}};
}

void TableLayoutPanelCompat::set_column_count_property(const BindingValue& value) {
    set_column_count(count_from(value));
}

BindingValue TableLayoutPanelCompat::row_count_property() const {
    return BindingValue{std::uint64_t{row_count_}};
}

void TableLayoutPanelCompat::set_row_count_property(const BindingValue& value) {
    set_row_count(count_from(value));
}

BindingValue TableLayoutPanelCompat::column_styles_property() const {
    const std::vector<std::string> items = styles_text(column_styles_);
    BindingValue value = text_collection_property::value_of(items);
    return value;
}

void TableLayoutPanelCompat::set_column_styles_property(const BindingValue& value) {
    const std::vector<TableLayoutStyle> styles = styles_from(value, "TableLayoutPanelCompat.column_styles");
    set_column_styles(styles);
}

BindingValue TableLayoutPanelCompat::row_styles_property() const {
    const std::vector<std::string> items = styles_text(row_styles_);
    BindingValue value = text_collection_property::value_of(items);
    return value;
}

void TableLayoutPanelCompat::set_row_styles_property(const BindingValue& value) {
    const std::vector<TableLayoutStyle> styles = styles_from(value, "TableLayoutPanelCompat.row_styles");
    set_row_styles(styles);
}

void TableLayoutPanelCompat::set_column_styles(std::span<const TableLayoutStyle> styles) {
    require_mutable();
    if (styles.empty() || styles.size() > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat column count must be 1 through 64");
    }
    for (const TableLayoutStyle style : styles) validate_style(style);
    if (std::equal(styles.begin(), styles.end(), column_styles_.begin(), column_styles_.end())) return;
    column_styles_.assign(styles.begin(), styles.end());
    column_count_ = styles.size();
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_row_styles(std::span<const TableLayoutStyle> styles) {
    require_mutable();
    if (styles.empty() || styles.size() > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat row count must be 1 through 64");
    }
    for (const TableLayoutStyle style : styles) validate_style(style);
    if (std::equal(styles.begin(), styles.end(), row_styles_.begin(), row_styles_.end())) return;
    row_styles_.assign(styles.begin(), styles.end());
    row_count_ = styles.size();
    invalidate(invalidation::bounds);
}

const std::array<ChildPropertyEntry, TableLayoutPanelCompat::table_child_property_count>&
TableLayoutPanelCompat::table_child_property_entries() noexcept {
    static constexpr std::array<ChildPropertyEntry, table_child_property_count> entries{{
        {"cell", &describe_cell, &read_cell, &write_cell, &reset_cell, &cell_serialized},
        {"column_span", &describe_column_span, &read_column_span, &write_column_span, nullptr, nullptr},
        {"row_span", &describe_row_span, &read_row_span, &write_row_span, nullptr, nullptr},
    }};
    static_assert(property_names_distinct(entries), "TableLayoutPanelCompat's child table names a property twice");
    return entries;
}

std::span<const ChildPropertyEntry> TableLayoutPanelCompat::child_property_table() const noexcept {
    const std::array<ChildPropertyEntry, table_child_property_count>& entries = table_child_property_entries();
    return std::span<const ChildPropertyEntry>(entries);
}

void TableLayoutPanelCompat::validate_style(TableLayoutStyle style) {
    if (!std::isfinite(style.size) || style.size < 0.0 ||
        (style.size_mode == TableSizeMode::percent && style.size <= 0.0)) {
        throw std::invalid_argument(
            "TableLayoutPanelCompat style size must be finite and nonnegative; percent must be positive");
    }
}

void TableLayoutPanelCompat::set_column_count(std::size_t count) {
    require_mutable();
    if (count == 0U || count > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat column count must be 1 through 64");
    }
    if (column_count_ == count) return;
    column_count_ = count;
    column_styles_.resize(count);
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_row_count(std::size_t count) {
    require_mutable();
    if (count == 0U || count > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat row count must be 1 through 64");
    }
    if (row_count_ == count) return;
    row_count_ = count;
    row_styles_.resize(count);
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_grow_style(TableLayoutGrowStyle style) {
    require_mutable();
    if (grow_style_ == style) return;
    grow_style_ = style;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_cell_border_style(TableCellBorderStyle style) {
    require_mutable();
    if (cell_border_style_ == style) return;
    cell_border_style_ = style;
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void TableLayoutPanelCompat::set_track_spacing(Size spacing) {
    require_mutable();
    if (!std::isfinite(spacing.width) || !std::isfinite(spacing.height) ||
        spacing.width < 0.0 || spacing.height < 0.0 ||
        spacing.width > 256.0 || spacing.height > 256.0) {
        throw std::invalid_argument(
            "TableLayoutPanelCompat track spacing must be finite and between zero and 256");
    }
    if (track_spacing_ == spacing) return;
    track_spacing_ = spacing;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_column_style(std::size_t column,
                                        TableLayoutStyle style) {
    require_mutable();
    if (column >= column_count_) {
        throw std::out_of_range("TableLayoutPanelCompat column style index");
    }
    validate_style(style);
    if (column_styles_[column] == style) return;
    column_styles_[column] = style;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::set_row_style(std::size_t row, TableLayoutStyle style) {
    require_mutable();
    if (row >= row_count_) throw std::out_of_range("TableLayoutPanelCompat row style index");
    validate_style(style);
    if (row_styles_[row] == style) return;
    row_styles_[row] = style;
    invalidate(invalidation::bounds);
}

void TableLayoutPanelCompat::prepare_write(const Control& child) const {
    require_mutable();
    require_direct_child(child);
}

void TableLayoutPanelCompat::written(const Control& child) {
    static_cast<void>(child);
    invalidate(invalidation::bounds);
}

bool TableLayoutPanelCompat::has_cell(const Control& child) const {
    require_direct_child(child);
    const bool found = cells_.contains(child.runtime_id().value);
    return found;
}

const GridSpan* TableLayoutPanelCompat::anchored_span(GridCell anchor) const noexcept {
    for (const GridSpan& span : spans_) {
        if (span.anchor == anchor) return &span;
    }
    return nullptr;
}

std::size_t TableLayoutPanelCompat::children_anchored_at(GridCell anchor) const noexcept {
    std::size_t count = 0U;
    for (const CellMap::value_type& entry : cells_) {
        if (entry.second == anchor) ++count;
    }
    return count;
}

void TableLayoutPanelCompat::check_span(const GridSpan& candidate, const GridSpan* replaced) const {
    if (static_cast<std::size_t>(candidate.anchor.column) + candidate.columns > maximum_layout_tracks ||
        static_cast<std::size_t>(candidate.anchor.row) + candidate.rows > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat span must end within 64 tracks");
    }
    for (const GridSpan& span : spans_) {
        if (&span == replaced) continue;
        if (grid_spans_overlap(span, candidate)) {
            throw std::invalid_argument("TableLayoutPanelCompat spans may not overlap");
        }
    }
    for (const CellMap::value_type& entry : cells_) {
        if (grid_span_interior(candidate, entry.second)) {
            throw std::invalid_argument("TableLayoutPanelCompat span would cover another child's cell");
        }
    }
}

void TableLayoutPanelCompat::store_span(GridCell anchor, const GridSpan& span) {
    const bool single = span.rows == 1U && span.columns == 1U;
    for (std::vector<GridSpan>::iterator stored = spans_.begin(); stored != spans_.end(); ++stored) {
        if ((*stored).anchor != anchor) continue;
        if (single) {
            spans_.erase(stored);
        } else {
            *stored = span;
        }
        return;
    }
    if (!single) spans_.push_back(span);
}

bool TableLayoutPanelCompat::replace_cell(const Control& child, GridCell cell) {
    if (cell.row >= maximum_layout_tracks || cell.column >= maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat cell exceeds 64 tracks");
    }
    const std::uint64_t id = child.runtime_id().value;
    const CellMap::iterator found = cells_.find(id);
    if (found != cells_.end() && (*found).second == cell) return false;
    // The child's span moves with it unless another child keeps the anchor.
    const GridSpan* carried = nullptr;
    if (found != cells_.end() && children_anchored_at((*found).second) == 1U) {
        carried = anchored_span((*found).second);
    }
    for (const GridSpan& span : spans_) {
        if (&span != carried && grid_span_interior(span, cell)) {
            throw std::invalid_argument("TableLayoutPanelCompat cell lies inside another child's span");
        }
    }
    if (carried == nullptr) {
        cells_[id] = cell;
        return true;
    }
    GridSpan moved = *carried;
    moved.anchor = cell;
    const GridCell previous = (*found).second;
    // Checked without the child's own old cell, which moves with it.
    cells_.erase(found);
    try {
        check_span(moved, carried);
    } catch (...) {
        cells_[id] = previous;
        throw;
    }
    const GridSpan single{std::string{}, previous, 1U, 1U};
    store_span(previous, single);
    spans_.push_back(moved);
    cells_[id] = cell;
    return true;
}

bool TableLayoutPanelCompat::remove_cell(const Control& child) {
    const CellMap::iterator found = cells_.find(child.runtime_id().value);
    if (found == cells_.end()) return false;
    if (anchored_span((*found).second) != nullptr && children_anchored_at((*found).second) == 1U) {
        throw std::invalid_argument("TableLayoutPanelCompat child spans; set its spans to 1 before clearing its cell");
    }
    cells_.erase(found);
    return true;
}

void TableLayoutPanelCompat::clear_cell(const Control& child) {
    prepare_write(child);
    if (!remove_cell(child)) return;
    written(child);
}

bool TableLayoutPanelCompat::replace_span(const Control& child, TableSpanAxis axis, std::uint32_t extent) {
    if (extent == 0U || extent > maximum_layout_tracks) {
        throw std::out_of_range("TableLayoutPanelCompat span must be 1 through 64");
    }
    const CellMap::const_iterator found = cells_.find(child.runtime_id().value);
    if (found == cells_.end()) {
        if (extent == 1U) return false;
        throw std::invalid_argument("TableLayoutPanelCompat child needs a cell before it can span");
    }
    const GridCell anchor = (*found).second;
    const GridSpan* existing = anchored_span(anchor);
    GridSpan candidate = existing == nullptr ? GridSpan{std::string{}, anchor, 1U, 1U} : *existing;
    std::uint32_t& changed = axis == TableSpanAxis::columns ? candidate.columns : candidate.rows;
    if (changed == extent) return false;
    changed = extent;
    check_span(candidate, existing);
    store_span(anchor, candidate);
    return true;
}

GridCell TableLayoutPanelCompat::stored_cell(const Control& child) const {
    require_direct_child(child);
    const CellMap::const_iterator found = cells_.find(child.runtime_id().value);
    const GridCell cell = found == cells_.end() ? GridCell{} : (*found).second;
    return cell;
}

std::uint32_t TableLayoutPanelCompat::stored_span(const Control& child, TableSpanAxis axis) const {
    require_direct_child(child);
    const CellMap::const_iterator found = cells_.find(child.runtime_id().value);
    if (found == cells_.end()) return 1U;
    const GridSpan* span = anchored_span((*found).second);
    if (span == nullptr) return 1U;
    const std::uint32_t extent = axis == TableSpanAxis::columns ? (*span).columns : (*span).rows;
    return extent;
}

std::optional<TableLayoutCellPosition> TableLayoutPanelCompat::cell_position(const Control& child) const {
    if (child.parent().get() != this || !child.is_alive()) return std::nullopt;
    if (attached_window() != nullptr) static_cast<void>(arranged_bounds());
    const ResolvedCellMap::const_iterator resolved = resolved_cells_.find(child.runtime_id().value);
    if (resolved != resolved_cells_.end()) return (*resolved).second.position;
    const CellMap::const_iterator found = cells_.find(child.runtime_id().value);
    if (found == cells_.end()) return std::nullopt;
    const TableLayoutCellPosition position{(*found).second.column, (*found).second.row};
    return position;
}

void TableLayoutPanelCompat::child_leaving(const Control& child) noexcept {
    const CellMap::iterator found = cells_.find(child.runtime_id().value);
    if (found == cells_.end()) return;
    const GridCell anchor = (*found).second;
    cells_.erase(found);
    // A span no child is anchored at any longer goes with its child.
    if (children_anchored_at(anchor) != 0U) return;
    for (std::vector<GridSpan>::iterator stored = spans_.begin(); stored != spans_.end(); ++stored) {
        if ((*stored).anchor == anchor) {
            spans_.erase(stored);
            return;
        }
    }
}

void TableLayoutPanelCompat::place_items(Workspace& workspace) {
    std::vector<std::uint8_t>& occupied = workspace.occupied;
    occupied.assign(occupancy_capacity, 0U);
    std::size_t columns = column_count_;
    std::size_t rows = row_count_;
    for (const Control::Ptr& child : workspace.retained) {
        if (!is_current_layout_child(child) || !(*child).visible.read()) continue;
        const CellMap::const_iterator found = cells_.find((*child).runtime_id().value);
        if (found == cells_.end()) {
            workspace.automatic.push_back(child);
            continue;
        }
        const TableLayoutCellPosition position{(*found).second.column, (*found).second.row};
        const GridSpan* span = anchored_span((*found).second);
        const std::size_t column_span = span == nullptr ? 1U : (*span).columns;
        const std::size_t row_span = span == nullptr ? 1U : (*span).rows;
        if (!grow_to_fit(columns, rows, position, column_span, row_span, grow_style_) ||
            !region_free(occupied, columns, rows, position.column, position.row, column_span, row_span)) {
            workspace.overflow.push_back(child);
            continue;
        }
        occupy(occupied, position.column, position.row, column_span, row_span);
        workspace.items.push_back(TableItem{child, position, column_span, row_span, Size{}, (*child).margin.read()});
    }

    // An automatic child takes one cell: a span needs an explicit cell.
    const std::size_t column_span = 1U;
    const std::size_t row_span = 1U;
    for (const Control::Ptr& child : workspace.automatic) {
        std::optional<TableLayoutCellPosition> position;
        while (!position) {
            for (std::size_t row = 0U; row < rows && !position; ++row) {
                for (std::size_t column = 0U; column < columns; ++column) {
                    if (region_free(occupied, columns, rows, column, row, column_span, row_span)) {
                        position = TableLayoutCellPosition{column, row};
                        break;
                    }
                }
            }
            if (position) break;
            if (grow_style_ == TableLayoutGrowStyle::add_rows && rows < maximum_layout_tracks) {
                ++rows;
            } else if (grow_style_ == TableLayoutGrowStyle::add_columns && columns < maximum_layout_tracks) {
                ++columns;
            } else {
                break;
            }
        }
        if (!position) {
            workspace.overflow.push_back(child);
            continue;
        }
        occupy(occupied, (*position).column, (*position).row, column_span, row_span);
        workspace.items.push_back(TableItem{child, *position, column_span, row_span, Size{}, (*child).margin.read()});
    }
    workspace.columns = columns;
    workspace.rows = rows;
}

Size TableLayoutPanelCompat::layout_children(Size available, bool assign) {
    if (workspace_busy_) {
        // A child's measure laid this panel out again inside a pass: the
        // nested pass takes its own storage, so the outer pass's survives.
        Workspace nested{};
        const Size nested_size = layout_pass(available, assign, nested);
        return nested_size;
    }
    workspace_busy_ = true;
    struct Release final {
        bool& busy;
        ~Release() { busy = false; }
    } release{workspace_busy_};
    const Size size = layout_pass(available, assign, workspace_);
    return size;
}

Size TableLayoutPanelCompat::layout_pass(Size available, bool assign, Workspace& workspace) {
    const std::uint64_t pass = ++pass_;
    const Insets inset = padding.read();
    const Size inner{std::max(0.0, available.width - horizontal_extent(inset)),
                     std::max(0.0, available.height - vertical_extent(inset))};
    layout_overflowed_ = false;

    // The children, held for the pass: a measure callback may change the
    // tree, so each is revalidated after it is measured.
    const std::span<const Control::Ptr> all = children();
    workspace.retained.assign(all.begin(), all.end());
    workspace.automatic.clear();
    workspace.overflow.clear();
    workspace.items.clear();
    workspace.items.reserve(workspace.retained.size());
    place_items(workspace);
    workspace.retained.clear();
    workspace.automatic.clear();
    const std::size_t columns = workspace.columns;
    const std::size_t rows = workspace.rows;

    std::vector<TableLayoutStyle>& column_styles = workspace.column_styles;
    std::vector<TableLayoutStyle>& row_styles = workspace.row_styles;
    column_styles.assign(column_styles_.begin(), column_styles_.end());
    row_styles.assign(row_styles_.begin(), row_styles_.end());
    column_styles.resize(columns);
    row_styles.resize(rows);
    const double horizontal_spacing = track_spacing_.width *
        static_cast<double>(columns > 0U ? columns - 1U : 0U);
    const double vertical_spacing = track_spacing_.height *
        static_cast<double>(rows > 0U ? rows - 1U : 0U);
    std::vector<double>& column_minimum = workspace.column_minimum;
    std::vector<double>& row_minimum = workspace.row_minimum;
    column_minimum.assign(columns, 0.0);
    row_minimum.assign(rows, 0.0);
    workspace.column_spans.clear();
    workspace.row_spans.clear();
    for (TableItem& item : workspace.items) {
        if (!is_current_layout_child(item.control) || !(*item.control).visible.read()) {
            item.control.reset();
            continue;
        }
        item.desired = preferred_child_size(item.control, inner);
        if (!is_alive()) return {};
        if (!is_current_layout_child(item.control) || !(*item.control).visible.read()) {
            item.control.reset();
            continue;
        }
        const double required_width = item.desired.width + horizontal_extent(item.margin);
        const double required_height = item.desired.height + vertical_extent(item.margin);
        if (item.column_span == 1U && column_styles[item.position.column].size_mode != TableSizeMode::absolute) {
            column_minimum[item.position.column] = std::max(column_minimum[item.position.column], required_width);
        } else if (item.column_span > 1U) {
            const double spacing = track_spacing_.width * static_cast<double>(item.column_span - 1U);
            workspace.column_spans.push_back(
                TrackDemand{item.position.column, item.column_span, std::max(0.0, required_width - spacing)});
        }
        if (item.row_span == 1U && row_styles[item.position.row].size_mode != TableSizeMode::absolute) {
            row_minimum[item.position.row] = std::max(row_minimum[item.position.row], required_height);
        } else if (item.row_span > 1U) {
            const double spacing = track_spacing_.height * static_cast<double>(item.row_span - 1U);
            workspace.row_spans.push_back(
                TrackDemand{item.position.row, item.row_span, std::max(0.0, required_height - spacing)});
        }
    }

    std::vector<double>& column_actual = workspace.column_actual;
    std::vector<double>& row_actual = workspace.row_actual;
    column_actual.assign(columns, 0.0);
    row_actual.assign(rows, 0.0);
    const double desired_width = resolve_tracks(column_styles, column_minimum, workspace.column_spans,
                                                std::max(0.0, inner.width - horizontal_spacing), column_actual);
    const double desired_height = resolve_tracks(row_styles, row_minimum, workspace.row_spans,
                                                 std::max(0.0, inner.height - vertical_spacing), row_actual);
    std::vector<double>& column_offsets = workspace.column_offsets;
    std::vector<double>& row_offsets = workspace.row_offsets;
    column_offsets.assign(columns + 1U, 0.0);
    row_offsets.assign(rows + 1U, 0.0);
    for (std::size_t index = 0U; index < columns; ++index) {
        column_offsets[index + 1U] = column_offsets[index] + column_actual[index] +
                                     (index + 1U < columns ? track_spacing_.width : 0.0);
    }
    for (std::size_t index = 0U; index < rows; ++index) {
        row_offsets[index + 1U] = row_offsets[index] + row_actual[index] +
                                  (index + 1U < rows ? track_spacing_.height : 0.0);
    }
    // Right to left mirrors each placed rectangle about the panel, once, at
    // output: x becomes the width less the logical right edge.
    const bool right_to_left = effective_layout_direction() == LayoutDirection::right_to_left;
    if (assign) {
        column_widths_.assign(column_actual.begin(), column_actual.end());
        row_heights_.assign(row_actual.begin(), row_actual.end());
        painted_column_widths_.assign(column_actual.begin(), column_actual.end());
        if (right_to_left) std::reverse(painted_column_widths_.begin(), painted_column_widths_.end());
        const double grid_width = column_offsets[columns];
        grid_left_ = right_to_left ? available.width - inset.left - grid_width : inset.left;
    }

    for (const TableItem& item : workspace.items) {
        if (!item.control || !is_current_layout_child(item.control)) continue;
        ResolvedCell& resolved = resolved_cells_[(*item.control).runtime_id().value];
        resolved.position = item.position;
        resolved.column_span = item.column_span;
        resolved.row_span = item.row_span;
        resolved.seen_pass = pass;
        if (!assign) continue;
        const std::size_t column_end = item.position.column + item.column_span;
        const std::size_t row_end = item.position.row + item.row_span;
        const double cell_width = column_offsets[column_end] - column_offsets[item.position.column] -
                                  (column_end < columns ? track_spacing_.width : 0.0);
        const double cell_height = row_offsets[row_end] - row_offsets[item.position.row] -
                                   (row_end < rows ? track_spacing_.height : 0.0);
        const double available_width = std::max(0.0, cell_width - horizontal_extent(item.margin));
        const double available_height = std::max(0.0, cell_height - vertical_extent(item.margin));
        const double cell_x = inset.left + column_offsets[item.position.column] + item.margin.left;
        const double cell_y = inset.top + row_offsets[item.position.row] + item.margin.top;
        const Rect area{cell_x, cell_y, available_width, available_height};
        Rect placed = place_in_cell((*item.control).dock.read(), (*item.control).anchor.read(), area, item.desired);
        if (right_to_left) placed.x = available.width - placed.x - placed.width;
        set_child_layout(item.control, placed);
    }
    // The cells this pass did not resolve go.
    ResolvedCellMap::iterator stale = resolved_cells_.begin();
    while (stale != resolved_cells_.end()) {
        if ((*stale).second.seen_pass != pass) {
            stale = resolved_cells_.erase(stale);
        } else {
            ++stale;
        }
    }
    layout_overflowed_ = !workspace.overflow.empty();
    if (assign) {
        const double corner = right_to_left ? available.width - inset.left : inset.left;
        for (const Control::Ptr& child : workspace.overflow) {
            if (is_current_layout_child(child)) set_child_layout(child, {corner, inset.top, 0.0, 0.0});
        }
    }
    // Releases the pass's references to the children; the capacity stays.
    workspace.items.clear();
    workspace.overflow.clear();
    return {desired_width + horizontal_spacing + horizontal_extent(inset),
            desired_height + vertical_spacing + vertical_extent(inset)};
}

Control::Ptr TableLayoutPanelCompat::control_from_position(std::size_t column,
                                                      std::size_t row) const {
    if (attached_window() != nullptr) static_cast<void>(arranged_bounds());
    for (const Control::Ptr& child : children()) {
        if (!child || !(*child).is_alive() || !(*child).visible.read()) continue;
        const ResolvedCellMap::const_iterator position =
            resolved_cells_.find((*child).runtime_id().value);
        if (position == resolved_cells_.end()) continue;
        const std::size_t column_span = (*position).second.column_span;
        const std::size_t row_span = (*position).second.row_span;
        const TableLayoutCellPosition cell = (*position).second.position;
        if (column >= cell.column && column < cell.column + column_span &&
            row >= cell.row && row < cell.row + row_span) {
            return child;
        }
    }
    return {};
}

Size TableLayoutPanelCompat::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    if (auto_size.read()) {
        const Size desired = layout_children(available, false);
        return {std::min(available.width, desired.width),
                std::min(available.height, desired.height)};
    }
    const Rect requested = requested_bounds();
    return {std::min(available.width,
                     requested.width > 0.0 ? requested.width : available.width),
            std::min(available.height,
                     requested.height > 0.0 ? requested.height : available.height)};
}

void TableLayoutPanelCompat::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    static_cast<void>(layout_children({final_bounds.width, final_bounds.height}, true));
}

void TableLayoutPanelCompat::on_paint(Painter& painter, Rect) {
    if (cell_border_style_ == TableCellBorderStyle::none ||
        column_widths_.empty() || row_heights_.empty()) {
        return;
    }
    const Insets inset = padding.read();
    const double width = std::accumulate(column_widths_.begin(),
                                         column_widths_.end(), 0.0) +
        track_spacing_.width * static_cast<double>(column_widths_.size() - 1U);
    const double height = std::accumulate(row_heights_.begin(),
                                          row_heights_.end(), 0.0) +
        track_spacing_.height * static_cast<double>(row_heights_.size() - 1U);
    // The grid starts at its left edge (the right padding less its width,
    // right to left), its columns in painted order.
    Insets origin = inset;
    origin.left = grid_left_;
    const std::span<const double> columns = painted_column_widths_;
    // Single borders are keylines; inset and outset borders a keyline and a
    // seam a unit apart, the keyline above and left for inset and below and
    // right for outset, in the theme's rule colours.
    PartPaint keyline;
    keyline.family = ControlVisualRole::panel;
    keyline.role = PartRole::keyline;
    PartPaint seam = keyline;
    seam.role = PartRole::seam;
    if (cell_border_style_ == TableCellBorderStyle::single) {
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, keyline, 0.0);
    } else if (cell_border_style_ == TableCellBorderStyle::inset) {
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, keyline, 0.0);
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, seam, 1.0);
    } else {
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, seam, 0.0);
        draw_table_grid(painter, *this, columns, row_heights_, origin, track_spacing_,
                        width, height, keyline, 1.0);
    }
}

SemanticDescriptor TableLayoutPanelCompat::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}


namespace {

constexpr std::array<PartField<TableLayoutPanelCompat::Parts>, 1> table_layout_panel_compat_extra_fields{{
    {.declaration = {.name = "cell_grid", .role = PartRole::divider, .composed_from = {}},
     .member = &TableLayoutPanelCompat::Parts::cell_grid},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 1>
    table_layout_panel_compat_parts = joined_declarations(
        scrollable_detail::parts, part_declarations(table_layout_panel_compat_extra_fields));

constexpr ControlAnatomy table_layout_panel_compat_anatomy{
    .control_id = "table_layout_panel_compat",
    .parts = table_layout_panel_compat_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& TableLayoutPanelCompat::anatomy() const noexcept {
    return table_layout_panel_compat_anatomy;
}

void TableLayoutPanelCompat::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();
    if (cell_border_style_ != TableCellBorderStyle::none &&
        !column_widths_.empty() && !row_heights_.empty()) {
        // The extent on_paint rules: every track and the spacing between
        // them, from the padding origin.
        const Insets inset = padding.read();
        const double width = std::accumulate(column_widths_.begin(),
                                             column_widths_.end(), 0.0) +
            track_spacing_.width * static_cast<double>(column_widths_.size() - 1U);
        const double height = std::accumulate(row_heights_.begin(),
                                              row_heights_.end(), 0.0) +
            track_spacing_.height * static_cast<double>(row_heights_.size() - 1U);
        const Rect grid{grid_left_, inset.top, width, height};
        record.cell_grid = PartGeometry::from_bounds(grid);
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), table_layout_panel_compat_extra_fields, record, geometry);
}

TableLayoutPanelCompat::Parts TableLayoutPanelCompat::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), table_layout_panel_compat_extra_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
