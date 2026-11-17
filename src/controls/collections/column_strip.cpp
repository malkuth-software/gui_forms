#include "gui_forms/controls/column_strip.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {

void ColumnStrip::assign(std::span<const double> widths, std::size_t frozen) {
    starts_.resize(widths.size() + 1U);
    double start = 0.0;
    for (std::size_t position = 0U; position < widths.size(); ++position) {
        starts_[position] = start;
        start += widths[position];
    }
    starts_[widths.size()] = start;
    frozen_ = std::min(frozen, widths.size());
}

void ColumnStrip::clear() noexcept {
    starts_.clear();
    frozen_ = 0U;
}

std::size_t ColumnStrip::count() const noexcept {
    const std::size_t result = starts_.empty() ? 0U : starts_.size() - 1U;
    return result;
}

double ColumnStrip::start(std::size_t position) const noexcept {
    if (starts_.empty()) return 0.0;
    const std::size_t held = std::min(position, starts_.size() - 1U);
    return starts_[held];
}

double ColumnStrip::width(std::size_t position) const noexcept {
    if (position >= count()) return 0.0;
    const double result = starts_[position + 1U] - starts_[position];
    return result;
}

double ColumnStrip::total() const noexcept {
    const double result = starts_.empty() ? 0.0 : starts_.back();
    return result;
}

double ColumnStrip::frozen_width() const noexcept {
    return start(frozen_);
}

std::size_t ColumnStrip::position_at(double offset) const noexcept {
    const std::size_t columns = count();
    if (columns == 0U || std::isnan(offset) || offset < 0.0 || offset >= total()) return columns;
    // The last start at or before the offset; zero-width columns before it
    // share its start and hold nothing.
    const std::vector<double>::const_iterator past = std::upper_bound(starts_.begin(), starts_.end() - 1, offset);
    const std::size_t result = static_cast<std::size_t>(past - starts_.begin()) - 1U;
    return result;
}

std::size_t ColumnStrip::first_start_at_or_past(double value, std::size_t from, std::size_t to) const noexcept {
    const std::size_t columns = count();
    const std::size_t end = std::min(to, columns);
    const std::size_t first = std::min(from, end);
    const std::vector<double>::const_iterator begin = starts_.begin() + static_cast<std::ptrdiff_t>(first);
    const std::vector<double>::const_iterator last = starts_.begin() + static_cast<std::ptrdiff_t>(end);
    if (starts_.empty()) return end;
    const std::vector<double>::const_iterator found = std::lower_bound(begin, last, value);
    const std::size_t result = static_cast<std::size_t>(found - starts_.begin());
    return result;
}

std::size_t ColumnStrip::first_end_past(double value, std::size_t from, std::size_t to) const noexcept {
    const std::size_t columns = count();
    const std::size_t end = std::min(to, columns);
    const std::size_t first = std::min(from, end);
    if (starts_.empty()) return end;
    // Column p ends at starts_[p + 1]: the first such start past the value.
    const std::vector<double>::const_iterator begin = starts_.begin() + static_cast<std::ptrdiff_t>(first + 1U);
    const std::vector<double>::const_iterator last = starts_.begin() + static_cast<std::ptrdiff_t>(end + 1U);
    const std::vector<double>::const_iterator found = std::upper_bound(begin, last, value);
    const std::size_t result = static_cast<std::size_t>(found - starts_.begin()) - 1U;
    return result;
}

} // namespace gui_forms
