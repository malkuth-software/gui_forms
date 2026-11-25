#include "console_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace gui_forms::automation {

void ConsoleScreen::set_visible_rows(std::size_t rows) {
    visible_ = std::max<std::size_t>(rows, 1U);
    trim();
}

void ConsoleScreen::queue_line(std::vector<ConsoleGlyphRow> rows) {
    for (ConsoleGlyphRow& row : rows) {
        unshown_ += row.glyphs.size();
        queued_.push_back(std::move(row));
    }
}

void ConsoleScreen::show_all() {
    if (!rows_.empty()) {
        ConsoleScreenRow& last = rows_.back();
        unshown_ -= last.row.glyphs.size() - last.shown;
        last.shown = last.row.glyphs.size();
    }
    while (!queued_.empty()) {
        take_row(false);
        ConsoleScreenRow& last = rows_.back();
        unshown_ -= last.row.glyphs.size();
        last.shown = last.row.glyphs.size();
    }
    scroll_ = 0.0;
    budget_ = 0.0;
    trim();
}

void ConsoleScreen::clear() noexcept {
    rows_.clear();
    queued_.clear();
    unshown_ = 0U;
    budget_ = 0.0;
    scroll_ = 0.0;
}

std::size_t ConsoleScreen::first_visible() const noexcept {
    const std::size_t first = rows_.size() > visible_ ? rows_.size() - visible_ : 0U;
    return first;
}

void ConsoleScreen::take_row(bool smooth) {
    ConsoleScreenRow row{};
    row.key = next_key_;
    ++next_key_;
    row.row = std::move(queued_.front());
    queued_.pop_front();
    const bool full = rows_.size() >= visible_;
    rows_.push_back(std::move(row));
    // On a full screen the others move up a row: smoothly while typing, at
    // once otherwise. More than a row at once jumps the rest.
    if (full && smooth) scroll_ = 1.0;
    trim();
}

void ConsoleScreen::trim() {
    // The rows in view and one above them for the scroll.
    while (rows_.size() > visible_ + 1U) rows_.pop_front();
}

bool ConsoleScreen::reveal_step() {
    if (rows_.empty()) return false;
    ConsoleScreenRow& last = rows_.back();
    const std::vector<ConsoleGlyph>& glyphs = last.row.glyphs;
    if (last.shown >= glyphs.size()) return false;
    if (glyphs[last.shown].tone == ConsoleTone::value) {
        // A value's run appears as a block.
        std::size_t end = last.shown;
        while (end < glyphs.size() && glyphs[end].tone == ConsoleTone::value) ++end;
        unshown_ -= end - last.shown;
        last.shown = end;
    } else {
        ++last.shown;
        --unshown_;
    }
    return true;
}

bool ConsoleScreen::advance(double seconds) {
    if (!busy()) {
        budget_ = 0.0;
        return false;
    }
    const double step = std::clamp(seconds, 0.0, 0.25);
    bool changed = false;
    if (scroll_ > 0.0) {
        scroll_ = std::max(0.0, scroll_ - step * scroll_rate);
        changed = true;
    }
    const double rate = typing_rate * (1.0 + static_cast<double>(unshown_) / catch_up);
    budget_ += step * rate;
    while (budget_ >= 1.0) {
        const bool row_complete = rows_.empty() || rows_.back().shown >= rows_.back().row.glyphs.size();
        if (row_complete) {
            if (queued_.empty()) break;
            take_row(true);
            changed = true;
            continue;
        }
        if (!reveal_step()) break;
        budget_ -= 1.0;
        changed = true;
    }
    if (!busy()) budget_ = 0.0;
    return changed;
}

bool ConsoleScreen::busy() const noexcept {
    const bool typing = unshown_ > 0U || !queued_.empty();
    return typing || scroll_ > 0.0;
}

} // namespace gui_forms::automation
