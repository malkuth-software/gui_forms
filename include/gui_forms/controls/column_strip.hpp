#pragma once

#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace gui_forms {

// The column model's placement, shared by VirtualRowsView and DataGridView
// (the one data engine, owner 2026-11-13): the columns laid along the
// reading axis in display order, by width in logical units at text scale 1,
// with a leading run of frozen columns that do not scroll.
//
// assign keeps each placed column's start, the sum of the widths before
// it, so a column's start, the frozen run's width and the total cost O(1)
// and the column holding an offset costs O(log count); assign costs
// O(count). Starts are summed in display order, the order the grids summed
// them before, so they are the same doubles. Which columns are placed
// (shown ones), their order and their freezing are the caller's model; the
// strip only places them. The storage is kept between assigns. Single UI
// thread.
class GUI_FORMS_API ColumnStrip final {
public:
    // Places `widths` (each finite and not negative) in order; the first
    // `frozen` of them (at most widths.size()) are the frozen run.
    void assign(std::span<const double> widths, std::size_t frozen);
    void clear() noexcept;

    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] std::size_t frozen() const noexcept { return frozen_; }
    // The start of placed column `position`; count() gives total(), and a
    // position past it is held there.
    [[nodiscard]] double start(std::size_t position) const noexcept;
    // A placed column's width; 0 past the end.
    [[nodiscard]] double width(std::size_t position) const noexcept;
    [[nodiscard]] double total() const noexcept;
    [[nodiscard]] double frozen_width() const noexcept;
    // The placed column whose extent [start, start + width) holds `offset`
    // (a zero-width column holds nothing): count() for an offset before the
    // first column, at or past the total, or not a number.
    [[nodiscard]] std::size_t position_at(double offset) const noexcept;
    // The first position in [from, to) whose start is at or past `value`,
    // or `to` (both held at count()).
    [[nodiscard]] std::size_t first_start_at_or_past(double value, std::size_t from, std::size_t to) const noexcept;
    // The first position in [from, to) whose end (start + width) is past
    // `value`, or `to` (both held at count()).
    [[nodiscard]] std::size_t first_end_past(double value, std::size_t from, std::size_t to) const noexcept;

private:
    // count() + 1 starts: starts_[i] is placed column i's start and the last
    // is the total; empty before the first assign.
    std::vector<double> starts_{};
    std::size_t frozen_{};
};

} // namespace gui_forms
