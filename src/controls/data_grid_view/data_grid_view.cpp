#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/window/window.hpp"

#include "../panel/input_control_utilities.hpp"
#include "data_grid_view_bound_index.hpp"
#include "../scrollable_control/scrolling_utilities.hpp"

#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/binding/number_format/number_format.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

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
// doubles, text and other kinds by their text in byte order.
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
    cursor = CursorKind::arrow;
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

// The caller guarantees `row` is below row_count().
std::string DataGridView::row_id_text(DataGridViewRowPosition row) const {
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
    const std::optional<std::size_t> bound = (*data_source_).column_of(field);
    if (!bound) return !(*data_source_).table();
    return (*data_source_).read_only(row.value, *bound);
}

DataGridViewRowId DataGridView::row_id(DataGridViewRowPosition position) const {
    if (position.value >= row_count()) throw std::out_of_range("DataGridView row is past the end");
    const DataGridViewRowId identity{row_id_text(position)};
    return identity;
}

std::optional<DataGridViewRowPosition> DataGridView::row_position(const DataGridViewRowId& row) const {
    const std::optional<std::size_t> found = find_row(row.value);
    if (!found) return std::nullopt;
    const DataGridViewRowPosition position{*found};
    return position;
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
        const std::optional<NumberFormat> format = parse_number_format(*style.format);
        if (format) {
            std::string text{};
            if (const std::int64_t* const signed_value = std::get_if<std::int64_t>(&value)) {
                append_formatted_number(text, *signed_value, *format);
            } else if (const std::uint64_t* const unsigned_value = std::get_if<std::uint64_t>(&value)) {
                append_formatted_number(text, *unsigned_value, *format);
            } else {
                append_formatted_number(text, std::get<double>(value), *format);
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
    if (!style.font) style.font = from.font;
    if (!style.alignment) style.alignment = from.alignment;
    if (!style.padding) style.padding = from.padding;
    if (!style.wrap_mode) style.wrap_mode = from.wrap_mode;
    if (!style.format) style.format = from.format;
    if (!style.null_value) style.null_value = from.null_value;
    if (!style.data_source_null_value) style.data_source_null_value = from.data_source_null_value;
    if (!style.back_color) style.back_color = from.back_color;
    if (!style.fore_color) style.fore_color = from.fore_color;
    if (!style.selection_back_color) style.selection_back_color = from.selection_back_color;
    if (!style.selection_fore_color) style.selection_fore_color = from.selection_fore_color;
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
    if (sorted_column_ && *sorted_column_ < columns_.size() &&
        column_sort_mode(*sorted_column_) == DataGridViewColumnSortMode::automatic) {
        columns_[*sorted_column_].sort_glyph = DataGridViewSortDirection::none;
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

std::optional<std::size_t> DataGridView::find_row(std::string_view row_id) const {
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        const std::optional<std::size_t> position = virtual_order_.position_of(row_id);
        if (position) return position;
        // The new row is last.
        if (new_row_shown() && row_id == new_row_.stable_id.value) return data_row_count();
        return std::nullopt;
    }
    std::optional<std::size_t> indexed{};
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (bound_index_) indexed = (*bound_index_).find(row_id);
    } else {
        const RowPositions::const_iterator found = row_positions_.find(row_id);
        if (found != row_positions_.end()) indexed = (*found).second;
    }
    if (!indexed) {
        // The new row is last; a data row that took its ID is found first.
        if (new_row_shown() && row_id == new_row_.stable_id.value) return data_row_count();
        return std::nullopt;
    }
    const std::size_t position = *indexed;
    // A binding source that changed without raising list_changed leaves the
    // index behind its records; a position now holding another row is not
    // this row.
    if (position >= data_row_count()) return std::nullopt;
    const DataGridViewRowPosition row{position};
    if (row_id_text(row) != row_id) return std::nullopt;
    return position;
}

DataGridViewRowPosition DataGridView::event_position(std::string_view row_id) const {
    DataGridViewRowPosition position{};
    if (row_source_ == DataGridViewRowSource::virtual_rows) {
        position.value = virtual_order_.held_position(row_id).value_or(0U);
        if (new_row_shown() && row_id == new_row_.stable_id.value) position.value = data_row_count();
        return position;
    }
    std::optional<std::size_t> indexed{};
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (bound_index_) indexed = (*bound_index_).find(row_id);
    } else {
        const RowPositions::const_iterator found = row_positions_.find(row_id);
        if (found != row_positions_.end()) indexed = (*found).second;
    }
    if (indexed) {
        position.value = *indexed;
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

// The caller guarantees `cell.row` is below row_count().
DataGridView::CellKey DataGridView::key_of(DataGridViewCellAddress cell) const {
    const CellKey key{row_id_text(cell.row), cell.column};
    return key;
}

std::optional<DataGridViewCellAddress> DataGridView::address_of(const CellKey& cell) const {
    const std::optional<std::size_t> position = find_row(cell.row_id);
    if (!position) return std::nullopt;
    const DataGridViewCellAddress address{DataGridViewRowPosition{*position}, cell.column};
    return address;
}

std::optional<std::size_t> DataGridView::current_position() const {
    if (!current_) return std::nullopt;
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

void DataGridView::drop_missing_rows(std::optional<std::size_t> former_current) {
    const std::uint64_t before = selection_revision_;
    RowIdSet::iterator row = selected_rows_.begin();
    while (row != selected_rows_.end()) {
        if (find_row(*row)) {
            ++row;
            continue;
        }
        row = selected_rows_.erase(row);
        ++selection_revision_;
    }
    SelectedCells::iterator cells = selected_cells_.begin();
    while (cells != selected_cells_.end()) {
        if (find_row((*cells).first)) {
            ++cells;
            continue;
        }
        cells = selected_cells_.erase(cells);
        ++selection_revision_;
    }
    if (anchor_ && !find_row((*anchor_).row_id)) anchor_.reset();
    if (pressed_content_ && !find_row((*pressed_content_).row_id)) pressed_content_.reset();
    SelectedCells::iterator visited = visited_links_.begin();
    while (visited != visited_links_.end()) {
        if (find_row((*visited).first)) {
            ++visited;
            continue;
        }
        visited = visited_links_.erase(visited);
    }
    RowHeightsById::iterator height = own_row_heights_.begin();
    while (height != own_row_heights_.end()) {
        if (find_row((*height).first)) {
            ++height;
            continue;
        }
        height = own_row_heights_.erase(height);
    }
    CellErrorRows::iterator errors = cell_errors_.begin();
    while (errors != cell_errors_.end()) {
        if (find_row((*errors).first)) {
            ++errors;
            continue;
        }
        errors = cell_errors_.erase(errors);
    }
    RowErrors::iterator row_error = row_errors_.begin();
    while (row_error != row_errors_.end()) {
        if (find_row((*row_error).first)) {
            ++row_error;
            continue;
        }
        row_error = row_errors_.erase(row_error);
    }
    CellStyleRows::iterator styles = cell_styles_.begin();
    while (styles != cell_styles_.end()) {
        if (find_row((*styles).first)) {
            ++styles;
            continue;
        }
        styles = cell_styles_.erase(styles);
    }
    RowStyles::iterator row_style = row_styles_.begin();
    while (row_style != row_styles_.end()) {
        if (find_row((*row_style).first)) {
            ++row_style;
            continue;
        }
        row_style = row_styles_.erase(row_style);
    }
    if (added_row_ && !find_row(*added_row_)) added_row_.reset();
    const std::size_t count = row_count();
    if (first_row_ >= count) first_row_ = 0U;
    if (current_ && !find_row((*current_).row_id)) {
        // The current row is gone: the row now at its position, or the new
        // last row, becomes current.
        std::optional<CellKey> next{};
        if (count > 0U && former_current) {
            const DataGridViewRowPosition replacement{std::min(*former_current, count - 1U)};
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
        !validate_utf8(column.data_property_name).valid()) {
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
}

void DataGridView::add_column(DataGridViewColumn column) {
    require_mutable();
    require_not_sorting();
    validate_column(column);
    if (column_index(column.name)) {
        throw std::invalid_argument("DataGridView column names must be unique");
    }
    columns_.push_back(std::move(column));
    append_display_column();
    mark_all_rows(*this);
    for (DataGridViewRow& row : row_elements(*this)) row.cells.resize(columns_.size());
    new_row_.cells.resize(columns_.size());
    structure_changed();
}

void DataGridView::remove_column_at(std::size_t column) {
    require_mutable();
    require_not_sorting();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    reset_row_state();
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
}

void DataGridView::insert_column(std::size_t column, DataGridViewColumn inserted) {
    require_mutable();
    require_not_sorting();
    if (column > columns_.size()) throw std::out_of_range("DataGridView column insertion is past the end");
    validate_column(inserted);
    if (column_index(inserted.name)) {
        throw std::invalid_argument("DataGridView column names must be unique");
    }
    // Validated: from here nothing refuses. The row state is keyed by column
    // index, which the insertion shifts.
    reset_row_state();
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
    if (!ordering_ && !order_target_) return;
    ordering_ = false;
    order_target_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

std::optional<std::size_t> DataGridView::column_index(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < columns_.size(); ++index) {
        if (columns_[index].name == name) return index;
    }
    return std::nullopt;
}

void DataGridView::set_column_width(std::size_t column, double width) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (!std::isfinite(width) || width < columns_[column].minimum_width) {
        throw std::invalid_argument("DataGridView column width must be finite and at least the minimum");
    }
    if (columns_[column].width == width) return;
    columns_[column].width = width;
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

void DataGridView::set_column_visible(std::size_t column, bool column_visible) {
    require_mutable();
    if (column >= columns_.size()) throw std::out_of_range("DataGridView column is past the end");
    if (columns_[column].visible == column_visible) return;
    const bool current_hidden = !column_visible && current_ && (*current_).column == column;
    if (current_hidden && editing_) cancel_edit();
    columns_[column].visible = column_visible;
    refresh_shown_order();
    structure_changed();
    if (!current_hidden || !is_alive()) return;
    // The current cell leaves the hidden column for a shown one in its row:
    // the next in display order, else the one before.
    const std::optional<DataGridViewCellAddress> current = current_cell();
    if (!current) return;
    if (shown_order_.empty()) {
        if (!validate_row_change(std::nullopt)) return;
        anchor_.reset();
        apply_current(std::nullopt, current_.has_value());
        return;
    }
    const std::size_t next = shown_indices_[column];
    const std::size_t shown = next < shown_order_.size() ? next : shown_order_.size() - 1U;
    const DataGridViewCellAddress target{(*current).row, shown_order_[shown]};
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

void DataGridView::clear_rows() {
    require_mutable();
    require_not_sorting();
    reset_row_state();
    detach_data_source();
    provider_.reset();
    virtual_order_.clear();
    window_source_.reset();
    rows.clear();
    row_positions_.clear();
    row_source_ = DataGridViewRowSource::unbound;
    rows_changed();
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
    reset_row_state();
    detach_data_source();
    rows.clear();
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
}

void DataGridView::set_virtual_rows(std::shared_ptr<DataGridViewCellValueProvider> provider,
                                    std::shared_ptr<DataGridViewRowWindowSource> source) {
    require_mutable();
    require_not_sorting();
    if (!provider) throw std::invalid_argument("DataGridView virtual rows require a provider");
    if (!source) throw std::invalid_argument("DataGridView virtual rows require a row window source");
    reset_row_state();
    detach_data_source();
    rows.clear();
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
}

void DataGridView::set_virtual_row_order(std::vector<DataGridViewRowId> order) {
    require_mutable();
    require_virtual_rows("DataGridView row order is set only on virtual rows");
    validate_virtual_order(order);
    // The current row's position in the old order, for the case that it is
    // gone from the new one.
    const std::optional<std::size_t> former =
        current_ ? virtual_order_.held_position((*current_).row_id) : std::optional<std::size_t>{};
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
        const std::optional<std::size_t> found = virtual_order_.position_of(*top);
        if (found) first = *found;
    }
    first_row_ = first;
    drop_missing_rows(former);
    rows_changed();
    update_row_order_mode(DataGridViewRowOrderSwitchReason::source);
}

std::optional<std::string> DataGridView::held_top_row() const {
    if (first_row_ >= virtual_order_.count()) return std::nullopt;
    std::optional<std::string> top = virtual_order_.held_id_at(first_row_);
    return top;
}

void DataGridView::after_reported_change(const std::optional<std::string>& top, std::size_t changed) {
    const std::size_t count = virtual_order_.count();
    if (top) {
        const std::optional<std::size_t> found = virtual_order_.position_of(*top);
        if (found) first_row_ = *found;
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
    const std::optional<std::size_t> former =
        current_ ? virtual_order_.held_position((*current_).row_id) : std::optional<std::size_t>{};
    bool current_removed = false;
    for (const DataGridViewRowId& row : ids) {
        if (current_ && row.value == (*current_).row_id) current_removed = true;
    }
    if (editing_ && current_removed) cancel_edit();
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
        visited_links_.erase(row.value);
        own_row_heights_.erase(row.value);
    }
    if (current_removed) {
        const std::size_t count = virtual_order_.count();
        std::optional<CellKey> next{};
        if (count > 0U && former) {
            const DataGridViewRowPosition replacement{std::min(*former, count - 1U)};
            next = CellKey{row_id_text(replacement), (*current_).column};
        }
        current_.reset();
        apply_current(std::move(next));
    }
    publish_selection(before);
    after_reported_change(top, ids.size());
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
    reset_row_state();
    detach_data_source();
    provider_.reset();
    virtual_order_.clear();
    window_source_.reset();
    rows.clear();
    row_positions_.clear();
    if (!source) {
        row_source_ = DataGridViewRowSource::unbound;
        rows_changed();
        return;
    }
    data_source_ = std::move(source);
    row_source_ = DataGridViewRowSource::binding_source;
    rebuild_row_index();
    source_list_changed_ = (*data_source_).list_changed().subscribe(
        *this, Delegate<const BindingListChange&>::bind<DataGridView, &DataGridView::on_source_list_changed>(*this));
    source_position_changed_ = (*data_source_).position_changed().subscribe(
        *this, Delegate<std::ptrdiff_t>::bind<DataGridView, &DataGridView::on_source_position_changed>(*this));
    rows_changed();
    on_source_position_changed((*data_source_).position());
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
    if (!bound_index_) rebuild_row_index();
    // The source has changed already; the index has not, so it still holds
    // the current row's position before this change. The changes of one
    // notification come one by one, each relative to the list the ones
    // before it left (BindingListChange), which is the order the index
    // follows them in.
    std::optional<std::size_t> former{};
    if (current_) former = (*bound_index_).find((*current_).row_id);
    const bool current_removed = removed && former && change.index >= 0 &&
                                 *former == static_cast<std::size_t>(change.index);
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
        const std::optional<std::size_t> held = bound_removed_current_ ? bound_removed_current_ : former;
        bound_removed_current_.reset();
        drop_missing_rows(held);
    }
    rows_changed();
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
    sorted_column_ = column;
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
}

void DataGridView::require_not_sorting() const {
    if (sorting_) {
        throw std::logic_error("DataGridView's rows, columns and values do not change while it sorts");
    }
}

DataGridViewColumnSortMode DataGridView::column_sort_mode(std::size_t column) const {
    require_column(column);
    const DataGridViewColumn& definition = columns_[column];
    if (definition.sort_mode) return *definition.sort_mode;
    if (definition.kind == DataGridViewColumnKind::text) return DataGridViewColumnSortMode::automatic;
    return DataGridViewColumnSortMode::not_sortable;
}

void DataGridView::set_column_sort_mode(std::size_t column, std::optional<DataGridViewColumnSortMode> mode) {
    require_mutable();
    require_column(column);
    if (mode && *mode != DataGridViewColumnSortMode::not_sortable && *mode != DataGridViewColumnSortMode::automatic &&
        *mode != DataGridViewColumnSortMode::programmatic) {
        throw std::invalid_argument("DataGridView column sort mode is outside the enumeration");
    }
    columns_[column].sort_mode = mode;
}

void DataGridView::sort_from_header(std::size_t column) {
    // A column_header_click handler may have changed the grid.
    if (!is_alive() || column >= columns_.size() || row_source_ != DataGridViewRowSource::unbound) return;
    if (column_sort_mode(column) != DataGridViewColumnSortMode::automatic) return;
    DataGridViewSortDirection direction = DataGridViewSortDirection::ascending;
    if (sorted_column_ == column && sort_order_ == DataGridViewSortDirection::ascending) {
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
    structure_changed();
    on_column_headers_height_changed();
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
    first_row_ = next;
    // The whole grid moves: a redraw-all, folding the cell region.
    redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::set_horizontal_scroll_offset(double offset) {
    require_mutable();
    if (!std::isfinite(offset) || offset < 0.0) {
        throw std::invalid_argument("DataGridView horizontal offset must be finite and not negative");
    }
    if (horizontal_offset_ == offset) return;
    horizontal_offset_ = offset;
    // The whole grid moves: a redraw-all, folding the cell region.
    redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::on_vertical_scroll(double value) {
    if (synchronizing_scroll_) return;
    const double rounded = std::max(0.0, std::round(value));
    first_row_ = static_cast<std::size_t>(rounded);
    // The whole grid moves: a redraw-all, folding the cell region.
    redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::on_horizontal_scroll(double value) {
    if (synchronizing_scroll_) return;
    horizontal_offset_ = std::max(0.0, value);
    // The whole grid moves: a redraw-all, folding the cell region.
    redraw_all(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::scroll_into_view(DataGridViewCellAddress cell) {
    const GridLayout layout = grid_layout();
    if (cell.row.value < first_row_) {
        first_row_ = cell.row.value;
    } else if (cell.row.value < row_count()) {
        first_row_ = std::max(first_row_, first_row_ending_at(cell.row.value, layout));
    }
    // Columns in logical units at text scale 1, in display order.
    double left = 0.0;
    const std::size_t display = cell.column < shown_indices_.size() ? shown_indices_[cell.column] : 0U;
    for (std::size_t position = 0; position < display; ++position) {
        left += shown_width(shown_order_[position]);
    }
    const double right = left + shown_width(cell.column);
    const double viewport = layout.data.width / layout.scale;
    if (left < horizontal_offset_) {
        horizontal_offset_ = left;
    } else if (right > horizontal_offset_ + viewport) {
        horizontal_offset_ = std::min(left, right - viewport);
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void DataGridView::arrange(Rect final_bounds) {
    const std::size_t first_before = first_row_;
    const double offset_before = horizontal_offset_;
    arrange_self(final_bounds);
    // Automatic widths first: everything below reads the columns' widths;
    // then automatic heights, which wrapped cells take from the widths.
    apply_auto_sizes();
    apply_row_auto_sizes();
    const GridLayout layout = grid_layout();
    first_row_ = std::min(first_row_, maximum_first_row(layout));
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
    if (vertical_bar_ && last_first > 0U) {
        VScrollBar& bar = *vertical_bar_;
        bar.set_range(0.0, static_cast<double>(last_first));
        bar.small_change = 1.0;
        bar.large_change = static_cast<double>(std::max<std::size_t>(1U, fully_visible_rows(layout)));
        bar.value = static_cast<double>(first_row_);
    }
    const double last_offset = maximum_horizontal_offset(layout);
    if (horizontal_bar_ && last_offset > 0.0) {
        HScrollBar& bar = *horizontal_bar_;
        bar.set_range(0.0, last_offset);
        bar.small_change = 16.0;
        bar.large_change = std::max(1.0, layout.data.width / layout.scale);
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
    if (selected_cells_.empty() && selected_rows_.empty()) return;
    selected_cells_.clear();
    selected_rows_.clear();
    ++selection_revision_;
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
    if (!find_row(row_id)) return true;
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
        const std::optional<std::size_t> position = find_row((*current_).row_id);
        if (position && *position < data_row_count()) {
            synchronizing_position_ = true;
            static_cast<void>((*data_source_).set_position(static_cast<std::ptrdiff_t>(*position)));
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
    const std::optional<DataGridViewCellAddress> placed = address_of(target_key);
    if (!placed) return false;
    const DataGridViewCellAddress at = *placed;
    const std::uint64_t before = selection_revision_;
    const bool full_row = selection_mode.read() == DataGridViewSelectionMode::full_row_select;
    if (gesture == SelectionGesture::extend && multi_select.read()) {
        // The range runs from the anchor, else the current cell, else the
        // target, at its row's present position.
        DataGridViewCellAddress from = at;
        std::optional<DataGridViewCellAddress> origin{};
        if (anchor_) origin = address_of(*anchor_);
        if (!origin && current_) origin = address_of(*current_);
        if (origin) from = *origin;
        clear_selection_state();
        select_range(from, at);
        anchor_ = key_of(from);
    } else if (gesture == SelectionGesture::toggle && multi_select.read()) {
        if (full_row) {
            if (row_in_selection(target_key.row_id)) erase_selected_row(target_key.row_id);
            else insert_selected_row(target_key.row_id);
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
        if (full_row) insert_selected_row(target_key.row_id);
        else insert_selected_cell(target_key.row_id, target_key.column);
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
        const std::size_t selected = selected_cell_count() + selected_rows_.size();
        if (selected > 1U) select_current_alone();
        publish_selection(before);
        if (!is_alive()) return;
    }
    on_multi_select_changed();
}

std::optional<DataGridViewCellAddress> DataGridView::current_cell() const {
    if (!current_) return std::nullopt;
    return address_of(*current_);
}

std::optional<DataGridViewRowId> DataGridView::current_row_id() const {
    if (!current_ || !find_row((*current_).row_id)) return std::nullopt;
    const DataGridViewRowId identity{(*current_).row_id};
    return identity;
}

bool DataGridView::set_current_cell(std::optional<DataGridViewCellAddress> cell) {
    require_mutable();
    require_settled_current();
    if (cell) require_cell((*cell).row, (*cell).column);
    if (cell && !columns_[(*cell).column].visible) {
        throw std::logic_error("DataGridView current cell cannot be in a hidden column");
    }
    if (!cell) {
        if (editing_ && !end_edit()) return false;
        if (!validate_row_change(std::nullopt)) return false;
        const bool validated = current_.has_value();
        anchor_.reset();
        apply_current(std::nullopt, validated);
        return true;
    }
    const bool moved = move_current(*cell, SelectionGesture::replace);
    return moved;
}

bool DataGridView::is_cell_selected(DataGridViewRowPosition row, std::size_t column) const {
    if (row.value >= row_count()) return false;
    const std::string row_id = row_id_text(row);
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) return row_in_selection(row_id);
    return cell_in_selection(row_id, column);
}

bool DataGridView::is_row_selected(DataGridViewRowPosition row) const {
    if (row.value >= row_count()) return false;
    const std::string row_id = row_id_text(row);
    if (selection_mode.read() == DataGridViewSelectionMode::full_row_select) return row_in_selection(row_id);
    if (columns_.empty()) return false;
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

std::vector<DataGridViewCellAddress> DataGridView::selected_cells() const {
    std::vector<DataGridViewCellAddress> cells{};
    if (selection_mode.read() == DataGridViewSelectionMode::cell_select) {
        cells.reserve(selected_cell_count());
        for (const SelectedCells::value_type& entry : selected_cells_) {
            const std::optional<std::size_t> position = find_row(entry.first);
            if (!position) continue;
            const DataGridViewRowPosition row{*position};
            for (const std::size_t column : entry.second) cells.push_back({row, column});
        }
        std::sort(cells.begin(), cells.end(), CellAddressOrder{});
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
            const std::optional<std::size_t> position = find_row(row_id);
            if (position) rows.push_back(DataGridViewRowPosition{*position});
        }
    } else if (!columns_.empty()) {
        for (const SelectedCells::value_type& entry : selected_cells_) {
            if (entry.second.size() != columns_.size()) continue;
            const std::optional<std::size_t> position = find_row(entry.first);
            if (position) rows.push_back(DataGridViewRowPosition{*position});
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
    const std::optional<DataGridViewCellAddress> cell = current_cell();
    if (!cell) return false;
    const DataGridViewColumnKind kind = columns_[(*cell).column].kind;
    if (kind == DataGridViewColumnKind::combo_box) return open_choices();
    if (kind != DataGridViewColumnKind::text) return false;
    const bool begun = begin_text_edit(cell_text((*cell).row, (*cell).column), select_all);
    return begun;
}

bool DataGridView::begin_text_edit(std::string initial_text, bool select_all) {
    const std::optional<DataGridViewCellAddress> placed = current_cell();
    if (!placed || !editor_) return false;
    const DataGridViewCellAddress cell = *placed;
    if (cell_read_only(cell.row, cell.column)) return false;
    const CellKey key = *current_;
    DataGridViewCellCancelEvent request{DataGridViewRowId{key.row_id}, cell.row, cell.column, false};
    cell_begin_edit_.emit(request);
    // A handler may cancel, or move the current cell or rows, or end the grid.
    if (request.cancel || !is_alive() || !current_ || !(*current_ == key)) return false;
    const std::optional<DataGridViewCellAddress> moved = current_cell();
    if (!moved) return false;
    scroll_into_view(*moved);
    editing_ = true;
    edit_kind_ = EditKind::text;
    edit_invalid_ = false;
    TextBox& editor = *editor_;
    // Typed text that differs from the cell's makes the edit dirty at once.
    const bool dirty = initial_text != cell_text((*moved).row, (*moved).column);
    writing_editor_ = true;
    editor.text = std::move(initial_text);
    writing_editor_ = false;
    set_child_layout_collapsed(editor_, false);
    const Parts editor_parts = grid_parts(grid_layout());
    set_child_layout(editor_, editor_parts.editor.bounds);
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
        const std::optional<DataGridViewCellAddress> at = address_of(key);
        stored = at && store_value((*at).row, key.column, value);
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
    const std::optional<DataGridViewCellAddress> placed = address_of(key);
    if (!placed) return CommitOutcome::discarded;
    at = *placed;
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
            const std::optional<NumberFormat> format = parse_number_format(*style.format);
            std::optional<std::string> unformatted{};
            if (format) unformatted = plain_number_text(text, *format);
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
    const std::optional<DataGridViewCellAddress> placed = address_of(key);
    if (!placed) return false;
    const std::string text((*editor_).text.read());
    const CommitOutcome outcome = commit_text(key, *placed, text);
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
    const std::optional<DataGridViewCellAddress> placed = current_cell();
    if (!placed) return false;
    writing_editor_ = true;
    (*editor_).text = cell_text((*placed).row, (*placed).column);
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
    const std::optional<DataGridViewCellAddress> placed = address_of(key);
    if (!placed) {
        // The edited row is gone without its edit being discarded.
        cancel_edit();
        return true;
    }
    const std::string text((*editor_).text.read());
    DataGridViewCellValidatingEvent validating{DataGridViewRowId{key.row_id}, (*placed).row, key.column, text,
                                               false};
    cell_validating_.emit(validating);
    if (!is_alive() || !editing_) return !editing_;
    if (validating.cancel) {
        edit_invalid_ = true;
        values_changed();
        return false;
    }
    // A handler may have moved rows; the edit stays on its row's ID.
    const std::optional<DataGridViewCellAddress> at = address_of(key);
    if (!at) {
        cancel_edit();
        return true;
    }
    const CommitOutcome outcome = commit_text(key, *at, text);
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
    const std::optional<DataGridViewCellAddress> placed = current_cell();
    if (!placed) return false;
    const DataGridViewCellAddress cell = *placed;
    const CellKey key = *current_;
    const DataGridViewColumn& column = columns_[cell.column];
    if (column.kind != DataGridViewColumnKind::combo_box || column.items.empty() ||
        cell_read_only(cell.row, cell.column)) {
        return false;
    }
    DataGridViewCellCancelEvent request{DataGridViewRowId{key.row_id}, cell.row, cell.column, false};
    cell_begin_edit_.emit(request);
    if (request.cancel || !is_alive() || !current_ || !(*current_ == key)) return false;
    const std::optional<DataGridViewCellAddress> moved = current_cell();
    if (!moved) return false;
    const DataGridViewCellAddress at = *moved;
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
    const std::optional<DataGridViewCellAddress> placed = current_cell();
    if (!placed) return;
    const DataGridViewCellAddress cell = *placed;
    const CellKey key = *current_;
    const std::string prefix = std::string(stable_id().value()) + ".choice.";
    if (invocation.item_id.compare(0U, prefix.size(), prefix) != 0) return;
    const std::vector<std::string>& items = columns_[cell.column].items;
    std::optional<std::size_t> chosen{};
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (invocation.item_id == prefix + std::to_string(index)) {
            chosen = index;
            break;
        }
    }
    if (!chosen) return;
    const BindingValue value{items[*chosen]};
    const BindingValue previous = stored_value(cell.row, cell.column);
    if (previous == value || !store_user_value(cell, value, items[*chosen]).stored) return;
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

bool DataGridView::toggle_check(DataGridViewCellAddress cell) {
    if (columns_[cell.column].kind != DataGridViewColumnKind::check_box ||
        cell_read_only(cell.row, cell.column)) {
        return false;
    }
    const bool checked = binding_value_to_bool(stored_value(cell.row, cell.column)).value_or(false);
    const BindingValue value{!checked};
    const CellKey key = key_of(cell);
    if (!store_user_value(cell, value, checked ? "false" : "true").stored) return false;
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
    const std::optional<DataGridViewCellAddress> current = current_cell();
    if (!current) return false;
    const DataGridViewCellAddress cell = *current;
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

std::optional<DataGridViewRowPosition> DataGridView::new_row_index() const noexcept {
    if (!new_row_shown()) return std::nullopt;
    const DataGridViewRowPosition position{data_row_count()};
    return position;
}

bool DataGridView::read_view(const decltype(allow_user_to_add_rows)&) const {
    return allow_add_rows_;
}

void DataGridView::write_view(const decltype(allow_user_to_add_rows)&, bool allow) {
    require_mutable();
    if (allow_add_rows_ == allow) return;
    const std::optional<std::size_t> former = current_position();
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
    if (row_source_ == DataGridViewRowSource::binding_source) {
        // The row takes the new row's ID and its values, field by field, in
        // the source's column order. The built-in table grows a field it
        // has not got first, as a record took any field.
        const std::shared_ptr<BindingSource> source = data_source_;
        const std::shared_ptr<BindingTable> built_in = (*source).table();
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            const std::string& field = columns_[column].data_property_name;
            if (field.empty() || binding_value_kind(values[column]) == BindingValueKind::null) continue;
            if (built_in && !(*source).column_of(field)) static_cast<void>((*built_in).find_or_add_column(field));
        }
        if (!is_alive() || !(*source).is_alive()) return false;
        BindingRow added{};
        added.stable_id = row_id;
        added.values.resize((*source).schema().size());
        for (std::size_t column = 0; column < columns_.size(); ++column) {
            const std::string& field = columns_[column].data_property_name;
            if (field.empty() || binding_value_kind(values[column]) == BindingValueKind::null) continue;
            const std::optional<std::size_t> bound = (*source).column_of(field);
            if (bound) added.values[*bound] = values[column];
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
            rows.append(std::move(row));
        } catch (...) {
            new_row_.stable_id.value = row_id;
            throw;
        }
    }
    // A new row appears after the row the user added, which keeps the ID the
    // current cell and the selection hold.
    new_row_.stable_id.value = generated_row_id();
    new_row_.cells.clear();
    new_row_.cells.resize(columns_.size());
    added_row_ = row_id;
    rows_changed();
    const DataGridViewRowEvent added{DataGridViewRowId{row_id}, position};
    user_added_row_.emit(added);
    const bool present = is_alive() && find_row(row_id).has_value();
    return present;
}

void DataGridView::remove_row_by_id(const std::string& row_id) {
    const std::optional<std::size_t> position = find_row(row_id);
    if (!position || is_new_row(DataGridViewRowPosition{*position})) return;
    if (row_source_ == DataGridViewRowSource::binding_source) {
        if (current_ && (*current_).row_id == row_id && editing_) cancel_edit();
        static_cast<void>((*data_source_).remove_at(*position));
        return;
    }
    if (row_source_ != DataGridViewRowSource::unbound) {
        throw std::logic_error("DataGridView rows are removed only from unbound rows");
    }
    rows.erase(*position, 1U);
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
        if (!find_row(row_id)) continue;
        const DataGridViewRowEvent target = row_event(row_id);
        DataGridViewRowCancelEvent deleting{target.row_id, target.row, false};
        user_deleting_row_.emit(deleting);
        if (!is_alive()) return true;
        if (deleting.cancel || !find_row(row_id)) continue;
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
    const std::optional<std::size_t> bound = (*data_source_).column_of(field);
    if (!bound) return {};
    return (*data_source_).error(row.value, *bound);
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
    if (style.alignment && !valid_alignment(*style.alignment)) {
        throw std::invalid_argument("DataGridView cell style alignment is outside the enumeration");
    }
    if (style.padding) {
        const Insets& padding = *style.padding;
        const bool finite = std::isfinite(padding.left) && std::isfinite(padding.top) &&
                            std::isfinite(padding.right) && std::isfinite(padding.bottom);
        if (!finite || padding.left < 0.0 || padding.top < 0.0 || padding.right < 0.0 || padding.bottom < 0.0) {
            throw std::invalid_argument("DataGridView cell style padding must be finite and not negative");
        }
    }
    if (style.format && !(*style.format).empty() && !parse_number_format(*style.format)) {
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
    const GridLayout layout = grid_layout();
    if (resizing_column_ && !has_pointer_capture()) resizing_column_.reset();
    if (resizing_row_ && !has_pointer_capture() && event.action != PointerAction::up) resizing_row_.reset();
    if (resizing_row_) {
        if (event.action == PointerAction::move && *resizing_row_ < row_count()) {
            const double delta = (local.y - resize_start_y_) / layout.scale;
            const double height = std::clamp(resize_start_height_ + delta, 4.0, 4096.0);
            set_row_height_at(DataGridViewRowPosition{*resizing_row_}, height);
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
    if (resizing_column_) {
        if (event.action == PointerAction::move) {
            const DataGridViewColumn& column = columns_[*resizing_column_];
            // Right to left the divider is at the column's left edge, so
            // dragging it left widens the column.
            const double moved = layout.mirrored ? resize_start_x_ - local.x : local.x - resize_start_x_;
            const double delta = moved / layout.scale;
            const double width = std::max(column.minimum_width, resize_start_width_ + delta);
            set_column_width(*resizing_column_, width);
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
        if (hot_column_) {
            hot_column_.reset();
            invalidate(Dirty::paint);
        }
        if (cursor.read() != CursorKind::arrow) cursor = CursorKind::arrow;
        return;
    }
    const HitResult hit = hit_test(local);
    if (event.action == PointerAction::move && pressed_column_ && allow_user_to_order_columns.read() && has_pointer_capture()) {
        // A header press becomes a drag once the pointer moves 4 units.
        const double moved = std::abs(local.x - order_start_x_) / layout.scale;
        if (!ordering_ && moved >= 4.0) ordering_ = true;
        if (ordering_) {
            const DataGridViewDisplayIndex target = order_target_at(local.x, layout);
            if (order_target_ != target) {
                order_target_ = target;
                invalidate(Dirty::paint | Dirty::semantics);
            }
        }
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        std::optional<std::size_t> hot{};
        if (hit.zone == HitZone::column_header || hit.zone == HitZone::column_divider) hot = hit.column;
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
        if (hit.zone == HitZone::row_divider) wanted = CursorKind::resize_vertical;
        if (hit.zone == HitZone::column_divider && column_resizable(hit.column)) {
            wanted = CursorKind::resize_horizontal;
        }
        if (cursor.read() != wanted) cursor = wanted;
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
            const double moved = std::round(static_cast<double>(first_row_) - rows);
            const double last = static_cast<double>(maximum_first_row(layout));
            const std::size_t next = static_cast<std::size_t>(std::clamp(moved, 0.0, last));
            if (next != first_row_) set_first_displayed_row(DataGridViewRowPosition{next});
        }
        event.handled = true;
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        event.handled = true;
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
        const bool shift = has_modifier(event.modifiers, Modifier::shift);
        const bool control = has_modifier(event.modifiers, Modifier::control);
        SelectionGesture gesture = SelectionGesture::replace;
        if (shift) gesture = SelectionGesture::extend;
        else if (control) gesture = SelectionGesture::toggle;
        if (hit.zone == HitZone::column_divider && column_resizable(hit.column)) {
            resizing_column_ = hit.column;
            resize_start_x_ = local.x;
            resize_start_width_ = columns_[hit.column].width;
            // Read again: the focus change above ran the program's handlers.
            Window* const capture_host = window();
            if (capture_host != nullptr) (*capture_host).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
            return;
        }
        if (hit.zone == HitZone::row_divider) {
            resizing_row_ = hit.row.value;
            resize_start_y_ = local.y;
            resize_start_height_ = row_height_at(hit.row);
            Window* const capture_host = window();
            if (capture_host != nullptr) (*capture_host).capture_pointer(lifetime, event.pointer_id);
            invalidate(Dirty::paint | Dirty::semantics);
            return;
        }
        if (hit.zone == HitZone::column_header || hit.zone == HitZone::column_divider) {
            pressed_column_ = hit.column;
            if (allow_user_to_order_columns.read()) {
                // Captured, so that a drag can leave the header band.
                order_start_x_ = local.x;
                Window* const order_host = window();
                if (order_host != nullptr) (*order_host).capture_pointer(lifetime, event.pointer_id);
            }
            invalidate(Dirty::paint);
            return;
        }
        if (hit.zone == HitZone::top_left) {
            select_all();
            return;
        }
        if (hit.zone == HitZone::row_header) {
            static_cast<void>(click_row_header(hit.row, gesture));
            return;
        }
        if (hit.zone != HitZone::cell) return;
        const DataGridViewCellAddress cell{hit.row, hit.column};
        if (!move_current(cell, gesture) || gesture != SelectionGesture::replace) return;
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
        if (pressed_column_) {
            const std::size_t pressed = *pressed_column_;
            pressed_column_.reset();
            invalidate(Dirty::paint);
            // The drag is read before the capture goes: releasing it may
            // deliver a leave that ends the drag.
            const bool dragged = ordering_;
            const std::optional<DataGridViewDisplayIndex> target = order_target_;
            end_column_order();
            Window* const host = window();
            if (host != nullptr && has_pointer_capture()) (*host).release_pointer();
            if (dragged) {
                // A drag moves the column and is no header click.
                if (target && is_alive() && pressed < columns_.size()) set_column_display_index(pressed, *target);
                event.handled = true;
                return;
            }
            const bool on_header = hit.zone == HitZone::column_header || hit.zone == HitZone::column_divider;
            if (on_header && hit.column == pressed) {
                column_header_click_.emit(pressed);
                sort_from_header(pressed);
            }
            event.handled = true;
            return;
        }
        if (pressed_content_) {
            const CellKey pressed = *pressed_content_;
            pressed_content_.reset();
            invalidate(Dirty::paint | Dirty::semantics);
            const std::optional<DataGridViewCellAddress> at = address_of(pressed);
            if (at && hit.zone == HitZone::cell && hit.row == (*at).row && hit.column == (*at).column) {
                click_content(*at);
            }
            event.handled = true;
        }
    }
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
        const std::optional<DataGridViewCellAddress> cell = current_cell();
        if (!cell) return;
        const DataGridViewRowPosition next{(*cell).row.value + 1U};
        if (next.value < row_count()) static_cast<void>(move_current({next, (*cell).column}, SelectionGesture::replace));
        return;
    }
    if (key == PhysicalKey::tab) {
        event.handled = true;
        if (!end_edit()) return;
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
        const std::optional<DataGridViewCellAddress> cell = current_cell();
        if (cell && (*cell).row.value + 1U < row_count()) {
            const DataGridViewRowPosition next{(*cell).row.value + 1U};
            static_cast<void>(move_current({next, (*cell).column}, SelectionGesture::replace));
        }
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::tab) {
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

bool DataGridView::tab_key(bool backward) {
    const std::size_t count = row_count();
    const std::size_t columns = shown_order_.size();
    if (count == 0U || columns == 0U) return false;
    const std::optional<DataGridViewCellAddress> current = current_cell();
    if (!current) {
        const DataGridViewCellAddress first{DataGridViewRowPosition{0U}, shown_order_.front()};
        const bool moved = move_current(first, SelectionGesture::replace);
        return moved;
    }
    DataGridViewRowPosition row = (*current).row;
    std::size_t display = std::min(shown_indices_[(*current).column], columns - 1U);
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
    const std::optional<DataGridViewCellAddress> current = current_cell();
    const DataGridViewCellAddress from =
        current ? *current : DataGridViewCellAddress{DataGridViewRowPosition{first_row_}, shown_order_.front()};
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
    if (!find_row(row_id)) return;
    if (!validate_row_change(std::nullopt) || !is_alive()) return;
    row_validated_.emit(row_event(row_id));
}

void DataGridView::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && !current_ && row_count() > 0U && !shown_order_.empty()) {
        const DataGridViewCellAddress first{DataGridViewRowPosition{first_row_}, shown_order_.front()};
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
    if (shown_order_.empty()) return false;
    const std::size_t column = current_ ? (*current_).column : shown_order_.front();
    if (!move_current({row, column}, gesture)) return false;
    if (selection_mode.read() == DataGridViewSelectionMode::cell_select && gesture == SelectionGesture::replace) {
        const std::uint64_t before = selection_revision_;
        select_range({row, shown_order_.front()}, {row, shown_order_.back()});
        publish_selection(before);
    }
    return true;
}

std::optional<DataGridViewCellAddress> DataGridView::cell_from_stable_id(std::string_view id) const {
    // Column names hold no '.', so the last '.' separates row and column.
    const std::size_t dot = id.rfind('.');
    if (dot == std::string_view::npos) return std::nullopt;
    const std::optional<std::size_t> column = column_index(id.substr(dot + 1U));
    if (!column) return std::nullopt;
    const std::optional<std::size_t> row = find_row(id.substr(0U, dot));
    if (!row) return std::nullopt;
    const DataGridViewCellAddress cell{DataGridViewRowPosition{*row}, *column};
    return cell;
}

SemanticDescriptor DataGridView::semantic_descriptor() const {
    // A table (12.13) whose rows hold their cells, sized by every row and
    // every column, realized or not.
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::table;
    descriptor.row_count = row_count();
    descriptor.column_count = columns_.size();
    if (multi_select.read()) descriptor.states |= SemanticState::multiselectable;
    descriptor.name = accessible_name.read().empty() ? "Data grid" : accessible_name.read();
    descriptor.description = accessible_description.read();
    const std::optional<DataGridViewCellAddress> current = current_cell();
    if (current && (*current).column < columns_.size()) {
        descriptor.value = cell_text((*current).row, (*current).column);
    }
    descriptor.actions = {SemanticAction::focus};
    if (read_only.read()) descriptor.states |= SemanticState::read_only;
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> DataGridView::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const Rect absolute = absolute_bounds();
    const GridLayout layout = grid_layout();
    const bool focus_within = focused_ || editing_;
    const std::size_t first_display = first_visible_display(layout);
    const std::size_t display_end = visible_display_end(layout);
    if (column_headers_visible.read()) {
        for (std::size_t display = first_display; display < display_end; ++display) {
            const std::size_t column = shown_order_[display];
            const ColumnParts placed = column_parts(column, layout);
            SemanticNode node{};
            node.stable_id = column_stable_id(column);
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
            node.role = SemanticRole::column_header;
            node.column_index = display + 1U;
            node.name = columns_[column].header_text.empty() ? columns_[column].name : columns_[column].header_text;
            node.bounds = {absolute.x + placed.column_header.bounds.x, absolute.y + placed.column_header.bounds.y,
                           placed.column_header.bounds.width, placed.column_header.bounds.height};
            node.states = SemanticState::visible | SemanticState::enabled;
            node.actions = {SemanticAction::press};
            nodes.push_back(std::move(node));
        }
    }
    const std::size_t row_end = visible_row_end(layout);
    const std::string header_prefix = row_header_prefix();
    for (std::size_t position = std::min(first_row_, row_end); position < row_end; ++position) {
        const DataGridViewRowPosition row{position};
        SemanticNode row_node{};
        row_node.stable_id = row_id_text(row);
        row_node.runtime_id = virtual_semantic_runtime_id(row_node.stable_id);
        row_node.role = SemanticRole::row;
        row_node.row_index = position + 1U;
        const bool labelled = row_source_ == DataGridViewRowSource::unbound && position < rows.read().size() &&
                              !rows.read()[position].header_text.empty();
        row_node.name = labelled ? rows.read()[position].header_text : "Row " + std::to_string(position + 1U);
        if (is_new_row(row)) row_node.name = "New row";
        row_node.description = row_error_text(row);
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
            header.row_index = position + 1U;
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
        for (std::size_t display = first_display; display < display_end; ++display) {
            const std::size_t column = shown_order_[display];
            const DataGridViewColumn& definition = columns_[column];
            const CellParts placed = cell_parts(row, column, layout);
            SemanticNode cell{};
            cell.stable_id = cell_stable_id(row, column);
            cell.runtime_id = virtual_semantic_runtime_id(cell.stable_id);
            cell.name = definition.header_text.empty() ? definition.name : definition.header_text;
            cell.value = cell_text(row, column);
            cell.row_index = position + 1U;
            cell.column_index = display + 1U;
            cell.bounds = {absolute.x + placed.cell.bounds.x, absolute.y + placed.cell.bounds.y,
                           placed.cell.bounds.width, placed.cell.bounds.height};
            cell.states = SemanticState::visible | SemanticState::enabled | SemanticState::focusable;
            if (is_cell_selected(row, column)) cell.states |= SemanticState::selected;
            const bool current = is_current_cell(row, column);
            if (focus_within && current) cell.states |= SemanticState::focused;
            const bool cell_is_read_only = cell_read_only(row, column);
            if (cell_is_read_only) cell.states |= SemanticState::read_only;
            cell.description = cell_error_text(row, column);
            if ((current && edit_invalid_) || !cell.description.empty()) cell.states |= SemanticState::invalid;
            cell.actions = {SemanticAction::focus, SemanticAction::select};
            // Every cell is a cell, placed by its row and column; its kind's
            // behaviour stays on it as states and actions (a toggle, a value,
            // a choice, a press), from which a host offers the editing it
            // needs (owner decision, stage A gap 1).
            cell.role = SemanticRole::cell;
            if (definition.kind == DataGridViewColumnKind::check_box) {
                cell.states |= SemanticState::checkable;
                const bool checked = binding_value_to_bool(stored_value(row, column)).value_or(false);
                if (checked) cell.states |= SemanticState::checked;
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
        const std::optional<std::size_t> column = column_index(id.substr(column_prefix.size()));
        if (!column || action != SemanticAction::press) return false;
        const std::size_t pressed = *column;
        column_header_click_.emit(pressed);
        sort_from_header(pressed);
        return true;
    }
    const std::string header_prefix = row_header_prefix();
    if (id.compare(0U, header_prefix.size(), header_prefix) == 0) {
        const std::optional<std::size_t> row = find_row(id.substr(header_prefix.size()));
        if (!row || columns_.empty() || action != SemanticAction::press) return false;
        Window* const host = window();
        if (host != nullptr) static_cast<void>((*host).request_focus(shared_from_this()));
        const bool clicked = click_row_header(DataGridViewRowPosition{*row}, SelectionGesture::replace);
        return clicked;
    }
    const std::optional<DataGridViewCellAddress> cell = cell_from_stable_id(id);
    if (!cell) {
        const std::optional<std::size_t> row = find_row(id);
        if (!row || columns_.empty()) return false;
        if (action != SemanticAction::focus && action != SemanticAction::select) return false;
        Window* const host = window();
        if (action == SemanticAction::focus && host != nullptr) {
            static_cast<void>((*host).request_focus(shared_from_this()));
        }
        const std::size_t column = current_ ? (*current_).column : 0U;
        const bool moved = move_current({DataGridViewRowPosition{*row}, column}, SelectionGesture::replace);
        if (!moved) return false;
        // A row's select selects the row, as its published state reports it:
        // in cell mode moving the current cell selects only that cell.
        if (action == SemanticAction::select) set_row_selected(DataGridViewRowPosition{*row}, true);
        return true;
    }
    Window* const host = window();
    if (action == SemanticAction::focus && host != nullptr) {
        static_cast<void>((*host).request_focus(shared_from_this()));
    }
    if (!move_current(*cell, SelectionGesture::replace)) return false;
    if (action == SemanticAction::focus || action == SemanticAction::select) return true;
    const DataGridViewColumn& column = columns_[(*cell).column];
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
    const std::optional<DataGridViewCellAddress> current = current_cell();
    if (!current || action != SemanticAction::set_value || cell_read_only((*current).row, (*current).column)) {
        return false;
    }
    if (column.kind == DataGridViewColumnKind::combo_box) {
        const std::vector<std::string>::const_iterator found =
            std::find(column.items.begin(), column.items.end(), value);
        if (found == column.items.end()) return false;
        const bool stored = set_cell_value((*current).row, (*current).column, BindingValue{std::string(value)});
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
            "data_grid_view_selection_mode", {{"cell_select", 0}, {"full_row_select", 1}}, false});
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

void DataGridView::prepare_write(const decltype(grid_color)&, std::optional<Color>&) {
    require_mutable();
}

void DataGridView::written(const decltype(grid_color)&) {
    invalidate(Dirty::paint);
    on_grid_color_changed();
}

void DataGridView::on_grid_color_changed() {}

void DataGridView::prepare_write(const decltype(background_color)&, std::optional<Color>&) {
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

// property_codemod end DataGridView

} // namespace gui_forms
