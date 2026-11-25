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
// - Typing (owner, 2026-11-25: candy at a human pace): the instruction's
//   glyphs (c = 0) appear left to right as if typed fast, about
//   typing_rate a second with each keystroke's interval varied by up to
//   typing_jitter either way (seeded by the row and the glyph, so a test
//   and a repaint see the same rhythm). A value's run (c = 1) appears whole
//   when the cursor reaches it, lit brighter, and eases to its formula
//   colour over fade_seconds (glow()). A session label appears whole.
// - Only the newest entry animates: a line that begins an entry (a call, a
//   typed line, a note; not a reply's lines) shows every older line at
//   once first, so a burst never leaves the console behind the record.
// - Scrolling: a row taken onto a full screen moves the others up by one
//   row, smoothly: scroll() falls from 1 to 0 at scroll_rate rows a second.
//
// advance() is the only clock: it is called with the time since the last
// frame and answers whether anything changed. busy() is false once every
// queued glyph is shown, every glow has settled and the scroll has
// stopped; the overlay then asks for no more frames (no timer, no wakeups).
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

// A value's run lit as it appeared: glyphs [first, last) of the row `key`,
// `age` seconds after it appeared.
struct ConsoleFade final {
    std::uint64_t key{};
    std::size_t first{};
    std::size_t last{};
    double age{};
};

class ConsoleScreen final {
public:
    // Keystrokes a second while typing (a fast typist, about 140 words a
    // minute) and how far each keystroke's interval varies either way.
    static constexpr double typing_rate = 12.0;
    static constexpr double typing_jitter = 0.3;
    // How long a value's run takes to ease from lit to its colour.
    static constexpr double fade_seconds = 0.4;
    // Rows a second the smooth scroll moves.
    static constexpr double scroll_rate = 14.0;

    // The rows in view (at least 1). Rows above them beyond the one the
    // scroll shows leave.
    void set_visible_rows(std::size_t rows);
    [[nodiscard]] std::size_t visible_rows() const noexcept { return visible_; }
    // Queues the rows of one line (console_wrap's output). A line that
    // begins an entry first shows every older line at once.
    void queue_line(std::vector<ConsoleGlyphRow> rows, bool begins_entry);
    // Shows everything queued at once, with no scroll and no glow (reduced
    // motion, a re-wrap after a resize).
    void show_all();
    // Forgets every row, shown or queued.
    void clear() noexcept;
    // Moves typing, glows and scrolling on by `seconds`; true when a glyph
    // appeared, a row arrived, a glow changed or the scroll moved.
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
    // How lit glyph `glyph` of row `key` still is: 1 as its run appears,
    // easing to 0 (its formula colour) over fade_seconds.
    [[nodiscard]] double glow(std::uint64_t key, std::size_t glyph) const noexcept;
    // The runs glowing now, and those that settled in the last advance (to
    // be drawn once more, in their formula colour).
    [[nodiscard]] const std::vector<ConsoleFade>& fades() const& noexcept { return fades_; }
    const std::vector<ConsoleFade>& fades() const&& = delete;
    [[nodiscard]] const std::vector<ConsoleFade>& settled() const& noexcept { return settled_; }
    const std::vector<ConsoleFade>& settled() const&& = delete;
    // The interval before keystroke `glyph` of row `key`, in seconds.
    [[nodiscard]] static double keystroke_seconds(std::uint64_t key, std::size_t glyph) noexcept;

private:
    // Takes the next queued row onto the screen.
    void take_row(bool smooth);
    // Shows every queued and partly shown glyph at once, with no glow.
    void complete();
    void trim();

    std::size_t visible_{1U};
    std::deque<ConsoleScreenRow> rows_{};
    std::deque<ConsoleGlyphRow> queued_{};
    std::vector<ConsoleFade> fades_{};
    std::vector<ConsoleFade> settled_{};
    std::uint64_t next_key_{1U};
    std::size_t unshown_{};
    // Seconds until the next keystroke may show (0 or less: now).
    double wait_{};
    double scroll_{};
};

} // namespace gui_forms::automation
