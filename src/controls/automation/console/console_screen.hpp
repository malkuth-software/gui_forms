#pragma once

// The MCP console's screen model (MCP 1.0.5; the owner's console brief of
// 2026-11-25): the rows a console shows, how a new line grows into them and
// how the screen scrolls. No drawing and no window: the overlay's surface
// (console_overlay.cpp) composites what this model shows from a glyph
// atlas, and the tests drive it with plain time steps.
//
// - Retained rows only: the screen keeps the rows in view and one above
//   them for the smooth scroll, never every row of the record (after
//   swiftedit's src/terminal_wrap_view.hpp, which retains only visible
//   rows and lays out nothing else).
// - Appending: a line queues its wrapped rows; the typing cursor takes the
//   next row onto the screen when it reaches it, and the screen follows the
//   tail, the oldest row leaving at the top (after chat_stress's
//   src/chat_view.cpp follow-tail append in malkuth-software/demonstrators).
// - Typing: the instruction's glyphs (c = 0) appear one at a time at
//   typing_rate glyphs a second; a value's run (c = 1) appears whole, as a
//   block, when the cursor reaches it. A queue longer than a line speeds the
//   typing up (catch_up), so a burst of calls never lags for long.
// - Scrolling: a row taken onto a full screen moves the others up by one
//   row, smoothly: scroll() falls from 1 to 0 at scroll_rate rows a second.
//
// advance() is the only clock: it is called with the time since the last
// frame and answers whether anything changed. busy() is false once every
// queued glyph is shown and the scroll has settled; the overlay then asks
// for no more frames (no timer, no wakeups).
//
// Thread: one, the console window's UI thread.

#include "console_log.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace gui_forms::automation {

struct ConsoleScreenRow final {
    // Unique per row taken onto the screen (1 for the first), never reused:
    // the surface keys its rasters by it.
    std::uint64_t key{};
    ConsoleGlyphRow row{};
    // The glyphs shown, a prefix of row.glyphs.
    std::size_t shown{};
};

class ConsoleScreen final {
public:
    // Glyphs a second while typing (about 11 ms a glyph).
    static constexpr double typing_rate = 90.0;
    // Queued glyphs that double the rate (catch-up: the rate is
    // typing_rate * (1 + queued / catch_up)).
    static constexpr double catch_up = 60.0;
    // Rows a second the smooth scroll moves.
    static constexpr double scroll_rate = 14.0;

    // The rows in view (at least 1). Rows above them beyond the one the
    // scroll shows leave.
    void set_visible_rows(std::size_t rows);
    [[nodiscard]] std::size_t visible_rows() const noexcept { return visible_; }
    // Queues the rows of one line (console_wrap's output).
    void queue_line(std::vector<ConsoleGlyphRow> rows);
    // Shows everything queued at once, with no scroll (reduced motion, a
    // re-wrap after a resize).
    void show_all();
    // Forgets every row, shown or queued.
    void clear() noexcept;
    // Moves typing and scrolling on by `seconds`; true when a glyph
    // appeared, a row arrived or the scroll moved.
    bool advance(double seconds);
    [[nodiscard]] bool busy() const noexcept;
    // The scroll still to run, in rows, 0 to 1: a row at index i of rows()
    // lies (i - first_visible() + scroll()) rows below the text's top.
    [[nodiscard]] double scroll() const noexcept { return scroll_; }
    // The index in rows() of the first row in view.
    [[nodiscard]] std::size_t first_visible() const noexcept;
    [[nodiscard]] const std::deque<ConsoleScreenRow>& rows() const noexcept { return rows_; }
    // Glyphs queued or on screen and not yet shown.
    [[nodiscard]] std::size_t unshown() const noexcept { return unshown_; }

private:
    // Takes the next queued row onto the screen.
    void take_row(bool smooth);
    // Shows the next step of the last row: one instruction glyph, or a whole
    // run of values. False when the row is complete.
    bool reveal_step();
    void trim();

    std::size_t visible_{1U};
    std::deque<ConsoleScreenRow> rows_{};
    std::deque<ConsoleGlyphRow> queued_{};
    std::uint64_t next_key_{1U};
    std::size_t unshown_{};
    double budget_{};
    double scroll_{};
};

} // namespace gui_forms::automation
