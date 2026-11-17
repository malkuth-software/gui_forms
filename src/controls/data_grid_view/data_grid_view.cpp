#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/window/window.hpp"

#include "../panel/input_control_utilities.hpp"
#include "data_grid_view_bound_index.hpp"
#include "../scrollable_control/scrolling_utilities.hpp"
#include "../basic/number_text.hpp"

#include "gui_forms/binding/binding_schema/binding_schema.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/binding/number_format/number_format.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <variant>
#include <span>
#include <cstdio>
#include <array>
#include <charconv>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// Restores a flag when the scope ends, thrown out of or not.
struct RestoreFlag final {
    bool* flag{};
    bool saved{};
    ~RestoreFlag() { *flag = saved; }
};

// Selected cells are reported by row position, then column.
struct CellAddressOrder final {
    bool operator()(const DataGridViewCellAddress& left,
                    const DataGridViewCellAddress& right) const noexcept {
        if (left.row != right.row) return left.row < right.row;
        return left.column < right.column;
    }
};

// The rank of a value's kind in sort order: empty values first, then
// booleans, numbers, text, and every other kind by its text.
int value_rank(const BindingValue& value) noexcept {
    const BindingValueKind kind = binding_value_kind(value);
    if (kind == BindingValueKind::null) return 0;
    if (kind == BindingValueKind::boolean) return 1;
    if (kind == BindingValueKind::signed_integer || kind == BindingValueKind::unsigned_integer ||
        kind == BindingValueKind::number) {
        return 2;
    }
    if (kind == BindingValueKind::text) return 3;
    return 4;
}

// Orders two cell values: by rank, then booleans false first, numbers as
// doubles with NaN before every other number (so the order stays a strict
// weak order for std::stable_sort, A-gf-controls-02-F8), text and other
// kinds by their text in byte order.
bool value_less(const BindingValue& left, const BindingValue& right) {
    const int left_rank = value_rank(left);
    const int right_rank = value_rank(right);
    if (left_rank != right_rank) return left_rank < right_rank;
    if (left_rank == 0) return false;
    if (left_rank == 1) {
        const bool left_value = std::get<bool>(left);
        const bool right_value = std::get<bool>(right);
        return !left_value && right_value;
    }
    if (left_rank == 2) {
        const double left_value = binding_value_to_number(left).value_or(0.0);
        const double right_value = binding_value_to_number(right).value_or(0.0);
        const bool left_nan = std::isnan(left_value);
        const bool right_nan = std::isnan(right_value);
        if (left_nan || right_nan) return left_nan && !right_nan;
        return left_value < right_value;
    }
    if (left_rank == 3) return std::get<std::string>(left) < std::get<std::string>(right);
    // Other kinds compare by their text; they are rare in a sort column.
    const std::string left_text = binding_value_to_string(left);
    const std::string right_text = binding_value_to_string(right);
    return left_text < right_text;
}

// Orders unbound row positions by one column's values for a stable sort.
struct RowValueOrder final {
    std::span<const DataGridViewRow> rows{};
    std::size_t column{};
    bool descending{};

    bool operator()(std::size_t left, std::size_t right) const {
        const BindingValue& left_value = rows[left].cells[column];
        const BindingValue& right_value = rows[right].cells[column];
        if (descending) return value_less(right_value, left_value);
        return value_less(left_value, right_value);
    }
};

// The grid's value order as a three-way result: negative, zero or
// positive when `left` goes before, with or after `right`.
int value_order(const BindingValue& left, const BindingValue& right) {
    if (value_less(left, right)) return -1;
    if (value_less(right, left)) return 1;
    return 0;
}

// -1, 0 or 1 for a comparison's result, so that reversing it for a
// descending sort cannot overflow.
int result_sign(int result) noexcept {
    if (result < 0) return -1;
    if (result > 0) return 1;
    return 0;
}

// Orders unbound row indices by an application's DataGridViewRowComparer.
struct RowComparerOrder final {
    const DataGridViewRowComparer* comparer{};
    std::span<const DataGridViewRow> rows{};

    int operator()(std::size_t left, std::size_t right) const {
        const int result = (*comparer).compare(rows[left], rows[right]);
        return result;
    }
};

// A stable bottom-up merge sort of `order` by `compare` (a functor whose
// three-way result for two indices need not be a consistent order: every
// read and write stays within `order` and `scratch` whatever it returns).
// An element of the right run goes first only when it compares strictly
// before the left run's, which keeps equal elements in order. `scratch` is
// resized to `order`'s size and its contents are left unspecified.
template <typename Compare>
void merge_sort_indices(std::vector<std::size_t>& order, std::vector<std::size_t>& scratch, Compare& compare) {
    const std::size_t count = order.size();
    scratch.resize(count);
    for (std::size_t width = 1U; width < count; width *= 2U) {
        for (std::size_t begin = 0U; begin < count; begin += width * 2U) {
            const std::size_t middle = std::min(begin + width, count);
            const std::size_t end = std::min(begin + width * 2U, count);
            std::size_t left = begin;
            std::size_t right = middle;
            std::size_t out = begin;
            while (left < middle && right < end) {
                if (compare(order[right], order[left]) < 0) {
                    scratch[out] = order[right];
                    ++right;
                } else {
                    scratch[out] = order[left];
                    ++left;
                }
                ++out;
            }
            for (; left < middle; ++left, ++out) scratch[out] = order[left];
            for (; right < end; ++right, ++out) scratch[out] = order[right];
        }
        order.swap(scratch);
        // Stop before width * 2 could wrap.
        if (width > count / 2U) break;
    }
}

bool contains_dot(std::string_view text) noexcept {
    const bool result = text.find('.') != std::string_view::npos;
    return result;
}

} // namespace

DataGridViewCellValueProvider::~DataGridViewCellValueProvider() = default;

bool DataGridViewCellValueProvider::can_add_rows() const {
    return false;
}

std::optional<DataGridViewRowId> DataGridViewCellValueProvider::add_row(std::span<const BindingValue>) {
    return std::nullopt;
}
DataGridViewRowComparer::~DataGridViewRowComparer() = default;

// Orders unbound row indices by one column, asking sort_compare's handlers
// first; a handler that does not decide leaves the grid's value order.
struct DataGridView::SortCompareOrder final {
    DataGridView* grid{};
    std::size_t column{};
    bool descending{};

    int operator()(std::size_t left, std::size_t right) const {
        const DataGridViewRow& first = (*grid).rows.read()[left];
        const DataGridViewRow& second = (*grid).rows.read()[right];
        DataGridViewSortCompareEvent request{.column = column,
                                             .cell_value1 = first.cells[column],
                                             .cell_value2 = second.cells[column],
                                             .row_id1 = first.stable_id.value,
                                             .row_id2 = second.stable_id.value,
                                             .row1 = DataGridViewRowPosition{left},
                                             .row2 = DataGridViewRowPosition{right}};
        (*grid).sort_compare_.emit(request);
        int result = 0;
        if (request.handled) {
            result = result_sign(request.sort_result);
        } else {
            result = value_order(first.cells[column], second.cells[column]);
        }
        if (descending) result = -result;
        return result;
    }
};

// Marks the grid as sorting for the comparisons an application decides,
// so that the mutations they may not make are refused; restored on every
// exit.
struct DataGridView::SortingScope final {
    DataGridView& grid;

    explicit SortingScope(DataGridView& sorted) : grid(sorted) { grid.sorting_ = true; }
    ~SortingScope() { grid.sorting_ = false; }
    SortingScope(const SortingScope&) = delete;
    SortingScope& operator=(const SortingScope&) = delete;
};

DataGridView::DataGridView(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
    set_paint_plane(PaintPlane::control);
    // The error glyphs are the grid's interface, not its data: each shows
    // its error text as a part tooltip (owner decision, 2026-11-02). Cells
    // and headers carry none.
    set_publishes_item_tool_tips(true);
    // allow_user_to_add_rows is on by default, so the new row has its ID
    // from the start.
    renew_new_row_id(false);
    choices_ = std::make_unique<ContextMenu>(std::string((*this).stable_id().value()) + ".choices");
    choice_invoked_ = (*choices_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<DataGridView, &DataGridView::on_choice_invoked>(*this));
    choices_changed_ = (*choices_).open_changed().subscribe(
        *this, Delegate<bool>::bind<DataGridView, &DataGridView::on_choices_open_changed>(*this));
}

DataGridView::~DataGridView() {
    close_drop_down();
}

void DataGridView::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"));
    (*editor_).border_style = BorderStyle::none;
    (*editor_).font = font.read();
    vertical_bar_ = make_control<VScrollBar>(StableId(prefix + ".vertical-scroll"));
    horizontal_bar_ = make_control<HScrollBar>(StableId(prefix + ".horizontal-scroll"));
    // The bars scroll the grid; keyboard focus stays on the grid.
    (*vertical_bar_).focusable = false;
    (*horizontal_bar_).focusable = false;
    add_child(editor_);
    add_child(vertical_bar_);
    add_child(horizontal_bar_);
    set_child_layout_collapsed(editor_, true);
    set_child_layout_collapsed(vertical_bar_, true);
    set_child_layout_collapsed(horizontal_bar_, true);
    editor_focus_ = (*editor_).focus_observed().subscribe(
        *this, Delegate<bool>::bind<DataGridView, &DataGridView::on_editor_focus>(*this));
    editor_text_changed_ = (*editor_).text_changed().subscribe(
        *this, Delegate<const std::string&>::bind<DataGridView, &DataGridView::on_editor_text_changed>(*this));
    vertical_scrolled_ = (*vertical_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<DataGridView, &DataGridView::on_vertical_scroll>(*this));
    horizontal_scrolled_ = (*horizontal_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<DataGridView, &DataGridView::on_horizontal_scroll>(*this));
}

void DataGridView::on_dispose() noexcept {
    close_drop_down();
    if (choices_) (*choices_).dispose();
    detach_data_source();
    Control::on_dispose();
}

// -- model -------------------------------------------------------------------

std::size_t DataGridView::data_row_count() const noexcept {
    if (row_source_ == DataGridViewRowSource::virtual_rows) return virtual_order_.count();
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (!data_source_ || !(*data_source_).is_alive()) return 0U;
        return (*data_source_).count();
    }
    return rows.read().size();
}

bool DataGridView::new_row_shown() const noexcept {
    if (!allow_add_rows_ || read_only.read() || columns_.empty() || new_row_.stable_id.value.empty()) return false;
    if (row_source_ == DataGridViewRowSource::unbound) return true;
    if (row_source_ == DataGridViewRowSource::virtual_rows) return provider_ && (*provider_).can_add_rows();
    if (row_source_ != DataGridViewRowSource::binding_source) return false;
    const bool shown = data_source_ && (*data_source_).is_alive() && (*data_source_).allow_new();
    return shown;
}

bool DataGridView::is_new_row(DataGridViewRowPosition row) const noexcept {
    const bool result = new_row_shown() && row.value == data_row_count();
    return result;
}

std::size_t DataGridView::row_count() const noexcept {
    const std::size_t data = data_row_count();
    const std::size_t result = new_row_shown() ? data + 1U : data;
    return result;
}

// Total (A-gf-controls-03-F1): empty, no row's ID, for a row at or past
// row_count(), a stale position kept across handlers that removed rows.
std::string DataGridView::row_id_text(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) return std::string{};
    if (is_new_row(row)) return new_row_.stable_id.value;
    if (row_source_ == DataGridViewRowSource::virtual_rows) return virtual_order_.id_at(row.value);
    if (row_source_ == DataGridViewRowSource::binding_source) {
        return (*data_source_).stable_id(row.value);
    }
    return rows.read()[row.value].stable_id.value;
}

void DataGridView::require_cell(DataGridViewRowPosition row, std::size_t column) const {
    if (row.value >= row_count() || column >= columns_.size()) {
        throw std::out_of_range("DataGridView cell is past the rows or columns");
    }
}

BindingValue DataGridView::stored_value(DataGridViewRowPosition row, std::size_t column) const {
    if (is_new_row(row)) {
        if (column >= new_row_.cells.size()) return BindingValue{};
        return new_row_.cells[column];
    }
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        if (!provider_) return BindingValue{};
        const DataGridViewRowId identity{virtual_order_.id_at(row.value)};
        return (*provider_).cell_value(identity, column);
    }
    if (row_source_ == DataGridViewRowSource::binding_source) {
        const std::string& field = columns_[column].data_property_name;
        if (field.empty()) return BindingValue{};
        // A cell the provider gave no valid answer for shows empty; its
        // error text says why (held_cell_error_text).
        const std::optional<BindingValue> value = (*data_source_).field(row.value, field);
        if (!value) return BindingValue{};
        return *value;
    }
    return rows.read()[row.value].cells[column];
}

bool DataGridView::store_value(DataGridViewRowPosition row, std::size_t column, const BindingValue& value) {
    if (is_new_row(row)) {
        // The new row shows the value; it becomes a row only by the user's
        // commit (store_user_value).
        new_row_.cells.resize(columns_.size());
        new_row_.cells[column] = value;
        return true;
    }
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        if (!provider_) return false;
        const DataGridViewRowId identity{virtual_order_.id_at(row.value)};
        const bool accepted = (*provider_).push_cell_value(identity, column, value);
        return accepted;
    }
    if (row_source_ == DataGridViewRowSource::binding_source) {
        const std::string& field = columns_[column].data_property_name;
        if (field.empty()) return false;
        BindingSource& source = *data_source_;
        const std::ptrdiff_t position = static_cast<std::ptrdiff_t>(row.value);
        if (source.position() != position) {
            synchronizing_position_ = true;
            static_cast<void>(source.set_position(position));
            synchronizing_position_ = false;
        }
        const bool accepted = source.set_current_field(field, value);
        return accepted;
    }
    // A field of the row, written in place and marked first.
    mark_row_replaced(*this, row.value);
    row_elements(*this)[row.value].cells[column] = value;
    return true;
}

bool DataGridView::row_read_only(DataGridViewRowPosition row) const {
    if (is_new_row(row) || row_source_ == DataGridViewRowSource::virtual_rows) return false;
    if (row_source_ == DataGridViewRowSource::binding_source) {
        // A row is read-only when the source is, or when every one of its
        // cells is (a BindingTable row that is not editable).
        BindingSource& source = *data_source_;
        if (!source.allow_edit()) return true;
        const std::size_t width = source.schema().size();
        bool every_cell_locked = width > 0U;
        for (std::size_t column = 0U; column < width && every_cell_locked; ++column) {
            every_cell_locked = source.read_only(row.value, column);
        }
        return every_cell_locked;
    }
    return rows.read()[row.value].read_only;
}

bool DataGridView::cell_read_only(DataGridViewRowPosition row, std::size_t column) const {
    if (read_only.read() || columns_[column].read_only || row_read_only(row)) return true;
    if (row_source_ != DataGridViewRowSource::binding_source || is_new_row(row)) return false;
    const std::string& field = columns_[column].data_property_name;
    if (field.empty()) return true;
    // A field the provider has not got is written by the built-in table,
    // which grows it; any other provider's missing field is read-only.
    const stxlib::Optional<std::size_t> bound = (*data_source_).column_of(field);
    if (bound.absent()) return !(*data_source_).table();
    return (*data_source_).read_only(row.value, (*bound.value()));
}

DataGridViewRowId DataGridView::row_id(DataGridViewRowPosition position) const {
    if (position.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    const DataGridViewRowId identity{row_id_text(position)};
    return identity;
}

stxlib::Optional<DataGridViewRowPosition> DataGridView::row_position(const DataGridViewRowId& row) const {
    const stxlib::Optional<std::size_t> found = find_row(row.value);
    if (found.absent()) return stxlib::Optional<DataGridViewRowPosition>::none();
    const DataGridViewRowPosition position{*found.value()};
    return stxlib::Optional<DataGridViewRowPosition>::of(position);
}

BindingValue DataGridView::cell_value(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    return stored_value(row, column);
}

namespace {

// The text a value shows in a cell. Numbers use the shortest text that reads
// back as the same value, not binding_value_to_string's seventeen digits.
std::string display_text(const BindingValue& value) {
    const BindingValueKind kind = binding_value_kind(value);
    if (kind == BindingValueKind::number) {
        const double number = std::get<double>(value);
        std::array<char, 32> buffer{};
        const std::to_chars_result written =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
        if (written.ec == std::errc{}) {
            const std::string text(buffer.data(), written.ptr);
            return text;
        }
    }
    return binding_value_to_string(value);
}

bool is_number_kind(BindingValueKind kind) noexcept {
    return kind == BindingValueKind::number || kind == BindingValueKind::signed_integer ||
           kind == BindingValueKind::unsigned_integer;
}

// A value's text under a style: the null value's text for an empty value,
// a number in the style's number format, else the value's own text.
std::string styled_text(const BindingValue& value, const DataGridViewCellStyle& style) {
    const BindingValueKind kind = binding_value_kind(value);
    if (kind == BindingValueKind::null) {
        if (!style.null_value) return {};
        std::string text = display_text(*style.null_value);
        return text;
    }
    if (style.format && is_number_kind(kind)) {
        const stxlib::Optional<NumberFormat> format = parse_number_format(*style.format);
        if (format.present()) {
            std::string text{};
            if (const std::int64_t* const signed_value = std::get_if<std::int64_t>(&value)) {
                append_formatted_number(text, *signed_value, (*format.value()));
            } else if (const std::uint64_t* const unsigned_value = std::get_if<std::uint64_t>(&value)) {
                append_formatted_number(text, *unsigned_value, (*format.value()));
            } else {
                append_formatted_number(text, std::get<double>(value), (*format.value()));
            }
            return text;
        }
    }
    std::string text = display_text(value);
    return text;
}

bool valid_text_value(const std::optional<BindingValue>& value) noexcept {
    if (!value) return true;
    const std::string* const text = std::get_if<std::string>(&*value);
    return text == nullptr || validate_utf8(*text).valid();
}

bool valid_alignment(DataGridViewContentAlignment alignment) noexcept {
    return static_cast<std::uint8_t>(alignment) <= static_cast<std::uint8_t>(DataGridViewContentAlignment::bottom_end);
}

// Fills the properties `style` leaves unset from `from` (one step of a cell
// style's inheritance chain).
void inherit_unset(DataGridViewCellStyle& style, const DataGridViewCellStyle& from) {
    if (style.font.absent()) style.font = from.font;
    if (style.alignment.absent()) style.alignment = from.alignment;
    if (style.padding.absent()) style.padding = from.padding;
    if (style.wrap_mode.absent()) style.wrap_mode = from.wrap_mode;
    if (!style.format) style.format = from.format;
    if (!style.null_value) style.null_value = from.null_value;
    if (!style.data_source_null_value) style.data_source_null_value = from.data_source_null_value;
    if (style.back_color.absent()) style.back_color = from.back_color;
    if (style.fore_color.absent()) style.fore_color = from.fore_color;
    if (style.selection_back_color.absent()) style.selection_back_color = from.selection_back_color;
    if (style.selection_fore_color.absent()) style.selection_fore_color = from.selection_fore_color;
}

} // namespace

DataGridView::FormattedCell DataGridView::formatted_cell(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    DataGridViewCellFormattingEvent request{};
    request.row_id = DataGridViewRowId{row_id_text(row)};
    request.row = row;
    request.column = column;
    request.value = stored_value(row, column);
    request.cell_style = inherited_cell_style(row, column);
    cell_formatting_.emit(request);
    // The handlers had the request alone; nothing of the grid is read again
    // here, so a handler that changed the grid leaves no dangling reference.
    FormattedCell result{};
    if (request.formatting_applied) {
        result.text = std::move(request.formatted_value);
    } else {
        result.text = styled_text(request.value, request.cell_style);
    }
    result.style = std::move(request.cell_style);
    return result;
}

std::string DataGridView::cell_text(DataGridViewRowPosition row, std::size_t column) const {
    FormattedCell formatted = formatted_cell(row, column);
    std::string text = shown_text(row, column, std::move(formatted.text));
    return text;
}

std::string DataGridView::shown_text(DataGridViewRowPosition row, std::size_t column, std::string formatted) const {
    // A formatting handler may have changed the grid: its text stands.
    if (column >= columns_.size() || row.value >= row_count()) return formatted;
    const DataGridViewColumn& definition = columns_[column];
    if (definition.kind == DataGridViewColumnKind::image) {
        // An image cell's text is its column's description while it holds an
        // image, and empty otherwise.
        const BindingValue value = stored_value(row, column);
        const ImageId* const image = std::get_if<ImageId>(&value);
        const bool holds_image = image != nullptr && (*image).value != 0U;
        std::string text = holds_image ? definition.image_description : std::string{};
        return text;
    }
    if (definition.kind == DataGridViewColumnKind::sparkline) {
        // The newest finite number of the collection, as assistive
        // technology reads it; empty without one.
        const BindingValue value = stored_value(row, column);
        const PropertyCollectionValue* const collection = std::get_if<PropertyCollectionValue>(&value);
        std::string text{};
        if (collection == nullptr) return text;
        const std::span<const BindingValue> items = property_collection_items(*collection);
        for (std::size_t remaining = items.size(); remaining > 0U; --remaining) {
            const double* const number = std::get_if<double>(&items[remaining - 1U]);
            if (number == nullptr || !std::isfinite(*number)) continue;
            // printf's "%g" in the C locale, whatever LC_NUMERIC says
            // (A-gf-controls-02-F4).
            number_text::append_number_text(text, *number, std::chars_format::general, 6);
            break;
        }
        return text;
    }
    if (formatted.empty() && definition.kind == DataGridViewColumnKind::button) return definition.button_text;
    if (formatted.empty() && definition.kind == DataGridViewColumnKind::link) return definition.link_text;
    return formatted;
}

double DataGridView::text_line_height(FontSpec face) const {
    // The primary face's metrics do not depend on the text, so a space
    // gives the line's height.
    const ResolvedTextLayout measured = resolve_text_layout_utf8(" ", face);
    const double height = std::max(measured.logical_size.height, face.size * 1.2);
    return height;
}

void DataGridView::require_link_cell(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    if (columns_[column].kind != DataGridViewColumnKind::link) {
        throw std::invalid_argument("DataGridView link state belongs to link columns only");
    }
}

bool DataGridView::is_link_visited(std::string_view row_id, std::size_t column) const {
    const SelectedCells::const_iterator found = visited_links_.find(row_id);
    if (found == visited_links_.end()) return false;
    const SelectedColumns& columns = (*found).second;
    const bool result = std::binary_search(columns.begin(), columns.end(), column);
    return result;
}

bool DataGridView::link_visited(DataGridViewRowPosition row, std::size_t column) const {
    require_link_cell(row, column);
    const bool result = is_link_visited(row_id_text(row), column);
    return result;
}

void DataGridView::set_link_visited(DataGridViewRowPosition row, std::size_t column, bool visited) {
    require_mutable();
    require_link_cell(row, column);
    const std::string row_id = row_id_text(row);
    SelectedCells::iterator found = visited_links_.find(row_id);
    if (visited) {
        if (found == visited_links_.end()) found = visited_links_.emplace(row_id, SelectedColumns{}).first;
        SelectedColumns& columns = (*found).second;
        const SelectedColumns::iterator place = std::lower_bound(columns.begin(), columns.end(), column);
        if (place != columns.end() && *place == column) return;
        columns.insert(place, column);
    } else {
        if (found == visited_links_.end()) return;
        SelectedColumns& columns = (*found).second;
        const SelectedColumns::iterator place = std::lower_bound(columns.begin(), columns.end(), column);
        if (place == columns.end() || *place != column) return;
        columns.erase(place);
        if (columns.empty()) visited_links_.erase(found);
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

bool DataGridView::set_cell_value(DataGridViewRowPosition row, std::size_t column, BindingValue value) {
    require_mutable();
    require_not_sorting();
    require_cell(row, column);
    const BindingValue previous = stored_value(row, column);
    if (previous == value) return true;
    const CellKey key = key_of({row, column});
    const DataGridViewCellEvent changed = cell_event(key);
    const bool new_row_cell = is_new_row(row);
    const bool accepted = store_value(row, column, value);
    if (!accepted) return false;
    // The new row holds its values apart from the rows.
    if (new_row_cell) {
        values_changed();
    } else {
        cell_changed(key);
    }
    cell_value_changed_.emit(changed);
    return true;
}

void DataGridView::structure_changed() {
    ++structure_revision_;
    auto_size_stale_ = true;
    row_auto_size_stale_ = true;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::reset_row_state() {
    cancel_edit();
    close_drop_down();
    const std::uint64_t before = selection_revision_;
    clear_selection_state();
    anchor_.reset();
    pressed_content_.reset();
    pressed_cell_.reset();
    visited_links_.clear();
    own_row_heights_.clear();
    row_heights_stale_ = true;
    first_row_ = 0U;
    cell_errors_.clear();
    row_errors_.clear();
    cell_styles_.clear();
    row_styles_.clear();
    // The rows are no longer in a sort's order; an automatic sort's glyph
    // goes with it.
    if (sorted_column_.present() && *sorted_column_.value() < columns_.size() &&
        column_sort_mode(*sorted_column_.value()) == DataGridViewColumnSortMode::automatic) {
        columns_[*sorted_column_.value()].sort_glyph = DataGridViewSortDirection::none;
    }
    sorted_column_.reset();
    sort_order_ = DataGridViewSortDirection::none;
    added_row_.reset();
    row_dirty_ = false;
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    apply_current(std::nullopt);
    publish_selection(before);
}

// -- row identity --------------------------------------------------------------

void DataGridView::rebuild_row_index() {
    // The new row is found by its own ID (find_row), not through the index.
    const std::size_t count = data_row_count();
    if (row_source_ == DataGridViewRowSource::binding_source) {
        std::vector<std::string> ids;
        ids.reserve(count);
        for (std::size_t position = 0; position < count; ++position) {
            const DataGridViewRowPosition row{position};
            ids.push_back(row_id_text(row));
        }
        if (!bound_index_) bound_index_ = std::make_unique<detail::DataGridViewBoundIndex>();
        (*bound_index_).assign(ids);
        row_positions_.clear();
        return;
    }
    RowPositions index{};
    index.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const DataGridViewRowPosition row{position};
        index.emplace(row_id_text(row), position);
    }
    row_positions_.swap(index);
}

void DataGridView::index_rows_from(std::size_t first) {
    for (std::size_t position = first; position < rows.read().size(); ++position) {
        row_positions_.insert_or_assign(rows.read()[position].stable_id.value, position);
    }
}

stxlib::Optional<std::size_t> DataGridView::find_row(std::string_view row_id) const {
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        const stxlib::Optional<std::size_t> position = virtual_order_.position_of(row_id);
        if (position.present()) return position;
        // The new row is last.
        if (new_row_shown() && row_id == new_row_.stable_id.value) return stxlib::Optional<std::size_t>::of(data_row_count());
        return stxlib::Optional<std::size_t>::none();
    }
    stxlib::Optional<std::size_t> indexed{};
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (bound_index_) indexed = (*bound_index_).find(row_id);
    } else {
        const RowPositions::const_iterator found = row_positions_.find(row_id);
        if (found != row_positions_.end()) indexed.set((*found).second);
    }
    if (indexed.absent()) {
        // The new row is last; a data row that took its ID is found first.
        if (new_row_shown() && row_id == new_row_.stable_id.value) return stxlib::Optional<std::size_t>::of(data_row_count());
        return stxlib::Optional<std::size_t>::none();
    }
    const std::size_t position = *indexed.value();
    // A binding source that changed without raising list_changed leaves the
    // index behind its records; a position now holding another row is not
    // this row.
    if (position >= data_row_count()) return stxlib::Optional<std::size_t>::none();
    const DataGridViewRowPosition row{position};
    if (row_id_text(row) != row_id) return stxlib::Optional<std::size_t>::none();
    return stxlib::Optional<std::size_t>::of(position);
}

DataGridViewRowPosition DataGridView::event_position(std::string_view row_id) const {
    DataGridViewRowPosition position{};
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        position.value = virtual_order_.held_position(row_id).value_or(0U);
        if (new_row_shown() && row_id == new_row_.stable_id.value) position.value = data_row_count();
        return position;
    }
    stxlib::Optional<std::size_t> indexed{};
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (bound_index_) indexed = (*bound_index_).find(row_id);
    } else {
        const RowPositions::const_iterator found = row_positions_.find(row_id);
        if (found != row_positions_.end()) indexed.set((*found).second);
    }
    if (indexed.present()) {
        position.value = *indexed.value();
    } else if (row_id == new_row_.stable_id.value) {
        position.value = data_row_count();
    }
    return position;
}

DataGridViewCellEvent DataGridView::cell_event(const CellKey& cell) const {
    DataGridViewCellEvent event{};
    event.row_id.value = cell.row_id;
    event.row = event_position(cell.row_id);
    event.column = cell.column;
    return event;
}

// A row at or past row_count() keys as no row (row_id_text is total).
DataGridView::CellKey DataGridView::key_of(DataGridViewCellAddress cell) const {
    const CellKey key{row_id_text(cell.row), cell.column};
    return key;
}

stxlib::Optional<DataGridViewCellAddress> DataGridView::address_of(const CellKey& cell) const {
    const stxlib::Optional<std::size_t> position = find_row(cell.row_id);
    if (position.absent()) return stxlib::Optional<DataGridViewCellAddress>::none();
    const DataGridViewCellAddress address{DataGridViewRowPosition{*position.value()}, cell.column};
    return stxlib::Optional<DataGridViewCellAddress>::of(address);
}

DataGridView::PressIdentity DataGridView::press_identity(const HitResult& hit) const {
    PressIdentity pressed{};
    const bool row_zone = hit.zone == HitZone::cell || hit.zone == HitZone::row_header || hit.zone == HitZone::row_divider;
    const bool column_zone =
        hit.zone == HitZone::cell || hit.zone == HitZone::column_header || hit.zone == HitZone::column_divider;
    if (row_zone && hit.row.value < row_count()) {
        pressed.row_id = row_id_text(hit.row);
        pressed.row = true;
    }
    if (column_zone && hit.column < columns_.size()) {
        pressed.column_name = columns_[hit.column].name;
        pressed.column = true;
    }
    return pressed;
}

stxlib::Optional<DataGridView::HitResult> DataGridView::resolve_press(const HitResult& hit,
                                                                      const PressIdentity& pressed) const {
    HitResult resolved = hit;
    if (pressed.row) {
        const stxlib::Optional<std::size_t> row = find_row(pressed.row_id);
        if (row.absent()) return stxlib::Optional<HitResult>::none();
        resolved.row = DataGridViewRowPosition{*row.value()};
    }
    if (pressed.column) {
        const stxlib::Optional<std::size_t> column = column_index(pressed.column_name);
        if (column.absent()) return stxlib::Optional<HitResult>::none();
        resolved.column = *column.value();
    }
    return stxlib::Optional<HitResult>::of(resolved);
}

stxlib::Optional<std::size_t> DataGridView::current_position() const {
    if (!current_) return stxlib::Optional<std::size_t>::none();
    return find_row((*current_).row_id);
}

bool DataGridView::is_current_row(DataGridViewRowPosition row) const {
    if (!current_ || row.value >= row_count()) return false;
    const bool result = (*current_).row_id == row_id_text(row);
    return result;
}

bool DataGridView::is_current_cell(DataGridViewRowPosition row, std::size_t column) const {
    const bool result = current_ && (*current_).column == column && is_current_row(row);
    return result;
}

void DataGridView::drop_missing_rows(stxlib::Optional<std::size_t> former_current) {
    const std::uint64_t before = selection_revision_;
    RowIdSet::iterator row = selected_rows_.begin();
    while (row != selected_rows_.end()) {
        if (find_row(*row).present()) {
            ++row;
            continue;
        }
        row = selected_rows_.erase(row);
        ++selection_revision_;
    }
    SelectedCells::iterator cells = selected_cells_.begin();
    while (cells != selected_cells_.end()) {
        if (find_row((*cells).first).present()) {
            ++cells;
            continue;
        }
        cells = selected_cells_.erase(cells);
        ++selection_revision_;
    }
    if (anchor_ && find_row((*anchor_).row_id).absent()) anchor_.reset();
    if (pressed_content_ && find_row((*pressed_content_).row_id).absent()) pressed_content_.reset();
    if (pressed_cell_ && find_row((*pressed_cell_).row_id).absent()) pressed_cell_.reset();
    SelectedCells::iterator visited = visited_links_.begin();
    while (visited != visited_links_.end()) {
        if (find_row((*visited).first).present()) {
            ++visited;
            continue;
        }
        visited = visited_links_.erase(visited);
    }
    RowHeightsById::iterator height = own_row_heights_.begin();
    while (height != own_row_heights_.end()) {
        if (find_row((*height).first).present()) {
            ++height;
            continue;
        }
        height = own_row_heights_.erase(height);
    }
    CellErrorRows::iterator errors = cell_errors_.begin();
    while (errors != cell_errors_.end()) {
        if (find_row((*errors).first).present()) {
            ++errors;
            continue;
        }
        errors = cell_errors_.erase(errors);
    }
    RowErrors::iterator row_error = row_errors_.begin();
    while (row_error != row_errors_.end()) {
        if (find_row((*row_error).first).present()) {
            ++row_error;
            continue;
        }
        row_error = row_errors_.erase(row_error);
    }
    CellStyleRows::iterator styles = cell_styles_.begin();
    while (styles != cell_styles_.end()) {
        if (find_row((*styles).first).present()) {
            ++styles;
            continue;
        }
        styles = cell_styles_.erase(styles);
    }
    RowStyles::iterator row_style = row_styles_.begin();
    while (row_style != row_styles_.end()) {
        if (find_row((*row_style).first).present()) {
            ++row_style;
            continue;
        }
        row_style = row_styles_.erase(row_style);
    }
    if (added_row_ && find_row(*added_row_).absent()) added_row_.reset();
    const std::size_t count = row_count();
    if (first_row_ >= count) first_row_ = 0U;
    if (current_ && find_row((*current_).row_id).absent()) {
        // The current row is gone: the row now at its position, or the new
        // last row, becomes current.
        std::optional<CellKey> next{};
        if (count > 0U && former_current.present()) {
            const DataGridViewRowPosition replacement{std::min(*former_current.value(), count - 1U)};
            next = CellKey{row_id_text(replacement), (*current_).column};
        }
        current_.reset();
        apply_current(std::move(next));
    }
    publish_selection(before);
}

// -- columns -----------------------------------------------------------------

void DataGridView::validate_column(const DataGridViewColumn& column) const {
    // The name is one segment of its header's and cells' semantic node IDs.
    if (contains_dot(column.name) || !valid_stable_id(column.name) ||
        !validate_utf8(column.header_text).valid() || !validate_utf8(column.button_text).valid() ||
        !validate_utf8(column.image_description).valid() || !validate_utf8(column.link_text).valid() ||
        !validate_utf8(column.data_property_name).valid() || !validate_utf8(column.tool_tip_text).valid()) {
        throw std::invalid_argument(
            "DataGridView columns require a name that is one StableId segment, and valid UTF-8");
    }
    if (!std::isfinite(column.width) || !std::isfinite(column.minimum_width) ||
        column.minimum_width < 1.0 || column.width < column.minimum_width) {
        throw std::invalid_argument("DataGridView column widths must be finite and at least the minimum");
    }
    if (column.kind != DataGridViewColumnKind::combo_box && !column.items.empty()) {
        throw std::invalid_argument("DataGridView column items belong to combo box columns only");
    }
    if (static_cast<std::uint8_t>(column.auto_size_mode) >
            static_cast<std::uint8_t>(DataGridViewAutoSizeColumnMode::fill) ||
        !std::isfinite(column.fill_weight) || column.fill_weight <= 0.0) {
        throw std::invalid_argument(
            "DataGridView column auto size mode must be in the enumeration and fill weight finite and positive");
    }
    if (static_cast<std::uint8_t>(column.display_style) >
        static_cast<std::uint8_t>(DataGridViewComboBoxDisplayStyle::nothing)) {
        throw std::invalid_argument("DataGridView combo box display style must be in the enumeration");
    }
    if (column.image_layout != DataGridViewImageLayout::normal &&
        column.image_layout != DataGridViewImageLayout::stretch && column.image_layout != DataGridViewImageLayout::zoom) {
        throw std::invalid_argument("DataGridView image layout must be normal, stretch or zoom");
    }
    validate_cell_style(column.default_cell_style);
    validate_cell_style(column.header_cell_style);
    for (const std::string& item : column.items) {
        if (!validate_utf8(item).valid()) {
            throw std::invalid_argument("DataGridView combo box items require valid UTF-8");
        }
    }
}

void DataGridView::set_columns(std::vector<DataGridViewColumn> columns) {
    require_mutable();
    require_not_sorting();
    std::unordered_set<std::string> names{};
    for (const DataGridViewColumn& column : columns) {
        validate_column(column);
        if (!names.insert(column.name).second) {
            throw std::invalid_argument("DataGridView column names must be unique");
        }
    }
    reset_row_state();
    std::vector<std::string> former_names{};
    former_names.reserve(columns_.size());
    for (const DataGridViewColumn& former : columns_) former_names.push_back(former.name);
    columns_ = std::move(columns);
    reset_display_order();
    mark_all_rows(*this);
    for (DataGridViewRow& row : row_elements(*this)) row.cells.resize(columns_.size());
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    horizontal_offset_ = 0.0;
    hot_column_.reset();
    pressed_column_.reset();
    resizing_column_.reset();
    end_column_order();
    structure_changed();
    // A handler that changes the columns ends the events for these.
    const std::uint64_t revision = structure_revision_;
    for (std::size_t index = former_names.size(); index > 0U; --index) {
        const DataGridViewColumnChangedEvent removed{index - 1U, former_names[index - 1U]};
        column_removed_.emit(removed);
        if (!is_alive() || structure_revision_ != revision) return;
    }
    for (std::size_t index = 0U; index < columns_.size(); ++index) {
        const DataGridViewColumnChangedEvent added{index, columns_[index].name};
        column_added_.emit(added);
        if (!is_alive() || structure_revision_ != revision) return;
    }
}

void DataGridView::add_column(DataGridViewColumn column) {
    require_mutable();
    require_not_sorting();
    validate_column(column);
    if (column_index(column.name).present()) {
        throw std::invalid_argument("DataGridView column names must be unique");
    }
    columns_.push_back(std::move(column));
    append_display_column();
    mark_all_rows(*this);
    for (DataGridViewRow& row : row_elements(*this)) row.cells.resize(columns_.size());
    new_row_.cells.resize(columns_.size());
    structure_changed();
    const DataGridViewColumnChangedEvent added{columns_.size() - 1U, columns_.back().name};
    column_added_.emit(added);
}

void DataGridView::remove_column_at(std::size_t column) {
    require_mutable();
    require_not_sorting();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    // reset_row_state raises the edit, cell, row, current-cell and
    // selection events, whose handlers may change the columns: the column
    // is named before them and found again after (A-gf-controls-02-F7). A
    // handler that removed it already did what was asked.
    const std::string name = columns_[column].name;
    reset_row_state();
    const stxlib::Optional<std::size_t> found = column_index(name);
    if (found.absent()) return;
    column = *found.value();
    const DataGridViewColumnChangedEvent removed{column, columns_[column].name};
    const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(column);
    columns_.erase(columns_.begin() + offset);
    remove_display_column(column);
    mark_all_rows(*this);
    for (DataGridViewRow& row : row_elements(*this)) row.cells.erase(row.cells.begin() + offset);
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    hot_column_.reset();
    pressed_column_.reset();
    resizing_column_.reset();
    end_column_order();
    structure_changed();
    column_removed_.emit(removed);
}

void DataGridView::insert_column(std::size_t column, DataGridViewColumn inserted) {
    require_mutable();
    require_not_sorting();
    if (column > columns_.size()) throw std::out_of_range("DataGridView column insertion is past the end");
    validate_column(inserted);
    if (column_index(inserted.name).present()) {
        throw std::invalid_argument("DataGridView column names must be unique");
    }
    // Validated. The row state is keyed by column index, which the
    // insertion shifts. reset_row_state raises events whose handlers may
    // change the columns: the checks are made again after it, and a change
    // that makes the insertion invalid refuses it with the row state reset
    // and the columns as the handlers left them (A-gf-controls-02-F7).
    reset_row_state();
    if (column > columns_.size() || column_index(inserted.name).present()) {
        throw std::logic_error("DataGridView columns changed under the insertion's row reset");
    }
    const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(column);
    columns_.insert(columns_.begin() + offset, std::move(inserted));
    insert_display_column(column);
    mark_all_rows(*this);
    for (DataGridViewRow& row : row_elements(*this)) {
        // A row shorter than the insertion point is padded by the resize.
        if (row.cells.size() >= column) row.cells.insert(row.cells.begin() + offset, BindingValue{});
        row.cells.resize(columns_.size());
    }
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    hot_column_.reset();
    pressed_column_.reset();
    resizing_column_.reset();
    end_column_order();
    structure_changed();
    const DataGridViewColumnChangedEvent added{column, columns_[column].name};
    column_added_.emit(added);
}

void DataGridView::set_column_header_text(std::size_t column, std::string text) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("DataGridView column header text requires valid UTF-8");
    }
    if (columns_[column].header_text == text) return;
    columns_[column].header_text = std::move(text);
    // A header-sized column measures its header again.
    auto_size_stale_ = true;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

// -- column order ------------------------------------------------------------

void DataGridView::reset_display_order() {
    const std::size_t count = columns_.size();
    display_order_.resize(count);
    display_indices_.resize(count);
    for (std::size_t column = 0U; column < count; ++column) {
        display_order_[column] = column;
        display_indices_[column] = column;
    }
    refresh_shown_order();
}

void DataGridView::append_display_column() {
    // The new column is the last by index and by display index.
    const std::size_t column = columns_.size() - 1U;
    display_order_.push_back(column);
    display_indices_.push_back(display_order_.size() - 1U);
    refresh_shown_order();
}

void DataGridView::remove_display_column(std::size_t column) {
    // Close the gap, and renumber the columns after the removed one, whose
    // indices fell by one.
    const std::size_t display = display_indices_[column];
    display_order_.erase(display_order_.begin() + static_cast<std::ptrdiff_t>(display));
    for (std::size_t& index : display_order_) {
        if (index > column) --index;
    }
    display_indices_.resize(display_order_.size());
    for (std::size_t position = 0U; position < display_order_.size(); ++position) {
        display_indices_[display_order_[position]] = position;
    }
    refresh_shown_order();
}

void DataGridView::insert_display_column(std::size_t column) {
    // Renumber the columns from the inserted index, which rose by one, then
    // place the new column at display index `column` (the last, past it).
    for (std::size_t& index : display_order_) {
        if (index >= column) ++index;
    }
    const std::size_t display = column < display_order_.size() ? column : display_order_.size();
    display_order_.insert(display_order_.begin() + static_cast<std::ptrdiff_t>(display), column);
    display_indices_.resize(display_order_.size());
    for (std::size_t position = 0U; position < display_order_.size(); ++position) {
        display_indices_[display_order_[position]] = position;
    }
    refresh_shown_order();
}

void DataGridView::refresh_shown_order() {
    columns_placed_changed();
    shown_order_.clear();
    shown_order_.reserve(display_order_.size());
    shown_indices_.resize(columns_.size());
    for (const std::size_t column : display_order_) {
        shown_indices_[column] = shown_order_.size();
        if (columns_[column].visible) shown_order_.push_back(column);
    }
}

double DataGridView::shown_width(std::size_t column) const noexcept {
    if (column >= columns_.size() || !columns_[column].visible) return 0.0;
    return columns_[column].width;
}

DataGridViewDisplayIndex DataGridView::column_display_index(std::size_t column) const {
    require_column(column);
    const DataGridViewDisplayIndex result{display_indices_[column]};
    return result;
}

std::size_t DataGridView::column_at_display_index(DataGridViewDisplayIndex display_index) const {
    if (display_index.value >= display_order_.size()) {
        throw std::out_of_range("DataGridView display index is past the last column");
    }
    return display_order_[display_index.value];
}

void DataGridView::set_column_display_index(std::size_t column, DataGridViewDisplayIndex display_index) {
    require_mutable();
    require_column(column);
    if (display_index.value >= columns_.size()) {
        throw std::out_of_range("DataGridView display index is past the last column");
    }
    const std::size_t from = display_indices_[column];
    const std::size_t to = display_index.value;
    if (from == to) return;
    display_order_.erase(display_order_.begin() + static_cast<std::ptrdiff_t>(from));
    display_order_.insert(display_order_.begin() + static_cast<std::ptrdiff_t>(to), column);
    const std::size_t low = std::min(from, to);
    const std::size_t high = std::max(from, to);
    // The columns whose display index changed, in their new order, kept
    // apart from the grid so that a handler's change cannot disturb the
    // list being raised.
    std::vector<std::size_t> moved{};
    moved.reserve(high - low + 1U);
    for (std::size_t position = low; position <= high; ++position) {
        display_indices_[display_order_[position]] = position;
        moved.push_back(display_order_[position]);
    }
    refresh_shown_order();
    structure_changed();
    for (const std::size_t changed : moved) {
        if (!is_alive()) return;
        column_display_index_changed_.emit(changed);
    }
}

void DataGridView::prepare_write(const decltype(allow_user_to_order_columns)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(allow_user_to_order_columns)&) {
    if (!allow_user_to_order_columns.read()) end_column_order();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    on_allow_user_to_order_columns_changed();
}

void DataGridView::end_column_order() {
    if (!ordering_ && order_target_.absent()) return;
    ordering_ = false;
    order_target_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

stxlib::Optional<std::size_t> DataGridView::column_index(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < columns_.size(); ++index) {
        if (columns_[index].name == name) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

void DataGridView::set_column_width(std::size_t column, double width) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (!std::isfinite(width) || width < columns_[column].minimum_width) {
        throw std::invalid_argument("DataGridView column width must be finite and at least the minimum");
    }
    if (columns_[column].width == width) return;
    columns_[column].width = width;
    columns_placed_changed();
    structure_changed();
    column_width_changed_.emit(column);
}

void DataGridView::set_column_sort_glyph(std::size_t column, DataGridViewSortDirection direction) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (columns_[column].sort_glyph == direction) return;
    columns_[column].sort_glyph = direction;
    invalidate(Dirty::paint | Dirty::semantics);
}

void DataGridView::set_column_read_only(std::size_t column, bool column_read_only) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (columns_[column].read_only == column_read_only) return;
    if (column_read_only && editing_ && current_ && (*current_).column == column) cancel_edit();
    columns_[column].read_only = column_read_only;
    invalidate(Dirty::paint | Dirty::semantics);
}

void DataGridView::set_column_frozen(std::size_t column, bool column_frozen) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (columns_[column].frozen == column_frozen) return;
    columns_[column].frozen = column_frozen;
    columns_placed_changed();
    // The whole grid moves: a redraw-all, folding the cell region.
    redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::size_t DataGridView::first_shown_row() const noexcept {
    const std::size_t result = frozen_rows() != 0U ? 0U : scroll_first_row();
    return result;
}

void DataGridView::prepare_write(const decltype(frozen_row_count)&, std::size_t&) {
    require_mutable();
}

void DataGridView::prepare_write(const decltype(minimum_row_height)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 4.0 || incoming > 4096.0) {
        throw std::out_of_range("DataGridView minimum row height must be between 4 and 4096");
    }
}

void DataGridView::written(const decltype(minimum_row_height)&) {
    row_heights_stale_ = true;
    structure_changed();
}

void DataGridView::written(const decltype(frozen_row_count)&) {
    structure_changed();
    // The whole grid moves: a redraw-all, folding the cell region.
    redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::set_column_visible(std::size_t column, bool column_visible) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (columns_[column].visible == column_visible) return;
    const bool current_hidden = !column_visible && current_ && (*current_).column == column;
    if (current_hidden && editing_) cancel_edit();
    columns_[column].visible = column_visible;
    columns_placed_changed();
    refresh_shown_order();
    structure_changed();
    if (!current_hidden || !is_alive()) return;
    // The current cell leaves the hidden column for a shown one in its row:
    // the next in display order, else the one before.
    const stxlib::Optional<DataGridViewCellAddress> current = current_cell();
    if (current.absent()) return;
    if (shown_order_.empty()) {
        if (!validate_row_change(std::nullopt)) return;
        anchor_.reset();
        apply_current(std::nullopt, current_.has_value());
        return;
    }
    const std::size_t next = shown_indices_[column];
    const std::size_t shown = next < shown_order_.size() ? next : shown_order_.size() - 1U;
    const DataGridViewCellAddress target{(*current.value()).row, shown_order_[shown]};
    static_cast<void>(move_current(target, SelectionGesture::replace));
}

// -- rows --------------------------------------------------------------------

std::string DataGridView::generated_row_id(const RowPositions& used) {
    for (;;) {
        std::string candidate = "row." + std::to_string(next_row_number_);
        ++next_row_number_;
        if (!used.contains(candidate) && candidate != new_row_.stable_id.value) return candidate;
    }
}

std::string DataGridView::generated_row_id() {
    for (;;) {
        std::string candidate = "row." + std::to_string(next_row_number_);
        ++next_row_number_;
        if (!row_id_taken(candidate) && candidate != new_row_.stable_id.value) return candidate;
    }
}

bool DataGridView::row_id_taken(std::string_view id) const {
    if (row_source_ == DataGridViewRowSource::binding_source) {
        const bool taken = bound_index_ && (*bound_index_).contains(id);
        return taken;
    }
    const bool taken = row_positions_.contains(id);
    return taken;
}

void DataGridView::validate_row_text(const DataGridViewRow& row) const {
    // A row's ID names its semantic node, so a given one follows the
    // StableId segment rule.
    if ((!row.stable_id.value.empty() && !valid_stable_id(row.stable_id.value)) ||
        !validate_utf8(row.header_text).valid()) {
        throw std::invalid_argument(
            "DataGridView rows require IDs following the StableId segment rule and valid UTF-8");
    }
    if (row.cells.size() > columns_.size()) {
        throw std::invalid_argument("DataGridView row has more values than columns");
    }
}

DataGridViewRow DataGridView::shared_row(DataGridViewRowPosition position) const {
    if (position.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    DataGridViewRow row{};
    row.stable_id.value = row_id_text(position);
    row.cells.reserve(columns_.size());
    for (std::size_t column = 0U; column < columns_.size(); ++column) row.cells.push_back(cell_value(position, column));
    return row;
}

void DataGridView::clear_rows() {
    require_mutable();
    require_not_sorting();
    const std::size_t former = data_row_count();
    reset_row_state();
    detach_data_source();
    provider_.reset();
    virtual_order_.clear();
    window_source_.reset();
    {
        const RestoreFlag restore{&quiet_row_edits_, quiet_row_edits_};
        quiet_row_edits_ = true;
        rows.clear();
    }
    row_positions_.clear();
    row_source_ = DataGridViewRowSource::unbound;
    rows_changed();
    static_cast<void>(raise_rows_removed(0U, former));
}

bool DataGridView::raise_rows_added(std::size_t position, std::size_t count) {
    if (count == 0U) return is_alive();
    const DataGridViewRowsChangedEvent added{DataGridViewRowPosition{position}, count};
    rows_added_.emit(added);
    return is_alive();
}

bool DataGridView::raise_rows_removed(std::size_t position, std::size_t count) {
    if (count == 0U) return is_alive();
    const DataGridViewRowsChangedEvent removed{DataGridViewRowPosition{position}, count};
    rows_removed_.emit(removed);
    return is_alive();
}

bool DataGridView::raise_rows_reset(std::size_t former) {
    if (!raise_rows_removed(0U, former)) return false;
    const bool alive = raise_rows_added(0U, data_row_count());
    return alive;
}

void DataGridView::set_row_count(std::size_t count) {
    require_mutable();
    require_not_sorting();
    if (row_source_ != DataGridViewRowSource::unbound) {
        throw std::logic_error("DataGridView virtual and bound rows are counted by their order and their source");
    }
    const std::size_t previous = rows.read().size();
    if (count < previous) {
        rows.erase(count, previous - count);
        return;
    }
    // Rows without IDs: the insertion generates them.
    std::vector<DataGridViewRow> added(count - previous);
    rows.append(std::move(added));
}

void DataGridView::validate_virtual_order(const std::vector<DataGridViewRowId>& order) {
    std::unordered_set<std::string_view> seen{};
    seen.reserve(order.size());
    for (const DataGridViewRowId& row : order) {
        if (row.value.empty() || !validate_utf8(row.value).valid()) {
            throw std::invalid_argument("DataGridView virtual row IDs must be valid UTF-8 and not empty");
        }
        if (!seen.insert(row.value).second) throw std::invalid_argument("DataGridView virtual row IDs must be unique");
    }
}

void DataGridView::require_virtual_rows(const char* what) const {
    if (row_source_ != DataGridViewRowSource::virtual_rows) throw std::logic_error(what);
}

void DataGridView::set_virtual_rows(std::shared_ptr<DataGridViewCellValueProvider> provider,
                                    std::vector<DataGridViewRowId> order) {
    require_mutable();
    require_not_sorting();
    if (!provider) throw std::invalid_argument("DataGridView virtual rows require a provider");
    validate_virtual_order(order);
    const std::size_t former = data_row_count();
    reset_row_state();
    detach_data_source();
    {
        const RestoreFlag restore{&quiet_row_edits_, quiet_row_edits_};
        quiet_row_edits_ = true;
        rows.clear();
    }
    row_positions_.clear();
    provider_ = std::move(provider);
    window_source_.reset();
    virtual_order_.reset(order);
    row_source_ = DataGridViewRowSource::virtual_rows;
    changes_this_pass_ = 0U;
    changes_last_pass_ = 0U;
    // Without a window source the order stays in edit mode; a policy pinned
    // to window falls back to automatic.
    if (row_order_policy.read() == DataGridViewRowOrderPolicy::window) row_order_policy = DataGridViewRowOrderPolicy::automatic;
    rows_changed();
    static_cast<void>(raise_rows_reset(former));
}

void DataGridView::set_virtual_rows(std::shared_ptr<DataGridViewCellValueProvider> provider,
                                    std::shared_ptr<DataGridViewRowWindowSource> source) {
    require_mutable();
    require_not_sorting();
    if (!provider) throw std::invalid_argument("DataGridView virtual rows require a provider");
    if (!source) throw std::invalid_argument("DataGridView virtual rows require a row window source");
    const std::size_t former = data_row_count();
    reset_row_state();
    detach_data_source();
    {
        const RestoreFlag restore{&quiet_row_edits_, quiet_row_edits_};
        quiet_row_edits_ = true;
        rows.clear();
    }
    row_positions_.clear();
    provider_ = std::move(provider);
    window_source_ = std::move(source);
    virtual_order_.reset_to_window(window_source_);
    window_reason_ = DataGridViewRowOrderSwitchReason::source;
    row_source_ = DataGridViewRowSource::virtual_rows;
    changes_this_pass_ = 0U;
    changes_last_pass_ = 0U;
    rows_changed();
    // Few rows, or a policy pinned to edit, take the whole order now.
    update_row_order_mode(DataGridViewRowOrderSwitchReason::source);
    static_cast<void>(raise_rows_reset(former));
}

void DataGridView::set_virtual_row_order(std::vector<DataGridViewRowId> order) {
    require_mutable();
    require_virtual_rows("DataGridView row order is set only on virtual rows");
    validate_virtual_order(order);
    const std::size_t former_count = data_row_count();
    // The current row's position in the old order, for the case that it is
    // gone from the new one.
    const stxlib::Optional<std::size_t> former =
        current_ ? virtual_order_.held_position((*current_).row_id) : stxlib::Optional<std::size_t>::none();
    bool current_kept = false;
    for (const DataGridViewRowId& row : order) {
        if (current_ && row.value == (*current_).row_id) current_kept = true;
    }
    if (editing_ && !current_kept) cancel_edit();
    // The first displayed row stays on its ID, as ObjectView's top row does.
    const std::optional<std::string> top = held_top_row();
    // A full republish is a reset: the order goes to edit mode with these
    // IDs, and the policy may take it to window mode again.
    virtual_order_.reset(order);
    std::size_t first = first_row_;
    if (top) {
        const stxlib::Optional<std::size_t> found = virtual_order_.position_of(*top);
        if (found.present()) first = *found.value();
    }
    first_row_ = first;
    drop_missing_rows(former);
    rows_changed();
    update_row_order_mode(DataGridViewRowOrderSwitchReason::source);
    static_cast<void>(raise_rows_reset(former_count));
}

std::optional<std::string> DataGridView::held_top_row() const {
    if (first_row_ >= virtual_order_.count()) return std::nullopt;
    std::optional<std::string> top = virtual_order_.held_id_at(first_row_);
    return top;
}

void DataGridView::after_reported_change(const std::optional<std::string>& top, std::size_t changed) {
    const std::size_t count = virtual_order_.count();
    if (top) {
        const stxlib::Optional<std::size_t> found = virtual_order_.position_of(*top);
        if (found.present()) first_row_ = *found.value();
    }
    if (first_row_ >= count) first_row_ = count == 0U ? 0U : count - 1U;
    changes_this_pass_ += changed;
    rows_changed();
    update_row_order_mode(DataGridViewRowOrderSwitchReason::change_rate);
}

void DataGridView::insert_virtual_rows(DataGridViewRowPosition position, std::vector<DataGridViewRowId> ids) {
    require_mutable();
    require_virtual_rows("DataGridView virtual rows are inserted only into virtual rows");
    const std::optional<std::string> top = held_top_row();
    // Validated by the order (edit mode) before anything changes; inserting
    // moves no selection, which is kept by ID.
    virtual_order_.insert(position.value, ids);
    after_reported_change(top, ids.size());
    static_cast<void>(raise_rows_added(position.value, ids.size()));
}

void DataGridView::remove_virtual_rows(std::vector<DataGridViewRowId> ids) {
    require_mutable();
    require_virtual_rows("DataGridView virtual rows are removed only from virtual rows");
    if (virtual_order_.mode() == DataGridViewRowOrderMode::edit) {
        std::unordered_set<std::string_view> seen{};
        seen.reserve(ids.size());
        for (const DataGridViewRowId& row : ids) {
            if (!virtual_order_.contains(row.value) || !seen.insert(row.value).second) {
                throw std::invalid_argument("DataGridView removed virtual rows must be present, each once");
            }
        }
    }
    const std::optional<std::string> top = held_top_row();
    const stxlib::Optional<std::size_t> former =
        current_ ? virtual_order_.held_position((*current_).row_id) : stxlib::Optional<std::size_t>::none();
    bool current_removed = false;
    for (const DataGridViewRowId& row : ids) {
        if (current_ && row.value == (*current_).row_id) current_removed = true;
    }
    if (editing_ && current_removed) cancel_edit();
    // The removed rows' positions the order holds, last first, so that each
    // rows_removed names a position in the rows the ones before it left.
    std::vector<std::size_t> removed_positions{};
    removed_positions.reserve(ids.size());
    for (const DataGridViewRowId& row : ids) {
        const stxlib::Optional<std::size_t> held = virtual_order_.held_position(row.value);
        if (held.present()) removed_positions.push_back(*held.value());
    }
    std::sort(removed_positions.begin(), removed_positions.end(), std::greater<std::size_t>{});
    virtual_order_.remove(ids);
    // Only the removed rows leave the selection: the cost follows the change,
    // not the selection's size.
    const std::uint64_t before = selection_revision_;
    for (const DataGridViewRowId& row : ids) {
        if (selected_rows_.erase(row.value) > 0U) ++selection_revision_;
        const SelectedCells::iterator cells = selected_cells_.find(row.value);
        if (cells != selected_cells_.end()) {
            selected_cells_.erase(cells);
            ++selection_revision_;
        }
        if (anchor_ && (*anchor_).row_id == row.value) anchor_.reset();
        if (pressed_content_ && (*pressed_content_).row_id == row.value) pressed_content_.reset();
        if (pressed_cell_ && (*pressed_cell_).row_id == row.value) pressed_cell_.reset();
        visited_links_.erase(row.value);
        own_row_heights_.erase(row.value);
    }
    if (current_removed) {
        const std::size_t count = virtual_order_.count();
        std::optional<CellKey> next{};
        if (count > 0U && former.present()) {
            const DataGridViewRowPosition replacement{std::min(*former.value(), count - 1U)};
            next = CellKey{row_id_text(replacement), (*current_).column};
        }
        current_.reset();
        apply_current(std::move(next));
    }
    publish_selection(before);
    after_reported_change(top, ids.size());
    for (const std::size_t position : removed_positions) {
        if (!raise_rows_removed(position, 1U)) return;
    }
}

void DataGridView::move_virtual_row(const DataGridViewRowId& row, DataGridViewRowPosition position) {
    require_mutable();
    require_virtual_rows("DataGridView virtual rows are moved only among virtual rows");
    const std::optional<std::string> top = held_top_row();
    virtual_order_.move(row.value, position.value);
    after_reported_change(top, 1U);
}

void DataGridView::set_virtual_row_window_source(std::shared_ptr<DataGridViewRowWindowSource> source) {
    require_mutable();
    require_virtual_rows("DataGridView row window sources serve only virtual rows");
    if (!source) {
        // The whole order comes from the old source before it goes.
        if (virtual_order_.mode() == DataGridViewRowOrderMode::window) {
            switch_row_order(DataGridViewRowOrderMode::edit, DataGridViewRowOrderSwitchReason::source);
        }
        if (virtual_order_.mode() == DataGridViewRowOrderMode::window) {
            throw std::logic_error("DataGridView could not take the whole row order from its window source");
        }
        window_source_.reset();
        if (row_order_policy.read() == DataGridViewRowOrderPolicy::window) row_order_policy = DataGridViewRowOrderPolicy::automatic;
        return;
    }
    window_source_ = std::move(source);
    update_row_order_mode(DataGridViewRowOrderSwitchReason::source);
}

void DataGridView::prepare_write(const decltype(row_order_policy)&, DataGridViewRowOrderPolicy& incoming) {
    require_mutable();
    if (incoming == DataGridViewRowOrderPolicy::window && !window_source_) {
        throw std::logic_error("DataGridView window mode requires a row window source");
    }
}

void DataGridView::written(const decltype(row_order_policy)&) {
    update_row_order_mode(DataGridViewRowOrderSwitchReason::policy);
}

void DataGridView::prepare_write(const decltype(row_order_thresholds)&, DataGridViewRowOrderThresholds& incoming) {
    require_mutable();
    if (incoming.edit_rows > incoming.window_rows || incoming.edit_changes > incoming.window_changes) {
        throw std::invalid_argument("DataGridView row order thresholds must leave edit below window");
    }
}

void DataGridView::written(const decltype(row_order_thresholds)&) {
    update_row_order_mode(DataGridViewRowOrderSwitchReason::policy);
}

void DataGridView::update_row_order_mode(DataGridViewRowOrderSwitchReason cause) {
    if (row_source_ != DataGridViewRowSource::virtual_rows) return;
    const DataGridViewRowOrderMode mode = virtual_order_.mode();
    if (row_order_policy.read() == DataGridViewRowOrderPolicy::edit) {
        if (mode != DataGridViewRowOrderMode::edit) switch_row_order(DataGridViewRowOrderMode::edit, cause);
        return;
    }
    if (row_order_policy.read() == DataGridViewRowOrderPolicy::window) {
        if (mode != DataGridViewRowOrderMode::window && window_source_) {
            switch_row_order(DataGridViewRowOrderMode::window, cause);
        }
        return;
    }
    if (!window_source_) return;
    const std::size_t count = virtual_order_.count();
    const DataGridViewRowOrderThresholds& limits = row_order_thresholds.read();
    const bool pinned_cause = cause == DataGridViewRowOrderSwitchReason::policy ||
                              cause == DataGridViewRowOrderSwitchReason::source;
    if (mode == DataGridViewRowOrderMode::edit) {
        if (count >= limits.window_rows) {
            switch_row_order(DataGridViewRowOrderMode::window,
                             pinned_cause ? cause : DataGridViewRowOrderSwitchReason::row_count);
        } else if (changes_this_pass_ >= limits.window_changes) {
            switch_row_order(DataGridViewRowOrderMode::window,
                             pinned_cause ? cause : DataGridViewRowOrderSwitchReason::change_rate);
        }
        return;
    }
    const bool few = count <= limits.edit_rows;
    const bool quiet = changes_last_pass_ <= limits.edit_changes && changes_this_pass_ <= limits.edit_changes;
    if (few && quiet) {
        // Back for the reason it left, unless a policy or source change moved it.
        const DataGridViewRowOrderSwitchReason reason = pinned_cause ? cause : window_reason_;
        switch_row_order(DataGridViewRowOrderMode::edit, reason);
    }
}

void DataGridView::switch_row_order(DataGridViewRowOrderMode mode, DataGridViewRowOrderSwitchReason reason) {
    const DataGridViewRowOrderMode old_mode = virtual_order_.mode();
    if (old_mode == mode) return;
    if (mode == DataGridViewRowOrderMode::edit) {
        // A source that gives a repeated or empty ID keeps the order in
        // window mode.
        if (!virtual_order_.switch_to_edit()) return;
    } else {
        if (!window_source_) return;
        virtual_order_.switch_to_window(window_source_);
        window_reason_ = reason;
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    DataGridViewRowOrderModeChange change{};
    change.old_mode = old_mode;
    change.new_mode = mode;
    change.reason = reason;
    change.row_count = virtual_order_.count();
    change.changes = changes_this_pass_;
    row_order_mode_changed_.emit(change);
}

void DataGridView::refresh_rows() {
    require_mutable();
    values_changed();
}

void DataGridView::detach_data_source() noexcept {
    source_list_changed_.disconnect();
    source_position_changed_.disconnect();
    data_source_.reset();
    bound_index_.reset();
    bound_removed_current_.reset();
}

std::shared_ptr<BindingSource> DataGridView::read_view(const decltype(data_source)&) const {
    std::shared_ptr<BindingSource> source = data_source_;
    return source;
}

void DataGridView::write_view(const decltype(data_source)&, std::shared_ptr<BindingSource> source) {
    require_mutable();
    require_not_sorting();
    const std::size_t former = data_row_count();
    reset_row_state();
    detach_data_source();
    provider_.reset();
    virtual_order_.clear();
    window_source_.reset();
    {
        const RestoreFlag restore{&quiet_row_edits_, quiet_row_edits_};
        quiet_row_edits_ = true;
        rows.clear();
    }
    row_positions_.clear();
    if (!source) {
        row_source_ = DataGridViewRowSource::unbound;
        rows_changed();
        static_cast<void>(raise_rows_removed(0U, former));
        return;
    }
    // Columns first, while no rows are bound.
    if (auto_generate_columns.read()) generate_columns((*source).schema());
    data_source_ = std::move(source);
    row_source_ = DataGridViewRowSource::binding_source;
    rebuild_row_index();
    source_list_changed_ = (*data_source_).list_changed().subscribe(
        *this, Delegate<const BindingListChange&>::bind<DataGridView, &DataGridView::on_source_list_changed>(*this));
    source_position_changed_ = (*data_source_).position_changed().subscribe(
        *this, Delegate<std::ptrdiff_t>::bind<DataGridView, &DataGridView::on_source_position_changed>(*this));
    rows_changed();
    on_source_position_changed((*data_source_).position());
    if (!is_alive()) return;
    static_cast<void>(raise_rows_reset(former));
}

void DataGridView::generate_columns(const BindingSchema& schema) {
    // A schema's field names are canonical (canonical_binding_name: trimmed,
    // ASCII letters lowered), and a source finds a field by any spelling that
    // folds to it, so a column's data_property_name names a field when its
    // canonical spelling is the field's ("Name" names the field "name").
    std::vector<std::string> named_fields{};
    named_fields.reserve(columns_.size());
    for (const DataGridViewColumn& column : columns_) {
        const bool blank = column.data_property_name.find_first_not_of(" \t\r\n") == std::string::npos;
        if (!blank) named_fields.push_back(canonical_binding_name(column.data_property_name));
    }
    for (std::size_t index = 0U; index < schema.size(); ++index) {
        const BindingColumn& field = schema.column(index);
        bool named = false;
        for (const std::string& name : named_fields) {
            if (name == field.name) named = true;
        }
        // The provider's spelling ("City"), as Windows Forms heads a
        // generated column with its property's name; it folds to the field.
        const std::string& spelling = schema.spelling(index);
        if (named || !validate_utf8(spelling).valid()) continue;
        DataGridViewColumn generated{};
        generated.name = generated_column_name(spelling);
        generated.header_text = spelling;
        generated.data_property_name = spelling;
        generated.value_kind = field.kind;
        generated.read_only = field.read_only;
        if (field.kind == BindingValueKind::boolean) generated.kind = DataGridViewColumnKind::check_box;
        add_column(std::move(generated));
    }
}

std::string DataGridView::generated_column_name(std::string_view field) const {
    // Lowercase letters and digits kept, every other run one '-', none at
    // either end: one StableId segment.
    std::string base{};
    base.reserve(field.size());
    for (const char character : field) {
        const char lower = (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a') : character;
        const bool kept = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9');
        if (kept) {
            base.push_back(lower);
        } else if (!base.empty() && base.back() != '-') {
            base.push_back('-');
        }
    }
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "column";
    std::string name = base;
    for (std::size_t suffix = 2U; column_index(name).present(); ++suffix) {
        name = base + "-" + std::to_string(suffix);
    }
    return name;
}

void DataGridView::on_source_list_changed(const BindingListChange& change) {
    if (row_source_ != DataGridViewRowSource::binding_source) return;
    const bool added = change.kind == BindingListChangeKind::item_added;
    const bool removed = change.kind == BindingListChangeKind::item_removed;
    const bool moved = change.kind == BindingListChangeKind::item_moved;
    const bool reset = change.kind == BindingListChangeKind::reset;
    if (!added && !removed && !moved && !reset) {
        // A changed field or position changes no geometry.
        values_changed();
        return;
    }
    // The rows the index held before a reset, for its rows_removed.
    const std::size_t indexed_before = bound_index_ ? (*bound_index_).size() : 0U;
    if (!bound_index_) rebuild_row_index();
    // The source has changed already; the index has not, so it still holds
    // the current row's position before this change. The changes of one
    // notification come one by one, each relative to the list the ones
    // before it left (BindingListChange), which is the order the index
    // follows them in.
    stxlib::Optional<std::size_t> former{};
    if (current_) former = (*bound_index_).find((*current_).row_id);
    const bool current_removed = removed && former.present() && change.index >= 0 &&
                                 *former.value() == static_cast<std::size_t>(change.index);
    if (editing_ && current_ && (current_removed || reset)) cancel_edit();
    if (current_removed) bound_removed_current_ = former;
    bool followed = false;
    if (added && change.index >= 0) {
        followed = (*bound_index_).insert(static_cast<std::size_t>(change.index), change.stable_id);
    } else if (removed && change.index >= 0) {
        followed = (*bound_index_).erase(static_cast<std::size_t>(change.index));
    } else if (moved && change.index >= 0 && change.to_index >= 0) {
        followed = (*bound_index_).move(static_cast<std::size_t>(change.index),
                                        static_cast<std::size_t>(change.to_index));
    }
    if (!followed) rebuild_row_index();
    // Part way through one notification's additions or removals the index
    // holds a different number of rows than the source, and the positions
    // after the change are not yet the source's: rows are dropped only once
    // it holds every row again. A move drops none.
    const bool settled = !moved && (*bound_index_).size() == data_row_count();
    if (settled) {
        // A record that took the new row's ID, other than the one
        // add_new_row is adding, moves the new row to another ID.
        if (!adding_row_ && row_id_taken(new_row_.stable_id.value)) renew_new_row_id(true);
        const stxlib::Optional<std::size_t> held = bound_removed_current_.present() ? bound_removed_current_ : former;
        bound_removed_current_.reset();
        drop_missing_rows(held);
    }
    rows_changed();
    if (added && change.index >= 0) {
        static_cast<void>(raise_rows_added(static_cast<std::size_t>(change.index), 1U));
    } else if (removed && change.index >= 0) {
        static_cast<void>(raise_rows_removed(static_cast<std::size_t>(change.index), 1U));
    } else if (reset) {
        static_cast<void>(raise_rows_reset(indexed_before));
    }
}

void DataGridView::on_source_position_changed(std::ptrdiff_t position) {
    if (synchronizing_position_ || row_source_ != DataGridViewRowSource::binding_source) return;
    if (position < 0 || columns_.empty()) return;
    const DataGridViewRowPosition row{static_cast<std::size_t>(position)};
    if (row.value >= row_count()) return;
    // An insert or removal above the current record renumbers the source's
    // position without moving it to another record; the grid keeps its
    // current cell and selection, which are kept by ID.
    if (is_current_row(row)) return;
    const std::size_t column = current_ ? (*current_).column : 0U;
    synchronizing_position_ = true;
    static_cast<void>(move_current({row, column}, SelectionGesture::replace));
    synchronizing_position_ = false;
}

bool DataGridView::sort(std::size_t column, DataGridViewSortDirection direction) {
    require_mutable();
    require_not_sorting();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (row_source_ != DataGridViewRowSource::unbound || direction == DataGridViewSortDirection::none) {
        return false;
    }
    if (!end_edit()) return false;
    // The edit's handlers may have changed the rows or columns.
    if (!is_alive() || column >= columns_.size() || row_source_ != DataGridViewRowSource::unbound) return false;
    const std::size_t count = rows.read().size();
    const bool descending = direction == DataGridViewSortDirection::descending;
    // order[new position] = old position.
    std::vector<std::size_t> order(count, 0U);
    for (std::size_t index = 0; index < count; ++index) order[index] = index;
    // Chosen before the sort: the grid's own order, or the comparisons
    // sort_compare's handlers decide (counted as connections less
    // disconnections, which the event keeps).
    const EventStatistics statistics = sort_compare_.statistics();
    const bool application_decides = statistics.subscriptions_connected > statistics.subscriptions_disconnected;
    if (!application_decides) {
        const RowValueOrder compare{rows.read(), column, descending};
        std::stable_sort(order.begin(), order.end(), compare);
    } else {
        const SortingScope scope(*this);
        SortCompareOrder compare{this, column, descending};
        std::vector<std::size_t> scratch{};
        merge_sort_indices(order, scratch, compare);
    }
    if (!is_alive()) return false;
    apply_row_order(order);
    for (std::size_t index = 0; index < columns_.size(); ++index) {
        columns_[index].sort_glyph = index == column ? direction : DataGridViewSortDirection::none;
    }
    sorted_column_.set(column);
    sort_order_ = direction;
    rows_changed();
    sorted_.emit();
    return true;
}

bool DataGridView::sort(const DataGridViewRowComparer& comparer) {
    require_mutable();
    require_not_sorting();
    if (row_source_ != DataGridViewRowSource::unbound) return false;
    if (!end_edit()) return false;
    if (!is_alive() || row_source_ != DataGridViewRowSource::unbound) return false;
    const std::size_t count = rows.read().size();
    std::vector<std::size_t> order(count, 0U);
    for (std::size_t index = 0; index < count; ++index) order[index] = index;
    {
        const SortingScope scope(*this);
        RowComparerOrder compare{&comparer, rows.read()};
        std::vector<std::size_t> scratch{};
        merge_sort_indices(order, scratch, compare);
    }
    if (!is_alive()) return false;
    apply_row_order(order);
    for (DataGridViewColumn& column : columns_) column.sort_glyph = DataGridViewSortDirection::none;
    sorted_column_.reset();
    sort_order_ = DataGridViewSortDirection::none;
    rows_changed();
    sorted_.emit();
    return true;
}

void DataGridView::apply_row_order(const std::vector<std::size_t>& order) {
    const std::size_t count = order.size();
    std::vector<DataGridViewRow> sorted{};
    sorted.reserve(count);
    const std::span<DataGridViewRow> present = row_elements(*this);
    for (std::size_t index = 0; index < count; ++index) sorted.push_back(std::move(present[order[index]]));
    // A reordering, not an edit of the rows: stored without the hooks.
    store_rows(*this, std::move(sorted));
    // The selection, the current cell and the anchor are kept by ID: only
    // the positions change.
    rebuild_row_index();
    pressed_content_.reset();
    pressed_cell_.reset();
}

void DataGridView::require_not_sorting() const {
    if (sorting_) {
        throw std::logic_error("DataGridView's rows, columns and values do not change while it sorts");
    }
}

DataGridViewColumnSortMode DataGridView::column_sort_mode(std::size_t column) const {
    require_column(column);
    const DataGridViewColumn& definition = columns_[column];
    if (definition.sort_mode.present()) return *definition.sort_mode.value();
    if (definition.kind == DataGridViewColumnKind::text) return DataGridViewColumnSortMode::automatic;
    return DataGridViewColumnSortMode::not_sortable;
}

void DataGridView::set_column_sort_mode(std::size_t column, stxlib::Optional<DataGridViewColumnSortMode> mode) {
    require_mutable();
    require_column(column);
    const DataGridViewColumnSortMode* const given = mode.value();
    if (given != nullptr && *given != DataGridViewColumnSortMode::not_sortable &&
        *given != DataGridViewColumnSortMode::automatic && *given != DataGridViewColumnSortMode::programmatic) {
        throw std::invalid_argument("DataGridView column sort mode is outside the enumeration");
    }
    columns_[column].sort_mode = mode;
}

void DataGridView::sort_from_header(std::size_t column) {
    // A column_header_click handler may have changed the grid.
    if (!is_alive() || column >= columns_.size() || row_source_ != DataGridViewRowSource::unbound) return;
    if (column_sort_mode(column) != DataGridViewColumnSortMode::automatic) return;
    DataGridViewSortDirection direction = DataGridViewSortDirection::ascending;
    if (sorted_column_ == stxlib::Optional<std::size_t>::of(column) && sort_order_ == DataGridViewSortDirection::ascending) {
        direction = DataGridViewSortDirection::descending;
    }
    static_cast<void>(sort(column, direction));
}

// -- clipboard -----------------------------------------------------------------

std::string DataGridView::row_header_text(DataGridViewRowPosition row) const {
    if (row_source_ != DataGridViewRowSource::unbound || row.value >= rows.read().size()) return {};
    return rows.read()[row.value].header_text;
}

std::optional<std::string> DataGridView::clipboard_content() const {
    if (clipboard_copy_mode.read() == DataGridViewClipboardCopyMode::disable) return std::nullopt;
    // By row, then column (CellAddressOrder).
    const std::vector<DataGridViewCellAddress> cells = selected_cells();
    if (cells.empty()) return std::nullopt;
    std::vector<std::size_t> columns{};
    columns.reserve(columns_.size());
    // The columns with a selected cell, by display index.
    std::vector<bool> copied(columns_.size(), false);
    for (const DataGridViewCellAddress& cell : cells) copied[cell.column] = true;
    for (const std::size_t column : display_order_) {
        if (copied[column]) columns.push_back(column);
    }
    const bool always = clipboard_copy_mode.read() == DataGridViewClipboardCopyMode::enable_always_include_header_text;
    const bool automatic = clipboard_copy_mode.read() == DataGridViewClipboardCopyMode::enable_with_auto_header_text;
    const bool row_headers =
        row_headers_visible.read() && (always || (automatic && selection_mode.read() == DataGridViewSelectionMode::full_row_select));
    const bool column_headers = column_headers_visible.read() && always;
    std::string text{};
    if (column_headers) {
        // The row header column's place stays empty in the header line.
        bool first = !row_headers;
        for (const std::size_t column : columns) {
            if (!first) text.push_back('\t');
            first = false;
            text.append(columns_[column].header_text);
        }
        text.append("\r\n");
    }
    std::size_t index = 0U;
    const std::size_t count = cells.size();
    while (index < count) {
        const DataGridViewRowPosition row = cells[index].row;
        bool first = true;
        if (row_headers) {
            text.append(row_header_text(row));
            first = false;
        }
        // Each field is the cell's text when the cell is selected.
        for (const std::size_t column : columns) {
            if (!first) text.push_back('\t');
            first = false;
            if (is_cell_selected(row, column)) text.append(cell_text(row, column));
        }
        while (index < count && cells[index].row == row) ++index;
        text.append("\r\n");
    }
    return text;
}

bool DataGridView::copy_selection_to_clipboard() {
    Window* const host = window();
    HostServices* const services = host != nullptr ? (*host).host_services() : nullptr;
    if (services == nullptr) return false;
    const std::optional<std::string> content = clipboard_content();
    if (!content) return false;
    const HostServiceStatus status = (*services).write_clipboard_text(*content);
    return status.accepted();
}

// -- appearance and scrolling --------------------------------------------------

void DataGridView::prepare_write(const decltype(column_headers_visible)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(column_headers_visible)&) {
    structure_changed();
}

void DataGridView::prepare_write(const decltype(row_headers_visible)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(row_headers_visible)&) {
    structure_changed();
}

void DataGridView::prepare_write(const decltype(column_headers_height)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 4.0 || incoming > 256.0) {
        throw std::out_of_range("DataGridView column header height must be between 4 and 256");
    }
}

void DataGridView::written(const decltype(column_headers_height)&) {
    if (header_auto_size_writing_) {
        // Arrange's own auto_size write: the pass in progress lays the grid
        // out at this height, so only what it drew and published is stale.
        header_auto_size_writing_ = false;
        invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    } else {
        structure_changed();
    }
    on_column_headers_height_changed();
}

void DataGridView::prepare_write(const decltype(column_headers_height_size_mode)&,
                                 DataGridViewColumnHeadersHeightSizeMode&) {
    require_mutable();
}

void DataGridView::written(const decltype(column_headers_height_size_mode)&) {
    structure_changed();
}

void DataGridView::prepare_write(const decltype(row_headers_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 4.0 || incoming > 1024.0) {
        throw std::out_of_range("DataGridView row header width must be between 4 and 1024");
    }
}

void DataGridView::written(const decltype(row_headers_width)&) {
    structure_changed();
    on_row_headers_width_changed();
}

void DataGridView::prepare_write(const decltype(row_height)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 4.0 || incoming > 256.0) {
        throw std::out_of_range("DataGridView row height must be between 4 and 256");
    }
}

void DataGridView::written(const decltype(row_height)&) {
    structure_changed();
}

double DataGridView::row_height_at(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    const double result = row_heights().height(row.value);
    return result;
}

void DataGridView::set_row_height_at(DataGridViewRowPosition row, double height) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    if (!std::isfinite(height) || height < 4.0 || height > 4096.0) {
        throw std::out_of_range("DataGridView row height must be between 4 and 4096");
    }
    // A height under the minimum row height is raised to it.
    height = std::max(height, minimum_row_height.read());
    const std::string row_id = row_id_text(row);
    const double before = row_height_at(row);
    own_row_heights_.insert_or_assign(row_id, height);
    // Automatic heights override own heights until the mode returns to none.
    if (auto_size_rows_mode.read() != DataGridViewAutoSizeRowsMode::none) return;
    if (before == height) return;
    // The index takes the one change in place, unless it is rebuilt anyway.
    if (!row_heights_stale_ && row_heights_.count() == row_count()) row_heights_.set_height(row.value, height);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const DataGridViewRowEvent changed{DataGridViewRowId{row_id}, row};
    row_height_changed_.emit(changed);
}

void DataGridView::reset_row_height_at(DataGridViewRowPosition row) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    const std::string row_id = row_id_text(row);
    const RowHeightsById::iterator found = own_row_heights_.find(row_id);
    if (found == own_row_heights_.end()) return;
    const double before = (*found).second;
    own_row_heights_.erase(found);
    if (auto_size_rows_mode.read() != DataGridViewAutoSizeRowsMode::none || before == row_height.read()) return;
    if (!row_heights_stale_ && row_heights_.count() == row_count()) row_heights_.set_height(row.value, row_height.read());
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const DataGridViewRowEvent changed{DataGridViewRowId{row_id}, row};
    row_height_changed_.emit(changed);
}

void DataGridView::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("DataGridView font specification is invalid");
    // Code's font wins over a theme's, even when the write changes nothing.
    set_font_from_code(true);
}

void DataGridView::written(const decltype(font)&) {
    if (editor_) (*editor_).font = font.read();
    structure_changed();
    on_font_changed();
}

void DataGridView::prepare_write(const decltype(read_only)&, bool& incoming) {
    require_mutable();
    // An edit in progress ends before the grid becomes read-only.
    if (incoming && !read_only.read()) cancel_edit();
}

void DataGridView::written(const decltype(read_only)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    on_read_only_changed();
}

void DataGridView::set_first_displayed_row(DataGridViewRowPosition row) {
    require_mutable();
    const std::size_t count = row_count();
    const std::size_t last = count == 0U ? 0U : count - 1U;
    const std::size_t next = std::min(row.value, last);
    if (first_row_ == next) return;
    const GridLayout layout = grid_layout();
    const std::size_t former_first = scroll_first_row();
    first_row_ = next;
    scroll_moved(layout, former_first, horizontal_offset_);
}

void DataGridView::set_horizontal_scroll_offset(double offset) {
    require_mutable();
    if (!std::isfinite(offset) || offset < 0.0) {
        throw std::invalid_argument("DataGridView horizontal offset must be finite and not negative");
    }
    if (horizontal_offset_ == offset) return;
    const GridLayout layout = grid_layout();
    const double former_offset = horizontal_offset_;
    horizontal_offset_ = offset;
    scroll_moved(layout, scroll_first_row(), former_offset);
}

double DataGridView::scrolling_left(std::size_t display) const noexcept {
    const ColumnStrip& strip = column_strip();
    const double left = std::max(0.0, strip.start(display) - strip.frozen_width());
    return left;
}

stxlib::Optional<std::size_t> DataGridView::first_displayed_scrolling_column() const {
    const IndexSpan displays = display_span(grid_layout());
    if (displays.first >= displays.end) return stxlib::Optional<std::size_t>::none();
    return stxlib::Optional<std::size_t>::of(shown_order_[displays.first]);
}

double DataGridView::first_displayed_scrolling_column_hidden_width() const {
    const IndexSpan displays = display_span(grid_layout());
    if (displays.first >= displays.end) return 0.0;
    const double hidden = std::max(0.0, horizontal_offset_ - scrolling_left(displays.first));
    return hidden;
}

void DataGridView::set_first_displayed_scrolling_column(std::size_t column) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    const std::size_t display = column < shown_indices_.size() ? shown_indices_[column] : shown_order_.size();
    if (!columns_[column].visible || display >= shown_order_.size() || display < frozen_display_count()) {
        throw std::logic_error("DataGridView first scrolling column must be shown and not frozen");
    }
    set_horizontal_scroll_offset(scrolling_left(display));
}

void DataGridView::on_vertical_scroll(double value) {
    if (synchronizing_scroll_) return;
    const double rounded = std::max(0.0, std::round(value));
    const GridLayout layout = grid_layout();
    const std::size_t former_first = scroll_first_row();
    first_row_ = static_cast<std::size_t>(rounded);
    scroll_moved(layout, former_first, horizontal_offset_);
}

void DataGridView::on_horizontal_scroll(double value) {
    if (synchronizing_scroll_) return;
    const GridLayout layout = grid_layout();
    const double former_offset = horizontal_offset_;
    horizontal_offset_ = std::max(0.0, value);
    scroll_moved(layout, scroll_first_row(), former_offset);
}

bool DataGridView::scroll_copy_allowed() const {
    if (cells_host_ || row_heights_stale_) return false;
    if (auto_size_rows_mode.read() != DataGridViewAutoSizeRowsMode::none) return false;
    for (std::size_t column = 0U; column < columns_.size(); ++column) {
        const DataGridViewAutoSizeColumnMode mode = column_auto_size_mode(column);
        const bool displayed = mode == DataGridViewAutoSizeColumnMode::displayed_cells ||
                               mode == DataGridViewAutoSizeColumnMode::displayed_cells_except_header;
        if (displayed) return false;
    }
    return true;
}

namespace {

// Subscribers still connected to an event.
template <typename... Arguments>
bool has_subscribers(const Event<Arguments...>& event) noexcept {
    const EventStatistics statistics = event.statistics();
    const bool result = statistics.subscriptions_connected > statistics.subscriptions_disconnected;
    return result;
}

} // namespace

bool DataGridView::rows_drawn_by_program() const {
    if (has_subscribers(row_pre_paint_) || has_subscribers(row_post_paint_)) return true;
    if (draw_mode.read() == ItemDrawMode::owner_draw) return true;
    for (const DataGridViewColumn& column : columns_) {
        if (column.owner_draw) return true;
    }
    return false;
}

void DataGridView::invalidate_row(DataGridViewRowPosition row) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    if (cells_host_) {
        redraw_all(Dirty::paint);
        return;
    }
    const GridLayout layout = grid_layout();
    const Rect band = Rect::united(layout.data, layout.row_header_band.empty() ? layout.data : layout.row_header_band);
    const Rect pane = row_pane(row.value < frozen_rows(), band, layout);
    const Rect bounds = row_paint_bounds(row, layout);
    const Rect damaged = Rect::intersection(bounds, pane);
    if (!damaged.empty()) invalidate(damaged);
}

void DataGridView::invalidate_cell(DataGridViewRowPosition row, std::size_t column) {
    require_mutable();
    require_cell(row, column);
    if (cells_host_) {
        redraw_all(Dirty::paint);
        return;
    }
    const GridLayout layout = grid_layout();
    const Rect damaged = Rect::intersection(cell_bounds(row, column, layout), cell_pane(row, column, layout));
    if (!damaged.empty()) invalidate(damaged);
}

void DataGridView::invalidate_column(std::size_t column) {
    require_column(column);
    require_mutable();
    if (cells_host_) {
        redraw_all(Dirty::paint);
        return;
    }
    const GridLayout layout = grid_layout();
    const std::size_t display = column < shown_indices_.size() ? shown_indices_[column] : shown_order_.size();
    const Rect header = layout.column_header_band.empty() ? layout.data : layout.column_header_band;
    const Rect band = Rect::united(layout.data, Rect{layout.data.x, header.y, layout.data.width, header.height});
    const Rect pane = column_pane(display < frozen_display_count(), band, layout);
    const Rect strip{column_left(column, layout), band.y, shown_width(column) * layout.scale, band.height};
    const Rect damaged = Rect::intersection(strip, pane);
    if (!damaged.empty()) invalidate(damaged);
}

void DataGridView::scroll_moved(const GridLayout& layout, std::size_t former_first, double former_offset) {
    if (!scroll_copy_allowed() || layout.data.empty()) {
        ++scroll_statistics_.scroll_repaints;
        // The whole grid moves: a redraw-all, folding the cell region.
        redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
        return;
    }
    invalidate(Dirty::arrange | Dirty::hit_test | Dirty::semantics);
    // Where the next arrange will place them, by its own clamps.
    const std::size_t highest = maximum_first_row(layout);
    const std::size_t first = std::clamp(first_row_, std::min(frozen_rows(), highest), highest);
    const double offset = std::clamp(horizontal_offset_, 0.0, maximum_horizontal_offset(layout));
    const DataGridViewRowHeights& heights = row_heights();
    const double moved_y = (heights.top(former_first) - heights.top(std::max(first, frozen_rows()))) * layout.scale;
    const double scrolled_x = (former_offset - offset) * layout.scale;
    const double moved_x = layout.mirrored ? -scrolled_x : scrolled_x;
    // Rows the program draws are repainted where they now are, never
    // copied: a hook may draw a row differently after the scroll.
    const bool copy = !rows_drawn_by_program();
    bool repainted = false;
    if (moved_y != 0.0) {
        // The scrolling rows, with their row headers and the frozen
        // columns' cells, move up or down; the frozen rows stay.
        const Rect band = Rect::united(layout.data, layout.row_header_band.empty() ? layout.data : layout.row_header_band);
        const Rect pane = row_pane(false, band, layout);
        if (!pane.empty() && !copy) {
            invalidate(pane);
            repainted = true;
        }
        if (!pane.empty() && copy && !scroll_content(pane, Point{0.0, moved_y})) repainted = true;
    }
    if (moved_x != 0.0) {
        // The scrolling columns, with their headers and the frozen rows'
        // cells, move sideways; the frozen columns stay.
        const Rect header = layout.column_header_band.empty() ? layout.data : layout.column_header_band;
        const Rect band = Rect::united(layout.data, Rect{layout.data.x, header.y, layout.data.width, header.height});
        const Rect pane = column_pane(false, band, layout);
        if (!pane.empty() && !copy) {
            invalidate(pane);
            repainted = true;
        }
        if (!pane.empty() && copy && !scroll_content(pane, Point{moved_x, 0.0})) repainted = true;
    }
    if (repainted) {
        ++scroll_statistics_.scroll_repaints;
    } else if (moved_y != 0.0 || moved_x != 0.0) {
        ++scroll_statistics_.scroll_copies;
    }
}

void DataGridView::scroll_into_view(DataGridViewCellAddress cell) {
    const GridLayout layout = grid_layout();
    // Frozen rows and columns are always in view; the others scroll in
    // the viewport the frozen ones leave.
    const std::size_t first = scroll_first_row();
    if (cell.row.value < frozen_rows()) {
        // A frozen row: no vertical scrolling.
    } else if (cell.row.value < first) {
        first_row_ = cell.row.value;
    } else if (cell.row.value < row_count()) {
        first_row_ = std::max(first, first_row_ending_at(cell.row.value, layout));
    }
    // Columns in logical units at text scale 1, in display order, from the
    // frozen columns' end.
    const std::size_t display = cell.column < shown_indices_.size() ? shown_indices_[cell.column] : 0U;
    if (display >= frozen_display_count()) {
        const double frozen_width = frozen_columns_width();
        double left = -frozen_width;
        for (std::size_t position = 0; position < display; ++position) {
            left += shown_width(shown_order_[position]);
        }
        const double right = left + shown_width(cell.column);
        const double viewport = std::max(0.0, layout.data.width / layout.scale - frozen_width);
        if (left < horizontal_offset_) {
            horizontal_offset_ = left;
        } else if (right > horizontal_offset_ + viewport) {
            horizontal_offset_ = std::min(left, right - viewport);
        }
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::arrange(Rect final_bounds) {
    const std::size_t first_before = first_row_;
    const double offset_before = horizontal_offset_;
    arrange_self(final_bounds);
    // The automatic header height first: one text line, independent of the
    // widths, and the data area below it is what the displayed-rows modes
    // measure. Then automatic widths: everything below reads the columns'
    // widths; then automatic heights, which wrapped cells take from the
    // widths.
    apply_column_headers_auto_size();
    apply_auto_sizes();
    apply_row_auto_sizes();
    const GridLayout layout = grid_layout();
    // The first scrolling row stays past the frozen rows and no further
    // than the last full viewport.
    const std::size_t highest = maximum_first_row(layout);
    first_row_ = std::clamp(first_row_, std::min(frozen_rows(), highest), highest);
    // A layout pass ends the change-rate interval of the virtual order
    // (owner decision W-5), and a window-mode order fetches the rows this
    // pass shows, plus its margin, unless it holds them already.
    changes_last_pass_ = changes_this_pass_;
    changes_this_pass_ = 0U;
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        virtual_order_.prepare_window(first_row_, visible_row_end(layout), row_order_thresholds.read().window_margin);
    }
    horizontal_offset_ = std::clamp(horizontal_offset_, 0.0, maximum_horizontal_offset(layout));
    synchronize_scroll_bars(layout);
    if (vertical_bar_) {
        set_child_layout_collapsed(vertical_bar_, layout.vertical_bar.empty());
        set_child_layout(vertical_bar_, layout.vertical_bar);
    }
    if (horizontal_bar_) {
        set_child_layout_collapsed(horizontal_bar_, layout.horizontal_bar.empty());
        set_child_layout(horizontal_bar_, layout.horizontal_bar);
    }
    // The cell region covers the data area (stage S7).
    if (cells_host_) {
        set_child_layout_collapsed(cells_host_, layout.data.empty());
        set_child_layout(cells_host_, layout.data);
    }
    if (editor_) {
        const Parts placed = grid_parts(layout);
        // A text edit scrolled out of view keeps its editor, and focus, at
        // no size until the cell is shown again. The editor follows its row
        // when rows move.
        set_child_layout(editor_, placed.editor.bounds);
    }
    // The rows' edits since the last layout: everything repaints when the
    // shown window or the viewport moved, or the cells size rows or
    // columns (their edits may have resized them).
    const bool viewport_moved = layout.data.x != settled_data_.x || layout.data.y != settled_data_.y ||
                                layout.data.width != settled_data_.width ||
                                layout.data.height != settled_data_.height;
    const bool whole = viewport_moved || first_row_ != first_before || horizontal_offset_ != offset_before ||
                       sized_by_cells();
    settled_data_ = layout.data;
    settle_rows_damage(whole, layout);
}

void DataGridView::synchronize_scroll_bars(const GridLayout& layout) {
    synchronizing_scroll_ = true;
    const std::size_t last_first = maximum_first_row(layout);
    // The bar runs over the scrolling rows: from the first one past the
    // frozen rows.
    const std::size_t lowest = std::min(frozen_rows(), last_first);
    if (vertical_bar_ && last_first > lowest) {
        VScrollBar& bar = *vertical_bar_;
        bar.set_range(static_cast<double>(lowest), static_cast<double>(last_first));
        bar.small_change = 1.0;
        bar.large_change = static_cast<double>(std::max<std::size_t>(1U, fully_visible_rows(layout)));
        bar.value = static_cast<double>(first_row_);
    }
    const double last_offset = maximum_horizontal_offset(layout);
    if (horizontal_bar_ && last_offset > 0.0) {
        HScrollBar& bar = *horizontal_bar_;
        bar.set_range(0.0, last_offset);
        bar.small_change = 16.0;
        bar.large_change = std::max(1.0, layout.data.width / layout.scale - frozen_columns_width());
        bar.value = horizontal_offset_;
    }
    synchronizing_scroll_ = false;
}

// -- selection -----------------------------------------------------------------

bool DataGridView::cell_in_selection(std::string_view row_id, std::size_t column) const {
    const SelectedCells::const_iterator found = selected_cells_.find(row_id);
    if (found == selected_cells_.end()) return false;
    const SelectedColumns& columns = (*found).second;
    const bool result = std::binary_search(columns.begin(), columns.end(), column);
    return result;
}

bool DataGridView::row_in_selection(std::string_view row_id) const {
    const bool result = selected_rows_.find(row_id) != selected_rows_.end();
    return result;
}

std::size_t DataGridView::selected_cell_count() const noexcept {
    std::size_t count = 0U;
    for (const SelectedCells::value_type& entry : selected_cells_) count += entry.second.size();
    return count;
}

void DataGridView::insert_selected_cell(const std::string& row_id, std::size_t column) {
    SelectedCells::iterator found = selected_cells_.find(row_id);
    if (found == selected_cells_.end()) found = selected_cells_.emplace(row_id, SelectedColumns{}).first;
    SelectedColumns& columns = (*found).second;
    const SelectedColumns::iterator place = std::lower_bound(columns.begin(), columns.end(), column);
    if (place != columns.end() && *place == column) return;
    columns.insert(place, column);
    ++selection_revision_;
}

void DataGridView::erase_selected_cell(std::string_view row_id, std::size_t column) {
    const SelectedCells::iterator found = selected_cells_.find(row_id);
    if (found == selected_cells_.end()) return;
    SelectedColumns& columns = (*found).second;
    const SelectedColumns::iterator place = std::lower_bound(columns.begin(), columns.end(), column);
    if (place == columns.end() || *place != column) return;
    columns.erase(place);
    if (columns.empty()) selected_cells_.erase(found);
    ++selection_revision_;
}

void DataGridView::insert_selected_row(const std::string& row_id) {
    if (!selected_rows_.insert(row_id).second) return;
    ++selection_revision_;
}

void DataGridView::erase_selected_row(std::string_view row_id) {
    const RowIdSet::iterator found = selected_rows_.find(row_id);
    if (found == selected_rows_.end()) return;
    selected_rows_.erase(found);
    ++selection_revision_;
}

void DataGridView::clear_selection_state() noexcept {
    if (selected_cells_.empty() && selected_rows_.empty() && selected_columns_.empty()) return;
    selected_cells_.clear();
    selected_rows_.clear();
    selected_columns_.clear();
    ++selection_revision_;
}

bool DataGridView::column_in_selection(std::size_t column) const {
    const bool result = std::binary_search(selected_columns_.begin(), selected_columns_.end(), column);
    return result;
}

void DataGridView::insert_selected_column(std::size_t column) {
    const SelectedColumns::iterator place = std::lower_bound(selected_columns_.begin(), selected_columns_.end(), column);
    if (place != selected_columns_.end() && *place == column) return;
    selected_columns_.insert(place, column);
    ++selection_revision_;
}

void DataGridView::erase_selected_column(std::size_t column) {
    const SelectedColumns::iterator place = std::lower_bound(selected_columns_.begin(), selected_columns_.end(), column);
    if (place == selected_columns_.end() || *place != column) return;
    selected_columns_.erase(place);
    ++selection_revision_;
}

bool DataGridView::header_selects_column() const noexcept {
    const DataGridViewSelectionMode mode = selection_mode.read();
    const bool result =
        mode == DataGridViewSelectionMode::full_column_select || mode == DataGridViewSelectionMode::column_header_select;
    return result;
}

void DataGridView::select_column_from_header(std::size_t column, bool toggle) {
    if (column >= columns_.size()) return;
    const std::uint64_t before = selection_revision_;
    if (toggle && multi_select.read()) {
        if (column_in_selection(column)) erase_selected_column(column);
        else insert_selected_column(column);
    } else {
        clear_selection_state();
        insert_selected_column(column);
    }
    publish_selection(before);
}

void DataGridView::publish_selection(std::uint64_t before) {
    if (selection_revision_ == before) return;
    invalidate(Dirty::paint | Dirty::semantics);
    selection_changed_.emit();
}

// Selects the cells (or rows) of the rectangle the two cells span, as the
// rows are presented now.
void DataGridView::select_range(DataGridViewCellAddress from, DataGridViewCellAddress to) {
    const std::size_t first_row = std::min(from.row.value, to.row.value);
    const std::size_t last_row = std::max(from.row.value, to.row.value);
    const bool full_column = selection_mode.read() == DataGridViewSelectionMode::full_column_select;
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) {
        for (std::size_t position = first_row; position <= last_row; ++position) {
            insert_selected_row(row_id_text(DataGridViewRowPosition{position}));
        }
        return;
    }
    // The columns between the two, as they are shown.
    // A hidden end counts as the shown column after it, clamped to the
    // last shown column; with no column shown nothing is selected.
    if (shown_order_.empty()) return;
    const std::size_t last_shown = shown_order_.size() - 1U;
    const std::size_t from_display = std::min(shown_indices_[from.column], last_shown);
    const std::size_t to_display = std::min(shown_indices_[to.column], last_shown);
    const std::size_t first_display = std::min(from_display, to_display);
    const std::size_t last_display = std::max(from_display, to_display);
    if (full_column) {
        for (std::size_t display = first_display; display <= last_display; ++display) {
            insert_selected_column(shown_order_[display]);
        }
        return;
    }
    for (std::size_t position = first_row; position <= last_row; ++position) {
        const std::string row_id = row_id_text(DataGridViewRowPosition{position});
        for (std::size_t display = first_display; display <= last_display; ++display) {
            insert_selected_cell(row_id, shown_order_[display]);
        }
    }
}

void DataGridView::select_current_alone() {
    clear_selection_state();
    if (!current_) return;
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) {
        insert_selected_row((*current_).row_id);
    } else if (selection_mode.read() == DataGridViewSelectionMode::full_column_select) {
        insert_selected_column((*current_).column);
    } else {
        insert_selected_cell((*current_).row_id, (*current_).column);
    }
}

DataGridViewRowEvent DataGridView::row_event(std::string_view row_id) const {
    DataGridViewRowEvent event{};
    event.row_id.value = std::string(row_id);
    event.row = event_position(row_id);
    return event;
}

void DataGridView::require_settled_current() const {
    if (changing_current_) {
        throw std::logic_error("DataGridView's current cell is not set from its own enter, leave or validating events");
    }
}

bool DataGridView::validate_row_change(const std::optional<CellKey>& target) {
    if (!current_ || synchronizing_position_) return true;
    if (target && (*target).row_id == (*current_).row_id) return true;
    const std::string row_id = (*current_).row_id;
    if (find_row(row_id).absent()) return true;
    const DataGridViewRowEvent leaving = row_event(row_id);
    DataGridViewRowCancelEvent validating{leaving.row_id, leaving.row, false};
    changing_current_ = true;
    row_validating_.emit(validating);
    changing_current_ = false;
    const bool accepted = !validating.cancel && is_alive();
    return accepted;
}

void DataGridView::apply_current(std::optional<CellKey> cell, bool validated) {
    if (current_ == cell) return;
    const std::optional<CellKey> previous = current_;
    current_ = std::move(cell);
    const std::optional<CellKey> now = current_;
    const bool row_changed = !previous || !now || (*previous).row_id != (*now).row_id;
    if (row_changed) {
        // The row the user added is kept once the current cell leaves it,
        // and a new row left without a commit forgets its values.
        row_dirty_ = false;
        if (previous && added_row_ == (*previous).row_id) added_row_.reset();
        if (previous && (*previous).row_id == new_row_.stable_id.value) {
            new_row_.cells.clear();
            new_row_.cells.resize(columns_.size());
        }
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    if (current_ && row_source_ == DataGridViewRowSource::binding_source && data_source_ &&
        !synchronizing_position_) {
        // The new row is no record: the source's position stays where it is.
        const stxlib::Optional<std::size_t> position = find_row((*current_).row_id);
        if (position.present() && *position.value() < data_row_count()) {
            synchronizing_position_ = true;
            static_cast<void>((*data_source_).set_position(static_cast<std::ptrdiff_t>(*position.value())));
            synchronizing_position_ = false;
        }
    }
    // The move's leave and enter events, in Windows Forms' order. A handler
    // that moves the current cell again ends this move's remaining events.
    const bool nested = changing_current_;
    changing_current_ = true;
    if (previous) {
        cell_leave_.emit(cell_event(*previous));
        if (row_changed && is_alive()) row_leave_.emit(row_event((*previous).row_id));
        if (row_changed && validated && is_alive()) row_validated_.emit(row_event((*previous).row_id));
    }
    bool settled = is_alive() && current_ == now;
    if (settled && now && row_changed) {
        row_enter_.emit(row_event((*now).row_id));
        settled = is_alive() && current_ == now;
        if (settled && (*now).row_id == new_row_.stable_id.value && new_row_shown()) {
            fill_default_values();
            settled = is_alive() && current_ == now;
        }
    }
    if (settled && now) {
        cell_enter_.emit(cell_event(*now));
        settled = is_alive() && current_ == now;
    }
    changing_current_ = nested;
    if (!settled) return;
    publish_change(current_cell_changed_, current_cell());
}

bool DataGridView::move_current(DataGridViewCellAddress target, SelectionGesture gesture) {
    if (target.row.value >= row_count() || target.column >= columns_.size()) return false;
    if (!columns_[target.column].visible) return false;
    const CellKey target_key = key_of(target);
    if (editing_ && !(current_ && *current_ == target_key) && !end_edit()) return false;
    if (!is_alive() || !validate_row_change(target_key)) return false;
    const bool validated = current_.has_value() && (*current_).row_id != target_key.row_id && !synchronizing_position_;
    // A commit's or validation's handlers may have moved rows; the target is
    // kept by ID.
    const stxlib::Optional<DataGridViewCellAddress> placed = address_of(target_key);
    if (placed.absent()) return false;
    const DataGridViewCellAddress at = *placed.value();
    const std::uint64_t before = selection_revision_;
    const bool full_row = selection_mode.read() == DataGridViewSelectionMode::full_row_select;
    if (gesture == SelectionGesture::extend && multi_select.read()) {
        // The range runs from the anchor, else the current cell, else the
        // target, at its row's present position.
        DataGridViewCellAddress from = at;
        stxlib::Optional<DataGridViewCellAddress> origin{};
        if (anchor_) origin = address_of(*anchor_);
        if (origin.absent() && current_) origin = address_of(*current_);
        if (origin.present()) from = *origin.value();
        clear_selection_state();
        select_range(from, at);
        anchor_ = key_of(from);
    } else if (gesture == SelectionGesture::toggle && multi_select.read()) {
        if (full_row) {
            if (row_in_selection(target_key.row_id)) erase_selected_row(target_key.row_id);
            else insert_selected_row(target_key.row_id);
        } else if (selection_mode.read() == DataGridViewSelectionMode::full_column_select) {
            if (column_in_selection(target_key.column)) erase_selected_column(target_key.column);
            else insert_selected_column(target_key.column);
        } else {
            if (cell_in_selection(target_key.row_id, target_key.column)) {
                erase_selected_cell(target_key.row_id, target_key.column);
            } else {
                insert_selected_cell(target_key.row_id, target_key.column);
            }
        }
        anchor_ = target_key;
    } else {
        clear_selection_state();
        // A plain move selects what the mode selects: the row, the column
        // (full column selection), else the cell.
        if (full_row) {
            insert_selected_row(target_key.row_id);
        } else if (selection_mode.read() == DataGridViewSelectionMode::full_column_select) {
            insert_selected_column(target_key.column);
        } else {
            insert_selected_cell(target_key.row_id, target_key.column);
        }
        anchor_ = target_key;
    }
    apply_current(target_key, validated);
    if (!is_alive()) return true;
    if (current_ == target_key) scroll_into_view(at);
    publish_selection(before);
    // Edit on enter begins the user's edit of a text cell they move to.
    if (edit_mode.read() == DataGridViewEditMode::edit_on_enter && focused_ && !editing_ && !synchronizing_position_ &&
        current_ == target_key && columns_[target_key.column].kind == DataGridViewColumnKind::text) {
        static_cast<void>(begin_edit(true));
    }
    return true;
}

void DataGridView::prepare_write(const decltype(selection_mode)&, DataGridViewSelectionMode&) {
    require_mutable();
}

void DataGridView::written(const decltype(selection_mode)&) {
    const std::uint64_t before = selection_revision_;
    select_current_alone();
    ++selection_revision_;
    publish_selection(before);
}

void DataGridView::prepare_write(const decltype(multi_select)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(multi_select)&) {
    if (!multi_select.read()) {
        const std::uint64_t before = selection_revision_;
        const std::size_t selected = selected_cell_count() + selected_rows_.size() + selected_columns_.size();
        if (selected > 1U) select_current_alone();
        publish_selection(before);
        if (!is_alive()) return;
    }
    on_multi_select_changed();
}

stxlib::Optional<DataGridViewCellAddress> DataGridView::current_cell() const {
    if (!current_) return stxlib::Optional<DataGridViewCellAddress>::none();
    return address_of(*current_);
}

std::optional<DataGridViewRowId> DataGridView::current_row_id() const {
    if (!current_ || find_row((*current_).row_id).absent()) return std::nullopt;
    const DataGridViewRowId identity{(*current_).row_id};
    return identity;
}

bool DataGridView::set_current_cell(stxlib::Optional<DataGridViewCellAddress> cell) {
    require_mutable();
    require_settled_current();
    if (cell.present()) require_cell((*cell.value()).row, (*cell.value()).column);
    if (cell.present() && !columns_[(*cell.value()).column].visible) {
        throw std::logic_error("DataGridView current cell cannot be in a hidden column");
    }
    if (cell.absent()) {
        if (editing_ && !end_edit()) return false;
        if (!validate_row_change(std::nullopt)) return false;
        const bool validated = current_.has_value();
        anchor_.reset();
        apply_current(std::nullopt, validated);
        return true;
    }
    const bool moved = move_current(*cell.value(), SelectionGesture::replace);
    return moved;
}

bool DataGridView::is_cell_selected(DataGridViewRowPosition row, std::size_t column) const {
    if (row.value >= row_count()) return false;
    const DataGridViewSelectionMode mode = selection_mode.read();
    if (mode == DataGridViewSelectionMode::full_column_select) return column_in_selection(column);
    const std::string row_id = row_id_text(row);
    if (mode == DataGridViewSelectionMode::full_row_select) return row_in_selection(row_id);
    if (mode == DataGridViewSelectionMode::column_header_select && column_in_selection(column)) return true;
    return cell_in_selection(row_id, column);
}

bool DataGridView::is_row_selected(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) return false;
    if (columns_.empty() && selection_mode.read() != DataGridViewSelectionMode::full_row_select) return false;
    // Every column selected selects every row.
    if (!columns_.empty() && selected_columns_.size() == columns_.size()) return true;
    const std::string row_id = row_id_text(row);
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) return row_in_selection(row_id);
    // A row's entry holds unique column indices below the column count.
    const SelectedCells::const_iterator found = selected_cells_.find(row_id);
    const bool result = found != selected_cells_.end() && (*found).second.size() == columns_.size();
    return result;
}

void DataGridView::set_cell_selected(DataGridViewRowPosition row, std::size_t column, bool selected) {
    require_mutable();
    require_cell(row, column);
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) {
        set_row_selected(row, selected);
        return;
    }
    if (selection_mode.read() == DataGridViewSelectionMode::full_column_select) {
        set_column_selected(column, selected);
        return;
    }
    const std::uint64_t before = selection_revision_;
    if (selected && !multi_select.read()) clear_selection_state();
    const std::string row_id = row_id_text(row);
    if (selected) insert_selected_cell(row_id, column);
    else erase_selected_cell(row_id, column);
    publish_selection(before);
}

void DataGridView::set_row_selected(DataGridViewRowPosition row, bool selected) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    if (selection_mode.read() == DataGridViewSelectionMode::full_column_select) {
        throw std::logic_error("DataGridView rows are not selected in full column selection");
    }
    const std::uint64_t before = selection_revision_;
    if (selected && !multi_select.read()) clear_selection_state();
    const std::string row_id = row_id_text(row);
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) {
        if (selected) insert_selected_row(row_id);
        else erase_selected_row(row_id);
    } else if (selected) {
        for (std::size_t column = 0; column < columns_.size(); ++column) insert_selected_cell(row_id, column);
    } else {
        for (std::size_t column = 0; column < columns_.size(); ++column) erase_selected_cell(row_id, column);
    }
    publish_selection(before);
}

bool DataGridView::is_column_selected(std::size_t column) const {
    if (column >= columns_.size()) return false;
    return column_in_selection(column);
}

void DataGridView::set_column_selected(std::size_t column, bool selected) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (!header_selects_column()) {
        throw std::logic_error("DataGridView columns are selected only in full column or column header selection");
    }
    const std::uint64_t before = selection_revision_;
    if (selected && !multi_select.read()) clear_selection_state();
    if (selected) insert_selected_column(column);
    else erase_selected_column(column);
    publish_selection(before);
}

std::vector<std::size_t> DataGridView::selected_columns() const {
    std::vector<std::size_t> result(selected_columns_.begin(), selected_columns_.end());
    return result;
}

std::vector<DataGridViewCellAddress> DataGridView::selected_cells() const {
    std::vector<DataGridViewCellAddress> cells{};
    const DataGridViewSelectionMode mode = selection_mode.read();
    if (mode != DataGridViewSelectionMode::full_row_select) {
        // Selected cells, and every cell of a selected column.
        const std::size_t count = row_count();
        cells.reserve(selected_cell_count() + selected_columns_.size() * count);
        for (const SelectedCells::value_type& entry : selected_cells_) {
            const stxlib::Optional<std::size_t> position = find_row(entry.first);
            if (position.absent()) continue;
            const DataGridViewRowPosition row{*position.value()};
            for (const std::size_t column : entry.second) cells.push_back({row, column});
        }
        for (std::size_t position = 0U; position < count && !selected_columns_.empty(); ++position) {
            const DataGridViewRowPosition row{position};
            for (const std::size_t column : selected_columns_) cells.push_back({row, column});
        }
        std::sort(cells.begin(), cells.end(), CellAddressOrder{});
        const std::vector<DataGridViewCellAddress>::iterator repeated = std::unique(cells.begin(), cells.end());
        cells.erase(repeated, cells.end());
        return cells;
    }
    const std::vector<DataGridViewRowPosition> rows = selected_rows();
    cells.reserve(rows.size() * columns_.size());
    for (const DataGridViewRowPosition row : rows) {
        for (std::size_t column = 0; column < columns_.size(); ++column) cells.push_back({row, column});
    }
    return cells;
}

std::vector<DataGridViewRowPosition> DataGridView::selected_rows() const {
    std::vector<DataGridViewRowPosition> rows{};
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) {
        rows.reserve(selected_rows_.size());
        for (const std::string& row_id : selected_rows_) {
            const stxlib::Optional<std::size_t> position = find_row(row_id);
            if (position.present()) rows.push_back(DataGridViewRowPosition{*position.value()});
        }
    } else if (!columns_.empty() && selected_columns_.size() == columns_.size()) {
        // Every column selected selects every row.
        const std::size_t count = row_count();
        rows.reserve(count);
        for (std::size_t position = 0U; position < count; ++position) rows.push_back(DataGridViewRowPosition{position});
    } else if (!columns_.empty()) {
        for (const SelectedCells::value_type& entry : selected_cells_) {
            if (entry.second.size() != columns_.size()) continue;
            const stxlib::Optional<std::size_t> position = find_row(entry.first);
            if (position.present()) rows.push_back(DataGridViewRowPosition{*position.value()});
        }
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void DataGridView::clear_selection() {
    require_mutable();
    const std::uint64_t before = selection_revision_;
    clear_selection_state();
    publish_selection(before);
}

void DataGridView::select_all() {
    require_mutable();
    if (!multi_select.read()) return;
    const std::uint64_t before = selection_revision_;
    const std::size_t count = row_count();
    if (count == 0U || shown_order_.empty()) return;
    clear_selection_state();
    const DataGridViewCellAddress first{DataGridViewRowPosition{0U}, shown_order_.front()};
    const DataGridViewCellAddress last{DataGridViewRowPosition{count - 1U}, shown_order_.back()};
    select_range(first, last);
    publish_selection(before);
}

// -- editing -------------------------------------------------------------------

bool DataGridView::begin_edit(bool select_all) {
    require_mutable();
    if (editing_) return true;
    const stxlib::Optional<DataGridViewCellAddress> cell = current_cell();
    if (cell.absent()) return false;
    const DataGridViewColumnKind kind = columns_[(*cell.value()).column].kind;
    if (kind == DataGridViewColumnKind::combo_box) return open_choices();
    if (kind != DataGridViewColumnKind::text) return false;
    const bool begun = begin_text_edit(cell_text((*cell.value()).row, (*cell.value()).column), select_all);
    return begun;
}

bool DataGridView::begin_text_edit(std::string initial_text, bool select_all) {
    const stxlib::Optional<DataGridViewCellAddress> placed = current_cell();
    if (placed.absent() || !editor_) return false;
    const DataGridViewCellAddress cell = *placed.value();
    if (cell_read_only(cell.row, cell.column)) return false;
    const CellKey key = *current_;
    DataGridViewCellCancelEvent request{DataGridViewRowId{key.row_id}, cell.row, cell.column, false};
    cell_begin_edit_.emit(request);
    // A handler may cancel, or move the current cell or rows, or end the grid.
    if (request.cancel || !is_alive() || !current_ || !(*current_ == key)) return false;
    const stxlib::Optional<DataGridViewCellAddress> moved = current_cell();
    if (moved.absent()) return false;
    scroll_into_view(*moved.value());
    editing_ = true;
    edit_kind_ = EditKind::text;
    edit_invalid_ = false;
    TextBox& editor = *editor_;
    // Typed text that differs from the cell's makes the edit dirty at once.
    const bool dirty = initial_text != cell_text((*moved.value()).row, (*moved.value()).column);
    writing_editor_ = true;
    editor.text = std::move(initial_text);
    writing_editor_ = false;
    set_child_layout_collapsed(editor_, false);
    const Parts editor_parts = grid_parts(grid_layout());
    set_child_layout(editor_, editor_parts.editor.bounds);
    const DataGridViewEditingControlShowingEvent showing{DataGridViewRowId{key.row_id}, (*moved.value()).row,
                                                         (*moved.value()).column, DataGridViewEditingControlKind::text_box};
    editing_control_showing_.emit(showing);
    // A handler may end the edit, move the current cell or end the grid.
    if (!is_alive() || !editing_ || edit_kind_ != EditKind::text || !current_ || !(*current_ == key)) return false;
    Window* const host = window();
    if (host != nullptr) static_cast<void>((*host).request_focus(editor_));
    if (select_all) {
        editor.select_all();
    } else {
        const Utf8Offset end(editor.text.read().size());
        editor.select(end, end);
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (dirty) set_cell_dirty(true);
    return true;
}

bool DataGridView::edit_trigger_allowed(EditTrigger trigger) const noexcept {
    const DataGridViewEditMode mode = edit_mode.read();
    if (mode == DataGridViewEditMode::edit_programmatically) return false;
    if (trigger == EditTrigger::pointer) return true;
    if (trigger == EditTrigger::entering) return mode == DataGridViewEditMode::edit_on_enter;
    if (trigger == EditTrigger::keystroke) {
        const bool typed = mode == DataGridViewEditMode::edit_on_enter ||
                           mode == DataGridViewEditMode::edit_on_keystroke ||
                           mode == DataGridViewEditMode::edit_on_keystroke_or_f2;
        return typed;
    }
    const bool f2 = mode != DataGridViewEditMode::edit_on_keystroke;
    return f2;
}

void DataGridView::set_cell_dirty(bool dirty) {
    if (cell_dirty_ == dirty) return;
    cell_dirty_ = dirty;
    invalidate(Dirty::paint | Dirty::semantics);
    current_cell_dirty_state_changed_.emit(dirty);
}

void DataGridView::on_editor_text_changed(const std::string&) {
    if (writing_editor_ || !editing_ || edit_kind_ != EditKind::text) return;
    set_cell_dirty(true);
}

void DataGridView::notify_current_cell_dirty(bool dirty) {
    require_mutable();
    if (!editing_) return;
    set_cell_dirty(dirty);
}

bool DataGridView::is_current_row_dirty() const noexcept {
    if (!current_) return false;
    const bool added = added_row_ && *added_row_ == (*current_).row_id;
    const bool result = cell_dirty_ || row_dirty_ || added;
    return result;
}

DataGridView::UserStore DataGridView::store_user_value(DataGridViewCellAddress cell, const BindingValue& value,
                                                      const std::string& formatted) {
    CellKey key = key_of(cell);
    UserStore result{};
    const bool adding = is_new_row(cell.row);
    bool stored = !adding || add_new_row();
    // A virtual row takes the ID the application gave it.
    if (stored && adding && added_row_) key.row_id = *added_row_;
    if (stored) {
        // Adding the row ran the program's handlers; the cell stays on its ID.
        const stxlib::Optional<DataGridViewCellAddress> at = address_of(key);
        stored = at.present() && store_value((*at.value()).row, key.column, value);
    }
    if (stored) {
        if (current_ && (*current_).row_id == key.row_id) row_dirty_ = true;
        result.stored = true;
        return result;
    }
    DataGridViewDataErrorEvent error{DataGridViewRowId{key.row_id}, event_position(key.row_id), key.column,
                                     DataGridViewDataErrorContext::commit, formatted, true};
    data_error_.emit(error);
    result.keep_edit = error.cancel;
    return result;
}

DataGridView::CommitOutcome DataGridView::commit_text(const CellKey& key, DataGridViewCellAddress at,
                                                      const std::string& text) {
    // An edit the user did not change stores nothing, as in Windows Forms:
    // its text is the formatted value, which need not convert back to the
    // value (a number shown in a fixed format, a null value's text).
    if (!cell_dirty_) return CommitOutcome::unchanged;
    DataGridViewCellParsingEvent parsing{};
    parsing.row_id = DataGridViewRowId{key.row_id};
    parsing.row = at.row;
    parsing.column = key.column;
    parsing.formatted_value = text;
    parsing.cell_style = inherited_cell_style(at.row, key.column);
    cell_parsing_.emit(parsing);
    if (!is_alive() || !editing_) return CommitOutcome::discarded;
    // A handler may have moved rows; the edit stays on its row's ID.
    const stxlib::Optional<DataGridViewCellAddress> placed = address_of(key);
    if (placed.absent()) return CommitOutcome::discarded;
    at = *placed.value();
    // The typed text converts to the column's value kind; empty text is an
    // empty value except in a text column. Text equal to the style's null
    // value stores its data source null value.
    const BindingValueKind value_kind = columns_[key.column].value_kind;
    BindingValue value{};
    bool keep_edit = true;
    const DataGridViewCellStyle& style = parsing.cell_style;
    const bool null_text = style.null_value && text == display_text(*style.null_value);
    if (parsing.parsing_applied) {
        value = std::move(parsing.value);
    } else if (null_text) {
        if (style.data_source_null_value) value = *style.data_source_null_value;
    } else if (value_kind == BindingValueKind::text) {
        value = text;
    } else if (!text.empty()) {
        // Text shown under a number format ("1,234.50", "12.5 %") converts
        // as the plain number it shows.
        std::string plain = text;
        if (style.format && is_number_kind(value_kind)) {
            const stxlib::Optional<NumberFormat> format = parse_number_format(*style.format);
            std::optional<std::string> unformatted{};
            if (format.present()) unformatted = plain_number_text(text, (*format.value()));
            if (unformatted) plain = std::move(*unformatted);
        }
        const std::optional<BindingValue> converted = convert_binding_value(BindingValue{plain}, value_kind);
        if (converted) {
            value = *converted;
        } else {
            DataGridViewDataErrorEvent error{DataGridViewRowId{key.row_id}, at.row, key.column,
                                             DataGridViewDataErrorContext::parsing, text, true};
            data_error_.emit(error);
            keep_edit = error.cancel;
            if (!is_alive() || !editing_ || !keep_edit) return CommitOutcome::discarded;
            edit_invalid_ = true;
            values_changed();
            return CommitOutcome::kept_invalid;
        }
    }
    const BindingValue previous = stored_value(at.row, key.column);
    if (previous == value) return CommitOutcome::unchanged;
    const UserStore stored = store_user_value(at, value, text);
    if (stored.stored) return CommitOutcome::stored;
    if (!is_alive() || !editing_ || !stored.keep_edit) return CommitOutcome::discarded;
    edit_invalid_ = true;
    values_changed();
    return CommitOutcome::kept_invalid;
}

bool DataGridView::commit_edit() {
    require_mutable();
    require_not_sorting();
    if (!editing_ || edit_kind_ != EditKind::text || !current_) return true;
    const CellKey key = *current_;
    const stxlib::Optional<DataGridViewCellAddress> placed = address_of(key);
    if (placed.absent()) return false;
    const std::string text((*editor_).text.read());
    const CommitOutcome outcome = commit_text(key, *placed.value(), text);
    if (outcome == CommitOutcome::kept_invalid) return false;
    if (outcome == CommitOutcome::discarded) {
        cancel_edit();
        return false;
    }
    edit_invalid_ = false;
    set_cell_dirty(false);
    // A user's store always lands in a row (the new row is added first).
    if (outcome == CommitOutcome::stored) {
        cell_changed(key);
        cell_value_changed_.emit(cell_event(key));
    } else {
        values_changed();
    }
    return true;
}

bool DataGridView::refresh_edit() {
    require_mutable();
    if (!editing_ || edit_kind_ != EditKind::text || !current_ || !editor_) return false;
    const stxlib::Optional<DataGridViewCellAddress> placed = current_cell();
    if (placed.absent()) return false;
    writing_editor_ = true;
    (*editor_).text = cell_text((*placed.value()).row, (*placed.value()).column);
    writing_editor_ = false;
    (*editor_).select_all();
    edit_invalid_ = false;
    set_cell_dirty(false);
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

bool DataGridView::end_edit() {
    require_mutable();
    require_not_sorting();
    if (!editing_ || !current_) return true;
    if (edit_kind_ == EditKind::choice) {
        close_drop_down();
        return true;
    }
    const CellKey key = *current_;
    const stxlib::Optional<DataGridViewCellAddress> placed = address_of(key);
    if (placed.absent()) {
        // The edited row is gone without its edit being discarded.
        cancel_edit();
        return true;
    }
    const std::string text((*editor_).text.read());
    DataGridViewCellValidatingEvent validating{DataGridViewRowId{key.row_id}, (*placed.value()).row, key.column, text,
                                               false};
    cell_validating_.emit(validating);
    if (!is_alive() || !editing_) return !editing_;
    if (validating.cancel) {
        edit_invalid_ = true;
        values_changed();
        return false;
    }
    // A handler may have moved rows; the edit stays on its row's ID.
    const stxlib::Optional<DataGridViewCellAddress> at = address_of(key);
    if (at.absent()) {
        cancel_edit();
        return true;
    }
    const CommitOutcome outcome = commit_text(key, *at.value(), text);
    if (!is_alive()) return true;
    if (outcome == CommitOutcome::kept_invalid) return false;
    if (outcome == CommitOutcome::discarded) {
        // A data_error handler gave the edit up: the old value stays.
        cancel_edit();
        return true;
    }
    finish_edit();
    const DataGridViewCellEvent ended = cell_event(key);
    if (outcome == CommitOutcome::stored) cell_value_changed_.emit(ended);
    if (!is_alive()) return true;
    cell_validated_.emit(ended);
    if (!is_alive()) return true;
    cell_end_edit_.emit(ended);
    return true;
}

void DataGridView::cancel_edit() {
    if (!editing_) return;
    if (edit_kind_ == EditKind::choice) {
        close_drop_down();
        return;
    }
    const std::optional<CellKey> cell = current_;
    finish_edit();
    const DataGridViewCellEvent ended = cell ? cell_event(*cell) : DataGridViewCellEvent{};
    cell_end_edit_.emit(ended);
}

// Leaves edit mode: the editor hides, and keyboard focus returns to the grid
// when the editor held it.
void DataGridView::finish_edit() {
    ending_edit_ = true;
    Window* const host = window();
    const bool editor_focused = host != nullptr && editor_ && (*host).focused_control() == editor_;
    editing_ = false;
    edit_kind_ = EditKind::none;
    edit_invalid_ = false;
    const bool was_dirty = cell_dirty_;
    cell_dirty_ = false;
    if (editor_focused) static_cast<void>((*host).request_focus(shared_from_this()));
    if (editor_) set_child_layout_collapsed(editor_, true);
    ending_edit_ = false;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (was_dirty) current_cell_dirty_state_changed_.emit(false);
}

void DataGridView::on_editor_focus(bool focused) {
    if (focused || !editing_ || ending_edit_ || edit_kind_ != EditKind::text) return;
    // Focus left the editor for another control: commit, or discard an edit
    // that does not commit, since the user has moved on.
    if (!end_edit()) cancel_edit();
}

bool DataGridView::open_choices() {
    if (!current_ || !choices_ || window() == nullptr) return false;
    const stxlib::Optional<DataGridViewCellAddress> placed = current_cell();
    if (placed.absent()) return false;
    const DataGridViewCellAddress cell = *placed.value();
    const CellKey key = *current_;
    const DataGridViewColumn& column = columns_[cell.column];
    if (column.kind != DataGridViewColumnKind::combo_box || column.items.empty() ||
        cell_read_only(cell.row, cell.column)) {
        return false;
    }
    DataGridViewCellCancelEvent request{DataGridViewRowId{key.row_id}, cell.row, cell.column, false};
    cell_begin_edit_.emit(request);
    if (request.cancel || !is_alive() || !current_ || !(*current_ == key)) return false;
    const stxlib::Optional<DataGridViewCellAddress> moved = current_cell();
    if (moved.absent()) return false;
    const DataGridViewCellAddress at = *moved.value();
    scroll_into_view(at);
    const std::string prefix = std::string(stable_id().value()) + ".choice.";
    const std::string current_text = cell_text(at.row, at.column);
    const std::size_t count = columns_[at.column].items.size();
    choice_commands_.reserve(count);
    std::vector<MenuItemSpec> menu{};
    menu.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string& item = columns_[at.column].items[index];
        std::string identity = prefix + std::to_string(index);
        if (index == choice_commands_.size()) {
            choice_commands_.push_back(std::make_shared<Command>(identity, item));
        }
        Command& command = *choice_commands_[index];
        command.set_text(item);
        command.set_checked(item == current_text);
        menu.emplace_back(std::move(identity), MenuItemKind::radio, choice_commands_[index], item);
    }
    (*choices_).set_items(std::move(menu));
    const GridLayout layout = grid_layout();
    const Rect bounds = cell_bounds(at.row, at.column, layout);
    const Rect absolute = absolute_bounds();
    editing_ = true;
    edit_kind_ = EditKind::choice;
    edit_invalid_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    const DataGridViewEditingControlShowingEvent showing{DataGridViewRowId{key.row_id}, at.row, at.column,
                                                         DataGridViewEditingControlKind::choice_list};
    editing_control_showing_.emit(showing);
    if (!is_alive() || !editing_ || edit_kind_ != EditKind::choice || !current_ || !(*current_ == key)) return false;
    (*choices_).preferred_width = std::max(bounds.width, 120.0);
    (*choices_).show(shared_from_this(), {absolute.x + bounds.x, absolute.y + bounds.y + bounds.height},
                     MenuOpenMode::keyboard);
    if (!(*choices_).is_open()) {
        editing_ = false;
        edit_kind_ = EditKind::none;
        const DataGridViewCellEvent ended = cell_event(key);
        cell_end_edit_.emit(ended);
        return false;
    }
    return true;
}

void DataGridView::on_choice_invoked(const MenuItemInvocation& invocation) {
    if (edit_kind_ != EditKind::choice || !current_) return;
    const stxlib::Optional<DataGridViewCellAddress> placed = current_cell();
    if (placed.absent()) return;
    const DataGridViewCellAddress cell = *placed.value();
    const CellKey key = *current_;
    const std::string prefix = std::string(stable_id().value()) + ".choice.";
    if (invocation.item_id.compare(0U, prefix.size(), prefix) != 0) return;
    const std::vector<std::string>& items = columns_[cell.column].items;
    stxlib::Optional<std::size_t> chosen{};
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (invocation.item_id == prefix + std::to_string(index)) {
            chosen.set(index);
            break;
        }
    }
    if (chosen.absent()) return;
    const BindingValue value{items[*chosen.value()]};
    const BindingValue previous = stored_value(cell.row, cell.column);
    if (previous == value || !store_user_value(cell, value, items[*chosen.value()]).stored) return;
    cell_changed(key);
    const DataGridViewCellEvent changed = cell_event(key);
    cell_value_changed_.emit(changed);
}

void DataGridView::on_choices_open_changed(bool open) {
    if (open || edit_kind_ != EditKind::choice) return;
    const std::optional<CellKey> cell = current_;
    editing_ = false;
    edit_kind_ = EditKind::none;
    invalidate(Dirty::paint | Dirty::semantics);
    const DataGridViewCellEvent ended = cell ? cell_event(*cell) : DataGridViewCellEvent{};
    cell_end_edit_.emit(ended);
}

bool DataGridView::drop_down_open() const noexcept {
    const bool result = choices_ && (*choices_).is_open();
    return result;
}

void DataGridView::close_drop_down() noexcept {
    if (choices_) (*choices_).close();
}

stxlib::Optional<bool> DataGridView::check_state(DataGridViewRowPosition row, std::size_t column) const {
    const BindingValue value = stored_value(row, column);
    if (columns_[column].three_state && std::holds_alternative<std::monostate>(value)) return stxlib::Optional<bool>::none();
    const bool checked = binding_value_to_bool(value).value_or(false);
    return stxlib::Optional<bool>::of(checked);
}

bool DataGridView::shows_drop_down(DataGridViewRowPosition row, std::size_t column) const {
    const DataGridViewColumn& definition = columns_[column];
    if (definition.kind != DataGridViewColumnKind::combo_box) return false;
    if (definition.display_style_for_current_cell_only && !is_current_cell(row, column)) return false;
    const bool shown = definition.display_style != DataGridViewComboBoxDisplayStyle::nothing;
    return shown;
}

bool DataGridView::toggle_check(DataGridViewCellAddress cell) {
    if (columns_[cell.column].kind != DataGridViewColumnKind::check_box ||
        cell_read_only(cell.row, cell.column)) {
        return false;
    }
    // Two states toggle; three run unchecked, checked, indeterminate (an
    // empty value), as Windows Forms' ThreeState cells do.
    const stxlib::Optional<bool> state = check_state(cell.row, cell.column);
    BindingValue value{false};
    std::string shown = "false";
    if (state.absent()) {
        value = BindingValue{false};
    } else if (!*state.value()) {
        value = BindingValue{true};
        shown = "true";
    } else if (columns_[cell.column].three_state) {
        value = BindingValue{};
        shown = "";
    }
    const CellKey key = key_of(cell);
    if (!store_user_value(cell, value, shown).stored) return false;
    // A toggle of the new row reports the row it became.
    const DataGridViewCellEvent changed = cell_event(key);
    cell_changed(key);
    cell_value_changed_.emit(changed);
    cell_content_click_.emit(changed);
    return true;
}

void DataGridView::click_content(DataGridViewCellAddress cell) {
    const DataGridViewColumn& column = columns_[cell.column];
    const DataGridViewColumnKind kind = column.kind;
    if (kind != DataGridViewColumnKind::button && kind != DataGridViewColumnKind::image &&
        kind != DataGridViewColumnKind::link) {
        return;
    }
    const DataGridViewCellEvent clicked = cell_event(key_of(cell));
    // A followed link is visited before handlers run, as LinkLabel's is, so
    // a handler sees the state the click left.
    if (kind == DataGridViewColumnKind::link && column.track_visited_state) {
        set_link_visited(cell.row, cell.column, true);
    }
    cell_content_click_.emit(clicked);
}

// The current cell's own action: a check box toggles, a button clicks, a
// combo box opens its choices and a text cell begins an edit.
bool DataGridView::activate_current() {
    const stxlib::Optional<DataGridViewCellAddress> current = current_cell();
    if (current.absent()) return false;
    const DataGridViewCellAddress cell = *current.value();
    const DataGridViewColumnKind kind = columns_[cell.column].kind;
    if (kind == DataGridViewColumnKind::check_box) return toggle_check(cell);
    if (kind == DataGridViewColumnKind::button || kind == DataGridViewColumnKind::link) {
        click_content(cell);
        return true;
    }
    const bool begun = begin_edit(true);
    return begun;
}

// -- adding and deleting rows ----------------------------------------------------

stxlib::Optional<DataGridViewRowPosition> DataGridView::new_row_index() const noexcept {
    if (!new_row_shown()) return stxlib::Optional<DataGridViewRowPosition>::none();
    const DataGridViewRowPosition position{data_row_count()};
    return stxlib::Optional<DataGridViewRowPosition>::of(position);
}

bool DataGridView::read_view(const decltype(allow_user_to_add_rows)&) const {
    return allow_add_rows_;
}

void DataGridView::write_view(const decltype(allow_user_to_add_rows)&, bool allow) {
    require_mutable();
    if (allow_add_rows_ == allow) return;
    const stxlib::Optional<std::size_t> former = current_position();
    if (!allow && editing_ && current_ && (*current_).row_id == new_row_.stable_id.value) cancel_edit();
    allow_add_rows_ = allow;
    if (allow) renew_new_row_id(false);
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    // Hiding the new row moves a current cell on it to the last row.
    drop_missing_rows(former);
    rows_changed();
}

void DataGridView::rekey_row(const std::string& from, const std::string& to) {
    if (current_ && (*current_).row_id == from) (*current_).row_id = to;
    if (anchor_ && (*anchor_).row_id == from) (*anchor_).row_id = to;
    if (pressed_content_ && (*pressed_content_).row_id == from) (*pressed_content_).row_id = to;
    if (pressed_cell_ && (*pressed_cell_).row_id == from) (*pressed_cell_).row_id = to;
    const RowIdSet::iterator row = selected_rows_.find(from);
    if (row != selected_rows_.end()) {
        selected_rows_.erase(row);
        selected_rows_.insert(to);
    }
    const SelectedCells::iterator cells = selected_cells_.find(from);
    if (cells != selected_cells_.end()) {
        SelectedColumns columns = std::move((*cells).second);
        selected_cells_.erase(cells);
        selected_cells_.emplace(to, std::move(columns));
    }
    // The new row's styles and height follow its ID too.
    const RowHeightsById::iterator height = own_row_heights_.find(from);
    if (height != own_row_heights_.end()) {
        const double moved = (*height).second;
        own_row_heights_.erase(height);
        own_row_heights_.insert_or_assign(to, moved);
    }
    const CellStyleRows::iterator styles = cell_styles_.find(from);
    if (styles != cell_styles_.end()) {
        CellStyles moved = std::move((*styles).second);
        cell_styles_.erase(styles);
        cell_styles_.insert_or_assign(to, std::move(moved));
    }
    const RowStyles::iterator row_style = row_styles_.find(from);
    if (row_style != row_styles_.end()) {
        DataGridViewCellStyle moved = std::move((*row_style).second);
        row_styles_.erase(row_style);
        row_styles_.insert_or_assign(to, std::move(moved));
    }
}

void DataGridView::renew_new_row_id(bool force) {
    if (!force && !new_row_.stable_id.value.empty()) return;
    const std::string former = new_row_.stable_id.value;
    // generated_row_id skips the new row's present ID, which a row may hold.
    std::string next = generated_row_id();
    new_row_.stable_id.value = next;
    if (!former.empty()) rekey_row(former, next);
}

void DataGridView::fill_default_values() {
    DataGridViewRowValuesEvent request{};
    request.row_id = new_row_.stable_id;
    request.row = DataGridViewRowPosition{data_row_count()};
    request.values.resize(columns_.size());
    default_values_needed_.emit(request);
    if (!is_alive()) return;
    // A handler may give fewer or more values than columns.
    request.values.resize(columns_.size());
    new_row_.cells = std::move(request.values);
    values_changed();
}

bool DataGridView::add_new_row() {
    if (!new_row_shown()) return false;
    std::string row_id = new_row_.stable_id.value;
    std::vector<BindingValue> values = std::move(new_row_.cells);
    values.resize(columns_.size());
    const DataGridViewRowPosition position{data_row_count()};
    // Whether this function raises the added row's rows_added (unbound and
    // edit-mode virtual rows).
    bool raised_added = false;
    if (row_source_ == DataGridViewRowSource::binding_source) {
        // The row takes the new row's ID and its values, field by field, in
        // the source's column order. The built-in table grows a field it
        // has not got first, as a record took any field.
        const std::shared_ptr<BindingSource> source = data_source_;
        const std::shared_ptr<BindingTable> built_in = (*source).table();
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            const std::string& field = columns_[column].data_property_name;
            if (field.empty() || binding_value_kind(values[column]) == BindingValueKind::null) continue;
            if (built_in && (*source).column_of(field).absent()) static_cast<void>((*built_in).find_or_add_column(field));
        }
        if (!is_alive() || !(*source).is_alive()) return false;
        BindingRow added{};
        added.stable_id = row_id;
        added.values.resize((*source).schema().size());
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            const std::string& field = columns_[column].data_property_name;
            if (field.empty() || binding_value_kind(values[column]) == BindingValueKind::null) continue;
            const stxlib::Optional<std::size_t> bound = (*source).column_of(field);
            if (bound.present()) added.values[(*bound.value())] = values[column];
        }
        adding_row_ = true;
        synchronizing_position_ = true;
        static_cast<void>((*source).add(std::move(added)));
        synchronizing_position_ = false;
        adding_row_ = false;
        if (!is_alive()) return false;
    } else if (row_source_ == DataGridViewRowSource::virtual_rows) {
        // The application adds the row and names it; the new row's state
        // (current cell, selection, styles, height) moves to that ID.
        const std::shared_ptr<DataGridViewCellValueProvider> provider = provider_;
        const std::optional<DataGridViewRowId> given = (*provider).add_row(std::span<const BindingValue>(values));
        if (!is_alive()) return false;
        if (!given) {
            new_row_.cells = std::move(values);
            return false;
        }
        const bool present = virtual_order_.mode() == DataGridViewRowOrderMode::edit &&
                             virtual_order_.contains((*given).value);
        if ((*given).value.empty() || !validate_utf8((*given).value).valid() || present ||
            (*given).value == row_id) {
            new_row_.cells = std::move(values);
            throw std::invalid_argument("DataGridView add_row must return a new, non-empty, valid UTF-8 row ID");
        }
        rekey_row(row_id, (*given).value);
        row_id = (*given).value;
        if (virtual_order_.mode() == DataGridViewRowOrderMode::edit) {
            const std::optional<std::string> top = held_top_row();
            const std::vector<DataGridViewRowId> inserted{*given};
            virtual_order_.insert(position.value, inserted);
            after_reported_change(top, 1U);
            raised_added = true;
        }
    } else {
        DataGridViewRow row{};
        row.stable_id.value = row_id;
        row.cells = std::move(values);
        // The added row keeps the new row's ID, and with it the current
        // cell and the selection; the new row takes a fresh ID below, not
        // the insertion's renewal, which would move them.
        new_row_.stable_id.value.clear();
        try {
            // rows_added is raised below, once the new row is renewed.
            const RestoreFlag restore{&quiet_row_edits_, quiet_row_edits_};
            quiet_row_edits_ = true;
            rows.append(std::move(row));
        } catch (...) {
            new_row_.stable_id.value = row_id;
            throw;
        }
        raised_added = true;
    }
    // A new row appears after the row the user added, which keeps the ID the
    // current cell and the selection hold.
    new_row_.stable_id.value = generated_row_id();
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    added_row_ = row_id;
    rows_changed();
    // A bound row's rows_added came with the source's notification.
    if (raised_added && !raise_rows_added(position.value, 1U)) return false;
    const DataGridViewRowEvent added{DataGridViewRowId{row_id}, position};
    user_added_row_.emit(added);
    const bool present = is_alive() && find_row(row_id).present();
    return present;
}

void DataGridView::remove_row_by_id(const std::string& row_id) {
    const stxlib::Optional<std::size_t> position = find_row(row_id);
    if (position.absent() || is_new_row(DataGridViewRowPosition{*position.value()})) return;
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (current_ && (*current_).row_id == row_id && editing_) cancel_edit();
        static_cast<void>((*data_source_).remove_at(*position.value()));
        return;
    }
    if (row_source_ != DataGridViewRowSource::unbound) {
        throw std::logic_error("DataGridView rows are removed only from unbound rows");
    }
    rows.erase(*position.value(), 1U);
}

bool DataGridView::cancel_row_edit() {
    require_mutable();
    if (!added_row_ || !current_ || (*current_).row_id != *added_row_) return false;
    const std::string row_id = *added_row_;
    cancel_edit();
    const DataGridViewRowEvent cancelled = row_event(row_id);
    added_row_.reset();
    row_dirty_ = false;
    remove_row_by_id(row_id);
    if (!is_alive()) return true;
    row_edit_cancelled_.emit(cancelled);
    return true;
}

bool DataGridView::delete_selected_rows() {
    if (!allow_user_to_delete_rows.read() || read_only.read() || editing_) return false;
    if (row_source_ == DataGridViewRowSource::virtual_rows) return false;
    if (row_source_ == DataGridViewRowSource::binding_source &&
        (!data_source_ || !(*data_source_).is_alive() || !(*data_source_).allow_remove())) {
        return false;
    }
    const std::vector<DataGridViewRowPosition> selected = selected_rows();
    std::vector<std::string> doomed{};
    doomed.reserve(selected.size());
    for (const DataGridViewRowPosition row : selected) {
        if (!is_new_row(row)) doomed.push_back(row_id_text(row));
    }
    if (doomed.empty()) return false;
    for (const std::string& row_id : doomed) {
        if (find_row(row_id).absent()) continue;
        const DataGridViewRowEvent target = row_event(row_id);
        DataGridViewRowCancelEvent deleting{target.row_id, target.row, false};
        user_deleting_row_.emit(deleting);
        if (!is_alive()) return true;
        if (deleting.cancel || find_row(row_id).absent()) continue;
        const DataGridViewRowEvent deleted = row_event(row_id);
        if (added_row_ == row_id) added_row_.reset();
        remove_row_by_id(row_id);
        if (!is_alive()) return true;
        user_deleted_row_.emit(deleted);
        if (!is_alive()) return true;
    }
    return true;
}

// -- error text ----------------------------------------------------------------

std::string DataGridView::cell_error_text(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    std::string held = held_cell_error_text(row, column);
    if (row_source_ == DataGridViewRowSource::unbound) return held;
    DataGridViewCellErrorTextNeededEvent needed{DataGridViewRowId{row_id_text(row)}, row, column, held};
    cell_error_text_needed_.emit(needed);
    if (!validate_utf8(needed.error_text).valid()) return held;
    return needed.error_text;
}

std::string DataGridView::cell_tool_tip_text(DataGridViewRowPosition row, std::size_t column) const {
    if (!show_cell_tool_tips.read()) return {};
    DataGridViewCellToolTipTextNeededEvent needed{DataGridViewRowId{row_id_text(row)}, row, column, std::string{}};
    cell_tool_tip_text_needed_.emit(needed);
    if (!validate_utf8(needed.tool_tip_text).valid()) return {};
    return needed.tool_tip_text;
}

std::string DataGridView::held_cell_error_text(DataGridViewRowPosition row, std::size_t column) const {
    const std::string row_id = row_id_text(row);
    const CellErrorRows::const_iterator found = cell_errors_.find(row_id);
    if (found != cell_errors_.end()) {
        const CellErrors::const_iterator text = (*found).second.find(column);
        if (text != (*found).second.end()) return (*text).second;
    }
    if (row_source_ != DataGridViewRowSource::binding_source || is_new_row(row)) return {};
    const std::string& field = columns_[column].data_property_name;
    if (field.empty()) return {};
    const stxlib::Optional<std::size_t> bound = (*data_source_).column_of(field);
    if (bound.absent()) return {};
    return (*data_source_).error(row.value, (*bound.value()));
}

void DataGridView::set_cell_error_text(DataGridViewRowPosition row, std::size_t column, std::string text) {
    require_mutable();
    require_cell(row, column);
    if (!validate_utf8(text).valid()) throw std::invalid_argument("DataGridView error text requires valid UTF-8");
    if (held_cell_error_text(row, column) == text) return;
    const std::string row_id = row_id_text(row);
    if (text.empty()) {
        const CellErrorRows::iterator found = cell_errors_.find(row_id);
        if (found != cell_errors_.end()) {
            (*found).second.erase(column);
            if ((*found).second.empty()) cell_errors_.erase(found);
        }
    } else {
        cell_errors_[row_id].insert_or_assign(column, std::move(text));
    }
    values_changed();
    cell_error_text_changed_.emit(cell_event(CellKey{row_id, column}));
}

std::string DataGridView::row_error_text(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    std::string held = held_row_error_text(row);
    if (row_source_ == DataGridViewRowSource::unbound) return held;
    DataGridViewRowErrorTextNeededEvent needed{DataGridViewRowId{row_id_text(row)}, row, held};
    row_error_text_needed_.emit(needed);
    if (!validate_utf8(needed.error_text).valid()) return held;
    return needed.error_text;
}

std::string DataGridView::held_row_error_text(DataGridViewRowPosition row) const {
    const std::string row_id = row_id_text(row);
    const RowErrors::const_iterator found = row_errors_.find(row_id);
    if (found != row_errors_.end()) return (*found).second;
    if (row_source_ != DataGridViewRowSource::binding_source || is_new_row(row)) return {};
    return (*data_source_).error(row.value, binding_whole_row);
}

void DataGridView::set_row_error_text(DataGridViewRowPosition row, std::string text) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    if (!validate_utf8(text).valid()) throw std::invalid_argument("DataGridView error text requires valid UTF-8");
    if (held_row_error_text(row) == text) return;
    const std::string row_id = row_id_text(row);
    if (text.empty()) row_errors_.erase(row_id);
    else row_errors_.insert_or_assign(row_id, std::move(text));
    invalidate(Dirty::paint | Dirty::semantics);
    row_error_text_changed_.emit(row_event(row_id));
}

// -- cell styles ---------------------------------------------------------------

void DataGridView::validate_cell_style(const DataGridViewCellStyle& style) {
    const DataGridViewContentAlignment* const alignment = style.alignment.value();
    if (alignment != nullptr && !valid_alignment(*alignment)) {
        throw std::invalid_argument("DataGridView cell style alignment is outside the enumeration");
    }
    const Insets* const authored_padding = style.padding.value();
    if (authored_padding != nullptr) {
        const Insets& padding = *authored_padding;
        const bool finite = std::isfinite(padding.left) && std::isfinite(padding.top) &&
                            std::isfinite(padding.right) && std::isfinite(padding.bottom);
        if (!finite || padding.left < 0.0 || padding.top < 0.0 || padding.right < 0.0 || padding.bottom < 0.0) {
            throw std::invalid_argument("DataGridView cell style padding must be finite and not negative");
        }
    }
    if (style.format && !(*style.format).empty() && parse_number_format(*style.format).absent()) {
        throw std::invalid_argument(
            "DataGridView cell style formats are the standard numeric formats F, N, P, C, E and G "
            "with a precision of 0 to 99");
    }
    if (!valid_text_value(style.null_value) || !valid_text_value(style.data_source_null_value)) {
        throw std::invalid_argument("DataGridView cell style text values require valid UTF-8");
    }
}

void DataGridView::require_column(std::size_t column) const {
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
}

void DataGridView::prepare_write(const decltype(default_cell_style)&, DataGridViewCellStyle& incoming) {
    require_mutable();
    validate_cell_style(incoming);
}

void DataGridView::written(const decltype(default_cell_style)&) {
    structure_changed();
    on_default_cell_style_changed();
}

void DataGridView::prepare_write(const decltype(rows_default_cell_style)&, DataGridViewCellStyle& incoming) {
    require_mutable();
    validate_cell_style(incoming);
}

void DataGridView::written(const decltype(rows_default_cell_style)&) {
    structure_changed();
    on_rows_default_cell_style_changed();
}

void DataGridView::prepare_write(const decltype(alternating_rows_default_cell_style)&, DataGridViewCellStyle& incoming) {
    require_mutable();
    validate_cell_style(incoming);
}

void DataGridView::written(const decltype(alternating_rows_default_cell_style)&) {
    structure_changed();
    on_alternating_rows_default_cell_style_changed();
}

void DataGridView::prepare_write(const decltype(column_headers_default_cell_style)&, DataGridViewCellStyle& incoming) {
    require_mutable();
    validate_cell_style(incoming);
}

void DataGridView::written(const decltype(column_headers_default_cell_style)&) {
    structure_changed();
    on_column_headers_default_cell_style_changed();
}

void DataGridView::prepare_write(const decltype(row_headers_default_cell_style)&, DataGridViewCellStyle& incoming) {
    require_mutable();
    validate_cell_style(incoming);
}

void DataGridView::written(const decltype(row_headers_default_cell_style)&) {
    structure_changed();
    on_row_headers_default_cell_style_changed();
}

void DataGridView::set_column_default_cell_style(std::size_t column, DataGridViewCellStyle style) {
    require_mutable();
    require_column(column);
    validate_cell_style(style);
    if (columns_[column].default_cell_style == style) return;
    columns_[column].default_cell_style = std::move(style);
    structure_changed();
}

void DataGridView::set_column_header_cell_style(std::size_t column, DataGridViewCellStyle style) {
    require_mutable();
    require_column(column);
    validate_cell_style(style);
    if (columns_[column].header_cell_style == style) return;
    columns_[column].header_cell_style = std::move(style);
    structure_changed();
}

DataGridViewCellStyle DataGridView::row_default_cell_style(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    const RowStyles::const_iterator found = row_styles_.find(row_id_text(row));
    if (found == row_styles_.end()) return {};
    return (*found).second;
}

void DataGridView::set_row_default_cell_style(DataGridViewRowPosition row, DataGridViewCellStyle style) {
    require_mutable();
    if (row.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    validate_cell_style(style);
    const std::string row_id = row_id_text(row);
    const DataGridViewCellStyle empty{};
    if (style == empty) {
        if (row_styles_.erase(row_id) == 0U) return;
    } else {
        row_styles_.insert_or_assign(row_id, std::move(style));
    }
    structure_changed();
}

DataGridViewCellStyle DataGridView::cell_style(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    const CellStyleRows::const_iterator found = cell_styles_.find(row_id_text(row));
    if (found == cell_styles_.end()) return {};
    const CellStyles::const_iterator cell = (*found).second.find(column);
    if (cell == (*found).second.end()) return {};
    return (*cell).second;
}

void DataGridView::set_cell_style(DataGridViewRowPosition row, std::size_t column, DataGridViewCellStyle style) {
    require_mutable();
    require_cell(row, column);
    validate_cell_style(style);
    const std::string row_id = row_id_text(row);
    const DataGridViewCellStyle empty{};
    if (style == empty) {
        const CellStyleRows::iterator found = cell_styles_.find(row_id);
        if (found == cell_styles_.end() || (*found).second.erase(column) == 0U) return;
        if ((*found).second.empty()) cell_styles_.erase(found);
    } else {
        cell_styles_[row_id].insert_or_assign(column, std::move(style));
    }
    structure_changed();
}

DataGridViewCellStyle DataGridView::inherited_cell_style(DataGridViewRowPosition row, std::size_t column) const {
    require_cell(row, column);
    DataGridViewCellStyle style{};
    const std::string row_id = row_id_text(row);
    const CellStyleRows::const_iterator cells = cell_styles_.find(row_id);
    if (cells != cell_styles_.end()) {
        const CellStyles::const_iterator cell = (*cells).second.find(column);
        if (cell != (*cells).second.end()) inherit_unset(style, (*cell).second);
    }
    const RowStyles::const_iterator own = row_styles_.find(row_id);
    if (own != row_styles_.end()) inherit_unset(style, (*own).second);
    // Odd positions (the second row, the fourth, ...) alternate, as in
    // Windows Forms.
    if (row.value % 2U == 1U) inherit_unset(style, alternating_rows_default_cell_style.read());
    inherit_unset(style, rows_default_cell_style.read());
    inherit_unset(style, columns_[column].default_cell_style);
    inherit_unset(style, default_cell_style.read());
    return style;
}

DataGridViewCellStyle DataGridView::inherited_column_header_style(std::size_t column) const {
    require_column(column);
    DataGridViewCellStyle style = columns_[column].header_cell_style;
    inherit_unset(style, column_headers_default_cell_style.read());
    inherit_unset(style, default_cell_style.read());
    return style;
}

DataGridViewCellStyle DataGridView::inherited_row_header_style() const {
    DataGridViewCellStyle style = row_headers_default_cell_style.read();
    inherit_unset(style, default_cell_style.read());
    return style;
}

// -- borders and owner drawing -------------------------------------------------

void DataGridView::prepare_write(const decltype(border_style)&, BorderStyle& incoming) {
    require_mutable();
    if (incoming != BorderStyle::none && incoming != BorderStyle::line && incoming != BorderStyle::sunken) {
        throw std::invalid_argument("DataGridView border style must be none, line or sunken");
    }
}

void DataGridView::written(const decltype(border_style)&) {
    structure_changed();
    on_border_style_changed();
}

void DataGridView::prepare_write(const decltype(cell_border_style)&, DataGridViewCellBorderStyle& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewCellBorderStyle::sunken_horizontal)) {
        throw std::invalid_argument("DataGridView cell border style is outside the enumeration");
    }
}

void DataGridView::written(const decltype(cell_border_style)&) {
    structure_changed();
    on_cell_border_style_changed();
}

void DataGridView::prepare_write(const decltype(column_headers_border_style)&, DataGridViewHeaderBorderStyle& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewHeaderBorderStyle::none)) {
        throw std::invalid_argument("DataGridView header border style is outside the enumeration");
    }
}

void DataGridView::written(const decltype(column_headers_border_style)&) {
    structure_changed();
    on_column_headers_border_style_changed();
}

void DataGridView::prepare_write(const decltype(row_headers_border_style)&, DataGridViewHeaderBorderStyle& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewHeaderBorderStyle::none)) {
        throw std::invalid_argument("DataGridView header border style is outside the enumeration");
    }
}

void DataGridView::written(const decltype(row_headers_border_style)&) {
    structure_changed();
    on_row_headers_border_style_changed();
}

// -- input ---------------------------------------------------------------------

void DataGridView::handle_pointer(PointerEvent& event) {
    const Control::Ptr lifetime = shared_from_this();
    const Point local = point_from_window(event.position);
    if (!raise_cell_mouse(event, local)) return;
    const GridLayout layout = grid_layout();
    if (resizing_column_.present() && !has_pointer_capture()) resizing_column_.reset();
    if (resizing_row_.present() && !has_pointer_capture() && event.action != PointerAction::up) resizing_row_.reset();
    if (resizing_headers_ && !has_pointer_capture() && event.action != PointerAction::up) resizing_headers_ = false;
    if (resizing_headers_) {
        if (event.action == PointerAction::move) {
            const double delta = (local.y - resize_start_y_) / layout.scale;
            column_headers_height = std::clamp(resize_start_height_ + delta, 4.0, 256.0);
        } else if (event.action == PointerAction::up) {
            resizing_headers_ = false;
            Window* const host = window();
            if (host != nullptr && has_pointer_capture()) (*host).release_pointer();
            invalidate(Dirty::paint | Dirty::semantics);
        }
        event.handled = true;
        return;
    }
    if (resizing_row_.present()) {
        if (event.action == PointerAction::move && *resizing_row_.value() < row_count()) {
            const double delta = (local.y - resize_start_y_) / layout.scale;
            const double height = std::clamp(resize_start_height_ + delta, minimum_row_height.read(), 4096.0);
            set_row_height_at(DataGridViewRowPosition{*resizing_row_.value()}, height);
        } else if (event.action == PointerAction::up) {
            resizing_row_.reset();
            Window* const host = window();
            if (host != nullptr && has_pointer_capture()) (*host).release_pointer();
            invalidate(Dirty::paint | Dirty::semantics);
        }
        event.handled = true;
        return;
    }
    // A capture lost mid-drag ends it; the window may release the capture
    // before the release itself arrives, which still ends it normally.
    if (ordering_ && !has_pointer_capture() && event.action != PointerAction::up) {
        end_column_order();
        pressed_column_.reset();
    }
    if (resizing_column_.present()) {
        if (event.action == PointerAction::move) {
            const DataGridViewColumn& column = columns_[*resizing_column_.value()];
            // Right to left the divider is at the column's left edge, so
            // dragging it left widens the column.
            const double moved = layout.mirrored ? resize_start_x_ - local.x : local.x - resize_start_x_;
            const double delta = moved / layout.scale;
            const double width = std::max(column.minimum_width, resize_start_width_ + delta);
            set_column_width(*resizing_column_.value(), width);
        } else if (event.action == PointerAction::up) {
            resizing_column_.reset();
            Window* const host = window();
            if (host != nullptr && has_pointer_capture()) (*host).release_pointer();
            invalidate(Dirty::paint | Dirty::semantics);
        }
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::leave) {
        if (hot_column_.present()) {
            hot_column_.reset();
            invalidate(Dirty::paint);
        }
        if (cursor.read() != stxlib::Optional<CursorKind>::of(CursorKind::arrow)) cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
        return;
    }
    const HitResult hit = hit_test(local);
    if (event.action == PointerAction::move && pressed_column_.present() && allow_user_to_order_columns.read() && has_pointer_capture()) {
        // A header press becomes a drag once the pointer moves 4 units.
        const double moved = std::abs(local.x - order_start_x_) / layout.scale;
        if (!ordering_ && moved >= 4.0) ordering_ = true;
        if (ordering_) {
            const DataGridViewDisplayIndex target = order_target_at(local.x, layout);
            if (order_target_ != stxlib::Optional<DataGridViewDisplayIndex>::of(target)) {
                order_target_.set(target);
                invalidate(Dirty::paint | Dirty::semantics);
            }
        }
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        stxlib::Optional<std::size_t> hot{};
        if (hit.zone == HitZone::column_header || hit.zone == HitZone::column_divider) hot.set(hit.column);
        if (hot_column_ != hot) {
            hot_column_ = hot;
            invalidate(Dirty::paint);
        }
        // The hand over a link cell's link, as over a LinkLabel's; the arrow
        // elsewhere.
        bool over_link = false;
        if (hit.zone == HitZone::cell && columns_[hit.column].kind == DataGridViewColumnKind::link) {
            const CellParts placed = cell_parts(hit.row, hit.column, layout);
            over_link = placed.cell_link.present && placed.cell_link.bounds.contains(local);
        }
        CursorKind wanted = over_link ? CursorKind::hand : CursorKind::arrow;
        if (hit.zone == HitZone::row_divider || hit.zone == HitZone::column_headers_divider) {
            wanted = CursorKind::resize_vertical;
        }
        if (hit.zone == HitZone::column_divider && column_resizable(hit.column)) {
            wanted = CursorKind::resize_horizontal;
        }
        if (cursor.read() != stxlib::Optional<CursorKind>::of(wanted)) cursor = stxlib::Optional<CursorKind>::of(wanted);
        return;
    }
    if (event.action == PointerAction::wheel) {
        const bool sideways = event.wheel_delta.x != 0.0 || has_modifier(event.modifiers, Modifier::shift);
        const double delta = event.wheel_delta.x != 0.0 ? event.wheel_delta.x : event.wheel_delta.y;
        if (delta == 0.0) return;
        // Small deltas are notches (three rows, or 48 units, each); larger
        // ones are already logical units.
        const bool notches = std::abs(delta) <= 8.0;
        if (sideways) {
            const double units = notches ? delta * 48.0 : delta;
            const double next = std::clamp(horizontal_offset_ - units, 0.0, maximum_horizontal_offset(layout));
            if (next != horizontal_offset_) set_horizontal_scroll_offset(next);
        } else {
            const double rows = notches ? delta * 3.0 : delta / layout.row_height;
            const std::size_t first = scroll_first_row();
            const double moved = std::round(static_cast<double>(first) - rows);
            const std::size_t highest = maximum_first_row(layout);
            const double last = static_cast<double>(highest);
            const double lowest = static_cast<double>(std::min(frozen_rows(), highest));
            const std::size_t next = static_cast<std::size_t>(std::clamp(moved, std::min(lowest, last), last));
            if (next != first) set_first_displayed_row(DataGridViewRowPosition{next});
        }
        event.handled = true;
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        event.handled = true;
        // end_edit and request_focus run the program's handlers, which may
        // remove or move rows and columns: the press keeps its row and column
        // by identity and finds them again after them (A-gf-controls-02-F6).
        const PressIdentity pressed = press_identity(hit);
        if (editing_ && !end_edit()) {
            // The edit stays open (its text is refused): the editor keeps
            // the focus. end_edit runs the program's handlers, so the
            // window is read after it.
            Window* const editing_host = window();
            if (edit_kind_ == EditKind::text && editing_host != nullptr) {
                static_cast<void>((*editing_host).request_focus(editor_));
            }
            return;
        }
        Window* const host = window();
        if (host != nullptr) static_cast<void>((*host).request_focus(lifetime));
        if (!is_alive()) return;
        const stxlib::Optional<HitResult> resolved = resolve_press(hit, pressed);
        if (resolved.absent()) return;
        const HitResult target = *resolved.value();
        const bool shift = has_modifier(event.modifiers, Modifier::shift);
        const bool control = has_modifier(event.modifiers, Modifier::control);
        SelectionGesture gesture = SelectionGesture::replace;
        if (shift) gesture = SelectionGesture::extend;
        else if (control) gesture = SelectionGesture::toggle;
        if (target.zone == HitZone::column_divider && column_resizable(target.column)) {
            resizing_column_.set(target.column);
            resize_start_x_ = local.x;
            resize_start_width_ = columns_[target.column].width;
            // Read again: the focus change above ran the program's handlers.
            Window* const capture_host = window();
            if (capture_host != nullptr) (*capture_host).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
            return;
        }
        if (target.zone == HitZone::column_headers_divider) {
            resizing_headers_ = true;
            resize_start_y_ = local.y;
            resize_start_height_ = column_headers_height.read();
            Window* const capture_host = window();
            if (capture_host != nullptr) (*capture_host).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
            return;
        }
        if (target.zone == HitZone::row_divider) {
            resizing_row_.set(target.row.value);
            resize_start_y_ = local.y;
            resize_start_height_ = row_height_at(target.row);
            Window* const capture_host = window();
            if (capture_host != nullptr) (*capture_host).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
            return;
        }
        if (target.zone == HitZone::column_header || target.zone == HitZone::column_divider) {
            pressed_column_.set(target.column);
            if (allow_user_to_order_columns.read()) {
                // Captured, so that a drag can leave the header band.
                order_start_x_ = local.x;
                Window* const order_host = window();
                if (order_host != nullptr) (*order_host).capture_pointer(lifetime, event.pointer_id);
            }
            invalidate(Dirty::paint);
            return;
        }
        if (target.zone == HitZone::top_left) {
            select_all();
            return;
        }
        if (target.zone == HitZone::row_header) {
            const std::string row_id = row_id_text(target.row);
            if (!click_row_header(target.row, gesture) || !is_alive() || find_row(row_id).absent()) return;
            row_header_click_.emit(row_event(row_id));
            return;
        }
        if (target.zone != HitZone::cell) return;
        const DataGridViewCellAddress pressed_at{target.row, target.column};
        if (!move_current(pressed_at, gesture)) return;
        // move_current ran the program's handlers, which may have moved or
        // removed the row or the column: the cell is found again by identity
        // and kept by it until the release.
        if (!is_alive()) return;
        const stxlib::Optional<HitResult> found = resolve_press(target, pressed);
        const HitResult* const moved = found.value();
        if (moved == nullptr) return;
        const DataGridViewCellAddress cell{(*moved).row, (*moved).column};
        pressed_cell_ = key_of(cell);
        pressed_cell_double_ = event.click_count >= 2U;
        if (gesture != SelectionGesture::replace) return;
        const CellParts placed = cell_parts(cell.row, cell.column, grid_layout());
        const DataGridViewColumnKind kind = columns_[cell.column].kind;
        if (kind == DataGridViewColumnKind::check_box && placed.cell_check_well.bounds.contains(local)) {
            static_cast<void>(toggle_check(cell));
        } else if ((kind == DataGridViewColumnKind::button && placed.cell_button_face.bounds.contains(local)) ||
                   (kind == DataGridViewColumnKind::image && placed.cell_image.present &&
                    placed.cell_image.bounds.contains(local)) ||
                   (kind == DataGridViewColumnKind::link && placed.cell_link.present &&
                    placed.cell_link.bounds.contains(local))) {
            // Released over the same cell, the press clicks the cell's
            // content (Windows Forms' CellContentClick).
            pressed_content_ = key_of(cell);
            invalidate(Dirty::paint | Dirty::semantics);
        } else if (kind == DataGridViewColumnKind::combo_box && edit_trigger_allowed(EditTrigger::pointer) &&
                   (placed.cell_drop_down.bounds.contains(local) || event.click_count >= 2U)) {
            static_cast<void>(open_choices());
        } else if (kind == DataGridViewColumnKind::text && event.click_count >= 2U &&
                   edit_trigger_allowed(EditTrigger::pointer)) {
            static_cast<void>(begin_edit(true));
        }
        return;
    }
    if (event.action == PointerAction::up) {
        if (pressed_column_.present()) {
            const std::size_t pressed = *pressed_column_.value();
            pressed_column_.reset();
            invalidate(Dirty::paint);
            // The drag is read before the capture goes: releasing it may
            // deliver a leave that ends the drag.
            const bool dragged = ordering_;
            const stxlib::Optional<DataGridViewDisplayIndex> target = order_target_;
            end_column_order();
            Window* const host = window();
            if (host != nullptr && has_pointer_capture()) (*host).release_pointer();
            if (dragged) {
                // A drag moves the column and is no header click.
                if (target.present() && is_alive() && pressed < columns_.size()) set_column_display_index(pressed, *target.value());
                event.handled = true;
                return;
            }
            const bool on_header = hit.zone == HitZone::column_header || hit.zone == HitZone::column_divider;
            if (on_header && hit.column == pressed) {
                column_header_click_.emit(pressed);
                if (!is_alive() || pressed >= columns_.size()) return;
                // The column selection modes select the column instead of
                // sorting (Windows Forms refuses automatic sorting there).
                if (header_selects_column()) {
                    select_column_from_header(pressed, has_modifier(event.modifiers, Modifier::control));
                } else {
                    sort_from_header(pressed);
                }
            }
            event.handled = true;
            return;
        }
        if (pressed_content_) {
            const CellKey pressed = *pressed_content_;
            pressed_content_.reset();
            invalidate(Dirty::paint | Dirty::semantics);
            const stxlib::Optional<DataGridViewCellAddress> at = address_of(pressed);
            if (at.present() && hit.zone == HitZone::cell && hit.row == (*at.value()).row && hit.column == (*at.value()).column) {
                click_content(*at.value());
            }
            event.handled = true;
            if (!is_alive()) return;
        }
        if (pressed_cell_) {
            const CellKey pressed = *pressed_cell_;
            const bool twice = pressed_cell_double_;
            pressed_cell_.reset();
            pressed_cell_double_ = false;
            // Read again: click_content ran the program's handlers.
            const HitResult released = hit_test(local);
            const stxlib::Optional<DataGridViewCellAddress> at = address_of(pressed);
            if (at.absent() || released.zone != HitZone::cell || !(released.row == (*at.value()).row) || released.column != (*at.value()).column) {
                return;
            }
            cell_click_.emit(cell_event(pressed));
            if (!is_alive() || !twice || find_row(pressed.row_id).absent() || pressed.column >= columns_.size()) return;
            cell_double_click_.emit(cell_event(pressed));
        }
    }
}

DataGridView::MouseTarget DataGridView::mouse_target_at(Point local) const {
    const DataGridViewHitTestInfo hit = hit_test_at(local);
    MouseTarget target{};
    switch (hit.type) {
        case DataGridViewHitTestType::cell:
            target.area = DataGridViewHitTestType::cell;
            target.row_id = row_id_text(*hit.row.value());
            target.column = *hit.column.value();
            break;
        case DataGridViewHitTestType::column_header:
        case DataGridViewHitTestType::column_divider:
            target.area = DataGridViewHitTestType::column_header;
            target.column = *hit.column.value();
            break;
        case DataGridViewHitTestType::column_headers_divider:
            // The header band's edge belongs to the header under it.
            if (hit.column.absent()) break;
            target.area = DataGridViewHitTestType::column_header;
            target.column = *hit.column.value();
            break;
        case DataGridViewHitTestType::row_header:
        case DataGridViewHitTestType::row_divider:
            target.area = DataGridViewHitTestType::row_header;
            target.row_id = row_id_text(*hit.row.value());
            break;
        case DataGridViewHitTestType::top_left:
            target.area = DataGridViewHitTestType::top_left;
            break;
        case DataGridViewHitTestType::none:
        default:
            target.area = DataGridViewHitTestType::none;
            break;
    }
    return target;
}

DataGridViewCellMouseEvent DataGridView::cell_mouse_event(const MouseTarget& target, const PointerEvent& event,
                                                          Point local) const {
    DataGridViewCellMouseEvent raised{};
    raised.area = target.area;
    raised.button = event.button;
    raised.click_count = event.action == PointerAction::down ? event.click_count : 0U;
    raised.modifiers = event.modifiers;
    const GridLayout layout = grid_layout();
    Point origin{layout.client.x, layout.client.y};
    stxlib::Optional<std::size_t> position{};
    if (!target.row_id.empty()) {
        raised.row_id.value = target.row_id;
        position = find_row(target.row_id);
        if (position.present()) raised.row.set(DataGridViewRowPosition{*position.value()});
    }
    const bool column_known = target.column < columns_.size();
    if (target.area == DataGridViewHitTestType::cell || target.area == DataGridViewHitTestType::column_header) {
        raised.column.set(target.column);
    }
    if (target.area == DataGridViewHitTestType::cell && position.present() && column_known) {
        const Rect bounds = cell_bounds(DataGridViewRowPosition{*position.value()}, target.column, layout);
        origin = Point{bounds.x, bounds.y};
    } else if (target.area == DataGridViewHitTestType::row_header && position.present()) {
        const Rect bounds = row_header_bounds(DataGridViewRowPosition{*position.value()}, layout);
        origin = Point{bounds.x, bounds.y};
    } else if (target.area == DataGridViewHitTestType::column_header && column_known) {
        const Rect bounds = column_header_bounds(target.column, layout);
        origin = Point{bounds.x, bounds.y};
    } else if (target.area == DataGridViewHitTestType::top_left) {
        origin = Point{layout.top_left.x, layout.top_left.y};
    }
    raised.location = Point{local.x - origin.x, local.y - origin.y};
    return raised;
}

bool DataGridView::raise_cell_mouse(const PointerEvent& event, Point local) {
    const PointerAction action = event.action;
    const bool tracked = action == PointerAction::move || action == PointerAction::enter ||
                         action == PointerAction::leave || action == PointerAction::down || action == PointerAction::up;
    if (!tracked) return true;
    // A resize or column drag owns the pointer.
    if (resizing_column_.present() || resizing_row_.present() || resizing_headers_ || ordering_) return true;
    MouseTarget target{};
    if (action != PointerAction::leave) target = mouse_target_at(local);
    if (!(target == hovered_target_)) {
        const MouseTarget left = hovered_target_;
        hovered_target_ = target;
        if (left.area != DataGridViewHitTestType::none) {
            cell_mouse_leave_.emit(cell_mouse_event(left, event, local));
            if (!is_alive()) return false;
        }
        if (target.area != DataGridViewHitTestType::none) {
            cell_mouse_enter_.emit(cell_mouse_event(target, event, local));
            if (!is_alive()) return false;
        }
    }
    if (target.area == DataGridViewHitTestType::none) {
        if (action == PointerAction::up) pressed_target_ = MouseTarget{};
        return true;
    }
    switch (action) {
        case PointerAction::move:
            cell_mouse_move_.emit(cell_mouse_event(target, event, local));
            break;
        case PointerAction::down:
            pressed_target_ = target;
            pressed_target_clicks_ = event.click_count;
            cell_mouse_down_.emit(cell_mouse_event(target, event, local));
            break;
        case PointerAction::up: {
            const MouseTarget pressed = pressed_target_;
            const std::uint32_t clicks = pressed_target_clicks_;
            pressed_target_ = MouseTarget{};
            pressed_target_clicks_ = 0U;
            cell_mouse_up_.emit(cell_mouse_event(target, event, local));
            if (!is_alive() || !(pressed == target)) break;
            cell_mouse_click_.emit(cell_mouse_event(target, event, local));
            if (!is_alive() || clicks < 2U) break;
            cell_mouse_double_click_.emit(cell_mouse_event(target, event, local));
            break;
        }
        case PointerAction::enter:
        case PointerAction::leave:
            // Cell enter and leave were raised above, from the target change.
            break;
        case PointerAction::wheel:
            // The wheel scrolls the grid (on_mouse_wheel); it raises no cell event.
            break;
        default:
            // No other action exists; a value outside the enumeration raises nothing.
            break;
    }
    const bool alive = is_alive();
    return alive;
}

DataGridViewHitTestInfo DataGridView::hit_test_at(Point local) const {
    const HitResult hit = hit_test(local);
    DataGridViewHitTestInfo info{};
    switch (hit.zone) {
        case HitZone::cell:
            info.type = DataGridViewHitTestType::cell;
            info.row.set(hit.row);
            info.column.set(hit.column);
            break;
        case HitZone::column_header:
            info.type = DataGridViewHitTestType::column_header;
            info.column.set(hit.column);
            break;
        case HitZone::column_divider:
            info.type = DataGridViewHitTestType::column_divider;
            info.column.set(hit.column);
            break;
        case HitZone::row_header:
            info.type = DataGridViewHitTestType::row_header;
            info.row.set(hit.row);
            break;
        case HitZone::row_divider:
            info.type = DataGridViewHitTestType::row_divider;
            info.row.set(hit.row);
            break;
        case HitZone::top_left:
            info.type = DataGridViewHitTestType::top_left;
            break;
        case HitZone::column_headers_divider:
            info.type = DataGridViewHitTestType::column_headers_divider;
            if (hit.edge_column) info.column.set(hit.column);
            break;
        case HitZone::none:
        default:
            info.type = DataGridViewHitTestType::none;
            break;
    }
    return info;
}

// The mouse raisers hand the target phase to handle_pointer, whose
// handling predates the raisers and reads the routed form.
void DataGridView::on_mouse_down(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_down(arguments);
}

void DataGridView::on_mouse_up(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_up(arguments);
}

void DataGridView::on_mouse_move(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_move(arguments);
}

void DataGridView::on_mouse_wheel(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_wheel(arguments);
}

void DataGridView::on_mouse_leave() {
    PointerEvent leave{};
    leave.action = PointerAction::leave;
    handle_pointer(leave);
    if (!is_alive()) return;
    Control::on_mouse_leave();
}

void DataGridView::on_key_preview(KeyEvent& event) {
    // Keys for a text edit in progress, before the editor sees them.
    if (!editing_ || edit_kind_ != EditKind::text || event.action != KeyAction::down) return;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::escape) {
        cancel_edit();
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::enter) {
        event.handled = true;
        if (!end_edit()) return;
        const stxlib::Optional<DataGridViewCellAddress> cell = current_cell();
        if (cell.absent()) return;
        const DataGridViewRowPosition next{(*cell.value()).row.value + 1U};
        if (next.value < row_count()) static_cast<void>(move_current({next, (*cell.value()).column}, SelectionGesture::replace));
        return;
    }
    if (key == PhysicalKey::tab) {
        const bool between_cells = tabs_between_cells(event.modifiers);
        if (!end_edit()) {
            event.handled = true;
            return;
        }
        // A Tab that leaves the grid stays unhandled once the edit is
        // committed, so the window moves the focus.
        if (!between_cells) return;
        event.handled = true;
        static_cast<void>(tab_key(has_modifier(event.modifiers, Modifier::shift)));
        return;
    }
    const bool vertical = key == PhysicalKey::up || key == PhysicalKey::down ||
                          key == PhysicalKey::page_up || key == PhysicalKey::page_down;
    if (!vertical) return;
    event.handled = true;
    if (!end_edit()) return;
    static_cast<void>(navigation_key(event));
}

void DataGridView::handle_key(KeyEvent& event) {
    if (event.action != KeyAction::down || editing_ || !focused_) return;
    const std::uint32_t key = event.physical_key;
    const bool alt = has_modifier(event.modifiers, Modifier::alt);
    const bool control = has_modifier(event.modifiers, Modifier::control);
    if (current_ && (key == PhysicalKey::f4 || (alt && key == PhysicalKey::down)) &&
        columns_[(*current_).column].kind == DataGridViewColumnKind::combo_box &&
        edit_trigger_allowed(EditTrigger::pointer)) {
        static_cast<void>(open_choices());
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::f2) {
        if (edit_trigger_allowed(EditTrigger::f2)) static_cast<void>(begin_edit(true));
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::escape) {
        // Escape with no cell in edit discards the row the user added.
        if (cancel_row_edit()) event.handled = true;
        return;
    }
    if (key == PhysicalKey::delete_forward) {
        if (delete_selected_rows()) event.handled = true;
        return;
    }
    // Copy: Ctrl+C (Command+C on macOS) and Ctrl+Insert.
    const bool copy_key = (input_control_detail::command_modifier(event.modifiers) && key == PhysicalKey::c) ||
                          (control && key == PhysicalKey::insert);
    if (copy_key) {
        if (copy_selection_to_clipboard()) event.handled = true;
        return;
    }
    if (key == PhysicalKey::space && current_) {
        const DataGridViewColumnKind kind = columns_[(*current_).column].kind;
        if (kind == DataGridViewColumnKind::check_box || kind == DataGridViewColumnKind::button ||
            kind == DataGridViewColumnKind::link) {
            static_cast<void>(activate_current());
            event.handled = true;
        }
        return;
    }
    if (control && key == PhysicalKey::a) {
        select_all();
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::enter) {
        // Enter moves to the cell below, as in Windows Forms.
        const stxlib::Optional<DataGridViewCellAddress> cell = current_cell();
        if (cell.present() && (*cell.value()).row.value + 1U < row_count()) {
            const DataGridViewRowPosition next{(*cell.value()).row.value + 1U};
            static_cast<void>(move_current({next, (*cell.value()).column}, SelectionGesture::replace));
        }
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::tab) {
        if (!tabs_between_cells(event.modifiers)) return;
        if (tab_key(has_modifier(event.modifiers, Modifier::shift))) event.handled = true;
        return;
    }
    if (navigation_key(event)) event.handled = true;
}

// The key raisers raise the event first; an unhandled key reaches
// handle_key, whose handling predates the raisers and reads the routed
// form.
void DataGridView::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled) return;
    KeyEvent event = key_event_of(arguments, KeyAction::down);
    handle_key(event);
    if (event.handled) arguments.handled = true;
}

bool DataGridView::tabs_between_cells(Modifier modifiers) const {
    // StandardTab swaps the roles of Tab and Ctrl+Tab.
    const bool control = has_modifier(modifiers, Modifier::control);
    const bool between = standard_tab.read() ? control : !control;
    return between;
}

bool DataGridView::tab_key(bool backward) {
    const std::size_t count = row_count();
    const std::size_t columns = shown_order_.size();
    if (count == 0U || columns == 0U) return false;
    const stxlib::Optional<DataGridViewCellAddress> current = current_cell();
    if (current.absent()) {
        const DataGridViewCellAddress first{DataGridViewRowPosition{0U}, shown_order_.front()};
        const bool moved = move_current(first, SelectionGesture::replace);
        return moved;
    }
    DataGridViewRowPosition row = (*current.value()).row;
    std::size_t display = std::min(shown_indices_[(*current.value()).column], columns - 1U);
    if (backward) {
        if (display > 0U) {
            --display;
        } else if (row.value > 0U) {
            --row.value;
            display = columns - 1U;
        } else {
            // Shift+Tab from the first cell leaves the grid.
            return false;
        }
    } else {
        if (display + 1U < columns) {
            ++display;
        } else if (row.value + 1U < count) {
            ++row.value;
            display = 0U;
        } else {
            // Tab from the last cell leaves the grid.
            return false;
        }
    }
    const DataGridViewCellAddress next{row, shown_order_[display]};
    static_cast<void>(move_current(next, SelectionGesture::replace));
    return true;
}

bool DataGridView::navigation_key(KeyEvent& event) {
    const std::size_t count = row_count();
    const std::size_t columns = shown_order_.size();
    if (count == 0U || columns == 0U) return false;
    // Right to left the columns read from the right: Left moves to the next
    // column and Right to the previous one.
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    std::uint32_t key = event.physical_key;
    if (mirrored && key == PhysicalKey::left) {
        key = PhysicalKey::right;
    } else if (mirrored && key == PhysicalKey::right) {
        key = PhysicalKey::left;
    }
    const bool control = has_modifier(event.modifiers, Modifier::control);
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    const bool full_row = selection_mode.read() == DataGridViewSelectionMode::full_row_select;
    const stxlib::Optional<DataGridViewCellAddress> current = current_cell();
    const DataGridViewCellAddress from =
        current.present() ? *current.value() : DataGridViewCellAddress{DataGridViewRowPosition{first_shown_row()}, shown_order_.front()};
    const std::size_t row = from.row.value;
    const std::size_t last_row = count - 1U;
    // Columns move by display index: the first, previous, next and last
    // as they are shown.
    const std::size_t display = std::min(shown_indices_[from.column], columns - 1U);
    const std::size_t last_display = columns - 1U;
    const std::size_t page = std::max<std::size_t>(1U, fully_visible_rows(grid_layout()));
    DataGridViewRowPosition target_row = from.row;
    std::size_t target_display = display;
    if (key == PhysicalKey::up) {
        target_row.value = control || row == 0U ? 0U : row - 1U;
    } else if (key == PhysicalKey::down) {
        target_row.value = control ? last_row : std::min(last_row, row + 1U);
    } else if (key == PhysicalKey::left) {
        target_display = control || display == 0U ? 0U : display - 1U;
    } else if (key == PhysicalKey::right) {
        target_display = control ? last_display : std::min(last_display, display + 1U);
    } else if (key == PhysicalKey::page_up) {
        target_row.value = row > page ? row - page : 0U;
    } else if (key == PhysicalKey::page_down) {
        target_row.value = std::min(last_row, row + page);
    } else if (key == PhysicalKey::home) {
        // Ctrl+Home is the first cell; Home is the row's first cell, or the
        // first row when whole rows are selected.
        if (control) {
            target_row.value = 0U;
            target_display = 0U;
        } else if (full_row) {
            target_row.value = 0U;
        } else {
            target_display = 0U;
        }
    } else if (key == PhysicalKey::end) {
        if (control) {
            target_row.value = last_row;
            target_display = last_display;
        } else if (full_row) {
            target_row.value = last_row;
        } else {
            target_display = last_display;
        }
    } else {
        return false;
    }
    const DataGridViewCellAddress target{target_row, shown_order_[target_display]};
    const SelectionGesture gesture = shift ? SelectionGesture::extend : SelectionGesture::replace;
    static_cast<void>(move_current(target, gesture));
    return true;
}

void DataGridView::on_text_input(TextInputEvent& event) {
    if (editing_ || !focused_ || !current_ || event.composing || event.text_utf8.empty()) return;
    if (columns_[(*current_).column].kind != DataGridViewColumnKind::text) return;
    if (!edit_trigger_allowed(EditTrigger::keystroke)) return;
    // Typing into a text cell begins an edit that starts with the typed text.
    if (begin_text_edit(event.text_utf8, false)) event.handled = true;
}

void DataGridView::on_attachment_committed() noexcept {
    Control::on_attachment_committed();
    Window* const host = attached_window();
    if (host == nullptr || followed_window_ == host) return;
    try {
        window_focus_.disconnect();
        window_focus_ = (*host).focused_control_changed().subscribe(
            *this, Delegate<const FocusedControlChange&>::bind<DataGridView, &DataGridView::on_window_focus_moved>(*this));
        followed_window_ = host;
    } catch (...) {
        // Without the subscription a focus loss validates no row; nothing
        // else depends on it.
        followed_window_ = nullptr;
    }
}

void DataGridView::on_window_focus_moved(const FocusedControlChange& change) {
    const RuntimeId grid = runtime_id();
    const RuntimeId editor = editor_ ? (*editor_).runtime_id() : RuntimeId{};
    const bool from_grid = change.previous_id == grid || (editor_ && change.previous_id == editor);
    const bool to_grid = change.current_id == grid || (editor_ && change.current_id == editor);
    // A combo box cell's open choices take focus while the edit goes on.
    if (!from_grid || to_grid || drop_down_open() || (editing_ && edit_kind_ == EditKind::choice)) return;
    // Focus left the grid: an edit commits (or is discarded when it does
    // not commit), then the current row validates. A handler's cancel
    // leaves the row unvalidated; focus is not held (a deviation: Windows
    // Forms keeps it in the grid).
    const Control::Ptr lifetime = shared_from_this();
    if (editing_ && !end_edit() && is_alive()) cancel_edit();
    if (!is_alive() || !current_) return;
    const std::string row_id = (*current_).row_id;
    if (find_row(row_id).absent()) return;
    if (!validate_row_change(std::nullopt) || !is_alive()) return;
    row_validated_.emit(row_event(row_id));
}

void DataGridView::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && !current_ && row_count() > 0U && !shown_order_.empty()) {
        const DataGridViewCellAddress first{DataGridViewRowPosition{first_shown_row()}, shown_order_.front()};
        static_cast<void>(move_current(first, SelectionGesture::replace));
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics -----------------------------------------------------------------

std::string DataGridView::cell_stable_id(DataGridViewRowPosition row, std::size_t column) const {
    std::string identity = row_id_text(row);
    identity += '.';
    identity += columns_[column].name;
    return identity;
}

std::string DataGridView::column_stable_id(std::size_t column) const {
    std::string identity(stable_id().value());
    identity += ".column.";
    identity += columns_[column].name;
    return identity;
}

// The error glyph of a cell or row: an image named and tooltipped by the
// error text, at the glyph's place.
SemanticNode DataGridView::error_glyph_node(std::string stable_id, const std::string& text, const PartGeometry& glyph,
                                            const Rect& absolute) const {
    SemanticNode node{};
    node.stable_id = std::move(stable_id);
    node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
    node.role = SemanticRole::image;
    node.name = text;
    node.tool_tip = text;
    node.bounds = {absolute.x + glyph.bounds.x, absolute.y + glyph.bounds.y, glyph.bounds.width, glyph.bounds.height};
    node.states = SemanticState::visible | SemanticState::enabled;
    return node;
}

// The starts of the error glyphs' identities, apart for cells and rows and
// beginning with the grid's own identity, as the row headers' do.
std::string DataGridView::cell_error_prefix() const {
    std::string prefix(stable_id().value());
    prefix += ".cell-error.";
    return prefix;
}

std::string DataGridView::row_error_prefix() const {
    std::string prefix(stable_id().value());
    prefix += ".row-error.";
    return prefix;
}

// The start of every row header's identity; the row's identity follows it.
// It begins with the grid's own identity, as a column header's does, so it
// is no row's identity followed by a column name.
std::string DataGridView::row_header_prefix() const {
    std::string prefix(stable_id().value());
    prefix += ".row-header.";
    return prefix;
}

bool DataGridView::click_row_header(DataGridViewRowPosition row, SelectionGesture gesture) {
    if (shown_order_.empty() || row.value >= row_count()) return false;
    const std::size_t column = current_ ? (*current_).column : shown_order_.front();
    // move_current runs the program's handlers, which may move or remove
    // the row or the columns: the row is found again by its ID after it
    // (A-gf-controls-02-F6).
    const std::string row_id = row_id_text(row);
    if (!move_current({row, column}, gesture)) return false;
    if (!is_alive()) return true;
    const stxlib::Optional<std::size_t> settled = find_row(row_id);
    if (settled.absent() || shown_order_.empty()) return false;
    const DataGridViewRowPosition at{*settled.value()};
    const DataGridViewSelectionMode mode = selection_mode.read();
    const bool cell_mode = mode == DataGridViewSelectionMode::cell_select ||
                           mode == DataGridViewSelectionMode::row_header_select ||
                           mode == DataGridViewSelectionMode::column_header_select;
    if (cell_mode && gesture == SelectionGesture::replace) {
        const std::uint64_t before = selection_revision_;
        select_range({at, shown_order_.front()}, {at, shown_order_.back()});
        publish_selection(before);
    }
    return true;
}

stxlib::Optional<DataGridViewCellAddress> DataGridView::cell_from_stable_id(std::string_view id) const {
    // Column names hold no '.', so the last '.' separates row and column.
    const std::size_t dot = id.rfind('.');
    if (dot == std::string_view::npos) return stxlib::Optional<DataGridViewCellAddress>::none();
    const stxlib::Optional<std::size_t> column = column_index(id.substr(dot + 1U));
    if (column.absent()) return stxlib::Optional<DataGridViewCellAddress>::none();
    const stxlib::Optional<std::size_t> row = find_row(id.substr(0U, dot));
    if (row.absent()) return stxlib::Optional<DataGridViewCellAddress>::none();
    const DataGridViewCellAddress cell{DataGridViewRowPosition{*row.value()}, *column.value()};
    return stxlib::Optional<DataGridViewCellAddress>::of(cell);
}

SemanticDescriptor DataGridView::semantic_descriptor() const {
    // A table (12.13) whose rows hold their cells, sized by every row and
    // every column, realized or not.
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::table;
    descriptor.row_count.set(row_count());
    descriptor.column_count.set(columns_.size());
    if (multi_select.read()) descriptor.states |= SemanticState::multiselectable;
    descriptor.name = accessible_name.read().empty() ? "Data grid" : accessible_name.read();
    descriptor.description = accessible_description.read();
    const stxlib::Optional<DataGridViewCellAddress> current = current_cell();
    if (current.present() && (*current.value()).column < columns_.size()) {
        descriptor.value = cell_text((*current.value()).row, (*current.value()).column);
    }
    descriptor.actions = {SemanticAction::focus};
    if (read_only.read()) descriptor.states |= SemanticState::read_only;
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> DataGridView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    // cell_text (cell_formatting), the error texts and the tool tip raise
    // program events inside the loops below. Their handlers may not change
    // the grid; one that does moves structure_revision_, and the walk then
    // returns the nodes gathered so far, as paint stops, rather than read
    // through stale indices and the column reference (A-gf-controls-02-F10).
    const std::uint64_t revision = structure_revision_;
    const Rect absolute = absolute_bounds();
    const GridLayout layout = grid_layout();
    const bool focus_within = focused_ || editing_;
    // Frozen columns and rows first, then the scrolling ones.
    const IndexSpan displays = display_span(layout);
    if (column_headers_visible.read()) {
        for (std::size_t display = displays.start(); display < displays.end; display = displays.next(display)) {
            const std::size_t column = shown_order_[display];
            const ColumnParts placed = column_parts(column, layout);
            SemanticNode node{};
            node.stable_id = column_stable_id(column);
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
            node.role = SemanticRole::column_header;
            node.column_index.set(display + 1U);
            node.name = columns_[column].header_text.empty() ? columns_[column].name : columns_[column].header_text;
            if (show_cell_tool_tips.read()) node.tool_tip = columns_[column].tool_tip_text;
            node.bounds = {absolute.x + placed.column_header.bounds.x, absolute.y + placed.column_header.bounds.y,
                           placed.column_header.bounds.width, placed.column_header.bounds.height};
            node.states = SemanticState::visible | SemanticState::enabled;
            if (column_in_selection(column)) node.states |= SemanticState::selected;
            node.actions = {SemanticAction::press};
            nodes.push_back(std::move(node));
        }
    }
    const IndexSpan row_run = row_span(layout);
    const std::string header_prefix = row_header_prefix();
    for (std::size_t position = row_run.start(); position < row_run.end; position = row_run.next(position)) {
        const DataGridViewRowPosition row{position};
        SemanticNode row_node{};
        row_node.stable_id = row_id_text(row);
        row_node.runtime_id = virtual_semantic_runtime_id(row_node.stable_id);
        row_node.role = SemanticRole::row;
        row_node.row_index.set(position + 1U);
        const bool labelled = row_source_ == DataGridViewRowSource::unbound && position < rows.read().size() &&
                              !rows.read()[position].header_text.empty();
        row_node.name = labelled ? rows.read()[position].header_text : "Row " + std::to_string(position + 1U);
        if (is_new_row(row)) row_node.name = "New row";
        row_node.description = row_error_text(row);
        if (structure_revision_ != revision) return nodes;
        if (!row_node.description.empty()) row_node.states |= SemanticState::invalid;
        const Rect row_bounds{layout.client.x, layout.data.y + row_offset(position, layout), layout.client.width,
                              row_heights().height(position) * layout.scale};
        row_node.bounds = {absolute.x + row_bounds.x, absolute.y + row_bounds.y, row_bounds.width,
                           row_bounds.height};
        row_node.states |= SemanticState::visible | SemanticState::enabled | SemanticState::focusable;
        if (is_row_selected(row)) row_node.states |= SemanticState::selected;
        if (row_read_only(row) || read_only.read()) row_node.states |= SemanticState::read_only;
        if (focus_within && is_current_row(row)) row_node.states |= SemanticState::focused;
        row_node.actions = {SemanticAction::focus, SemanticAction::select};
        // A drawn row header is published first in its row (owner decision,
        // stage A gap 4), named as the row is; pressing it clicks it.
        if (row_headers_visible.read()) {
            const RowParts placed_row = row_parts(row, layout);
            SemanticNode header{};
            header.stable_id = header_prefix + row_node.stable_id;
            header.runtime_id = virtual_semantic_runtime_id(header.stable_id);
            header.role = SemanticRole::row_header;
            header.row_index.set(position + 1U);
            header.name = row_node.name;
            header.bounds = {absolute.x + placed_row.row_header.bounds.x, absolute.y + placed_row.row_header.bounds.y,
                             placed_row.row_header.bounds.width, placed_row.row_header.bounds.height};
            header.states = SemanticState::visible | SemanticState::enabled;
            header.actions = {SemanticAction::press};
            if (placed_row.row_error.present && !row_node.description.empty()) {
                header.children.push_back(error_glyph_node(row_error_prefix() + row_node.stable_id,
                                                           row_node.description, placed_row.row_error, absolute));
            }
            row_node.children.push_back(std::move(header));
        }
        for (std::size_t display = displays.start(); display < displays.end; display = displays.next(display)) {
            const std::size_t column = shown_order_[display];
            const DataGridViewColumn& definition = columns_[column];
            const CellParts placed = cell_parts(row, column, layout);
            SemanticNode cell{};
            cell.stable_id = cell_stable_id(row, column);
            cell.runtime_id = virtual_semantic_runtime_id(cell.stable_id);
            cell.name = definition.header_text.empty() ? definition.name : definition.header_text;
            cell.value = cell_text(row, column);
            cell.row_index.set(position + 1U);
            cell.column_index.set(display + 1U);
            cell.bounds = {absolute.x + placed.cell.bounds.x, absolute.y + placed.cell.bounds.y,
                           placed.cell.bounds.width, placed.cell.bounds.height};
            cell.states = SemanticState::visible | SemanticState::enabled | SemanticState::focusable;
            if (is_cell_selected(row, column)) cell.states |= SemanticState::selected;
            const bool current = is_current_cell(row, column);
            if (focus_within && current) cell.states |= SemanticState::focused;
            const bool cell_is_read_only = cell_read_only(row, column);
            if (cell_is_read_only) cell.states |= SemanticState::read_only;
            cell.description = cell_error_text(row, column);
            cell.tool_tip = cell_tool_tip_text(row, column);
            // cell_text and these two ran handlers: nothing of the grid is
            // read again (the column definition included) once it changed.
            if (structure_revision_ != revision) return nodes;
            if ((current && edit_invalid_) || !cell.description.empty()) cell.states |= SemanticState::invalid;
            cell.actions = {SemanticAction::focus, SemanticAction::select};
            // Every cell is a cell, placed by its row and column; its kind's
            // behaviour stays on it as states and actions (a toggle, a value,
            // a choice, a press), from which a host offers the editing it
            // needs (owner decision, stage A gap 1).
            cell.role = SemanticRole::cell;
            if (definition.kind == DataGridViewColumnKind::check_box) {
                cell.states |= SemanticState::checkable;
                const stxlib::Optional<bool> state = check_state(row, column);
                if (state == stxlib::Optional<bool>::of(true)) cell.states |= SemanticState::checked;
                if (state.absent()) cell.states |= SemanticState::mixed;
                if (!cell_is_read_only) cell.actions.push_back(SemanticAction::press);
            } else if (definition.kind == DataGridViewColumnKind::button) {
                cell.name = cell.value;
                cell.actions.push_back(SemanticAction::press);
            } else if (definition.kind == DataGridViewColumnKind::combo_box) {
                if (current && edit_kind_ == EditKind::choice) cell.states |= SemanticState::expanded;
                if (!cell_is_read_only) {
                    cell.actions.push_back(SemanticAction::set_value);
                    cell.actions.push_back(SemanticAction::expand);
                }
            } else if (definition.kind == DataGridViewColumnKind::link) {
                // A link cell is named by its link's text, as a button cell is
                // by its face, and is pressed to follow the link. Its value
                // says whether the link is visited, as LinkLabel's does (a
                // cell keeps the cell role: owner decision, stage A gap 1).
                cell.name = cell.value;
                cell.value = is_link_visited(row_node.stable_id, column) ? "visited" : "unvisited";
                cell.actions.push_back(SemanticAction::press);
            } else if (definition.kind == DataGridViewColumnKind::image) {
                // An image is shown, not typed: the cell's value is its
                // description and it offers no set_value.
            } else {
                if (!cell_is_read_only) cell.actions.push_back(SemanticAction::set_value);
            }
            if (placed.cell_error.present && !cell.description.empty()) {
                cell.children.push_back(
                    error_glyph_node(cell_error_prefix() + cell.stable_id, cell.description, placed.cell_error, absolute));
            }
            row_node.children.push_back(std::move(cell));
        }
        nodes.push_back(std::move(row_node));
    }
    return nodes;
}

bool DataGridView::on_semantic_action(SemanticAction action, std::string_view value) {
    Window* const host = window();
    if (action == SemanticAction::focus && host != nullptr) {
        const bool focused = (*host).request_focus(shared_from_this());
        return focused;
    }
    return Control::on_semantic_action(action, value);
}

bool DataGridView::on_semantic_child_action(std::string_view id, SemanticAction action,
                                            std::string_view value) {
    const std::string column_prefix = std::string(stable_id().value()) + ".column.";
    if (id.compare(0U, column_prefix.size(), column_prefix) == 0) {
        const stxlib::Optional<std::size_t> column = column_index(id.substr(column_prefix.size()));
        if (column.absent() || action != SemanticAction::press) return false;
        const std::string column_name(id.substr(column_prefix.size()));
        column_header_click_.emit(*column.value());
        // The click's handlers may have moved or removed the column: it is
        // found again by its name (A-gf-controls-02-F6).
        if (!is_alive()) return true;
        const stxlib::Optional<std::size_t> clicked = column_index(column_name);
        if (clicked.absent()) return true;
        const std::size_t pressed = *clicked.value();
        if (header_selects_column()) {
            select_column_from_header(pressed, false);
        } else {
            sort_from_header(pressed);
        }
        return true;
    }
    const std::string header_prefix = row_header_prefix();
    if (id.compare(0U, header_prefix.size(), header_prefix) == 0) {
        const stxlib::Optional<std::size_t> row = find_row(id.substr(header_prefix.size()));
        if (row.absent() || columns_.empty() || action != SemanticAction::press) return false;
        const Control::Ptr lifetime = shared_from_this();
        Window* const host = window();
        if (host != nullptr) static_cast<void>((*host).request_focus(lifetime));
        // The focus change ran the program's handlers: the row is found
        // again by its ID (A-gf-controls-02-F6).
        if (!is_alive()) return true;
        const std::string row_id(id.substr(header_prefix.size()));
        const stxlib::Optional<std::size_t> focused_row = find_row(row_id);
        if (focused_row.absent()) return false;
        const bool clicked = click_row_header(DataGridViewRowPosition{*focused_row.value()}, SelectionGesture::replace);
        if (clicked && is_alive() && find_row(row_id).present()) row_header_click_.emit(row_event(row_id));
        return clicked;
    }
    const stxlib::Optional<DataGridViewCellAddress> cell = cell_from_stable_id(id);
    if (cell.absent()) {
        const stxlib::Optional<std::size_t> row = find_row(id);
        if (row.absent() || columns_.empty()) return false;
        if (action != SemanticAction::focus && action != SemanticAction::select) return false;
        const Control::Ptr lifetime = shared_from_this();
        Window* const host = window();
        if (action == SemanticAction::focus && host != nullptr) {
            static_cast<void>((*host).request_focus(lifetime));
        }
        // The focus change and the move run the program's handlers: the row
        // is found again by its ID after each (A-gf-controls-02-F6).
        if (!is_alive()) return true;
        const stxlib::Optional<std::size_t> focused_row = find_row(id);
        if (focused_row.absent()) return false;
        const std::size_t column = current_ ? (*current_).column : 0U;
        const bool moved = move_current({DataGridViewRowPosition{*focused_row.value()}, column}, SelectionGesture::replace);
        if (!moved) return false;
        if (!is_alive()) return true;
        // A row's select selects the row, as its published state reports it:
        // in cell mode moving the current cell selects only that cell.
        if (action == SemanticAction::select) {
            const stxlib::Optional<std::size_t> moved_row = find_row(id);
            if (moved_row.absent()) return false;
            set_row_selected(DataGridViewRowPosition{*moved_row.value()}, true);
        }
        return true;
    }
    const Control::Ptr lifetime = shared_from_this();
    Window* const host = window();
    if (action == SemanticAction::focus && host != nullptr) {
        static_cast<void>((*host).request_focus(lifetime));
    }
    // The focus change and the move run the program's handlers, which may
    // move or remove the row or the column: the cell is found again by its
    // identity after each (A-gf-controls-02-F6).
    if (!is_alive()) return true;
    const stxlib::Optional<DataGridViewCellAddress> focused_cell = cell_from_stable_id(id);
    if (focused_cell.absent()) return false;
    if (!move_current(*focused_cell.value(), SelectionGesture::replace)) return false;
    if (!is_alive()) return true;
    if (action == SemanticAction::focus || action == SemanticAction::select) return true;
    const stxlib::Optional<DataGridViewCellAddress> moved_cell = cell_from_stable_id(id);
    if (moved_cell.absent()) return false;
    const DataGridViewColumn& column = columns_[(*moved_cell.value()).column];
    if (action == SemanticAction::press) {
        if (column.kind != DataGridViewColumnKind::check_box && column.kind != DataGridViewColumnKind::button &&
            column.kind != DataGridViewColumnKind::link) {
            return false;
        }
        const bool activated = activate_current();
        return activated;
    }
    if (action == SemanticAction::expand) {
        const bool opened = open_choices();
        return opened;
    }
    // The cell may have moved while it became current; act on its present
    // position.
    const stxlib::Optional<DataGridViewCellAddress> current = current_cell();
    if (current.absent() || action != SemanticAction::set_value || cell_read_only((*current.value()).row, (*current.value()).column)) {
        return false;
    }
    if (column.kind == DataGridViewColumnKind::combo_box) {
        const std::vector<std::string>::const_iterator found =
            std::find(column.items.begin(), column.items.end(), value);
        if (found == column.items.end()) return false;
        const bool stored = set_cell_value((*current.value()).row, (*current.value()).column, BindingValue{std::string(value)});
        return stored;
    }
    if (column.kind != DataGridViewColumnKind::text) return false;
    // A text value goes through the same path as typing it: begin, validate,
    // convert and commit.
    if (!begin_text_edit(std::string(value), false)) return false;
    if (end_edit()) return true;
    cancel_edit();
    return false;
}

namespace {

// The DataGridViewSelectionMode enumeration, numbered as declared, for the
// selection_mode table entry. Static storage; any thread may read it.
std::shared_ptr<const PropertyEnumDescriptor> data_grid_view_selection_mode_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "data_grid_view_selection_mode",
            {{"cell_select", 0},
             {"full_row_select", 1},
             {"full_column_select", 2},
             {"row_header_select", 3},
             {"column_header_select", 4}},
            false});
    return value;
}

} // namespace

// property_codemod begin DataGridView (tools/property_families/data_grid_view.json; generated, do not edit)

namespace {

PropertyDescriptor data_grid_view_read_only_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "read_only";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether no cell can be edited.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_data_grid_view_read_only() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        data_grid_view_read_only_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor data_grid_view_selection_mode_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "selection_mode";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Behavior";
    descriptor.description = "Whether selection takes cells or full rows.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"data_grid_view_selection_mode", "cell_select", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = data_grid_view_selection_mode_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_data_grid_view_selection_mode() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        data_grid_view_selection_mode_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor data_grid_view_multi_select_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "multi_select";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether more than one cell or row may be selected.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_data_grid_view_multi_select() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        data_grid_view_multi_select_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor data_grid_view_allow_user_to_add_rows_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "allow_user_to_add_rows";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the grid shows a new row after the last row.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_data_grid_view_allow_user_to_add_rows() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        data_grid_view_allow_user_to_add_rows_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<DataGridViewSelectionMode>::type() {
    return PropertyType{BindingValueKind::enumeration, false, data_grid_view_selection_mode_enum()};
}

BindingValue PropertyValueTraits<DataGridViewSelectionMode>::to_binding(DataGridViewSelectionMode value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = data_grid_view_selection_mode_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

DataGridViewSelectionMode PropertyValueTraits<DataGridViewSelectionMode>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<DataGridViewSelectionMode>(choice.value);
}

const std::array<PropertyEntry, DataGridView::data_grid_view_property_count>& DataGridView::data_grid_view_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"read_only", &describe_data_grid_view_read_only,
         &read_cell_entry<DataGridView, decltype(&DataGridView::read_only), &DataGridView::read_only>,
         &write_cell_entry<DataGridView, decltype(&DataGridView::read_only), &DataGridView::read_only>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"selection_mode", &describe_data_grid_view_selection_mode,
         &read_cell_entry<DataGridView, decltype(&DataGridView::selection_mode), &DataGridView::selection_mode>,
         &write_cell_entry<DataGridView, decltype(&DataGridView::selection_mode), &DataGridView::selection_mode>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"multi_select", &describe_data_grid_view_multi_select,
         &read_cell_entry<DataGridView, decltype(&DataGridView::multi_select), &DataGridView::multi_select>,
         &write_cell_entry<DataGridView, decltype(&DataGridView::multi_select), &DataGridView::multi_select>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"allow_user_to_add_rows", &describe_data_grid_view_allow_user_to_add_rows,
         &read_cell_entry<DataGridView, decltype(&DataGridView::allow_user_to_add_rows), &DataGridView::allow_user_to_add_rows>,
         &write_cell_entry<DataGridView, decltype(&DataGridView::allow_user_to_add_rows), &DataGridView::allow_user_to_add_rows>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "DataGridView's property table names a property twice");
    static const std::array<PropertyEntry, data_grid_view_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> DataGridView::property_table() const noexcept {
    const std::array<PropertyEntry, data_grid_view_property_count>& entries = data_grid_view_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void DataGridView::on_auto_size_columns_mode_changed() {}

void DataGridView::on_auto_size_rows_mode_changed() {}

void DataGridView::prepare_write(const decltype(clipboard_copy_mode)&, DataGridViewClipboardCopyMode& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewClipboardCopyMode::enable_always_include_header_text)) throw std::invalid_argument("DataGridView clipboard copy mode is outside the enumeration");
}

void DataGridView::written(const decltype(clipboard_copy_mode)&) {}

void DataGridView::prepare_write(const decltype(allow_user_to_delete_rows)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(allow_user_to_delete_rows)&) {
    on_allow_user_to_delete_rows_changed();
}

void DataGridView::on_allow_user_to_delete_rows_changed() {}

void DataGridView::prepare_write(const decltype(show_cell_tool_tips)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(show_cell_tool_tips)&) {
    invalidate(Dirty::semantics);
}

void DataGridView::prepare_write(const decltype(show_cell_errors)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(show_cell_errors)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::prepare_write(const decltype(show_row_errors)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(show_row_errors)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void DataGridView::prepare_write(const decltype(show_editing_icon)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(show_editing_icon)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void DataGridView::on_default_cell_style_changed() {}

void DataGridView::on_rows_default_cell_style_changed() {}

void DataGridView::on_alternating_rows_default_cell_style_changed() {}

void DataGridView::on_column_headers_default_cell_style_changed() {}

void DataGridView::on_row_headers_default_cell_style_changed() {}

void DataGridView::prepare_write(const decltype(enable_headers_visual_styles)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(enable_headers_visual_styles)&) {
    invalidate(Dirty::paint);
}

void DataGridView::prepare_write(const decltype(grid_color)&, stxlib::Optional<Color>&) {
    require_mutable();
}

void DataGridView::written(const decltype(grid_color)&) {
    invalidate(Dirty::paint);
    on_grid_color_changed();
}

void DataGridView::on_grid_color_changed() {}

void DataGridView::prepare_write(const decltype(background_color)&, stxlib::Optional<Color>&) {
    require_mutable();
}

void DataGridView::written(const decltype(background_color)&) {
    invalidate(Dirty::paint);
    on_background_color_changed();
}

void DataGridView::on_background_color_changed() {}

void DataGridView::on_border_style_changed() {}

void DataGridView::on_cell_border_style_changed() {}

void DataGridView::on_column_headers_border_style_changed() {}

void DataGridView::on_row_headers_border_style_changed() {}

void DataGridView::prepare_write(const decltype(draw_mode)&, ItemDrawMode& incoming) {
    require_mutable();
    if (incoming != ItemDrawMode::normal && incoming != ItemDrawMode::owner_draw) throw std::invalid_argument("DataGridView draw mode must be normal or owner_draw");
}

void DataGridView::written(const decltype(draw_mode)&) {
    invalidate(Dirty::paint);
}

void DataGridView::on_column_headers_height_changed() {}

void DataGridView::on_row_headers_width_changed() {}

void DataGridView::on_font_changed() {}

void DataGridView::on_read_only_changed() {}

void DataGridView::prepare_write(const decltype(allow_user_to_resize_columns)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(allow_user_to_resize_columns)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    on_allow_user_to_resize_columns_changed();
}

void DataGridView::on_allow_user_to_resize_columns_changed() {}

void DataGridView::prepare_write(const decltype(allow_user_to_resize_rows)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(allow_user_to_resize_rows)&) {
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    on_allow_user_to_resize_rows_changed();
}

void DataGridView::on_allow_user_to_resize_rows_changed() {}

void DataGridView::on_allow_user_to_order_columns_changed() {}

void DataGridView::on_multi_select_changed() {}

void DataGridView::prepare_write(const decltype(edit_mode)&, DataGridViewEditMode& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(DataGridViewEditMode::edit_programmatically)) throw std::invalid_argument("DataGridView edit mode is outside the enumeration");
}

void DataGridView::written(const decltype(edit_mode)&) {
    on_edit_mode_changed();
}

void DataGridView::on_edit_mode_changed() {}

void DataGridView::prepare_write(const decltype(auto_generate_columns)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(auto_generate_columns)&) {}

void DataGridView::prepare_write(const decltype(standard_tab)&, bool&) {
    require_mutable();
}

void DataGridView::written(const decltype(standard_tab)&) {}

// property_codemod end DataGridView

} // namespace gui_forms
