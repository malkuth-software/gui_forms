#include "console_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::automation {

namespace {

// The lines dropped at once when the record is full, so the vector is not
// shifted for every line.
constexpr std::size_t drop_batch = 200U;

constexpr std::string_view call_mark = "call ";
constexpr std::string_view reply_mark = "‹ ";
constexpr std::string_view ellipsis = "…";

// One step of SplitMix64: a well-mixed 64-bit hash of `value`.
std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// A number in [0, 1) from 24 bits of `hash` at bit `shift`.
double unit(std::uint64_t hash, unsigned shift) noexcept {
    const double fraction = static_cast<double>((hash >> shift) & 0xffffffULL) / 16777216.0;
    return fraction;
}

// `text` on one line: line breaks and tabs as spaces.
std::string one_line(std::string_view text) {
    std::string line{};
    for (const char character : text) {
        line += character == '\n' || character == '\r' || character == '\t' ? ' ' : character;
    }
    return line;
}

// The text's first word with its colon ("ok:", "not disconnected:"), up
// to the first ": ", as the instruction; the rest as the value. Text
// without ": " in its first 48 bytes is all value.
void split_lead(std::string_view text, std::vector<ConsoleSpan>& spans) {
    const std::size_t colon = text.find(": ");
    if (colon == std::string_view::npos || colon > 48U) {
        spans.push_back(ConsoleSpan{console_clip(text, ConsoleLog::maximum_value_bytes), ConsoleTone::value});
        return;
    }
    spans.push_back(ConsoleSpan{std::string(text.substr(0U, colon + 1U)), ConsoleTone::instruction});
    spans.push_back(
        ConsoleSpan{console_clip(text.substr(colon + 1U), ConsoleLog::maximum_value_bytes), ConsoleTone::value});
}

} // namespace

std::string console_clip(std::string_view text, std::size_t limit) {
    if (text.size() <= limit) return std::string(text);
    std::size_t cut = limit;
    while (cut > 0U && (static_cast<unsigned char>(text[cut]) & 0xc0U) == 0x80U) --cut;
    std::string clipped(text.substr(0U, cut));
    clipped += ellipsis;
    return clipped;
}

void ConsoleLog::append(std::vector<ConsoleSpan> spans) {
    const stxlib::ScopedLock lock(mutex_);
    if (lines_.size() >= maximum_lines) {
        lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(drop_batch));
    }
    ++serial_;
    ConsoleLine line{};
    line.serial = serial_;
    line.session = session_;
    line.spans = std::move(spans);
    lines_.push_back(std::move(line));
}

void ConsoleLog::set_session(std::string label) {
    const stxlib::ScopedLock lock(mutex_);
    session_ = std::move(label);
}

// The "[label] " a labelled session's call line begins with, or nothing.
void ConsoleLog::label_span(std::vector<ConsoleSpan>& spans) const {
    const stxlib::ScopedLock lock(mutex_);
    if (session_.empty()) return;
    spans.push_back(ConsoleSpan{"[" + session_ + "] ", ConsoleTone::instruction});
}

void ConsoleLog::record_call(std::string_view tool, std::string_view arguments_json) {
    std::vector<ConsoleSpan> spans{};
    label_span(spans);
    spans.push_back(ConsoleSpan{std::string(call_mark) + std::string(tool), ConsoleTone::instruction});
    if (!arguments_json.empty() && arguments_json != "{}") {
        spans.push_back(ConsoleSpan{" " + console_clip(one_line(arguments_json), maximum_value_bytes),
                                    ConsoleTone::value});
    }
    append(std::move(spans));
}

void ConsoleLog::record_typed(std::string_view line) {
    std::vector<ConsoleSpan> spans{};
    label_span(spans);
    const std::string flat = one_line(line);
    const std::size_t space = flat.find(' ');
    if (space == std::string::npos) {
        spans.push_back(ConsoleSpan{console_clip(flat, maximum_value_bytes), ConsoleTone::instruction});
    } else {
        spans.push_back(ConsoleSpan{flat.substr(0U, space), ConsoleTone::instruction});
        spans.push_back(
            ConsoleSpan{console_clip(std::string_view(flat).substr(space), maximum_value_bytes), ConsoleTone::value});
    }
    append(std::move(spans));
}

void ConsoleLog::record_reply(const ToolReply& reply) {
    if (const ToolFailure* const failure = reply.failure_value()) {
        std::vector<ConsoleSpan> spans{};
        spans.push_back(
            ConsoleSpan{std::string(reply_mark) + tool_error_name((*failure).error) + ":", ConsoleTone::instruction});
        spans.push_back(ConsoleSpan{" " + console_clip(one_line((*failure).message), maximum_value_bytes),
                                    ConsoleTone::value});
        append(std::move(spans));
        if (!(*failure).hint.empty()) {
            std::vector<ConsoleSpan> hint{};
            hint.push_back(ConsoleSpan{"  " + console_clip(one_line((*failure).hint), maximum_value_bytes),
                                       ConsoleTone::value});
            append(std::move(hint));
        }
        return;
    }
    const ToolSuccess& success = *reply.success_value();
    const std::string_view text(success.text);
    std::size_t start = 0U;
    std::size_t shown = 0U;
    std::size_t total = 0U;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        const std::string_view line = text.substr(start, end - start);
        if (!line.empty()) {
            ++total;
            if (shown < maximum_reply_lines) {
                std::vector<ConsoleSpan> spans{};
                if (shown == 0U) {
                    spans.push_back(ConsoleSpan{std::string(reply_mark), ConsoleTone::instruction});
                    split_lead(line, spans);
                } else {
                    spans.push_back(ConsoleSpan{"  " + console_clip(line, maximum_value_bytes), ConsoleTone::value});
                }
                append(std::move(spans));
                ++shown;
            }
        }
        if (end == text.size()) break;
        start = end + 1U;
    }
    if (total == 0U) {
        std::vector<ConsoleSpan> spans{};
        spans.push_back(ConsoleSpan{std::string(reply_mark) + "ok", ConsoleTone::instruction});
        append(std::move(spans));
    }
    if (total > shown) {
        std::vector<ConsoleSpan> spans{};
        spans.push_back(ConsoleSpan{"  (" + std::to_string(total - shown) + " more lines)", ConsoleTone::value});
        append(std::move(spans));
    }
    for (const ToolImage& image : success.images) {
        std::vector<ConsoleSpan> spans{};
        spans.push_back(ConsoleSpan{"  [" + image.mime_type + ", " + std::to_string(image.bytes.size()) + " bytes]",
                                    ConsoleTone::value});
        append(std::move(spans));
    }
}

void ConsoleLog::record_note(std::string_view note) {
    std::vector<ConsoleSpan> spans{};
    spans.push_back(ConsoleSpan{"# " + console_clip(one_line(note), maximum_value_bytes), ConsoleTone::instruction});
    append(std::move(spans));
}

std::uint64_t ConsoleLog::serial() const {
    const stxlib::ScopedLock lock(mutex_);
    return serial_;
}

void ConsoleLog::copy_since(std::uint64_t after, std::vector<ConsoleLine>& lines) const {
    const stxlib::ScopedLock lock(mutex_);
    for (const ConsoleLine& line : lines_) {
        if (line.serial > after) lines.push_back(line);
    }
}

std::string ConsoleLog::transcript_text() const {
    const stxlib::ScopedLock lock(mutex_);
    std::string text{};
    std::string session{};
    for (const ConsoleLine& line : lines_) {
        if (line.session != session && !line.session.empty()) text += "== session " + line.session + "\n";
        session = line.session;
        for (const ConsoleSpan& span : line.spans) text += span.text;
        text += '\n';
    }
    return text;
}

std::uint64_t console_run_hash(std::uint64_t serial, std::size_t run) noexcept {
    const std::uint64_t hash = mix(mix(serial) ^ (static_cast<std::uint64_t>(run) * 0x2545f4914f6cdd1dULL));
    return hash;
}

ConsoleShade console_shade(ConsoleTone tone, std::uint64_t serial, std::size_t run) noexcept {
    const std::uint64_t hash = console_run_hash(serial, run);
    ConsoleShade shade{};
    // c clusters: the instruction within 0.10 of 0, a value within 0.10 of
    // 1, the spread weighted toward the end (the square of a uniform).
    const double spread = unit(hash, 0U);
    const double offset = 0.10 * spread * spread;
    shade.c = tone == ConsoleTone::instruction ? offset : 1.0 - offset;
    // v hovers around 0.8: plus or minus 0.04.
    shade.v = 0.8 + 0.08 * (unit(hash, 24U) - 0.5);
    return shade;
}

ConsoleRgb console_formula(ConsoleShade shade) noexcept {
    const double c = std::clamp(shade.c, 0.0, 1.0);
    const double v = std::clamp(shade.v, 0.0, 1.0);
    ConsoleRgb rgb{};
    rgb.r = c * 0.5 * v + (1.0 - c) * v;
    rgb.g = c * v + (1.0 - c) * 0.5 * v;
    rgb.b = c * 0.25 * v;
    return rgb;
}

Color console_colour(ConsoleShade shade, std::uint64_t seed) noexcept {
    const ConsoleRgb rgb = console_formula(shade);
    const std::uint64_t noise = mix(seed ^ 0x5851f42d4c957f2dULL);
    // A triangular dither of one 8-bit step: the sum of two uniforms in
    // [0, 1) less 1, before rounding.
    const double channels[3] = {rgb.r, rgb.g, rgb.b};
    std::uint8_t levels[3] = {};
    for (unsigned index = 0U; index < 3U; ++index) {
        const double dither = unit(noise, index * 16U) + unit(mix(noise + index), 8U) - 1.0;
        const double level = std::floor(channels[index] * 255.0 + 0.5 + dither);
        levels[index] = static_cast<std::uint8_t>(std::clamp(level, 0.0, 255.0));
    }
    const Color colour{levels[0], levels[1], levels[2], 255U};
    return colour;
}

} // namespace gui_forms::automation
