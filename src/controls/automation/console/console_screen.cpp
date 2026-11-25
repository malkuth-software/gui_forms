#include "console_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gui_forms::automation {

void ConsoleScreen::set_visible_rows(std::size_t rows) {
    visible_ = std::max<std::size_t>(rows, 1U);
    trim();
}

void ConsoleScreen::queue_line(std::vector<ConsoleGlyphRow> rows, bool begins_entry) {
    // Only the newest entry animates: the older lines show at once.
    if (begins_entry) complete();
    for (ConsoleGlyphRow& row : rows) {
        unshown_ += row.glyphs.size();
        queued_.push_back(std::move(row));
    }
}

void ConsoleScreen::complete() {
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
    wait_ = 0.0;
}

void ConsoleScreen::show_all() {
    complete();
    for (const ConsoleFade& fade : fades_) settled_.push_back(fade);
    fades_.clear();
    scroll_ = 0.0;
    trim();
}

void ConsoleScreen::clear() noexcept {
    rows_.clear();
    queued_.clear();
    fades_.clear();
    settled_.clear();
    unshown_ = 0U;
    wait_ = 0.0;
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
    // once otherwise.
    if (full && smooth) scroll_ = 1.0;
    trim();
}

void ConsoleScreen::trim() {
    // The rows in view and one above them for the scroll.
    while (rows_.size() > visible_ + 1U) rows_.pop_front();
}

double ConsoleScreen::keystroke_seconds(std::uint64_t key, std::size_t glyph) noexcept {
    const std::uint64_t hash = console_glyph_hash(key ^ 0x6b657973ULL, glyph);
    const double unit = static_cast<double>((hash >> 11U) & 0xfffffULL) / 1048576.0;
    const double seconds = (1.0 + typing_jitter * (2.0 * unit - 1.0)) / typing_rate;
    return seconds;
}

double ConsoleScreen::glow(std::uint64_t key, std::size_t glyph) const noexcept {
    for (const ConsoleFade& fade : fades_) {
        if (fade.key != key || glyph < fade.first || glyph >= fade.last) continue;
        // Eased out: bright at first, settling gently.
        const double left = std::clamp(1.0 - fade.age / fade_seconds, 0.0, 1.0);
        return left * left;
    }
    return 0.0;
}

bool ConsoleScreen::advance(double seconds) {
    settled_.clear();
    if (!busy()) {
        wait_ = 0.0;
        return false;
    }
    const double step = std::clamp(seconds, 0.0, 0.25);
    bool changed = false;
    if (!fades_.empty()) {
        std::vector<ConsoleFade> live{};
        for (ConsoleFade fade : fades_) {
            fade.age += step;
            if (fade.age >= fade_seconds) {
                settled_.push_back(fade);
            } else {
                live.push_back(fade);
            }
        }
        fades_ = std::move(live);
        changed = true;
    }
    if (scroll_ > 0.0) {
        scroll_ = std::max(0.0, scroll_ - step * scroll_rate);
        changed = true;
    }
    wait_ -= step;
    while (true) {
        const bool row_complete = rows_.empty() || rows_.back().shown >= rows_.back().row.glyphs.size();
        if (row_complete) {
            if (queued_.empty()) break;
            take_row(true);
            changed = true;
            continue;
        }
        ConsoleScreenRow& last = rows_.back();
        const std::vector<ConsoleGlyph>& glyphs = last.row.glyphs;
        const ConsoleTone tone = glyphs[last.shown].tone;
        if (tone != ConsoleTone::instruction) {
            // A value's run appears whole and lit; a label appears whole.
            std::size_t end = last.shown;
            while (end < glyphs.size() && glyphs[end].tone == tone) ++end;
            if (tone == ConsoleTone::value) fades_.push_back(ConsoleFade{last.key, last.shown, end, 0.0});
            unshown_ -= end - last.shown;
            last.shown = end;
            changed = true;
            continue;
        }
        if (wait_ > 0.0) break;
        ++last.shown;
        --unshown_;
        changed = true;
        wait_ += keystroke_seconds(last.key, last.shown);
    }
    const bool typing = unshown_ > 0U || !queued_.empty();
    // Idle typing starts the next line's first keystroke at once.
    if (!typing) wait_ = 0.0;
    return changed;
}

bool ConsoleScreen::busy() const noexcept {
    const bool typing = unshown_ > 0U || !queued_.empty();
    return typing || scroll_ > 0.0 || !fades_.empty();
}

} // namespace gui_forms::automation
