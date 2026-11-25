#pragma once

// The MCP console's record (MCP 1.0.5; the owner's console ruling,
// DECISIONS 2026-11-24 "MCP console overlay and attach"): every MCP call
// and its reply, kept as lines of toned spans for the console overlay
// (console_overlay.hpp), which shows them to the user like a terminal. The
// model never reads it: it is the human's window onto the traffic, and it
// is never in a capture.
//
// Tones follow the owner's colour formula (console_colour), glyph by
// glyph: the instruction (the verb and the tool's name, a reply's leading
// word) is c = 0; values (arguments, reply text) and wrapped continuations
// are c = 1; a session's label is c = 0, dimmer. Each glyph's v varies.
// console_wrap turns a line into rows of glyphs, each with its own shade,
// seeded by the glyph's place in the line so that a repaint, a capture and
// a test all see the same colours.
//
// Thread: record_* from any thread (the transport's handler tasks), the
// rest from any thread too; one mutex guards the lines. Bounded: the oldest
// lines go once maximum_lines are kept.

#include "../tools/automation_tools.hpp"

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <stxlib/threads.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {

enum class ConsoleTone : std::uint8_t {
    // The instruction: c near 0 (orange).
    instruction,
    // A value, or text wrapped onto a continuation row: c near 1 (green).
    value,
    // A session's label ("[console ec4c] "): the instruction's c, dimmer.
    label,
};

struct ConsoleSpan final {
    std::string text{};
    ConsoleTone tone{ConsoleTone::value};
};

struct ConsoleLine final {
    // 1 for the first line recorded; never reused.
    std::uint64_t serial{};
    // The session the line belongs to ("claude-code 3f2a"), empty for
    // none (ConsoleLog::set_session).
    std::string session{};
    std::vector<ConsoleSpan> spans{};
};

class ConsoleLog final {
public:
    static constexpr std::size_t maximum_lines = 2000U;
    // The longest argument or reply text one line shows, in bytes; longer
    // text is cut at a UTF-8 boundary and ends with U+2026.
    static constexpr std::size_t maximum_value_bytes = 600U;
    // The reply text's lines shown, after which the count of the rest.
    static constexpr std::size_t maximum_reply_lines = 4U;

    ConsoleLog() = default;
    ConsoleLog(const ConsoleLog&) = delete;
    ConsoleLog& operator=(const ConsoleLog&) = delete;

    // The session later lines belong to, by its label (the client's name
    // and the token's first digits; empty for none). One record serves the
    // socket's sessions and the overlay alike (owner, attach revision 3:
    // the visible console mirrors the socket session itself), so the label
    // tells several sessions apart: a call line of a labelled session
    // begins with "[label]".
    void set_session(std::string label);
    // A call as it begins, as the socket's console would type it:
    // "call tool {arguments}".
    void record_call(std::string_view tool, std::string_view arguments_json);
    // A line a console session typed, as it was typed (its first word as the
    // instruction).
    void record_typed(std::string_view line);
    // Its reply: "‹ drew ..." with the first lines of the text and a note
    // of each image, or "‹ <error>: message" for a failure. The first line's
    // leading word is the instruction; the rest, and the later lines, are
    // values.
    void record_reply(const ToolReply& reply);
    // A line of the run's own ("# connected ...", "# disconnected"): its
    // first word the instruction, the rest a value.
    void record_note(std::string_view note);

    // The last line's serial (0 before any).
    [[nodiscard]] std::uint64_t serial() const;
    // The lines after `after` still kept, oldest first, appended to `lines`.
    void copy_since(std::uint64_t after, std::vector<ConsoleLine>& lines) const;
    // Every line still kept as plain text, one per line, each session's
    // lines after a "== session <label>" line: the transcript a console
    // session's `transcript` command answers.
    [[nodiscard]] std::string transcript_text() const;

private:
    void append(std::vector<ConsoleSpan> spans);
    void label_span(std::vector<ConsoleSpan>& spans) const;

    mutable stxlib::Mutex mutex_{};
    std::vector<ConsoleLine> lines_{};
    std::uint64_t serial_{};
    // Under mutex_.
    std::string session_{};
};

// `text` cut to at most `limit` bytes at a UTF-8 boundary, with U+2026 after
// it when it was cut.
[[nodiscard]] std::string console_clip(std::string_view text, std::size_t limit);

// ---- the owner's colour formula ----

// One glyph's shade: c from 0 (instruction, orange) to 1 (value, green),
// v the brightness, near 0.8. Over black:
//   r = c * 0.5v + (1 - c) * v
//   g = c * v + (1 - c) * 0.5v
//   b = c * 0.25v
struct ConsoleShade final {
    double c{};
    double v{};
};

// The shade of glyph `glyph` (its code point index in the line) of line
// `serial` in `tone`, as the owner's FastMatrixDashboard shades a cell:
// c is 0 for the instruction and a label and 1 for a value; v is the
// glyph's confidence, max(50/255, conf) at most 1, with conf drawn around
// 0.9 (0.62 for a label) within plus or minus 0.12, from a hash of the serial and the
// glyph, so a repaint, a capture and a test draw the same colours.
[[nodiscard]] ConsoleShade console_shade(ConsoleTone tone, std::uint64_t serial, std::size_t glyph) noexcept;
// The formula's colour for `shade`, each channel quantized to 8 bits with
// a triangular dither drawn from `seed`, so neighbouring glyphs of one tone
// do not band into identical steps.
[[nodiscard]] Color console_colour(ConsoleShade shade, std::uint64_t seed) noexcept;
// The formula without dither or quantization, for tests: {r, g, b} in 0..1.
struct ConsoleRgb final {
    double r{};
    double g{};
    double b{};
};
[[nodiscard]] ConsoleRgb console_formula(ConsoleShade shade) noexcept;
// A value glyph's colour as it appears lit (owner, 2026-11-25: green
// "brighter then dim to the correct colour"): at `glow` 1 the formula at
// v = 1 lightened towards white, at 0 exactly console_formula(shade), and
// a straight mix between.
[[nodiscard]] ConsoleRgb console_glow(ConsoleShade shade, double glow) noexcept;
// The hash console_shade and the glyph's dither draw from.
[[nodiscard]] std::uint64_t console_glyph_hash(std::uint64_t serial, std::size_t glyph) noexcept;

// One glyph of a console row: its text (one code point), its column, its
// tone and its shade (the formula's inputs; the screen composites the
// glyph's mask in the formula's colour).
struct ConsoleGlyph final {
    std::string text{};
    std::size_t column{};
    ConsoleTone tone{ConsoleTone::value};
    ConsoleShade shade{};
};

struct ConsoleGlyphRow final {
    std::vector<ConsoleGlyph> glyphs{};
};

// `line` as rows of at most `columns` code points (at least 1), appended to
// `rows`: a row the line wraps onto is all value (the ruling: values and
// wrapping are c = 1), except that brackets, braces, parentheses and quotes
// ([](){}'") are the instruction wherever they stand (owner, 2026-11-25).
// Spaces take their column and have no glyph.
void console_wrap(const ConsoleLine& line, std::size_t columns, std::vector<ConsoleGlyphRow>& rows);

} // namespace gui_forms::automation
