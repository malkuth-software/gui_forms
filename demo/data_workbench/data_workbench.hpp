#pragma once

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The data-workbench demonstrator (owner, 2026-11-22; the DevExpress
// survey's "demo only" grid takes, notes:planning/audits/
// DEVEXPRESS_OVERKILL_SURVEY_2026-11-22.md): one window over fifty thousand
// generated orders on a virtual DataGridView, composed from stock parts with
// no new library API:
// - a find panel (SearchField) that filters the rows and marks the matching
//   cells through cell_formatting;
// - a column chooser (a CheckedListBox beside the grid) that shows and hides
//   columns;
// - layout save and restore: widths, order, visibility and the sort as one
//   line of text, kept for the session (GUI.Forms has no per-user settings
//   store, the gap the queue entry names);
// - app-computed summaries under the grid (count, quantity, average price,
//   total) that follow the filter, the column widths and the column order;
// - a header click sorts, in the program, since a virtual grid leaves the
//   order to its owner;
// - a CRM dashboard view over the same orders: key figures and bar charts
//   on DrawingSurface (bars, since GUI.Forms has no pie series yet).
//
// Demo code over public GUI.Forms vocabulary: no host, renderer or
// platform type appears here. The hosts' mains are src/host/*/
// data_workbench_main.*; every one takes the demonstration programs'
// options (program_options.hpp), --headless --capture included.
namespace gui_forms::data_workbench {

inline constexpr std::string_view program_name = "GUI.Forms Data Workbench";
inline constexpr Size window_size{1180.0, 760.0};
inline constexpr std::size_t order_count = 50000U;

// The grid's columns, in their first order.
inline constexpr std::size_t column_count = 8U;

// One generated order, a pure function of its index, so no table is held.
struct Order final {
    std::int64_t number{};
    std::int32_t day_of_year{};
    std::size_t customer{};
    std::size_t region{};
    std::size_t product{};
    std::int64_t quantity{};
    std::int64_t price_cents{};
    std::int64_t total_cents{};
};

[[nodiscard]] Order order_at(std::size_t index) noexcept;

// The text the grid shows for `order` in column `column` (an index in the
// first order); empty past the last column.
[[nodiscard]] std::string order_text(const Order& order, std::size_t column);

// Cents as "1234.56".
[[nodiscard]] std::string money_text(std::int64_t cents);

// The demonstrator's window, laid out.
[[nodiscard]] std::unique_ptr<Window> make_data_workbench();

} // namespace gui_forms::data_workbench
