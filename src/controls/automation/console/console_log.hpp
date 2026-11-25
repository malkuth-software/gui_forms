#pragma once

// The MCP console's record (MCP 1.0.5; the owner's console ruling,
// DECISIONS 2026-11-24 "MCP console overlay and attach"): every MCP call
// and its reply, kept as lines of toned spans for the console overlay
// (console_overlay.hpp), which shows them to the user like a terminal. The
// model never reads it: it is the human's window onto the traffic, and it
// is never in a capture.
//
// Tones follow the owner's colour formula (console_colour): the
// instruction (the tool's name, a reply's leading word) is c near 0,
// orange; values (arguments, reply text) and wrapped continuations are c
// near 1, green.
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
};

struct ConsoleSpan final {
    std::string text{};
    ConsoleTone tone{ConsoleTone::value};
};

struct ConsoleLine final {
    // 1 for the first line recorded; never reused.
    std::uint64_t serial{};
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

    // A call as it begins: "› tool {arguments}".
    void record_call(std::string_view tool, std::string_view arguments_json);
    // Its reply: "‹ ok: ..." with the first lines of the text and a note of
    // each image, or "‹ <error>: message" for a failure.
    void record_reply(const ToolReply& reply);
    // A line of the run's own ("connected: ...", "disconnected"), all
    // instruction.
    void record_note(std::string_view note);

    // The last line's serial (0 before any).
    [[nodiscard]] std::uint64_t serial() const;
    // The lines after `after` still kept, oldest first, appended to `lines`.
    void copy_since(std::uint64_t after, std::vector<ConsoleLine>& lines) const;

private:
    void append(std::vector<ConsoleSpan> spans);

    mutable stxlib::Mutex mutex_{};
    std::vector<ConsoleLine> lines_{};
    std::uint64_t serial_{};
};

// `text` cut to at most `limit` bytes at a UTF-8 boundary, with U+2026 after
// it when it was cut.
[[nodiscard]] std::string console_clip(std::string_view text, std::size_t limit);

// ---- the owner's colour formula ----

// One glyph run's shade: c from 0 (instruction, orange) to 1 (value,
// green), v the brightness, near 0.8. Over black:
//   r = c * 0.5v + (1 - c) * v
//   g = c * v + (1 - c) * 0.5v
//   b = c * 0.25v
struct ConsoleShade final {
    double c{};
    double v{};
};

// The shade of run `run` of line `serial` in `tone`: c clusters near 0 or
// near 1 and v hovers around 0.8, each with a slight variation drawn from a
// hash of the serial and the run, so a repaint draws the same colours.
[[nodiscard]] ConsoleShade console_shade(ConsoleTone tone, std::uint64_t serial, std::size_t run) noexcept;
// The formula's colour for `shade`, each channel quantized to 8 bits with
// a triangular dither drawn from `seed` (the run's hash), so neighbouring
// runs of one tone do not band into identical steps.
[[nodiscard]] Color console_colour(ConsoleShade shade, std::uint64_t seed) noexcept;
// The formula without dither or quantization, for tests: {r, g, b} in 0..1.
struct ConsoleRgb final {
    double r{};
    double g{};
    double b{};
};
[[nodiscard]] ConsoleRgb console_formula(ConsoleShade shade) noexcept;
// The hash console_shade and console_colour draw from.
[[nodiscard]] std::uint64_t console_run_hash(std::uint64_t serial, std::size_t run) noexcept;

} // namespace gui_forms::automation
