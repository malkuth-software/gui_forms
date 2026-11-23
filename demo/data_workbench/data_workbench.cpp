#include "data_workbench.hpp"

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box_values.hpp"
#include "gui_forms/controls/checked_list_box.hpp"
#include "gui_forms/controls/choice_group.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view_row_order.hpp"
#include "gui_forms/controls/data_grid_view_types.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/search_field.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <stxlib/optional.hpp>
#include <utility>
#include <vector>

namespace gui_forms::data_workbench {
namespace {

// -- the generated orders -----------------------------------------------------
constexpr std::array<std::string_view, 24> customers{
    "Abbott Joinery", "Birch & Vale",   "Cobalt Freight", "Dunmore Bakery", "Eastgate Optics", "Fennick Tools",
    "Granite Labs",   "Harbour Print",  "Iverson Farms",  "Juniper Studio", "Kestrel Marine",  "Larch Dental",
    "Moss & Pike",    "Northwind Cafe", "Orchard Clinic", "Pallas Audio",   "Quarry Stone",    "Rowan Textiles",
    "Saltmarsh Inn",  "Tamsin Florist", "Umber Ceramics", "Vantage Cycles", "Wexford Paper",   "Yarrow Games"};
constexpr std::array<std::string_view, 5> regions{"North", "South", "East", "West", "Central"};
constexpr std::array<std::string_view, 12> products{"Oak shelf",    "Desk lamp",   "Office chair", "Standing desk",
                                                    "Monitor arm",  "Cable tray",  "Footrest",     "Whiteboard",
                                                    "Filing cabinet", "Bookcase",  "Coat stand",   "Plant pot"};
constexpr std::array<std::int64_t, 12> base_cents{8900, 3450, 24900, 49900, 7900, 1590,
                                                  2990, 6490, 18900, 15900, 4290, 1290};
constexpr std::array<std::string_view, column_count> column_names{"order",   "date",     "customer", "region",
                                                                  "product", "quantity", "price",    "total"};
constexpr std::array<std::string_view, column_count> column_headers{"Order",   "Date",     "Customer", "Region",
                                                                    "Product", "Qty",      "Price",    "Total"};
constexpr std::array<double, column_count> column_widths{80.0, 96.0, 170.0, 90.0, 140.0, 60.0, 90.0, 100.0};
constexpr std::array<bool, column_count> column_numeric{false, false, false, false, false, true, true, true};
constexpr std::array<std::string_view, 12> month_names{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

[[nodiscard]] std::string lower_ascii(std::string_view text) {
    std::string lowered(text);
    for (char& letter : lowered) {
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
    }
    return lowered;
}

[[nodiscard]] DateTimeValue order_date(const Order& order) {
    const DateTimeValue first{2026, 1U, 1U, 0U, 0U, 0U, 0U};
    return add_days(first, order.day_of_year);
}

[[nodiscard]] std::string two_digits(unsigned value) {
    std::string text = std::to_string(value);
    if (text.size() < 2U) text.insert(text.begin(), '0');
    return text;
}

// -- the virtual rows ---------------------------------------------------------
// Row IDs are "o<index>"; every value is computed from the index.
[[nodiscard]] std::optional<std::size_t> index_of(const DataGridViewRowId& row) {
    const std::string& text = row.value;
    if (text.size() < 2U || text.front() != 'o') return std::nullopt;
    std::size_t index = 0U;
    const std::from_chars_result parsed = std::from_chars(text.data() + 1, text.data() + text.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || index >= order_count) {
        return std::nullopt;
    }
    return index;
}

class OrderRows final : public DataGridViewCellValueProvider {
public:
    OrderRows() {
        ids_.reserve(order_count);
        for (std::size_t index = 0; index < order_count; ++index) {
            ids_.push_back(DataGridViewRowId{"o" + std::to_string(index)});
        }
    }

    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        const std::optional<std::size_t> index = index_of(row);
        if (!index.has_value()) return BindingValue{};
        return BindingValue{order_text(order_at(*index), column)};
    }

    // The orders are read only.
    bool push_cell_value(const DataGridViewRowId& row, std::size_t column, const BindingValue& value) override {
        static_cast<void>(row);
        static_cast<void>(column);
        static_cast<void>(value);
        return false;
    }

    [[nodiscard]] const DataGridViewRowId& id(std::size_t index) const& noexcept { return ids_[index]; }
    const DataGridViewRowId& id(std::size_t index) const&& = delete;

private:
    std::vector<DataGridViewRowId> ids_{};
};

// Orders by one column's value, numbers as numbers.
struct OrderByColumn final {
    std::size_t column{};
    bool descending{};

    [[nodiscard]] static std::int64_t key(const Order& order, std::size_t column) noexcept {
        if (column == 1U) return order.day_of_year;
        if (column == 5U) return order.quantity;
        if (column == 6U) return order.price_cents;
        if (column == 7U) return order.total_cents;
        return order.number;
    }

    bool operator()(std::size_t left, std::size_t right) const {
        const Order first = order_at(left);
        const Order second = order_at(right);
        bool less = false;
        if (column == 2U || column == 3U || column == 4U) {
            const std::string_view first_text = column == 2U   ? customers[first.customer]
                                                : column == 3U ? regions[first.region]
                                                               : products[first.product];
            const std::string_view second_text = column == 2U   ? customers[second.customer]
                                                 : column == 3U ? regions[second.region]
                                                                : products[second.product];
            less = descending ? second_text < first_text : first_text < second_text;
            return less;
        }
        less = descending ? key(second, column) < key(first, column) : key(first, column) < key(second, column);
        return less;
    }
};

// -- the dashboard's charts ---------------------------------------------------
struct BarChart final {
    std::vector<std::string> labels{};
    std::vector<double> values{};
    // Bars across (one row per value) instead of up.
    bool across{};
    Color colour{};
};

class BarChartPaint final {
public:
    explicit BarChartPaint(std::shared_ptr<const BarChart> chart) : chart_(std::move(chart)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        const BarChart& chart = *chart_;
        painter.fill_rect(bounds, Color::rgba(250, 251, 252));
        if (chart.values.empty()) return;
        double largest = 0.0;
        for (const double value : chart.values) largest = std::max(largest, value);
        if (largest <= 0.0) return;
        const FontSpec font{FontRole::content, 10.0, 400, false};
        const Color ink = Color::rgba(28, 39, 50);
        const std::size_t count = chart.values.size();
        if (chart.across) {
            const double label_width = 130.0;
            const double pitch = bounds.height / static_cast<double>(count);
            for (std::size_t bar = 0; bar < count; ++bar) {
                const double top = bounds.y + pitch * static_cast<double>(bar);
                const double length = (bounds.width - label_width - 90.0) * chart.values[bar] / largest;
                painter.draw_text_utf8({bounds.x + 6.0, top + pitch * 0.5 + 4.0}, chart.labels[bar], font, ink);
                painter.fill_rect({bounds.x + label_width, top + pitch * 0.2, length, pitch * 0.6}, chart.colour);
                painter.draw_text_utf8({bounds.x + label_width + length + 6.0, top + pitch * 0.5 + 4.0},
                                       money_text(static_cast<std::int64_t>(chart.values[bar])), font, ink);
            }
            return;
        }
        const double base = bounds.y + bounds.height - 22.0;
        const double pitch = bounds.width / static_cast<double>(count);
        painter.draw_line({bounds.x + 4.0, base}, {bounds.x + bounds.width - 4.0, base}, Color::rgba(166, 179, 191),
                          1.0);
        for (std::size_t bar = 0; bar < count; ++bar) {
            const double left = bounds.x + pitch * static_cast<double>(bar);
            const double height = (base - bounds.y - 18.0) * chart.values[bar] / largest;
            painter.fill_rect({left + pitch * 0.18, base - height, pitch * 0.64, height}, chart.colour);
            painter.draw_text_utf8({left + pitch * 0.18, base + 15.0}, chart.labels[bar], font, ink);
        }
    }

private:
    std::shared_ptr<const BarChart> chart_;
};

template <typename Child>
void add_at(Control& parent, const std::shared_ptr<Child>& child, Rect bounds) {
    (*child).set_requested_bounds(bounds);
    parent.add_child(child);
}

std::shared_ptr<Label> make_label(std::string id, std::string text, double size, std::uint16_t weight = 400) {
    std::shared_ptr<Label> made = make_control<Label>(StableId(std::move(id)), std::move(text));
    (*made).font = stxlib::Optional<FontSpec>::of(FontSpec{FontRole::control, size, weight, false});
    return made;
}

constexpr AnchorStyles anchor_all = AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left | AnchorStyles::right;
constexpr AnchorStyles anchor_bottom_left = AnchorStyles::bottom | AnchorStyles::left;
constexpr AnchorStyles anchor_bottom_wide = AnchorStyles::bottom | AnchorStyles::left | AnchorStyles::right;
constexpr AnchorStyles anchor_top_right = AnchorStyles::top | AnchorStyles::right;

constexpr Color match_colour = Color::rgba(255, 233, 140);
constexpr double grid_left = 16.0;
constexpr double grid_width = 1148.0;
constexpr double chooser_width = 180.0;

// The window's root: the view switch, the orders view and the dashboard.
class DataWorkbenchRoot final : public Panel {
public:
    explicit DataWorkbenchRoot(StableId id) : Panel(std::move(id)) {}

    void build() {
        add_at(*this, make_label("workbench.title", std::string(program_name), 18.0, 700), Rect{16.0, 12.0, 500.0, 28.0});
        views_ = make_control<ChoiceGroup>(StableId("workbench.view"));
        (*views_).accessible_name = "View";
        (*views_).set_items({ChoiceItem{0, "Orders"}, ChoiceItem{1, "Dashboard"}});
        (*views_).selected_index = 0;
        (*views_).anchor = anchor_top_right;
        add_at(*this, views_, Rect{900.0, 12.0, 264.0, 28.0});
        orders_view_ = make_control<Panel>(StableId("workbench.orders"));
        (*orders_view_).anchor = anchor_all;
        add_at(*this, orders_view_, Rect{0.0, 48.0, window_size.width, window_size.height - 48.0});
        dashboard_view_ = make_control<Panel>(StableId("workbench.dashboard"));
        (*dashboard_view_).anchor = anchor_all;
        (*dashboard_view_).visible = false;
        add_at(*this, dashboard_view_, Rect{0.0, 48.0, window_size.width, window_size.height - 48.0});
        build_orders();
        build_dashboard();
        on((*views_).changed(), *this, &DataWorkbenchRoot::on_view);
        on((*find_).search_requested(), *this, &DataWorkbenchRoot::on_find);
        on((*grid_).column_header_click(), *this, &DataWorkbenchRoot::on_header_click);
        on((*grid_).cell_formatting(), *this, &DataWorkbenchRoot::on_cell_formatting);
        on((*grid_).column_width_changed(), *this, &DataWorkbenchRoot::on_columns_moved);
        on((*grid_).column_display_index_changed(), *this, &DataWorkbenchRoot::on_columns_moved);
        on((*chooser_list_).item_check_state_changed(), *this, &DataWorkbenchRoot::on_column_checked);
        on((*columns_button_).clicked(), *this, &DataWorkbenchRoot::on_columns_button);
        on((*save_button_).clicked(), *this, &DataWorkbenchRoot::on_save_layout);
        on((*restore_button_).clicked(), *this, &DataWorkbenchRoot::on_restore_layout);
        on((*reset_button_).clicked(), *this, &DataWorkbenchRoot::on_reset_layout);
        default_layout_ = layout_text();
    }

private:
    // -- orders view ----------------------------------------------------------
    void build_orders() {
        Panel& view = *orders_view_;
        find_ = make_control<SearchField>(StableId("workbench.find"));
        (*find_).accessible_name = "Find in orders";
        add_at(view, find_, Rect{16.0, 8.0, 320.0, 28.0});
        find_status_ = make_label("workbench.find.status", "", 11.0);
        add_at(view, find_status_, Rect{344.0, 12.0, 280.0, 20.0});
        columns_button_ = make_control<Button>(StableId("workbench.columns"), "Columns");
        (*columns_button_).expanded_state = stxlib::Optional<bool>::of(false);
        (*columns_button_).anchor = anchor_top_right;
        add_at(view, columns_button_, Rect{640.0, 8.0, 110.0, 28.0});
        save_button_ = make_control<Button>(StableId("workbench.layout.save"), "Save layout");
        (*save_button_).anchor = anchor_top_right;
        add_at(view, save_button_, Rect{760.0, 8.0, 120.0, 28.0});
        restore_button_ = make_control<Button>(StableId("workbench.layout.restore"), "Restore layout");
        (*restore_button_).anchor = anchor_top_right;
        (*restore_button_).enabled = false;
        add_at(view, restore_button_, Rect{888.0, 8.0, 130.0, 28.0});
        reset_button_ = make_control<Button>(StableId("workbench.layout.reset"), "Reset layout");
        (*reset_button_).anchor = anchor_top_right;
        add_at(view, reset_button_, Rect{1026.0, 8.0, 138.0, 28.0});

        grid_ = make_control<DataGridView>(StableId("workbench.grid"));
        (*grid_).accessible_name = "Orders";
        (*grid_).read_only = true;
        (*grid_).row_headers_visible = false;
        (*grid_).selection_mode = DataGridViewSelectionMode::full_row_select;
        (*grid_).allow_user_to_order_columns = true;
        (*grid_).row_order_policy = DataGridViewRowOrderPolicy::edit;
        std::vector<DataGridViewColumn> columns{};
        for (std::size_t column = 0; column < column_count; ++column) {
            DataGridViewColumn spec{};
            spec.name = std::string(column_names[column]);
            spec.header_text = std::string(column_headers[column]);
            spec.width = column_widths[column];
            spec.read_only = true;
            if (column_numeric[column]) {
                spec.default_cell_style.alignment =
                    stxlib::Optional<DataGridViewContentAlignment>::of(DataGridViewContentAlignment::middle_end);
            }
            columns.push_back(std::move(spec));
        }
        (*grid_).set_columns(std::move(columns));
        rows_ = std::make_shared<OrderRows>();
        shown_.reserve(order_count);
        for (std::size_t index = 0; index < order_count; ++index) shown_.push_back(index);
        (*grid_).set_virtual_rows(rows_, shown_ids());
        (*grid_).anchor = anchor_all;
        add_at(view, grid_, Rect{grid_left, 44.0, grid_width, 590.0});

        chooser_ = make_control<Panel>(StableId("workbench.chooser"));
        (*chooser_).anchor = AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::right;
        (*chooser_).visible = false;
        add_at(*chooser_, make_label("workbench.chooser.title", "Columns", 12.0, 600), Rect{8.0, 4.0, 160.0, 22.0});
        chooser_list_ = make_control<CheckedListBox>(StableId("workbench.chooser.list"));
        (*chooser_list_).accessible_name = "Visible columns";
        (*chooser_list_).check_on_click = true;
        for (std::size_t column = 0; column < column_count; ++column) {
            (*chooser_list_).add_item(std::string(column_headers[column]), CheckState::checked);
        }
        (*chooser_list_).anchor = anchor_all;
        add_at(*chooser_, chooser_list_, Rect{8.0, 30.0, chooser_width - 16.0, 540.0});
        add_at(view, chooser_, Rect{grid_left + grid_width - chooser_width, 44.0, chooser_width, 590.0});

        for (std::size_t summary = 0; summary < summaries_.size(); ++summary) {
            std::shared_ptr<Label> made = make_label("workbench.summary." + std::to_string(summary), "", 11.0, 600);
            (*made).anchor = anchor_bottom_left;
            add_at(view, made, Rect{grid_left, 640.0, 100.0, 20.0});
            summaries_[summary] = made;
        }
        layout_status_ = make_label("workbench.layout.status",
                                    "Layouts last for this session: GUI.Forms has no per-user settings store yet.",
                                    10.0);
        (*layout_status_).anchor = anchor_bottom_wide;
        add_at(view, layout_status_, Rect{16.0, 668.0, 1148.0, 20.0});
        refresh_summaries();
    }

    [[nodiscard]] std::vector<DataGridViewRowId> shown_ids() const {
        std::vector<DataGridViewRowId> ids{};
        ids.reserve(shown_.size());
        for (const std::size_t index : shown_) ids.push_back((*rows_).id(index));
        return ids;
    }

    // -- find panel -----------------------------------------------------------
    // Keeps the orders whose text in any shown column holds the query
    // (ASCII case ignored), in the current sort, and marks the matches.
    void on_find(const std::string& query) {
        query_ = lower_ascii(query);
        shown_.clear();
        for (std::size_t index = 0; index < order_count; ++index) {
            if (query_.empty() || order_matches(order_at(index))) shown_.push_back(index);
        }
        apply_sort();
        (*grid_).set_virtual_row_order(shown_ids());
        (*find_status_).text = query_.empty() ? std::string()
                                              : std::to_string(shown_.size()) + " of " + std::to_string(order_count) +
                                                    " orders match";
        refresh_summaries();
    }

    [[nodiscard]] bool order_matches(const Order& order) const {
        for (std::size_t column = 0; column < column_count; ++column) {
            if (!(*grid_).columns()[column].visible) continue;
            if (lower_ascii(order_text(order, column)).find(query_) != std::string::npos) return true;
        }
        return false;
    }

    // A matching cell's background; handlers must not change the grid.
    void on_cell_formatting(DataGridViewCellFormattingEvent& event) {
        if (query_.empty()) return;
        const std::string text = binding_value_to_string(event.value);
        if (lower_ascii(text).find(query_) == std::string::npos) return;
        event.cell_style.back_color = stxlib::Optional<Color>::of(match_colour);
    }

    // -- sorting --------------------------------------------------------------
    void on_header_click(std::size_t column) {
        if (column >= column_count) return;
        descending_ = sort_column_ == static_cast<int>(column) ? !descending_ : false;
        sort_column_ = static_cast<int>(column);
        apply_sort();
        (*grid_).set_virtual_row_order(shown_ids());
        show_sort_glyphs();
    }

    void apply_sort() {
        if (sort_column_ < 0) {
            std::sort(shown_.begin(), shown_.end());
            return;
        }
        const OrderByColumn order{static_cast<std::size_t>(sort_column_), descending_};
        std::stable_sort(shown_.begin(), shown_.end(), order);
    }

    void show_sort_glyphs() {
        for (std::size_t column = 0; column < column_count; ++column) {
            DataGridViewSortDirection glyph = DataGridViewSortDirection::none;
            if (static_cast<int>(column) == sort_column_) {
                glyph = descending_ ? DataGridViewSortDirection::descending : DataGridViewSortDirection::ascending;
            }
            (*grid_).set_column_sort_glyph(column, glyph);
        }
    }

    // -- column chooser -------------------------------------------------------
    void on_columns_button() {
        const bool open = !(*chooser_).visible.read();
        (*chooser_).visible = open;
        (*columns_button_).expanded_state = stxlib::Optional<bool>::of(open);
        const double width = open ? grid_width - chooser_width - 8.0 : grid_width;
        Rect bounds = (*grid_).requested_bounds();
        bounds.width = width;
        (*grid_).set_requested_bounds(bounds);
    }

    void on_column_checked(std::size_t item, CheckState state) {
        if (syncing_chooser_ || item >= column_count) return;
        const bool wanted = state == CheckState::checked;
        std::size_t shown = 0U;
        for (const DataGridViewColumn& column : (*grid_).columns()) shown += column.visible ? 1U : 0U;
        if (!wanted && shown <= 1U) {
            // The last shown column stays: a grid with none shows nothing.
            syncing_chooser_ = true;
            (*chooser_list_).set_item_checked(item, true);
            syncing_chooser_ = false;
            return;
        }
        (*grid_).set_column_visible(item, wanted);
        if (!query_.empty()) on_find(query_);
        refresh_summaries();
    }

    void sync_chooser() {
        syncing_chooser_ = true;
        for (std::size_t column = 0; column < column_count; ++column) {
            (*chooser_list_).set_item_checked(column, (*grid_).columns()[column].visible);
        }
        syncing_chooser_ = false;
    }

    // -- layouts --------------------------------------------------------------
    // The layout as one line: "v1", then name:width:display:visible for
    // each column, then the sort.
    [[nodiscard]] std::string layout_text() const {
        std::string text = "v1";
        for (std::size_t column = 0; column < column_count; ++column) {
            const DataGridViewColumn& spec = (*grid_).columns()[column];
            text += "|" + spec.name + ":" + std::to_string(static_cast<long long>(spec.width)) + ":" +
                    std::to_string((*grid_).column_display_index(column).value) + ":" + (spec.visible ? "1" : "0");
        }
        text += "|sort=";
        if (sort_column_ < 0) {
            text += "none";
        } else {
            text += std::string(column_names[static_cast<std::size_t>(sort_column_)]) + (descending_ ? ":d" : ":a");
        }
        return text;
    }

    struct ColumnLayout final {
        std::size_t column{};
        double width{};
        std::size_t display{};
        bool visible{};
    };

    struct ByDisplay final {
        bool operator()(const ColumnLayout& left, const ColumnLayout& right) const { return left.display < right.display; }
    };

    [[nodiscard]] static std::optional<std::size_t> parse_count(std::string_view text) {
        std::size_t value = 0U;
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
        return value;
    }

    // Applies a layout_text line; false, changing nothing, when it does not
    // read.
    bool apply_layout(std::string_view text) {
        std::vector<std::string_view> parts{};
        std::size_t at = 0U;
        while (at <= text.size()) {
            const std::size_t bar = text.find('|', at);
            const std::size_t end = bar == std::string_view::npos ? text.size() : bar;
            parts.push_back(text.substr(at, end - at));
            at = end + 1U;
        }
        if (parts.size() != column_count + 2U || parts.front() != "v1") return false;
        std::vector<ColumnLayout> layouts{};
        for (std::size_t part = 1U; part <= column_count; ++part) {
            const std::string_view token = parts[part];
            std::array<std::string_view, 4> fields{};
            std::size_t field = 0U;
            std::size_t start = 0U;
            while (field < fields.size()) {
                const std::size_t colon = token.find(':', start);
                const std::size_t end = colon == std::string_view::npos ? token.size() : colon;
                fields[field] = token.substr(start, end - start);
                ++field;
                if (colon == std::string_view::npos) break;
                start = colon + 1U;
            }
            if (field != fields.size()) return false;
            const stxlib::Optional<std::size_t> column = (*grid_).column_index(fields[0]);
            const std::optional<std::size_t> width = parse_count(fields[1]);
            const std::optional<std::size_t> display = parse_count(fields[2]);
            if (column.absent() || !width.has_value() || !display.has_value() || *display >= column_count) return false;
            if (fields[3] != "0" && fields[3] != "1") return false;
            layouts.push_back(ColumnLayout{*column.value(), static_cast<double>(*width), *display, fields[3] == "1"});
        }
        const std::string_view sort = parts.back();
        if (!sort.starts_with("sort=")) return false;
        const std::string_view sort_value = sort.substr(5U);
        int sort_column = -1;
        bool descending = false;
        if (sort_value != "none") {
            const std::size_t colon = sort_value.find(':');
            if (colon == std::string_view::npos) return false;
            const stxlib::Optional<std::size_t> column = (*grid_).column_index(sort_value.substr(0U, colon));
            const std::string_view direction = sort_value.substr(colon + 1U);
            if (column.absent() || (direction != "a" && direction != "d")) return false;
            sort_column = static_cast<int>(*column.value());
            descending = direction == "d";
        }
        // Read: apply, the display order lowest first so each move lands.
        std::stable_sort(layouts.begin(), layouts.end(), ByDisplay{});
        for (const ColumnLayout& layout : layouts) {
            (*grid_).set_column_width(layout.column, std::max(20.0, layout.width));
            (*grid_).set_column_visible(layout.column, layout.visible);
            (*grid_).set_column_display_index(layout.column, DataGridViewDisplayIndex{layout.display});
        }
        sort_column_ = sort_column;
        descending_ = descending;
        sync_chooser();
        on_find(query_);
        show_sort_glyphs();
        return true;
    }

    void on_save_layout() {
        saved_layout_ = layout_text();
        (*restore_button_).enabled = true;
        (*layout_status_).text = "Saved for this session: " + saved_layout_;
    }

    void on_restore_layout() {
        if (saved_layout_.empty()) return;
        const bool applied = apply_layout(saved_layout_);
        (*layout_status_).text = applied ? "Restored: " + saved_layout_ : std::string("The saved layout did not read");
    }

    void on_reset_layout() {
        static_cast<void>(apply_layout(default_layout_));
        (*layout_status_).text = "Reset to the first layout";
    }

    // -- summaries ------------------------------------------------------------
    void on_columns_moved(std::size_t column) {
        static_cast<void>(column);
        place_summaries();
    }

    // The footer under the shown rows: their count under the first shown
    // column, the quantity and total sums and the average price under
    // their columns.
    void refresh_summaries() {
        std::int64_t quantity = 0;
        std::int64_t total = 0;
        std::int64_t price = 0;
        for (const std::size_t index : shown_) {
            const Order order = order_at(index);
            quantity += order.quantity;
            total += order.total_cents;
            price += order.price_cents;
        }
        const std::int64_t count = static_cast<std::int64_t>(shown_.size());
        (*summaries_[0]).text = "Count " + std::to_string(count);
        (*summaries_[1]).text = "Sum " + std::to_string(quantity);
        (*summaries_[2]).text = "Avg " + money_text(count > 0 ? price / count : 0);
        (*summaries_[3]).text = "Sum " + money_text(total);
        place_summaries();
    }

    // Puts each summary under its column as the grid shows it now: in
    // display order, after the shown columns before it.
    void place_summaries() {
        constexpr std::array<std::size_t, 4> summary_columns{0U, 5U, 6U, 7U};
        std::array<double, column_count> left{};
        std::array<bool, column_count> shown{};
        std::size_t first_shown = column_count;
        double x = grid_left + 1.0;
        for (std::size_t display = 0; display < column_count; ++display) {
            const std::size_t column = (*grid_).column_at_display_index(DataGridViewDisplayIndex{display});
            const DataGridViewColumn& spec = (*grid_).columns()[column];
            if (!spec.visible) continue;
            if (first_shown == column_count) first_shown = column;
            left[column] = x;
            shown[column] = true;
            x += spec.width;
        }
        for (std::size_t summary = 0; summary < summaries_.size(); ++summary) {
            const std::size_t column = summary == 0U ? first_shown : summary_columns[summary];
            Label& label = *summaries_[summary];
            const bool visible = column < column_count && shown[column];
            label.visible = visible;
            if (!visible) continue;
            const double width = std::max(90.0, (*grid_).columns()[column].width);
            label.set_requested_bounds({left[column], 640.0, width, 20.0});
            label.alignment = summary == 0U ? HorizontalAlignment::start : HorizontalAlignment::end;
        }
    }

    // -- dashboard ------------------------------------------------------------
    void build_dashboard() {
        Panel& view = *dashboard_view_;
        std::array<std::int64_t, regions.size()> by_region{};
        std::array<std::int64_t, 12> by_month{};
        std::array<std::int64_t, products.size()> by_product{};
        std::array<std::int64_t, customers.size()> by_customer{};
        std::int64_t revenue = 0;
        for (std::size_t index = 0; index < order_count; ++index) {
            const Order order = order_at(index);
            revenue += order.total_cents;
            by_region[order.region] += order.total_cents;
            by_month[static_cast<std::size_t>(order_date(order).month) - 1U] += order.total_cents;
            by_product[order.product] += order.total_cents;
            by_customer[order.customer] += order.total_cents;
        }
        std::size_t top_customer = 0U;
        for (std::size_t customer = 0; customer < customers.size(); ++customer) {
            if (by_customer[customer] > by_customer[top_customer]) top_customer = customer;
        }
        constexpr std::array<std::string_view, 4> titles{"Orders", "Revenue", "Average order", "Top customer"};
        const std::array<std::string, 4> values{std::to_string(order_count), money_text(revenue),
                                                money_text(revenue / static_cast<std::int64_t>(order_count)),
                                                std::string(customers[top_customer])};
        for (std::size_t figure = 0; figure < titles.size(); ++figure) {
            const double left = 16.0 + 290.0 * static_cast<double>(figure);
            const std::string id = "workbench.dashboard.figure." + std::to_string(figure);
            add_at(view, make_label(id + ".title", std::string(titles[figure]), 11.0), Rect{left, 12.0, 270.0, 18.0});
            add_at(view, make_label(id + ".value", values[figure], 20.0, 700), Rect{left, 32.0, 270.0, 32.0});
        }
        std::shared_ptr<BarChart> regions_chart = std::make_shared<BarChart>();
        for (std::size_t region = 0; region < regions.size(); ++region) {
            (*regions_chart).labels.push_back(std::string(regions[region]));
            (*regions_chart).values.push_back(static_cast<double>(by_region[region]));
        }
        (*regions_chart).colour = Color::rgba(39, 116, 184);
        add_chart(view, "workbench.dashboard.regions", "Revenue by region", regions_chart,
                  Rect{16.0, 84.0, 560.0, 290.0});
        std::shared_ptr<BarChart> months_chart = std::make_shared<BarChart>();
        for (std::size_t month = 0; month < by_month.size(); ++month) {
            (*months_chart).labels.push_back(std::string(month_names[month]));
            (*months_chart).values.push_back(static_cast<double>(by_month[month]));
        }
        (*months_chart).colour = Color::rgba(49, 139, 94);
        add_chart(view, "workbench.dashboard.months", "Revenue by month", months_chart, Rect{604.0, 84.0, 560.0, 290.0});
        std::shared_ptr<BarChart> products_chart = std::make_shared<BarChart>();
        (*products_chart).across = true;
        for (std::size_t product = 0; product < products.size(); ++product) {
            (*products_chart).labels.push_back(std::string(products[product]));
            (*products_chart).values.push_back(static_cast<double>(by_product[product]));
        }
        (*products_chart).colour = Color::rgba(211, 121, 42);
        add_chart(view, "workbench.dashboard.products", "Revenue by product", products_chart,
                  Rect{16.0, 396.0, 1148.0, 270.0});
        add_at(view,
               make_label("workbench.dashboard.note",
                          "Share of revenue is drawn as bars: GUI.Forms has no pie series yet.", 10.0),
               Rect{16.0, 672.0, 1148.0, 20.0});
    }

    void add_chart(Panel& view, const std::string& id, std::string title, std::shared_ptr<BarChart> chart, Rect bounds) {
        add_at(view, make_label(id + ".title", std::move(title), 12.0, 600), Rect{bounds.x, bounds.y, bounds.width, 20.0});
        std::shared_ptr<DrawingSurface> surface = make_control<DrawingSurface>(StableId(id));
        (*surface).accessible_name = id;
        (*surface).semantic_role = SemanticRole::figure;
        (*surface).set_paint_callback(BarChartPaint(std::move(chart)));
        add_at(view, surface, Rect{bounds.x, bounds.y + 24.0, bounds.width, bounds.height - 24.0});
    }

    void on_view(int index) {
        (*orders_view_).visible = index != 1;
        (*dashboard_view_).visible = index == 1;
    }

    std::shared_ptr<ChoiceGroup> views_;
    std::shared_ptr<Panel> orders_view_;
    std::shared_ptr<Panel> dashboard_view_;
    std::shared_ptr<SearchField> find_;
    std::shared_ptr<Label> find_status_;
    std::shared_ptr<Button> columns_button_;
    std::shared_ptr<Button> save_button_;
    std::shared_ptr<Button> restore_button_;
    std::shared_ptr<Button> reset_button_;
    std::shared_ptr<DataGridView> grid_;
    std::shared_ptr<OrderRows> rows_;
    std::shared_ptr<Panel> chooser_;
    std::shared_ptr<CheckedListBox> chooser_list_;
    std::array<std::shared_ptr<Label>, 4> summaries_{};
    std::shared_ptr<Label> layout_status_;
    std::vector<std::size_t> shown_{};
    std::string query_{};
    int sort_column_{-1};
    bool descending_{};
    bool syncing_chooser_{};
    std::string saved_layout_{};
    std::string default_layout_{};
};

} // namespace

Order order_at(std::size_t index) noexcept {
    Order order{};
    order.number = 100000 + static_cast<std::int64_t>(index);
    order.day_of_year = static_cast<std::int32_t>((index * 37U + index / 11U) % 365U);
    order.customer = (index * 7U + index / 13U) % customers.size();
    order.region = (index * 5U + index / 7U) % regions.size();
    order.product = (index * 11U + index / 3U) % products.size();
    order.quantity = 1 + static_cast<std::int64_t>((index * 31U + index / 17U) % 20U);
    const std::int64_t base = base_cents[order.product];
    order.price_cents = base + base * static_cast<std::int64_t>((index * 13U) % 7U) / 100;
    order.total_cents = order.quantity * order.price_cents;
    return order;
}

std::string money_text(std::int64_t cents) {
    const bool negative = cents < 0;
    const std::int64_t whole = (negative ? -cents : cents) / 100;
    const std::int64_t part = (negative ? -cents : cents) % 100;
    std::string text = negative ? "-" : "";
    text += std::to_string(whole) + "." + two_digits(static_cast<unsigned>(part));
    return text;
}

std::string order_text(const Order& order, std::size_t column) {
    if (column == 0U) return std::to_string(order.number);
    if (column == 1U) {
        const DateTimeValue date = order_date(order);
        return std::to_string(date.year) + "-" + two_digits(date.month) + "-" + two_digits(date.day);
    }
    if (column == 2U) return std::string(customers[order.customer]);
    if (column == 3U) return std::string(regions[order.region]);
    if (column == 4U) return std::string(products[order.product]);
    if (column == 5U) return std::to_string(order.quantity);
    if (column == 6U) return money_text(order.price_cents);
    if (column == 7U) return money_text(order.total_cents);
    return {};
}

std::unique_ptr<Window> make_data_workbench() {
    std::shared_ptr<DataWorkbenchRoot> root = make_control<DataWorkbenchRoot>(StableId("workbench.root"));
    (*root).build();
    std::unique_ptr<Window> window =
        std::make_unique<Window>(root, window_size, std::make_shared<ApplicationResources>());
    (*window).perform_layout();
    return window;
}

} // namespace gui_forms::data_workbench
