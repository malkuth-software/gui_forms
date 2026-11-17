#pragma once

// Writing JSON text through a standard stream, whatever the program's
// global locale (A-gf-core-11-F1, A-gf-core-16-F2). A default stream takes
// the global C++ locale, whose numeric facets may group digits or write a
// decimal comma; a stream readied here writes numbers in the classic
// locale. A double written as JsonNumber is the shortest text that reads
// back to the same value, or null when it is not finite (JSON has no
// infinity or NaN).

#include <ostream>
#include <string_view>

namespace gui_forms::detail {

// Imbues stream with the classic locale, so integers are never grouped and
// a double written directly has a decimal point. Its precision is the
// writer's. Call it once, before the first number is written.
void prepare_json_stream(std::ostream& stream);

// A double to write as a JSON number (operator<< below).
struct JsonNumber final {
    double value{};
};

// Writes number.value by std::to_chars (never locale-dependent): the
// shortest round-trip text, or null when it is infinite or NaN.
std::ostream& operator<<(std::ostream& stream, JsonNumber number);

// Writes value as a JSON string, quoted: a quote and a backslash are
// escaped, a control byte is written as \n, \r, \t or \u00XX, and a byte
// that does not start a well-formed UTF-8 sequence (validate_utf8's rules)
// as �, so the document stays valid UTF-8.
void append_json_string(std::ostream& stream, std::string_view value);

} // namespace gui_forms::detail
