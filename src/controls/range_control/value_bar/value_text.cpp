#include "value_text.hpp"

#include "gui_forms/engine/engine_expression/engine_expression.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <span>
#include <system_error>

// The folded power and the scale factors are computed without fused
// multiply-add, as the expression engine computes: the same text gives the
// same value on every host.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms {

namespace {

constexpr double pi_value = 3.14159265358979323846;
constexpr double radians_per_degree = pi_value / 180.0;

// Each unit's suffixes, matched exactly (case matters: "m" is a metre, and
// no suffix is "M").
constexpr std::array<UnitSuffix, 1> percent_suffixes{{{.suffix = "%", .factor = 0.01}}};
constexpr std::array<UnitSuffix, 8> length_suffixes{{
    {.suffix = "mm", .factor = 0.001},
    {.suffix = "cm", .factor = 0.01},
    {.suffix = "m", .factor = 1.0},
    {.suffix = "km", .factor = 1000.0},
    {.suffix = "in", .factor = 0.0254},
    {.suffix = "ft", .factor = 0.3048},
    {.suffix = "yd", .factor = 0.9144},
    {.suffix = "mi", .factor = 1609.344},
}};
// "\xC2\xB0" is the degree sign, U+00B0, in UTF-8.
constexpr std::array<UnitSuffix, 3> angle_suffixes{{
    {.suffix = "deg", .factor = radians_per_degree},
    {.suffix = "\xC2\xB0", .factor = radians_per_degree},
    {.suffix = "rad", .factor = 1.0},
}};
constexpr std::array<UnitSuffix, 4> time_suffixes{{
    {.suffix = "ms", .factor = 0.001},
    {.suffix = "s", .factor = 1.0},
    {.suffix = "min", .factor = 60.0},
    {.suffix = "h", .factor = 3600.0},
}};

constexpr UnitFormat none_format{
    .unit = ValueUnit::none, .display = {.suffix = "", .factor = 1.0}, .suffixes = {}, .space_before_suffix = false};
constexpr UnitFormat percent_format{.unit = ValueUnit::percent,
                                    .display = {.suffix = "%", .factor = 0.01},
                                    .suffixes = percent_suffixes,
                                    .space_before_suffix = false};
constexpr UnitFormat length_format{.unit = ValueUnit::length,
                                   .display = {.suffix = "m", .factor = 1.0},
                                   .suffixes = length_suffixes,
                                   .space_before_suffix = true};
constexpr UnitFormat angle_format{.unit = ValueUnit::angle,
                                  .display = {.suffix = "\xC2\xB0", .factor = radians_per_degree},
                                  .suffixes = angle_suffixes,
                                  .space_before_suffix = false};
constexpr UnitFormat time_format{.unit = ValueUnit::time,
                                 .display = {.suffix = "s", .factor = 1.0},
                                 .suffixes = time_suffixes,
                                 .space_before_suffix = true};

} // namespace

const UnitFormat& unit_format(ValueUnit unit) noexcept {
    switch (unit) {
    case ValueUnit::percent: return percent_format;
    case ValueUnit::length: return length_format;
    case ValueUnit::angle: return angle_format;
    case ValueUnit::time: return time_format;
    case ValueUnit::none: return none_format;
    }
    // A value outside the enumeration is a plain number.
    return none_format;
}

std::string_view value_text_defect_name(ValueTextDefect defect) noexcept {
    switch (defect) {
    case ValueTextDefect::none: return "none";
    case ValueTextDefect::empty: return "empty";
    case ValueTextDefect::too_long: return "too_long";
    case ValueTextDefect::unexpected_character: return "unexpected_character";
    case ValueTextDefect::unknown_unit: return "unknown_unit";
    case ValueTextDefect::unknown_name: return "unknown_name";
    case ValueTextDefect::missing_operand: return "missing_operand";
    case ValueTextDefect::unexpected_token: return "unexpected_token";
    case ValueTextDefect::too_deep: return "too_deep";
    case ValueTextDefect::bad_operand: return "bad_operand";
    case ValueTextDefect::over_budget: return "over_budget";
    case ValueTextDefect::not_finite: return "not_finite";
    }
    return "unknown";
}

namespace value_text {

namespace {

enum class TokenKind : std::uint8_t { end, number, name, plus, minus, star, slash, caret, open, close, comma, invalid };

// One token: where it starts and how many bytes it takes (a number's
// suffix included). A number's value has its suffix's factor applied;
// `suffixed` says it had one. An invalid token carries its defect.
struct Token final {
    TokenKind kind{TokenKind::end};
    std::size_t position{};
    std::size_t length{};
    double number{};
    bool suffixed{};
    ValueTextDefect defect{ValueTextDefect::none};
};

bool is_digit(char character) noexcept {
    const bool result = character >= '0' && character <= '9';
    return result;
}

bool is_letter(char character) noexcept {
    const bool result = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    return result;
}

bool is_space(char character) noexcept {
    const bool result = character == ' ' || character == '\t';
    return result;
}

// The degree sign's first byte; its second is 0xB0.
constexpr char degree_lead = '\xC2';
constexpr char degree_trail = '\xB0';

// The recursive-descent parser of value_text.hpp's grammar. It writes the
// postfix program into fixed storage as it goes; the first defect stops it.
// One per parse, on the caller's stack.
class Parser final {
public:
    Parser(std::string_view text, const UnitFormat& format) noexcept : text_(text), format_(&format) {}

    [[nodiscard]] ValueTextResult run() noexcept;

private:
    [[nodiscard]] Token scan(std::size_t from) const noexcept;
    [[nodiscard]] Token scan_number(std::size_t start) const noexcept;
    void advance() noexcept { current_ = scan(current_.position + current_.length); }

    bool expression() noexcept;
    bool term() noexcept;
    bool signed_value() noexcept;
    bool power() noexcept;
    bool primary() noexcept;
    bool function(std::string_view name, std::size_t position) noexcept;
    bool close_parenthesis() noexcept;

    bool emit(ExpressionInstruction instruction) noexcept;
    // Evaluates the instructions from `start` to the end, by the engine,
    // and removes them; false (a defect recorded) when they make no value.
    bool fold(std::size_t start, double& value) noexcept;
    bool enter() noexcept;
    void leave() noexcept { --depth_; }
    bool fail(ValueTextDefect defect, std::size_t position) noexcept;

    std::string_view text_;
    const UnitFormat* format_;
    Token current_{};
    // The program: capacity maximum_expression_operations, live count_.
    std::array<ExpressionInstruction, maximum_expression_operations> program_{};
    std::size_t count_{};
    std::size_t depth_{};
    // Whether any number had a suffix (the text names a unit), and whether
    // the last primary was such a number (for "1m 50cm").
    bool named_unit_{};
    bool last_suffixed_{};
    ValueTextDefect defect_{ValueTextDefect::none};
    std::size_t defect_position_{};
};

Token Parser::scan_number(std::size_t start) const noexcept {
    Token token{};
    token.kind = TokenKind::number;
    token.position = start;
    const char* const first = text_.data() + start;
    const char* const last = text_.data() + text_.size();
    double parsed{};
    const std::from_chars_result read = std::from_chars(first, last, parsed, std::chars_format::general);
    if (read.ec != std::errc{}) {
        token.kind = TokenKind::invalid;
        token.length = 1U;
        token.defect = read.ec == std::errc::result_out_of_range ? ValueTextDefect::not_finite
                                                                 : ValueTextDefect::unexpected_character;
        return token;
    }
    const std::size_t number_end = static_cast<std::size_t>(read.ptr - text_.data());
    token.number = parsed;
    token.length = number_end - start;
    // A suffix may follow after spaces: a run of letters, a percent sign or
    // the degree sign.
    std::size_t suffix_start = number_end;
    while (suffix_start < text_.size() && is_space(text_[suffix_start])) ++suffix_start;
    std::size_t suffix_end = suffix_start;
    if (suffix_end < text_.size() && text_[suffix_end] == '%') {
        ++suffix_end;
    } else if (suffix_end + 1U < text_.size() && text_[suffix_end] == degree_lead &&
               text_[suffix_end + 1U] == degree_trail) {
        suffix_end += 2U;
    } else {
        while (suffix_end < text_.size() && is_letter(text_[suffix_end])) ++suffix_end;
    }
    if (suffix_end == suffix_start) return token;
    const std::string_view suffix = text_.substr(suffix_start, suffix_end - suffix_start);
    for (const UnitSuffix& candidate : (*format_).suffixes) {
        if (candidate.suffix != suffix) continue;
        token.number = parsed * candidate.factor;
        token.suffixed = true;
        token.length = suffix_end - start;
        return token;
    }
    token.kind = TokenKind::invalid;
    token.position = suffix_start;
    token.length = suffix_end - suffix_start;
    token.defect = ValueTextDefect::unknown_unit;
    return token;
}

Token Parser::scan(std::size_t from) const noexcept {
    std::size_t start = from;
    while (start < text_.size() && is_space(text_[start])) ++start;
    Token token{};
    token.position = start;
    if (start >= text_.size()) return token;
    const char character = text_[start];
    const bool fraction = character == '.' && start + 1U < text_.size() && is_digit(text_[start + 1U]);
    if (is_digit(character) || fraction) {
        const Token number = scan_number(start);
        return number;
    }
    token.length = 1U;
    if (is_letter(character)) {
        std::size_t end = start + 1U;
        while (end < text_.size() && (is_letter(text_[end]) || is_digit(text_[end]) || text_[end] == '_')) ++end;
        token.kind = TokenKind::name;
        token.length = end - start;
        return token;
    }
    switch (character) {
    case '+': token.kind = TokenKind::plus; return token;
    case '-': token.kind = TokenKind::minus; return token;
    case '*': token.kind = TokenKind::star; return token;
    case '/': token.kind = TokenKind::slash; return token;
    case '^': token.kind = TokenKind::caret; return token;
    case '(': token.kind = TokenKind::open; return token;
    case ')': token.kind = TokenKind::close; return token;
    case ',': token.kind = TokenKind::comma; return token;
    default: break;
    }
    token.kind = TokenKind::invalid;
    token.defect = ValueTextDefect::unexpected_character;
    return token;
}

bool Parser::fail(ValueTextDefect defect, std::size_t position) noexcept {
    if (defect_ == ValueTextDefect::none) {
        defect_ = defect;
        defect_position_ = position;
    }
    return false;
}

bool Parser::emit(ExpressionInstruction instruction) noexcept {
    if (count_ >= program_.size()) return fail(ValueTextDefect::over_budget, current_.position);
    program_[count_] = instruction;
    ++count_;
    return true;
}

bool Parser::enter() noexcept {
    if (depth_ >= maximum_value_text_depth) return fail(ValueTextDefect::too_deep, current_.position);
    ++depth_;
    return true;
}

bool Parser::fold(std::size_t start, double& value) noexcept {
    const std::span<const ExpressionInstruction> code(program_.data() + start, count_ - start);
    EngineExpression program{};
    const ExpressionCompilation compiled = program.compile(code, 0.0, ExpressionLimits{});
    if (compiled.defect == ExpressionDefect::over_budget) return fail(ValueTextDefect::over_budget, current_.position);
    if (compiled.defect != ExpressionDefect::none) return fail(ValueTextDefect::missing_operand, current_.position);
    const ExpressionEnvironment environment{};
    const ExpressionResult result = program.evaluate(environment);
    if (result.fallback != ExpressionFallback::none) return fail(ValueTextDefect::not_finite, current_.position);
    count_ = start;
    value = result.value;
    return true;
}

// Each pass of the loops below consumes at least one byte of the text, so
// they end within maximum_value_text_bytes passes.
bool Parser::expression() noexcept {
    if (!term()) return false;
    for (std::size_t pass = 0U; pass < maximum_value_text_bytes; ++pass) {
        const TokenKind kind = current_.kind;
        if (kind == TokenKind::plus || kind == TokenKind::minus) {
            advance();
            if (!term()) return false;
            const ExpressionOperation operation =
                kind == TokenKind::plus ? ExpressionOperation::add : ExpressionOperation::subtract;
            if (!emit(ExpressionInstruction::apply(operation))) return false;
            continue;
        }
        // "1m 50cm": a suffixed number right after another adds to it.
        const bool joined = kind == TokenKind::number && current_.suffixed && last_suffixed_;
        if (!joined) return true;
        if (!term()) return false;
        if (!emit(ExpressionInstruction::apply(ExpressionOperation::add))) return false;
    }
    return true;
}

bool Parser::term() noexcept {
    if (!signed_value()) return false;
    for (std::size_t pass = 0U; pass < maximum_value_text_bytes; ++pass) {
        const TokenKind kind = current_.kind;
        if (kind != TokenKind::star && kind != TokenKind::slash) return true;
        advance();
        if (!signed_value()) return false;
        const ExpressionOperation operation =
            kind == TokenKind::star ? ExpressionOperation::multiply : ExpressionOperation::divide;
        if (!emit(ExpressionInstruction::apply(operation))) return false;
    }
    return true;
}

bool Parser::signed_value() noexcept {
    const TokenKind kind = current_.kind;
    if (kind != TokenKind::minus && kind != TokenKind::plus) {
        const bool powered = power();
        return powered;
    }
    advance();
    if (!enter()) return false;
    const bool operand = signed_value();
    leave();
    if (!operand) return false;
    if (kind == TokenKind::plus) return true;
    const bool negated = emit(ExpressionInstruction::apply(ExpressionOperation::negate));
    return negated;
}

bool Parser::power() noexcept {
    const std::size_t base_start = count_;
    if (!primary()) return false;
    if (current_.kind != TokenKind::caret) return true;
    const std::size_t caret_position = current_.position;
    advance();
    const std::size_t exponent_start = count_;
    if (!enter()) return false;
    const bool read = signed_value();
    leave();
    if (!read) return false;
    // The exponent lies after the base in the program: fold it first.
    double exponent{};
    if (!fold(exponent_start, exponent)) return false;
    double base{};
    if (!fold(base_start, base)) return false;
    const double limit = static_cast<double>(maximum_value_text_exponent);
    if (exponent != std::floor(exponent) || std::abs(exponent) > limit) {
        return fail(ValueTextDefect::bad_operand, caret_position);
    }
    const std::int32_t steps = static_cast<std::int32_t>(std::abs(exponent));
    double product = 1.0;
    for (std::int32_t step = 0; step < steps; ++step) product = product * base;
    if (exponent < 0.0) {
        if (product == 0.0) return fail(ValueTextDefect::not_finite, caret_position);
        product = 1.0 / product;
    }
    if (!std::isfinite(product)) return fail(ValueTextDefect::not_finite, caret_position);
    last_suffixed_ = false;
    const bool emitted = emit(ExpressionInstruction::number(product));
    return emitted;
}

bool Parser::close_parenthesis() noexcept {
    if (current_.kind != TokenKind::close) return fail(ValueTextDefect::missing_operand, current_.position);
    advance();
    last_suffixed_ = false;
    return true;
}

bool Parser::primary() noexcept {
    const Token token = current_;
    switch (token.kind) {
    case TokenKind::number: {
        advance();
        last_suffixed_ = token.suffixed;
        if (token.suffixed) named_unit_ = true;
        const bool emitted = emit(ExpressionInstruction::number(token.number));
        return emitted;
    }
    case TokenKind::open: {
        advance();
        if (!enter()) return false;
        const bool inner = expression();
        leave();
        if (!inner) return false;
        const bool closed = close_parenthesis();
        return closed;
    }
    case TokenKind::name: {
        advance();
        const std::string_view name = text_.substr(token.position, token.length);
        if (name == "pi") {
            last_suffixed_ = false;
            const bool emitted = emit(ExpressionInstruction::number(pi_value));
            return emitted;
        }
        const bool called = function(name, token.position);
        return called;
    }
    case TokenKind::invalid: return fail(token.defect, token.position);
    case TokenKind::end: return fail(ValueTextDefect::missing_operand, token.position);
    case TokenKind::plus:
    case TokenKind::minus:
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::caret:
    case TokenKind::close:
    case TokenKind::comma: return fail(ValueTextDefect::missing_operand, token.position);
    }
    return fail(ValueTextDefect::unexpected_token, token.position);
}

// sqrt, min and max, after their name.
bool Parser::function(std::string_view name, std::size_t position) noexcept {
    const bool root = name == "sqrt";
    const bool smallest = name == "min";
    const bool largest = name == "max";
    if (!root && !smallest && !largest) return fail(ValueTextDefect::unknown_name, position);
    if (current_.kind != TokenKind::open) return fail(ValueTextDefect::missing_operand, current_.position);
    advance();
    if (!enter()) return false;
    const std::size_t start = count_;
    bool read = expression();
    if (read && root) {
        double operand{};
        read = fold(start, operand);
        if (read && operand < 0.0) read = fail(ValueTextDefect::bad_operand, position);
        if (read) read = emit(ExpressionInstruction::number(std::sqrt(operand)));
    }
    if (read && !root) {
        if (current_.kind != TokenKind::comma) read = fail(ValueTextDefect::missing_operand, current_.position);
        const ExpressionOperation operation = smallest ? ExpressionOperation::minimum : ExpressionOperation::maximum;
        for (std::size_t pass = 0U; read && pass < maximum_value_text_bytes; ++pass) {
            if (current_.kind != TokenKind::comma) break;
            advance();
            read = expression() && emit(ExpressionInstruction::apply(operation));
        }
    }
    leave();
    if (!read) return false;
    const bool closed = close_parenthesis();
    return closed;
}

ValueTextResult Parser::run() noexcept {
    ValueTextResult result{};
    if (text_.size() > maximum_value_text_bytes) {
        result.defect = ValueTextDefect::too_long;
        result.position = maximum_value_text_bytes;
        return result;
    }
    current_ = scan(0U);
    if (current_.kind == TokenKind::end) {
        result.defect = ValueTextDefect::empty;
        return result;
    }
    double value{};
    bool parsed = expression();
    if (parsed && current_.kind == TokenKind::invalid) parsed = fail(current_.defect, current_.position);
    if (parsed && current_.kind != TokenKind::end) parsed = fail(ValueTextDefect::unexpected_token, current_.position);
    if (parsed) parsed = fold(0U, value);
    if (parsed && !named_unit_) value = value * (*format_).display.factor;
    if (parsed && !std::isfinite(value)) parsed = fail(ValueTextDefect::not_finite, text_.size());
    if (!parsed) {
        result.defect = defect_;
        result.position = defect_position_;
        return result;
    }
    result.value = value;
    return result;
}

} // namespace

ValueTextResult parse_value_text(std::string_view text, ValueUnit unit) noexcept {
    Parser parser(text, unit_format(unit));
    const ValueTextResult result = parser.run();
    return result;
}

std::size_t format_value_text(double value, ValueUnit unit, std::int32_t precision, std::span<char> out) noexcept {
    const UnitFormat& format = unit_format(unit);
    const std::int32_t decimals = std::clamp(precision, std::int32_t{0}, maximum_value_bar_precision);
    const double shown = value / format.display.factor;
    // Room for the largest double in fixed notation (309 digits) with its
    // sign, point and decimals.
    std::array<char, 352> digits{};
    char* const first = digits.data();
    char* const last = digits.data() + digits.size();
    std::to_chars_result written = std::to_chars(first, last, shown, std::chars_format::fixed, decimals);
    if (written.ec != std::errc{}) written = std::to_chars(first, last, shown, std::chars_format::general);
    const std::size_t length = written.ec == std::errc{} ? static_cast<std::size_t>(written.ptr - first) : 0U;
    // A negative value that rounds to zero shows as zero, not "-0.00".
    std::size_t begin = 0U;
    if (length > 1U && digits[0] == '-') {
        bool zero = true;
        for (std::size_t index = 1U; index < length; ++index) {
            if (digits[index] != '0' && digits[index] != '.') zero = false;
        }
        if (zero) begin = 1U;
    }
    const std::size_t separator = format.display.suffix.empty() || !format.space_before_suffix ? 0U : 1U;
    const std::size_t needed = length - begin + separator + format.display.suffix.size();
    if (needed > out.size()) return 0U;
    std::size_t at = 0U;
    for (std::size_t index = begin; index < length; ++index) {
        out[at] = digits[index];
        ++at;
    }
    if (separator != 0U) {
        out[at] = ' ';
        ++at;
    }
    for (const char character : format.display.suffix) {
        out[at] = character;
        ++at;
    }
    return at;
}

void format_value_text(double value, ValueUnit unit, std::int32_t precision, std::string& out) {
    std::array<char, maximum_formatted_value_bytes> text{};
    const std::size_t length = format_value_text(value, unit, precision, std::span<char>(text));
    out.assign(text.data(), length);
}

} // namespace value_text

} // namespace gui_forms
