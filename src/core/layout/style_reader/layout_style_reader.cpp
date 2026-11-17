// The layout style reader (stage L, phase L2; PLAN sections 2.2 to 2.5).
// One pass over the declarations of an inline style:
// - the declaration scanner walks the tokens once, splitting declarations at
//   top-level semicolons and matching brackets on a fixed stack of
//   maximum_nesting_depth entries, so each value arrives as a list of its
//   significant tokens with each opener's closer already known;
// - the property readers then consume one value each, left to right, into
//   the scratch style. Only calc() nests, and its reader recurses at most
//   maximum_nesting_depth deep, the depth the scanner admits.

#include "layout_style_reader.hpp"

#include "../../theme_language/syntax_tree/theme_syntax_tree.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <utility>

// Identical bits on every host need the same operations in the same order:
// no fused multiply-add in calc() (as theme_value.cpp).
// Off in every build, whatever GUI_FORMS_REFERENCE_ARITHMETIC says
// (CMakeLists.txt, "Floating-point contraction").
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::layout_style {

namespace {

using theme_language::ByteOffset;
using theme_language::DiagnosticCode;
using theme_language::SourcePositionCursor;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeNumber;
using theme_language::ThemeSource;
using theme_language::Token;
using theme_language::TokenKind;
using theme_language::clipped_theme_detail;
using theme_language::equals_ascii_folded;

// -- properties -----------------------------------------------------------------

enum class PropertyId : std::uint8_t {
    display,
    position,
    inset,
    top,
    right,
    bottom,
    left,
    width,
    height,
    min_width,
    min_height,
    max_width,
    max_height,
    margin,
    margin_top,
    margin_right,
    margin_bottom,
    margin_left,
    padding,
    padding_top,
    padding_right,
    padding_bottom,
    padding_left,
    box_sizing,
    gap,
    row_gap,
    column_gap,
    flex_direction,
    flex_wrap,
    flex_flow,
    flex,
    flex_grow,
    flex_shrink,
    flex_basis,
    order,
    justify_content,
    align_content,
    justify_items,
    align_items,
    justify_self,
    align_self,
    place_items,
    place_self,
    grid_template_columns,
    grid_template_rows,
    grid_template_areas,
    grid_auto_flow,
    grid_auto_columns,
    grid_auto_rows,
    grid_column,
    grid_row,
    grid_area,
    grid_column_start,
    grid_column_end,
    grid_row_start,
    grid_row_end,
    text_align,
    vertical_align,
};

struct PropertyEntry final {
    std::string_view name{};
    PropertyId id{PropertyId::display};
};

inline constexpr std::size_t admitted_property_count = 58U;

constexpr std::array<PropertyEntry, admitted_property_count> admitted_properties{{
    {"display", PropertyId::display},
    {"position", PropertyId::position},
    {"inset", PropertyId::inset},
    {"top", PropertyId::top},
    {"right", PropertyId::right},
    {"bottom", PropertyId::bottom},
    {"left", PropertyId::left},
    {"width", PropertyId::width},
    {"height", PropertyId::height},
    {"min-width", PropertyId::min_width},
    {"min-height", PropertyId::min_height},
    {"max-width", PropertyId::max_width},
    {"max-height", PropertyId::max_height},
    {"margin", PropertyId::margin},
    {"margin-top", PropertyId::margin_top},
    {"margin-right", PropertyId::margin_right},
    {"margin-bottom", PropertyId::margin_bottom},
    {"margin-left", PropertyId::margin_left},
    {"padding", PropertyId::padding},
    {"padding-top", PropertyId::padding_top},
    {"padding-right", PropertyId::padding_right},
    {"padding-bottom", PropertyId::padding_bottom},
    {"padding-left", PropertyId::padding_left},
    {"box-sizing", PropertyId::box_sizing},
    {"gap", PropertyId::gap},
    {"row-gap", PropertyId::row_gap},
    {"column-gap", PropertyId::column_gap},
    {"flex-direction", PropertyId::flex_direction},
    {"flex-wrap", PropertyId::flex_wrap},
    {"flex-flow", PropertyId::flex_flow},
    {"flex", PropertyId::flex},
    {"flex-grow", PropertyId::flex_grow},
    {"flex-shrink", PropertyId::flex_shrink},
    {"flex-basis", PropertyId::flex_basis},
    {"order", PropertyId::order},
    {"justify-content", PropertyId::justify_content},
    {"align-content", PropertyId::align_content},
    {"justify-items", PropertyId::justify_items},
    {"align-items", PropertyId::align_items},
    {"justify-self", PropertyId::justify_self},
    {"align-self", PropertyId::align_self},
    {"place-items", PropertyId::place_items},
    {"place-self", PropertyId::place_self},
    {"grid-template-columns", PropertyId::grid_template_columns},
    {"grid-template-rows", PropertyId::grid_template_rows},
    {"grid-template-areas", PropertyId::grid_template_areas},
    {"grid-auto-flow", PropertyId::grid_auto_flow},
    {"grid-auto-columns", PropertyId::grid_auto_columns},
    {"grid-auto-rows", PropertyId::grid_auto_rows},
    {"grid-column", PropertyId::grid_column},
    {"grid-row", PropertyId::grid_row},
    {"grid-area", PropertyId::grid_area},
    {"grid-column-start", PropertyId::grid_column_start},
    {"grid-column-end", PropertyId::grid_column_end},
    {"grid-row-start", PropertyId::grid_row_start},
    {"grid-row-end", PropertyId::grid_row_end},
    {"text-align", PropertyId::text_align},
    {"vertical-align", PropertyId::vertical_align},
}};

// The array's count is written out, so a missing entry would be an empty
// name at the end.
static_assert(!admitted_properties[admitted_property_count - 1U].name.empty());

// CSS layout properties L leaves out (PLAN sections 2.2 to 2.5): each drops
// its declaration with W107 rather than W101, so an author sees that the
// name is known and not admitted.
constexpr std::array<std::string_view, 47U> left_out_properties{{
    "float",
    "clear",
    "grid",
    "grid-template",
    "grid-gap",
    "grid-row-gap",
    "grid-column-gap",
    "place-content",
    "aspect-ratio",
    "writing-mode",
    "direction",
    "columns",
    "column-count",
    "column-width",
    "column-span",
    "column-fill",
    "contain",
    "container",
    "container-type",
    "container-name",
    "z-index",
    "visibility",
    "overflow",
    "overflow-x",
    "overflow-y",
    "white-space",
    "inline-size",
    "block-size",
    "min-inline-size",
    "max-inline-size",
    "min-block-size",
    "max-block-size",
    "inset-inline",
    "inset-block",
    "inset-inline-start",
    "inset-inline-end",
    "inset-block-start",
    "inset-block-end",
    "margin-inline",
    "margin-block",
    "margin-inline-start",
    "margin-inline-end",
    "margin-block-start",
    "margin-block-end",
    "padding-inline",
    "padding-block",
    "justify-tracks",
}};

static_assert(!left_out_properties.back().empty());

// -- keywords -------------------------------------------------------------------

template <typename Value> struct KeywordEntry final {
    std::string_view name{};
    Value value{};
};

constexpr std::array<KeywordEntry<DisplayKind>, 6U> display_keywords{{
    {"block", DisplayKind::block},
    {"inline", DisplayKind::inline_level},
    {"inline-block", DisplayKind::inline_block},
    {"flex", DisplayKind::flex},
    {"grid", DisplayKind::grid},
    {"none", DisplayKind::none},
}};

constexpr std::array<std::string_view, 19U> display_left_out{{
    "inline-flex",
    "inline-grid",
    "contents",
    "flow",
    "flow-root",
    "list-item",
    "run-in",
    "ruby",
    "table",
    "inline-table",
    "table-row",
    "table-cell",
    "table-column",
    "table-caption",
    "table-row-group",
    "table-header-group",
    "table-footer-group",
    "table-column-group",
    "math",
}};

constexpr std::array<KeywordEntry<PositionKind>, 3U> position_keywords{{
    {"static", PositionKind::static_position},
    {"relative", PositionKind::relative},
    {"absolute", PositionKind::absolute},
}};

constexpr std::array<std::string_view, 2U> position_left_out{{"fixed", "sticky"}};

constexpr std::array<KeywordEntry<FlexDirection>, 4U> flex_direction_keywords{{
    {"row", FlexDirection::row},
    {"row-reverse", FlexDirection::row_reverse},
    {"column", FlexDirection::column},
    {"column-reverse", FlexDirection::column_reverse},
}};

constexpr std::array<KeywordEntry<FlexWrap>, 3U> flex_wrap_keywords{{
    {"nowrap", FlexWrap::nowrap},
    {"wrap", FlexWrap::wrap},
    {"wrap-reverse", FlexWrap::wrap_reverse},
}};

// justify-content and align-content: CSS's content distribution and
// position values, `left` and `right` and the overflow and baseline forms
// left out.
constexpr std::array<KeywordEntry<LayoutAlignment>, 10U> content_alignment_keywords{{
    {"normal", LayoutAlignment::normal},
    {"stretch", LayoutAlignment::stretch},
    {"start", LayoutAlignment::start},
    {"end", LayoutAlignment::end},
    {"flex-start", LayoutAlignment::flex_start},
    {"flex-end", LayoutAlignment::flex_end},
    {"center", LayoutAlignment::center},
    {"space-between", LayoutAlignment::space_between},
    {"space-around", LayoutAlignment::space_around},
    {"space-evenly", LayoutAlignment::space_evenly},
}};

constexpr std::array<std::string_view, 4U> justify_content_left_out{{"left", "right", "safe", "unsafe"}};
constexpr std::array<std::string_view, 5U> align_content_left_out{{"baseline", "first", "last", "safe", "unsafe"}};

// align-items and justify-items; align-self and justify-self add `auto`.
constexpr std::array<KeywordEntry<LayoutAlignment>, 8U> item_alignment_keywords{{
    {"normal", LayoutAlignment::normal},
    {"stretch", LayoutAlignment::stretch},
    {"start", LayoutAlignment::start},
    {"end", LayoutAlignment::end},
    {"flex-start", LayoutAlignment::flex_start},
    {"flex-end", LayoutAlignment::flex_end},
    {"center", LayoutAlignment::center},
    {"baseline", LayoutAlignment::baseline},
}};

constexpr std::array<KeywordEntry<LayoutAlignment>, 9U> self_alignment_keywords{{
    {"auto", LayoutAlignment::automatic},
    {"normal", LayoutAlignment::normal},
    {"stretch", LayoutAlignment::stretch},
    {"start", LayoutAlignment::start},
    {"end", LayoutAlignment::end},
    {"flex-start", LayoutAlignment::flex_start},
    {"flex-end", LayoutAlignment::flex_end},
    {"center", LayoutAlignment::center},
    {"baseline", LayoutAlignment::baseline},
}};

constexpr std::array<std::string_view, 7U> align_item_left_out{
    {"self-start", "self-end", "first", "last", "safe", "unsafe", "anchor-center"}};
constexpr std::array<std::string_view, 10U> justify_items_left_out{
    {"self-start", "self-end", "first", "last", "safe", "unsafe", "anchor-center", "left", "right", "legacy"}};
constexpr std::array<std::string_view, 9U> justify_self_left_out{
    {"self-start", "self-end", "first", "last", "safe", "unsafe", "anchor-center", "left", "right"}};

// Every alignment value's name, for the serializer.
constexpr std::array<KeywordEntry<LayoutAlignment>, 12U> alignment_names{{
    {"auto", LayoutAlignment::automatic},
    {"normal", LayoutAlignment::normal},
    {"stretch", LayoutAlignment::stretch},
    {"start", LayoutAlignment::start},
    {"end", LayoutAlignment::end},
    {"flex-start", LayoutAlignment::flex_start},
    {"flex-end", LayoutAlignment::flex_end},
    {"center", LayoutAlignment::center},
    {"baseline", LayoutAlignment::baseline},
    {"space-between", LayoutAlignment::space_between},
    {"space-around", LayoutAlignment::space_around},
    {"space-evenly", LayoutAlignment::space_evenly},
}};

constexpr std::array<KeywordEntry<TextAlignment>, 5U> text_align_keywords{{
    {"start", TextAlignment::start},
    {"end", TextAlignment::end},
    {"left", TextAlignment::left},
    {"right", TextAlignment::right},
    {"center", TextAlignment::center},
}};

constexpr std::array<std::string_view, 3U> text_align_left_out{{"justify", "justify-all", "match-parent"}};

constexpr std::array<KeywordEntry<InlineVerticalAlignment>, 4U> vertical_align_keywords{{
    {"baseline", InlineVerticalAlignment::baseline},
    {"middle", InlineVerticalAlignment::middle},
    {"top", InlineVerticalAlignment::top},
    {"bottom", InlineVerticalAlignment::bottom},
}};

constexpr std::array<std::string_view, 4U> vertical_align_left_out{{"sub", "super", "text-top", "text-bottom"}};

constexpr std::array<KeywordEntry<GridAutoFlow>, 4U> grid_auto_flow_names{{
    {"row", GridAutoFlow::row},
    {"column", GridAutoFlow::column},
    {"row dense", GridAutoFlow::row_dense},
    {"column dense", GridAutoFlow::column_dense},
}};

// CSS-wide keywords and the functions L leaves out, refused wherever they
// are written (question 14).
constexpr std::array<std::string_view, 5U> global_keywords{{"inherit", "initial", "unset", "revert", "revert-layer"}};
constexpr std::array<std::string_view, 6U> refused_functions{{"var", "env", "attr", "min", "max", "clamp"}};

constexpr std::array<std::string_view, 0U> nothing_left_out{};

// -- the value under reading ------------------------------------------------------

// One declaration's value and where its diagnostics go, borrowed for the
// declaration. Positions are indices into `value_tokens` (significant
// tokens only); `closers[i]` is the index of the bracket closing token i.
struct ValueReader final {
    std::string_view text;
    std::span<const Token> tokens;
    std::span<const ThemeNumber> numbers;
    std::span<const std::uint32_t> value_tokens;
    std::span<const std::uint32_t> closers;
    std::vector<TemplateAreaCell>& template_cells;
    std::string_view file;
    std::string_view property;
    SourcePositionCursor& positions;
    ThemeDiagnosticList& diagnostics;
};

std::string_view token_text(std::string_view text, const Token& token) noexcept {
    const std::string_view written = text.substr(token.offset.value, token.length.value);
    return written;
}

const Token& value_token(const ValueReader& reader, std::size_t index) noexcept {
    const std::uint32_t position = reader.value_tokens[index];
    return reader.tokens[position];
}

std::string_view value_text(const ValueReader& reader, std::size_t index) noexcept {
    const std::string_view written = token_text(reader.text, value_token(reader, index));
    return written;
}

// A function token's name, without its `(`.
std::string_view function_name(const ValueReader& reader, std::size_t index) noexcept {
    const std::string_view written = value_text(reader, index);
    return written.substr(0U, written.size() - 1U);
}

// A dimension's unit: its text after the number.
std::string_view dimension_unit(const ValueReader& reader, std::size_t index) noexcept {
    const Token& token = value_token(reader, index);
    const ThemeNumber& number = reader.numbers[token.number];
    const std::string_view written = token_text(reader.text, token);
    return written.substr(number.numeric_length.value);
}

double token_number(const ValueReader& reader, std::size_t index) noexcept {
    const Token& token = value_token(reader, index);
    return reader.numbers[token.number].value;
}

bool token_is_integer(const ValueReader& reader, std::size_t index) noexcept {
    const Token& token = value_token(reader, index);
    return reader.numbers[token.number].integer;
}

bool is_kind(const ValueReader& reader, std::size_t index, std::size_t last, TokenKind kind) noexcept {
    return index < last && value_token(reader, index).kind == kind;
}

bool is_delim(const ValueReader& reader, std::size_t index, std::size_t last, char delim) noexcept {
    if (!is_kind(reader, index, last, TokenKind::delim)) return false;
    const std::string_view written = value_text(reader, index);
    return written.size() == 1U && written[0] == delim;
}

bool is_ident(const ValueReader& reader, std::size_t index, std::size_t last, std::string_view lower_name) noexcept {
    if (!is_kind(reader, index, last, TokenKind::ident)) return false;
    return equals_ascii_folded(value_text(reader, index), lower_name);
}

bool is_function(const ValueReader& reader, std::size_t index, std::size_t last,
                 std::string_view lower_name) noexcept {
    if (!is_kind(reader, index, last, TokenKind::function)) return false;
    return equals_ascii_folded(function_name(reader, index), lower_name);
}

// Whether whitespace or a comment separates value token `index` from the
// one before it: the two are not neighbours in the token buffer.
bool separated_from_previous(const ValueReader& reader, std::size_t index) noexcept {
    if (index == 0U) return true;
    return reader.value_tokens[index] != reader.value_tokens[index - 1U] + 1U;
}

bool contains_folded(std::span<const std::string_view> names, std::string_view written) noexcept {
    for (const std::string_view name : names) {
        if (equals_ascii_folded(written, name)) return true;
    }
    return false;
}

void report(ThemeDiagnosticList& diagnostics, SourcePositionCursor& positions, std::string_view file,
            DiagnosticCode code, ByteOffset offset, std::string_view property, std::string_view detail) {
    ThemeDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.file = std::string(file);
    diagnostic.position = positions.locate(offset);
    diagnostic.property = clipped_theme_detail(property);
    diagnostic.detail = clipped_theme_detail(detail);
    diagnostics.add(std::move(diagnostic));
}

// Reports an error at value token `index`, or just after the value when
// `index` is past its end, and returns false for the reader to return.
bool refuse(ValueReader& reader, DiagnosticCode code, std::size_t index, std::string_view detail) {
    ByteOffset offset{};
    if (index < reader.value_tokens.size()) {
        offset = value_token(reader, index).offset;
    } else {
        const Token& final_token = value_token(reader, reader.value_tokens.size() - 1U);
        offset = ByteOffset{final_token.offset.value + final_token.length.value};
    }
    report(reader.diagnostics, reader.positions, reader.file, code, offset, reader.property, detail);
    return false;
}

bool refuse_token(ValueReader& reader, DiagnosticCode code, std::size_t index) {
    const std::string_view detail = index < reader.value_tokens.size() ? value_text(reader, index) : "";
    return refuse(reader, code, index, detail);
}

bool refuse_missing(ValueReader& reader, std::size_t index) {
    return refuse(reader, DiagnosticCode::value_syntax, index, "a value is missing");
}

// -- keywords, numbers and lengths ------------------------------------------------

// Reads one ident from `admitted`. A name in `left_out` is E405; any other
// name is E406; a token that is not a name is E402.
template <typename Value>
bool read_keyword(ValueReader& reader, std::size_t& index, std::size_t last,
                  std::span<const KeywordEntry<Value>> admitted, std::span<const std::string_view> left_out,
                  Value& value) {
    if (index == last) return refuse_missing(reader, index);
    if (!is_kind(reader, index, last, TokenKind::ident)) {
        return refuse_token(reader, DiagnosticCode::value_wrong_kind, index);
    }
    const std::string_view written = value_text(reader, index);
    for (const KeywordEntry<Value>& entry : admitted) {
        if (!equals_ascii_folded(written, entry.name)) continue;
        value = entry.value;
        ++index;
        return true;
    }
    if (contains_folded(left_out, written)) {
        return refuse_token(reader, DiagnosticCode::value_form_not_admitted, index);
    }
    return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
}

// Whether token `index` is a name in `admitted`, without reporting.
template <typename Value>
bool match_keyword(const ValueReader& reader, std::size_t index, std::size_t last,
                   std::span<const KeywordEntry<Value>> admitted, Value& value) noexcept {
    if (!is_kind(reader, index, last, TokenKind::ident)) return false;
    const std::string_view written = value_text(reader, index);
    for (const KeywordEntry<Value>& entry : admitted) {
        if (!equals_ascii_folded(written, entry.name)) continue;
        value = entry.value;
        return true;
    }
    return false;
}

// Refuses a token in a slot for a number or a length that is not one: a
// name is a keyword the property lacks (E406), calc() of a number is left
// out (E405), anything else is the wrong kind (E402).
bool refuse_not_a_quantity(ValueReader& reader, std::size_t index) {
    if (is_kind(reader, index, reader.value_tokens.size(), TokenKind::ident)) {
        return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
    }
    return refuse_token(reader, DiagnosticCode::value_wrong_kind, index);
}

bool read_number(ValueReader& reader, std::size_t& index, std::size_t last, double& value) {
    if (index == last) return refuse_missing(reader, index);
    if (is_function(reader, index, last, "calc")) {
        return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "calc() of a number");
    }
    if (!is_kind(reader, index, last, TokenKind::number)) return refuse_not_a_quantity(reader, index);
    const double number = token_number(reader, index);
    if (number < 0.0) return refuse_token(reader, DiagnosticCode::value_out_of_range, index);
    value = number;
    ++index;
    return true;
}

bool read_integer(ValueReader& reader, std::size_t& index, std::size_t last, std::int32_t& value) {
    if (index == last) return refuse_missing(reader, index);
    if (is_function(reader, index, last, "calc")) {
        return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "calc() of a number");
    }
    if (!is_kind(reader, index, last, TokenKind::number) || !token_is_integer(reader, index)) {
        return refuse_not_a_quantity(reader, index);
    }
    const double number = token_number(reader, index);
    const double lowest = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const double highest = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (number < lowest || number > highest) {
        return refuse_token(reader, DiagnosticCode::value_out_of_range, index);
    }
    value = static_cast<std::int32_t>(number);
    ++index;
    return true;
}

// calc()'s operands: a number, or a length and a percentage summed.
struct CalcValue final {
    bool number{};
    double scalar{};
    double length{};
    double percent{};
};

bool read_calc_sum(ValueReader& reader, std::size_t& index, std::size_t last, CalcValue& value);

bool finite_calc_value(const CalcValue& value) noexcept {
    return std::isfinite(value.scalar) && std::isfinite(value.length) && std::isfinite(value.percent);
}

// A parenthesised sum, `( ... )` or a nested `calc( ... )`; `index` is at
// the opener.
bool read_calc_group(ValueReader& reader, std::size_t& index, CalcValue& value) {
    const std::size_t closer = reader.closers[index];
    std::size_t inner = index + 1U;
    if (inner == closer) return refuse_missing(reader, closer);
    if (!read_calc_sum(reader, inner, closer, value)) return false;
    if (inner != closer) return refuse_token(reader, DiagnosticCode::value_syntax, inner);
    index = closer + 1U;
    return true;
}

bool read_calc_factor(ValueReader& reader, std::size_t& index, std::size_t last, CalcValue& value) {
    if (index == last) return refuse_missing(reader, index);
    const TokenKind kind = value_token(reader, index).kind;
    if (kind == TokenKind::number) {
        value = CalcValue{true, token_number(reader, index), 0.0, 0.0};
        ++index;
        return true;
    }
    if (kind == TokenKind::percentage) {
        value = CalcValue{false, 0.0, 0.0, token_number(reader, index)};
        ++index;
        return true;
    }
    if (kind == TokenKind::dimension) {
        if (!equals_ascii_folded(dimension_unit(reader, index), "px")) {
            return refuse(reader, DiagnosticCode::unit_not_admitted, index, dimension_unit(reader, index));
        }
        value = CalcValue{false, 0.0, token_number(reader, index), 0.0};
        ++index;
        return true;
    }
    if (kind == TokenKind::left_paren || is_function(reader, index, last, "calc")) {
        const bool read = read_calc_group(reader, index, value);
        return read;
    }
    return refuse_token(reader, DiagnosticCode::value_syntax, index);
}

bool read_calc_product(ValueReader& reader, std::size_t& index, std::size_t last, CalcValue& value) {
    if (!read_calc_factor(reader, index, last, value)) return false;
    while (is_delim(reader, index, last, '*') || is_delim(reader, index, last, '/')) {
        const bool divide = is_delim(reader, index, last, '/');
        const std::size_t operator_index = index;
        ++index;
        CalcValue operand{};
        if (!read_calc_factor(reader, index, last, operand)) return false;
        if (divide) {
            if (!operand.number) {
                return refuse(reader, DiagnosticCode::value_wrong_kind, operator_index, "divide by a number");
            }
            if (operand.scalar == 0.0) {
                return refuse(reader, DiagnosticCode::value_out_of_range, operator_index, "division by zero");
            }
            value.scalar = value.scalar / operand.scalar;
            value.length = value.length / operand.scalar;
            value.percent = value.percent / operand.scalar;
        } else if (value.number) {
            const double factor = value.scalar;
            value = operand;
            value.scalar = value.scalar * factor;
            value.length = value.length * factor;
            value.percent = value.percent * factor;
        } else {
            if (!operand.number) {
                return refuse(reader, DiagnosticCode::value_wrong_kind, operator_index, "multiply by a number");
            }
            value.length = value.length * operand.scalar;
            value.percent = value.percent * operand.scalar;
        }
        if (!finite_calc_value(value)) {
            return refuse(reader, DiagnosticCode::value_out_of_range, operator_index, "calc() overflows");
        }
    }
    return true;
}

// The sum of products. CSS requires whitespace on both sides of `+` and
// `-` (`100%-24px` is a percentage and a negative length, not a
// difference).
bool read_calc_sum(ValueReader& reader, std::size_t& index, std::size_t last, CalcValue& value) {
    if (!read_calc_product(reader, index, last, value)) return false;
    while (index < last) {
        const bool add = is_delim(reader, index, last, '+');
        const bool subtract = is_delim(reader, index, last, '-');
        if (!add && !subtract) return true;
        const std::size_t operator_index = index;
        if (!separated_from_previous(reader, index) || index + 1U >= last ||
            !separated_from_previous(reader, index + 1U)) {
            return refuse(reader, DiagnosticCode::value_syntax, operator_index,
                          "+ and - need whitespace on both sides");
        }
        ++index;
        CalcValue operand{};
        if (!read_calc_product(reader, index, last, operand)) return false;
        if (operand.number != value.number) {
            return refuse(reader, DiagnosticCode::value_wrong_kind, operator_index, "a number and a length");
        }
        const double sign = add ? 1.0 : -1.0;
        value.scalar = value.scalar + sign * operand.scalar;
        value.length = value.length + sign * operand.length;
        value.percent = value.percent + sign * operand.percent;
        if (!finite_calc_value(value)) {
            return refuse(reader, DiagnosticCode::value_out_of_range, operator_index, "calc() overflows");
        }
    }
    return true;
}

// A length-percentage: `<n>px`, a unitless 0, `<n>%`, or linear calc().
// A literal negative is E403 unless `negative_admitted`; a calc() result is
// kept as written and clamped where it is used, as CSS does.
bool read_length_percentage(ValueReader& reader, std::size_t& index, std::size_t last, bool negative_admitted,
                            LengthPercentage& value) {
    if (index == last) return refuse_missing(reader, index);
    const TokenKind kind = value_token(reader, index).kind;
    if (is_function(reader, index, last, "calc")) {
        CalcValue calculated{};
        const std::size_t opener = index;
        if (!read_calc_group(reader, index, calculated)) return false;
        if (calculated.number) {
            return refuse(reader, DiagnosticCode::value_wrong_kind, opener, "calc() of a number");
        }
        value = LengthPercentage{LogicalLength{calculated.length}, calculated.percent};
        return true;
    }
    double number = 0.0;
    LengthPercentage read{};
    if (kind == TokenKind::dimension) {
        if (!equals_ascii_folded(dimension_unit(reader, index), "px")) {
            return refuse(reader, DiagnosticCode::unit_not_admitted, index, dimension_unit(reader, index));
        }
        number = token_number(reader, index);
        read = LengthPercentage{LogicalLength{number}, 0.0};
    } else if (kind == TokenKind::percentage) {
        number = token_number(reader, index);
        read = LengthPercentage{LogicalLength{0.0}, number};
    } else if (kind == TokenKind::number) {
        number = token_number(reader, index);
        if (number != 0.0) return refuse(reader, DiagnosticCode::value_wrong_kind, index, "a length needs a unit");
    } else {
        return refuse_not_a_quantity(reader, index);
    }
    if (number < 0.0 && !negative_admitted) return refuse_token(reader, DiagnosticCode::value_out_of_range, index);
    value = read;
    ++index;
    return true;
}

// -- sizes, boxes and gaps ----------------------------------------------------------

enum class SizeSlot : std::uint8_t { size, minimum, maximum, flex_basis };

bool read_size(ValueReader& reader, std::size_t& index, std::size_t last, SizeSlot slot, SizeValue& value) {
    if (index == last) return refuse_missing(reader, index);
    if (is_kind(reader, index, last, TokenKind::ident)) {
        const std::string_view written = value_text(reader, index);
        const bool content_keywords_admitted = slot != SizeSlot::flex_basis;
        SizeKind kind = SizeKind::automatic;
        bool admitted = false;
        if (equals_ascii_folded(written, "auto") && slot != SizeSlot::maximum) {
            kind = SizeKind::automatic;
            admitted = true;
        } else if (equals_ascii_folded(written, "none") && slot == SizeSlot::maximum) {
            kind = SizeKind::none;
            admitted = true;
        } else if (equals_ascii_folded(written, "content") && slot == SizeSlot::flex_basis) {
            kind = SizeKind::content;
            admitted = true;
        } else if (equals_ascii_folded(written, "min-content") && content_keywords_admitted) {
            kind = SizeKind::min_content;
            admitted = true;
        } else if (equals_ascii_folded(written, "max-content") && content_keywords_admitted) {
            kind = SizeKind::max_content;
            admitted = true;
        }
        if (admitted) {
            value = SizeValue{kind, {}};
            ++index;
            return true;
        }
        // CSS Sizing's other keywords, and the content keywords in
        // flex-basis, which PLAN section 2.2 leaves out there.
        constexpr std::array<std::string_view, 6U> size_left_out{
            {"stretch", "fit-content", "-webkit-fill-available", "-moz-available", "min-content", "max-content"}};
        if (contains_folded(size_left_out, written)) {
            return refuse_token(reader, DiagnosticCode::value_form_not_admitted, index);
        }
        return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
    }
    if (is_function(reader, index, last, "fit-content")) {
        if (slot == SizeSlot::flex_basis) {
            return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "fit-content()");
        }
        const std::size_t closer = reader.closers[index];
        std::size_t inner = index + 1U;
        LengthPercentage limit{};
        if (!read_length_percentage(reader, inner, closer, false, limit)) return false;
        if (inner != closer) return refuse_token(reader, DiagnosticCode::value_syntax, inner);
        value = SizeValue{SizeKind::fit_content, limit};
        index = closer + 1U;
        return true;
    }
    LengthPercentage length{};
    if (!read_length_percentage(reader, index, last, false, length)) return false;
    value = SizeValue{SizeKind::length_percentage, length};
    return true;
}

// `auto` or a length-percentage, which may be negative: a margin or an
// inset.
bool read_auto_length_percentage(ValueReader& reader, std::size_t& index, std::size_t last,
                                 AutoLengthPercentage& value) {
    if (is_ident(reader, index, last, "auto")) {
        value = AutoLengthPercentage{true, {}};
        ++index;
        return true;
    }
    LengthPercentage length{};
    if (!read_length_percentage(reader, index, last, true, length)) return false;
    value = AutoLengthPercentage{false, length};
    return true;
}

bool read_padding_length(ValueReader& reader, std::size_t& index, std::size_t last, LengthPercentage& value) {
    const bool read = read_length_percentage(reader, index, last, false, value);
    return read;
}

// CSS's box shorthand: one to four values, top, right, bottom, left, with
// missing ones copied from their opposite sides.
template <typename Value>
void expand_box_sides(const std::array<Value, 4U>& values, std::size_t count, BoxSides<Value>& sides) {
    sides.top = values[0];
    sides.right = count >= 2U ? values[1] : values[0];
    sides.bottom = count >= 3U ? values[2] : values[0];
    sides.left = count >= 4U ? values[3] : sides.right;
}

bool read_margin_box(ValueReader& reader, std::size_t& index, std::size_t last,
                     BoxSides<AutoLengthPercentage>& sides) {
    std::array<AutoLengthPercentage, 4U> values{};
    std::size_t count = 0U;
    while (index < last) {
        if (count == values.size()) return refuse_token(reader, DiagnosticCode::value_syntax, index);
        if (!read_auto_length_percentage(reader, index, last, values[count])) return false;
        ++count;
    }
    expand_box_sides(values, count, sides);
    return true;
}

bool read_padding_box(ValueReader& reader, std::size_t& index, std::size_t last, BoxSides<LengthPercentage>& sides) {
    std::array<LengthPercentage, 4U> values{};
    std::size_t count = 0U;
    while (index < last) {
        if (count == values.size()) return refuse_token(reader, DiagnosticCode::value_syntax, index);
        if (!read_padding_length(reader, index, last, values[count])) return false;
        ++count;
    }
    expand_box_sides(values, count, sides);
    return true;
}

// `normal` (zero) or a non-negative length-percentage.
bool read_gap(ValueReader& reader, std::size_t& index, std::size_t last, LengthPercentage& value) {
    if (is_ident(reader, index, last, "normal")) {
        value = LengthPercentage{};
        ++index;
        return true;
    }
    const bool read = read_length_percentage(reader, index, last, false, value);
    return read;
}

// -- flex -------------------------------------------------------------------------

bool read_flex_flow(ValueReader& reader, std::size_t& index, std::size_t last, FlexContainerStyle& flex) {
    bool direction_read = false;
    bool wrap_read = false;
    FlexContainerStyle read{};
    while (index < last) {
        FlexDirection direction{};
        FlexWrap wrap{};
        if (!direction_read && match_keyword<FlexDirection>(reader, index, last, flex_direction_keywords, direction)) {
            read.direction = direction;
            direction_read = true;
        } else if (!wrap_read && match_keyword<FlexWrap>(reader, index, last, flex_wrap_keywords, wrap)) {
            read.wrap = wrap;
            wrap_read = true;
        } else if (is_kind(reader, index, last, TokenKind::ident) &&
                   !match_keyword<FlexDirection>(reader, index, last, flex_direction_keywords, direction) &&
                   !match_keyword<FlexWrap>(reader, index, last, flex_wrap_keywords, wrap)) {
            return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
        } else {
            return refuse_token(reader, DiagnosticCode::value_syntax, index);
        }
        ++index;
    }
    flex = read;
    return true;
}

// The flex shorthand, CSS's grammar: `none`, `auto`, or
// `<grow> <shrink>? || <basis>`. An omitted grow or shrink is 1 and an
// omitted basis is 0; a unitless zero after both factors is the basis.
bool read_flex(ValueReader& reader, std::size_t& index, std::size_t last, FlexItemStyle& item) {
    if (index + 1U == last && is_ident(reader, index, last, "none")) {
        item.grow = FlexFactor{0.0};
        item.shrink = FlexFactor{0.0};
        item.basis = SizeValue{};
        ++index;
        return true;
    }
    if (index + 1U == last && is_ident(reader, index, last, "auto")) {
        item.grow = FlexFactor{1.0};
        item.shrink = FlexFactor{1.0};
        item.basis = SizeValue{};
        ++index;
        return true;
    }
    bool grow_read = false;
    bool shrink_read = false;
    bool basis_read = false;
    double grow = 1.0;
    double shrink = 1.0;
    SizeValue basis{SizeKind::length_percentage, {}};
    while (index < last) {
        const bool number = is_kind(reader, index, last, TokenKind::number);
        if (number && !grow_read) {
            // The factors are adjacent: a number right after grow is shrink.
            if (!read_number(reader, index, last, grow)) return false;
            grow_read = true;
            if (is_kind(reader, index, last, TokenKind::number)) {
                if (!read_number(reader, index, last, shrink)) return false;
                shrink_read = true;
            }
            continue;
        }
        if (number && grow_read && shrink_read && !basis_read && token_number(reader, index) == 0.0) {
            basis = SizeValue{SizeKind::length_percentage, {}};
            basis_read = true;
            ++index;
            continue;
        }
        if (number || basis_read) return refuse_token(reader, DiagnosticCode::value_syntax, index);
        if (!read_size(reader, index, last, SizeSlot::flex_basis, basis)) return false;
        basis_read = true;
    }
    item.grow = FlexFactor{grow};
    item.shrink = FlexFactor{shrink};
    item.basis = basis;
    return true;
}

// -- grid tracks --------------------------------------------------------------------

bool read_track_breadth(ValueReader& reader, std::size_t& index, std::size_t last, bool fraction_admitted,
                        TrackBreadth& breadth) {
    if (index == last) return refuse_missing(reader, index);
    if (is_kind(reader, index, last, TokenKind::ident)) {
        const std::string_view written = value_text(reader, index);
        TrackBreadthKind kind = TrackBreadthKind::automatic;
        if (equals_ascii_folded(written, "auto")) {
            kind = TrackBreadthKind::automatic;
        } else if (equals_ascii_folded(written, "min-content")) {
            kind = TrackBreadthKind::min_content;
        } else if (equals_ascii_folded(written, "max-content")) {
            kind = TrackBreadthKind::max_content;
        } else if (equals_ascii_folded(written, "subgrid") || equals_ascii_folded(written, "masonry")) {
            return refuse_token(reader, DiagnosticCode::value_form_not_admitted, index);
        } else {
            return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
        }
        breadth = TrackBreadth{kind, {}, FractionFactor{}};
        ++index;
        return true;
    }
    if (is_kind(reader, index, last, TokenKind::dimension) &&
        equals_ascii_folded(dimension_unit(reader, index), "fr")) {
        if (!fraction_admitted) {
            return refuse(reader, DiagnosticCode::value_wrong_kind, index, "a fraction is not a minimum");
        }
        const double fraction = token_number(reader, index);
        if (fraction < 0.0) return refuse_token(reader, DiagnosticCode::value_out_of_range, index);
        breadth = TrackBreadth{TrackBreadthKind::fraction, {}, FractionFactor{fraction}};
        ++index;
        return true;
    }
    LengthPercentage length{};
    if (!read_length_percentage(reader, index, last, false, length)) return false;
    breadth = TrackBreadth{TrackBreadthKind::length_percentage, length, FractionFactor{}};
    return true;
}

bool read_track_size(ValueReader& reader, std::size_t& index, std::size_t last, GridTrackSize& size) {
    if (index == last) return refuse_missing(reader, index);
    if (is_function(reader, index, last, "minmax")) {
        const std::size_t closer = reader.closers[index];
        std::size_t inner = index + 1U;
        GridTrackSize read{};
        if (!read_track_breadth(reader, inner, closer, false, read.minimum)) return false;
        if (!is_kind(reader, inner, closer, TokenKind::comma)) return refuse_token(reader, DiagnosticCode::value_syntax, inner);
        ++inner;
        if (!read_track_breadth(reader, inner, closer, true, read.maximum)) return false;
        if (inner != closer) return refuse_token(reader, DiagnosticCode::value_syntax, inner);
        size = read;
        index = closer + 1U;
        return true;
    }
    if (is_function(reader, index, last, "fit-content")) {
        const std::size_t closer = reader.closers[index];
        std::size_t inner = index + 1U;
        GridTrackSize read{};
        if (!read_length_percentage(reader, inner, closer, false, read.fit_content_limit)) return false;
        if (inner != closer) return refuse_token(reader, DiagnosticCode::value_syntax, inner);
        read.minimum = TrackBreadth{TrackBreadthKind::automatic, {}, FractionFactor{}};
        read.maximum = TrackBreadth{TrackBreadthKind::max_content, {}, FractionFactor{}};
        read.fit_content = true;
        size = read;
        index = closer + 1U;
        return true;
    }
    if (is_kind(reader, index, last, TokenKind::left_square)) {
        return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "named grid lines");
    }
    if (is_kind(reader, index, last, TokenKind::function) && !is_function(reader, index, last, "calc")) {
        return refuse_token(reader, DiagnosticCode::value_syntax, index);
    }
    TrackBreadth breadth{};
    if (!read_track_breadth(reader, index, last, true, breadth)) return false;
    GridTrackSize read{};
    read.maximum = breadth;
    read.minimum = breadth.kind == TrackBreadthKind::fraction ? TrackBreadth{} : breadth;
    size = read;
    return true;
}

// CSS's <fixed-size>: a track with a length-percentage at one end and no
// fraction at its minimum. Automatic repeats need nothing else (CSS Grid
// 7.2.3).
bool fixed_track(const GridTrackSize& size) noexcept {
    if (size.fit_content) return false;
    return size.minimum.kind == TrackBreadthKind::length_percentage ||
           size.maximum.kind == TrackBreadthKind::length_percentage;
}

bool append_track(ValueReader& reader, std::size_t index, std::size_t counted, std::vector<GridTrackSize>& tracks,
                  const GridTrackSize& size) {
    if (counted >= maximum_grid_tracks) {
        return refuse(reader, DiagnosticCode::value_out_of_range, index, "more than 10000 tracks");
    }
    tracks.push_back(size);
    return true;
}

// `repeat(<n> | auto-fill | auto-fit, <track-size>+)`; `index` is at the
// function.
bool read_repeat(ValueReader& reader, std::size_t& index, GridTrackList& list) {
    const std::size_t opener = index;
    const std::size_t closer = reader.closers[index];
    std::size_t inner = index + 1U;
    AutoRepeatKind automatic = AutoRepeatKind::none;
    double count = 0.0;
    if (is_ident(reader, inner, closer, "auto-fill")) {
        automatic = AutoRepeatKind::auto_fill;
    } else if (is_ident(reader, inner, closer, "auto-fit")) {
        automatic = AutoRepeatKind::auto_fit;
    } else if (is_kind(reader, inner, closer, TokenKind::number)) {
        if (!token_is_integer(reader, inner)) return refuse_token(reader, DiagnosticCode::value_wrong_kind, inner);
        count = token_number(reader, inner);
        if (count < 1.0 || count > static_cast<double>(maximum_grid_tracks)) {
            return refuse_token(reader, DiagnosticCode::value_out_of_range, inner);
        }
    } else if (inner == closer) {
        return refuse_missing(reader, inner);
    } else if (is_kind(reader, inner, closer, TokenKind::ident)) {
        return refuse_token(reader, DiagnosticCode::keyword_unknown, inner);
    } else {
        return refuse_token(reader, DiagnosticCode::value_wrong_kind, inner);
    }
    ++inner;
    if (!is_kind(reader, inner, closer, TokenKind::comma)) return refuse_token(reader, DiagnosticCode::value_syntax, inner);
    ++inner;
    if (inner == closer) return refuse_missing(reader, inner);
    if (automatic != AutoRepeatKind::none && list.auto_repeat != AutoRepeatKind::none) {
        return refuse(reader, DiagnosticCode::value_syntax, opener, "a second automatic repeat");
    }
    const std::size_t counted_before = list.tracks.size() + list.auto_repeat_tracks.size();
    std::vector<GridTrackSize>& destination = automatic == AutoRepeatKind::none ? list.tracks : list.auto_repeat_tracks;
    const std::size_t first_repeated = destination.size();
    while (inner < closer) {
        if (is_function(reader, inner, closer, "repeat")) {
            return refuse(reader, DiagnosticCode::value_syntax, inner, "repeat() inside repeat()");
        }
        GridTrackSize size{};
        const std::size_t size_index = inner;
        if (!read_track_size(reader, inner, closer, size)) return false;
        const std::size_t counted = counted_before + destination.size() - first_repeated;
        if (!append_track(reader, size_index, counted, destination, size)) return false;
    }
    if (automatic != AutoRepeatKind::none) {
        list.auto_repeat = automatic;
        list.auto_repeat_index = static_cast<std::uint32_t>(list.tracks.size());
        index = closer + 1U;
        return true;
    }
    // Expand `repeat(<n>, ...)`: n copies of the tracks just read, within
    // the track limit (both factors are at most maximum_grid_tracks, so the
    // product cannot overflow).
    const std::size_t repeated = destination.size() - first_repeated;
    const std::size_t copies = static_cast<std::size_t>(count);
    const std::size_t total = counted_before + repeated * copies;
    if (total > maximum_grid_tracks) {
        return refuse(reader, DiagnosticCode::value_out_of_range, opener, "more than 10000 tracks");
    }
    destination.reserve(first_repeated + repeated * copies);
    for (std::size_t copy = 1U; copy < copies; ++copy) {
        for (std::size_t track = 0U; track < repeated; ++track) {
            destination.push_back(destination[first_repeated + track]);
        }
    }
    index = closer + 1U;
    return true;
}

bool read_track_list(ValueReader& reader, std::size_t& index, std::size_t last, GridTrackList& list) {
    list.tracks.clear();
    list.auto_repeat_tracks.clear();
    list.auto_repeat = AutoRepeatKind::none;
    list.auto_repeat_index = 0U;
    if (index + 1U == last && is_ident(reader, index, last, "none")) {
        ++index;
        return true;
    }
    std::size_t automatic_repeat_index = 0U;
    while (index < last) {
        if (is_function(reader, index, last, "repeat")) {
            const std::size_t repeat_index = index;
            const AutoRepeatKind repeat_before = list.auto_repeat;
            if (!read_repeat(reader, index, list)) return false;
            if (repeat_before != list.auto_repeat) automatic_repeat_index = repeat_index;
            continue;
        }
        GridTrackSize size{};
        const std::size_t size_index = index;
        if (!read_track_size(reader, index, last, size)) return false;
        const std::size_t counted = list.tracks.size() + list.auto_repeat_tracks.size();
        if (!append_track(reader, size_index, counted, list.tracks, size)) return false;
    }
    if (list.auto_repeat == AutoRepeatKind::none) return true;
    // CSS's <auto-track-list>: with an automatic repeat, every track is
    // fixed.
    const bool repeated_fixed = std::all_of(list.auto_repeat_tracks.begin(), list.auto_repeat_tracks.end(), fixed_track);
    const bool others_fixed = std::all_of(list.tracks.begin(), list.tracks.end(), fixed_track);
    if (!repeated_fixed || !others_fixed) {
        return refuse(reader, DiagnosticCode::value_wrong_kind, automatic_repeat_index,
                      "an automatic repeat needs fixed tracks");
    }
    return true;
}

bool read_auto_tracks(ValueReader& reader, std::size_t& index, std::size_t last, std::vector<GridTrackSize>& tracks) {
    tracks.clear();
    while (index < last) {
        if (is_function(reader, index, last, "repeat")) {
            return refuse(reader, DiagnosticCode::value_syntax, index, "repeat() in implicit tracks");
        }
        GridTrackSize size{};
        const std::size_t size_index = index;
        if (!read_track_size(reader, index, last, size)) return false;
        if (!append_track(reader, size_index, tracks.size(), tracks, size)) return false;
    }
    return true;
}

// -- grid-template-areas ------------------------------------------------------------

bool area_name_byte(char byte) noexcept {
    const unsigned char value = static_cast<unsigned char>(byte);
    const bool letter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    const bool digit = value >= '0' && value <= '9';
    return letter || digit || value == '-' || value == '_' || value >= 0x80U;
}

bool area_space_byte(char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
}

bool template_cell_before(const TemplateAreaCell& left, const TemplateAreaCell& right) noexcept {
    if (left.name != right.name) return left.name < right.name;
    if (left.row != right.row) return left.row < right.row;
    return left.column < right.column;
}

bool area_written_before(const GridNamedArea& left, const GridNamedArea& right) noexcept {
    if (left.row_start != right.row_start) return left.row_start < right.row_start;
    return left.column_start < right.column_start;
}

// One row string's cells: names and runs of `.` (null cells), separated by
// whitespace. Appends its named cells; `columns` is its cell count, at most
// 2,048 since a string holds at most maximum_string_bytes (E505).
bool read_area_row(ValueReader& reader, std::size_t index, std::uint32_t row, std::uint32_t& columns) {
    const std::string_view written = value_text(reader, index);
    const std::string_view content = written.substr(1U, written.size() - 2U);
    std::uint32_t column = 0U;
    std::size_t position = 0U;
    while (position < content.size()) {
        const char byte = content[position];
        if (area_space_byte(byte)) {
            ++position;
            continue;
        }
        const std::size_t start = position;
        if (byte == '.') {
            while (position < content.size() && content[position] == '.') ++position;
        } else if (area_name_byte(byte)) {
            while (position < content.size() && area_name_byte(content[position])) ++position;
            reader.template_cells.push_back(TemplateAreaCell{content.substr(start, position - start), row, column});
        } else {
            return refuse(reader, DiagnosticCode::value_syntax, index, written);
        }
        ++column;
    }
    if (column == 0U) return refuse(reader, DiagnosticCode::value_syntax, index, "a row without cells");
    columns = column;
    return true;
}

bool read_template_areas(ValueReader& reader, std::size_t& index, std::size_t last, GridTemplateAreas& areas) {
    areas.rows = 0U;
    areas.columns = 0U;
    areas.areas.clear();
    if (index + 1U == last && is_ident(reader, index, last, "none")) {
        ++index;
        return true;
    }
    reader.template_cells.clear();
    std::uint32_t rows = 0U;
    std::uint32_t columns = 0U;
    const std::size_t first = index;
    for (; index < last; ++index) {
        if (!is_kind(reader, index, last, TokenKind::string)) {
            if (is_kind(reader, index, last, TokenKind::ident)) {
                return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
            }
            return refuse_token(reader, DiagnosticCode::value_wrong_kind, index);
        }
        if (rows == maximum_grid_tracks) {
            return refuse(reader, DiagnosticCode::value_out_of_range, index, "more than 10000 rows");
        }
        std::uint32_t row_columns = 0U;
        if (!read_area_row(reader, index, rows, row_columns)) return false;
        if (index != first && row_columns != columns) {
            return refuse(reader, DiagnosticCode::value_syntax, index, "rows of different lengths");
        }
        columns = row_columns;
        ++rows;
    }
    // Group the named cells by name; each name's cells must fill the
    // rectangle they span.
    std::sort(reader.template_cells.begin(), reader.template_cells.end(), template_cell_before);
    std::size_t group = 0U;
    while (group < reader.template_cells.size()) {
        const TemplateAreaCell& first_cell = reader.template_cells[group];
        std::size_t end = group;
        GridNamedArea area{};
        area.row_start = first_cell.row;
        area.row_end = first_cell.row + 1U;
        area.column_start = first_cell.column;
        area.column_end = first_cell.column + 1U;
        while (end < reader.template_cells.size() && reader.template_cells[end].name == first_cell.name) {
            const TemplateAreaCell& cell = reader.template_cells[end];
            area.row_end = std::max(area.row_end, cell.row + 1U);
            area.column_start = std::min(area.column_start, cell.column);
            area.column_end = std::max(area.column_end, cell.column + 1U);
            ++end;
        }
        const std::size_t spanned = static_cast<std::size_t>(area.row_end - area.row_start) *
                                    static_cast<std::size_t>(area.column_end - area.column_start);
        if (spanned != end - group) {
            return refuse(reader, DiagnosticCode::value_syntax, first, first_cell.name);
        }
        area.name = std::string(first_cell.name);
        areas.areas.push_back(std::move(area));
        group = end;
    }
    std::sort(areas.areas.begin(), areas.areas.end(), area_written_before);
    areas.rows = rows;
    areas.columns = columns;
    return true;
}

// -- grid placement -----------------------------------------------------------------

bool read_grid_auto_flow(ValueReader& reader, std::size_t& index, std::size_t last, GridAutoFlow& flow) {
    bool direction_read = false;
    bool dense = false;
    bool column = false;
    while (index < last) {
        if (!direction_read && is_ident(reader, index, last, "row")) {
            direction_read = true;
        } else if (!direction_read && is_ident(reader, index, last, "column")) {
            direction_read = true;
            column = true;
        } else if (!dense && is_ident(reader, index, last, "dense")) {
            dense = true;
        } else if (is_kind(reader, index, last, TokenKind::ident) && !is_ident(reader, index, last, "row") &&
                   !is_ident(reader, index, last, "column") && !is_ident(reader, index, last, "dense")) {
            return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
        } else {
            return refuse_token(reader, DiagnosticCode::value_syntax, index);
        }
        ++index;
    }
    if (column) {
        flow = dense ? GridAutoFlow::column_dense : GridAutoFlow::column;
    } else {
        flow = dense ? GridAutoFlow::row_dense : GridAutoFlow::row;
    }
    return true;
}

// The end of one grid line reference: the next `/` or `last`.
std::size_t grid_line_end(const ValueReader& reader, std::size_t index, std::size_t last) noexcept {
    std::size_t end = index;
    while (end < last && !is_delim(reader, end, last, '/')) ++end;
    return end;
}

// One line reference, [index, last): `auto`, a non-zero integer, `span
// <n>` (either order), or an area's name. Named lines (`<name> <n>`, `span
// <name>`) are left out.
bool read_grid_line(ValueReader& reader, std::size_t index, std::size_t last, GridLine& line) {
    const std::size_t count = last - index;
    if (count == 0U) return refuse_missing(reader, index);
    if (count == 1U) {
        if (is_ident(reader, index, last, "auto")) {
            line = GridLine{};
            return true;
        }
        if (is_ident(reader, index, last, "span")) return refuse(reader, DiagnosticCode::value_syntax, index, "span needs a count");
        if (is_kind(reader, index, last, TokenKind::ident)) {
            line = GridLine{GridLineKind::area, 0, std::string(value_text(reader, index))};
            return true;
        }
        std::size_t position = index;
        std::int32_t number = 0;
        if (!read_integer(reader, position, last, number)) return false;
        if (number == 0) return refuse_token(reader, DiagnosticCode::value_out_of_range, index);
        line = GridLine{GridLineKind::line, number, {}};
        return true;
    }
    if (count == 2U) {
        const bool span_first = is_ident(reader, index, last, "span");
        const bool span_second = is_ident(reader, index + 1U, last, "span");
        if (span_first || span_second) {
            const std::size_t count_index = span_first ? index + 1U : index;
            if (is_kind(reader, count_index, last, TokenKind::ident)) {
                return refuse(reader, DiagnosticCode::value_form_not_admitted, count_index, "named grid lines");
            }
            std::size_t position = count_index;
            std::int32_t number = 0;
            if (!read_integer(reader, position, count_index + 1U, number)) return false;
            if (number < 1) return refuse_token(reader, DiagnosticCode::value_out_of_range, count_index);
            line = GridLine{GridLineKind::span, number, {}};
            return true;
        }
        if (is_kind(reader, index, last, TokenKind::ident) || is_kind(reader, index + 1U, last, TokenKind::ident)) {
            return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "named grid lines");
        }
        return refuse_token(reader, DiagnosticCode::value_syntax, index + 1U);
    }
    const bool any_name = is_kind(reader, index, last, TokenKind::ident);
    if (count == 3U && any_name) {
        return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "named grid lines");
    }
    return refuse_token(reader, DiagnosticCode::value_syntax, index + 2U);
}

// An omitted end line: the start's area when it names one, else `auto`.
GridLine omitted_grid_line(const GridLine& given) {
    if (given.kind == GridLineKind::area) return given;
    return GridLine{};
}

// `<line> [/ <line>]{0, parts - 1}` into `lines`; `count` is the number
// read.
bool read_grid_lines(ValueReader& reader, std::size_t& index, std::size_t last, std::array<GridLine, 4U>& lines,
                     std::size_t parts, std::size_t& count) {
    count = 0U;
    while (true) {
        if (count == parts) return refuse_token(reader, DiagnosticCode::value_syntax, index - 1U);
        const std::size_t end = grid_line_end(reader, index, last);
        if (!read_grid_line(reader, index, end, lines[count])) return false;
        ++count;
        index = end;
        if (index == last) return true;
        ++index;
        if (index == last) return refuse_missing(reader, index);
    }
}

bool read_grid_line_pair(ValueReader& reader, std::size_t& index, std::size_t last, GridLine& start, GridLine& end) {
    std::array<GridLine, 4U> lines{};
    std::size_t count = 0U;
    if (!read_grid_lines(reader, index, last, lines, 2U, count)) return false;
    start = lines[0];
    end = count == 2U ? lines[1] : omitted_grid_line(lines[0]);
    return true;
}

// grid-area: row-start / column-start / row-end / column-end.
bool read_grid_area(ValueReader& reader, std::size_t& index, std::size_t last, GridItemStyle& item) {
    std::array<GridLine, 4U> lines{};
    std::size_t count = 0U;
    if (!read_grid_lines(reader, index, last, lines, 4U, count)) return false;
    item.row_start = lines[0];
    item.column_start = count >= 2U ? lines[1] : omitted_grid_line(lines[0]);
    item.row_end = count >= 3U ? lines[2] : omitted_grid_line(lines[0]);
    item.column_end = count >= 4U ? lines[3] : omitted_grid_line(item.column_start);
    return true;
}

bool read_single_grid_line(ValueReader& reader, std::size_t& index, std::size_t last, GridLine& line) {
    const std::size_t slash = grid_line_end(reader, index, last);
    if (slash != last) return refuse_token(reader, DiagnosticCode::value_syntax, slash);
    if (!read_grid_line(reader, index, last, line)) return false;
    index = last;
    return true;
}

// -- one declaration ------------------------------------------------------------------

template <typename Value>
bool read_alignment_pair(ValueReader& reader, std::size_t& index, std::size_t last,
                         std::span<const KeywordEntry<Value>> admitted, std::span<const std::string_view> align_left_out,
                         std::span<const std::string_view> justify_left_out, Value& align, Value& justify) {
    Value first{};
    if (!read_keyword<Value>(reader, index, last, admitted, align_left_out, first)) return false;
    Value second = first;
    if (index < last && !read_keyword<Value>(reader, index, last, admitted, justify_left_out, second)) return false;
    align = first;
    justify = second;
    return true;
}

bool read_display(ValueReader& reader, std::size_t& index, std::size_t last, DisplayKind& display) {
    if (index + 1U < last && is_kind(reader, index, last, TokenKind::ident) &&
        is_kind(reader, index + 1U, last, TokenKind::ident)) {
        return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "two-keyword display");
    }
    const bool read = read_keyword<DisplayKind>(reader, index, last, display_keywords, display_left_out, display);
    return read;
}

bool read_vertical_align(ValueReader& reader, std::size_t& index, std::size_t last,
                         InlineVerticalAlignment& alignment) {
    const bool quantity = is_kind(reader, index, last, TokenKind::dimension) ||
                          is_kind(reader, index, last, TokenKind::percentage) ||
                          is_kind(reader, index, last, TokenKind::number) || is_function(reader, index, last, "calc");
    if (quantity) return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "a length in vertical-align");
    const bool read = read_keyword<InlineVerticalAlignment>(reader, index, last, vertical_align_keywords,
                                                            vertical_align_left_out, alignment);
    return read;
}

bool read_box_sizing(ValueReader& reader, std::size_t& index, std::size_t last) {
    if (is_ident(reader, index, last, "border-box")) {
        ++index;
        return true;
    }
    if (is_ident(reader, index, last, "content-box")) {
        return refuse_token(reader, DiagnosticCode::value_form_not_admitted, index);
    }
    if (is_kind(reader, index, last, TokenKind::ident)) return refuse_token(reader, DiagnosticCode::keyword_unknown, index);
    return refuse_token(reader, DiagnosticCode::value_wrong_kind, index);
}

bool read_property_value(ValueReader& reader, PropertyId property, std::size_t& index, std::size_t last,
                         LayoutStyle& style) {
    switch (property) {
    case PropertyId::display:
        return read_display(reader, index, last, style.display);
    case PropertyId::position:
        return read_keyword<PositionKind>(reader, index, last, position_keywords, position_left_out, style.position);
    case PropertyId::inset: {
        BoxSides<AutoLengthPercentage> sides{};
        if (!read_margin_box(reader, index, last, sides)) return false;
        style.inset = sides;
        return true;
    }
    case PropertyId::top:
        return read_auto_length_percentage(reader, index, last, style.inset.top);
    case PropertyId::right:
        return read_auto_length_percentage(reader, index, last, style.inset.right);
    case PropertyId::bottom:
        return read_auto_length_percentage(reader, index, last, style.inset.bottom);
    case PropertyId::left:
        return read_auto_length_percentage(reader, index, last, style.inset.left);
    case PropertyId::width:
        return read_size(reader, index, last, SizeSlot::size, style.width);
    case PropertyId::height:
        return read_size(reader, index, last, SizeSlot::size, style.height);
    case PropertyId::min_width:
        return read_size(reader, index, last, SizeSlot::minimum, style.min_width);
    case PropertyId::min_height:
        return read_size(reader, index, last, SizeSlot::minimum, style.min_height);
    case PropertyId::max_width:
        return read_size(reader, index, last, SizeSlot::maximum, style.max_width);
    case PropertyId::max_height:
        return read_size(reader, index, last, SizeSlot::maximum, style.max_height);
    case PropertyId::margin: {
        BoxSides<AutoLengthPercentage> sides{};
        if (!read_margin_box(reader, index, last, sides)) return false;
        style.margin = sides;
        return true;
    }
    case PropertyId::margin_top:
        return read_auto_length_percentage(reader, index, last, style.margin.top);
    case PropertyId::margin_right:
        return read_auto_length_percentage(reader, index, last, style.margin.right);
    case PropertyId::margin_bottom:
        return read_auto_length_percentage(reader, index, last, style.margin.bottom);
    case PropertyId::margin_left:
        return read_auto_length_percentage(reader, index, last, style.margin.left);
    case PropertyId::padding: {
        BoxSides<LengthPercentage> sides{};
        if (!read_padding_box(reader, index, last, sides)) return false;
        style.padding = sides;
        return true;
    }
    case PropertyId::padding_top:
        return read_padding_length(reader, index, last, style.padding.top);
    case PropertyId::padding_right:
        return read_padding_length(reader, index, last, style.padding.right);
    case PropertyId::padding_bottom:
        return read_padding_length(reader, index, last, style.padding.bottom);
    case PropertyId::padding_left:
        return read_padding_length(reader, index, last, style.padding.left);
    case PropertyId::box_sizing:
        return read_box_sizing(reader, index, last);
    case PropertyId::gap: {
        LengthPercentage row{};
        if (!read_gap(reader, index, last, row)) return false;
        LengthPercentage column = row;
        if (index < last && !read_gap(reader, index, last, column)) return false;
        style.row_gap = row;
        style.column_gap = column;
        return true;
    }
    case PropertyId::row_gap:
        return read_gap(reader, index, last, style.row_gap);
    case PropertyId::column_gap:
        return read_gap(reader, index, last, style.column_gap);
    case PropertyId::flex_direction:
        return read_keyword<FlexDirection>(reader, index, last, flex_direction_keywords, nothing_left_out,
                                           style.flex.direction);
    case PropertyId::flex_wrap:
        return read_keyword<FlexWrap>(reader, index, last, flex_wrap_keywords, nothing_left_out, style.flex.wrap);
    case PropertyId::flex_flow:
        return read_flex_flow(reader, index, last, style.flex);
    case PropertyId::flex:
        return read_flex(reader, index, last, style.flex_item);
    case PropertyId::flex_grow: {
        double grow = 0.0;
        if (!read_number(reader, index, last, grow)) return false;
        style.flex_item.grow = FlexFactor{grow};
        return true;
    }
    case PropertyId::flex_shrink: {
        double shrink = 0.0;
        if (!read_number(reader, index, last, shrink)) return false;
        style.flex_item.shrink = FlexFactor{shrink};
        return true;
    }
    case PropertyId::flex_basis:
        return read_size(reader, index, last, SizeSlot::flex_basis, style.flex_item.basis);
    case PropertyId::order:
        return read_integer(reader, index, last, style.flex_item.order);
    case PropertyId::justify_content:
        return read_keyword<LayoutAlignment>(reader, index, last, content_alignment_keywords, justify_content_left_out,
                                             style.alignment.justify_content);
    case PropertyId::align_content:
        return read_keyword<LayoutAlignment>(reader, index, last, content_alignment_keywords, align_content_left_out,
                                             style.alignment.align_content);
    case PropertyId::justify_items:
        return read_keyword<LayoutAlignment>(reader, index, last, item_alignment_keywords, justify_items_left_out,
                                             style.alignment.justify_items);
    case PropertyId::align_items:
        return read_keyword<LayoutAlignment>(reader, index, last, item_alignment_keywords, align_item_left_out,
                                             style.alignment.align_items);
    case PropertyId::justify_self:
        return read_keyword<LayoutAlignment>(reader, index, last, self_alignment_keywords, justify_self_left_out,
                                             style.alignment.justify_self);
    case PropertyId::align_self:
        return read_keyword<LayoutAlignment>(reader, index, last, self_alignment_keywords, align_item_left_out,
                                             style.alignment.align_self);
    case PropertyId::place_items:
        return read_alignment_pair<LayoutAlignment>(reader, index, last, item_alignment_keywords, align_item_left_out,
                                                    justify_items_left_out, style.alignment.align_items,
                                                    style.alignment.justify_items);
    case PropertyId::place_self:
        return read_alignment_pair<LayoutAlignment>(reader, index, last, self_alignment_keywords, align_item_left_out,
                                                    justify_self_left_out, style.alignment.align_self,
                                                    style.alignment.justify_self);
    case PropertyId::grid_template_columns:
        return read_track_list(reader, index, last, style.grid.template_columns);
    case PropertyId::grid_template_rows:
        return read_track_list(reader, index, last, style.grid.template_rows);
    case PropertyId::grid_template_areas:
        return read_template_areas(reader, index, last, style.grid.template_areas);
    case PropertyId::grid_auto_flow:
        return read_grid_auto_flow(reader, index, last, style.grid.auto_flow);
    case PropertyId::grid_auto_columns:
        return read_auto_tracks(reader, index, last, style.grid.auto_columns);
    case PropertyId::grid_auto_rows:
        return read_auto_tracks(reader, index, last, style.grid.auto_rows);
    case PropertyId::grid_column:
        return read_grid_line_pair(reader, index, last, style.grid_item.column_start, style.grid_item.column_end);
    case PropertyId::grid_row:
        return read_grid_line_pair(reader, index, last, style.grid_item.row_start, style.grid_item.row_end);
    case PropertyId::grid_area:
        return read_grid_area(reader, index, last, style.grid_item);
    case PropertyId::grid_column_start:
        return read_single_grid_line(reader, index, last, style.grid_item.column_start);
    case PropertyId::grid_column_end:
        return read_single_grid_line(reader, index, last, style.grid_item.column_end);
    case PropertyId::grid_row_start:
        return read_single_grid_line(reader, index, last, style.grid_item.row_start);
    case PropertyId::grid_row_end:
        return read_single_grid_line(reader, index, last, style.grid_item.row_end);
    case PropertyId::text_align:
        return read_keyword<TextAlignment>(reader, index, last, text_align_keywords, text_align_left_out,
                                           style.text_align);
    case PropertyId::vertical_align:
        return read_vertical_align(reader, index, last, style.vertical_align);
    }
    return refuse_token(reader, DiagnosticCode::value_syntax, index);
}

// Forms refused wherever they appear, found before the property's own
// reader: the global keywords, the functions L leaves out, `!important`.
bool refuse_left_out_forms(ValueReader& reader) {
    const std::size_t count = reader.value_tokens.size();
    if (count == 1U && is_kind(reader, 0U, count, TokenKind::ident) &&
        contains_folded(global_keywords, value_text(reader, 0U))) {
        return refuse_token(reader, DiagnosticCode::value_form_not_admitted, 0U);
    }
    for (std::size_t index = 0U; index < count; ++index) {
        if (is_kind(reader, index, count, TokenKind::function) &&
            contains_folded(refused_functions, function_name(reader, index))) {
            return refuse_token(reader, DiagnosticCode::value_form_not_admitted, index);
        }
        if (is_delim(reader, index, count, '!')) {
            if (is_ident(reader, index + 1U, count, "important")) {
                return refuse(reader, DiagnosticCode::value_form_not_admitted, index, "!important");
            }
            return refuse_token(reader, DiagnosticCode::value_syntax, index);
        }
    }
    return true;
}

bool read_declaration_value(ValueReader& reader, PropertyId property, LayoutStyle& style) {
    if (!refuse_left_out_forms(reader)) return false;
    const std::size_t last = reader.value_tokens.size();
    std::size_t index = 0U;
    if (!read_property_value(reader, property, index, last, style)) return false;
    if (index != last) return refuse_token(reader, DiagnosticCode::value_syntax, index);
    return true;
}

bool find_property(std::string_view written, PropertyId& property) noexcept {
    for (const PropertyEntry& entry : admitted_properties) {
        if (!equals_ascii_folded(written, entry.name)) continue;
        property = entry.id;
        return true;
    }
    return false;
}

bool insignificant_token(TokenKind kind) noexcept {
    return kind == TokenKind::whitespace || kind == TokenKind::comment || kind == TokenKind::byte_order_mark;
}

bool opens_block(TokenKind kind) noexcept {
    return kind == TokenKind::function || kind == TokenKind::left_paren || kind == TokenKind::left_square;
}

bool closes_block(TokenKind kind) noexcept {
    return kind == TokenKind::right_paren || kind == TokenKind::right_square;
}

bool closer_matches(TokenKind opener, TokenKind closer) noexcept {
    if (opener == TokenKind::left_square) return closer == TokenKind::right_square;
    return closer == TokenKind::right_paren;
}

// -- the serializer ---------------------------------------------------------------

void append_number(std::string& text, double value) {
    const double normalized = value == 0.0 ? 0.0 : value;
    std::array<char, 32U> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), normalized);
    text.append(buffer.data(), written.ptr);
}

void append_length_percentage(std::string& text, const LengthPercentage& value) {
    const double length = value.length.value();
    if (value.percent == 0.0) {
        append_number(text, length);
        text += "px";
        return;
    }
    if (length == 0.0) {
        append_number(text, value.percent);
        text += '%';
        return;
    }
    text += "calc(";
    append_number(text, value.percent);
    text += length < 0.0 ? "% - " : "% + ";
    append_number(text, std::abs(length));
    text += "px)";
}

void append_size(std::string& text, const SizeValue& value) {
    switch (value.kind) {
    case SizeKind::automatic:
        text += "auto";
        return;
    case SizeKind::none:
        text += "none";
        return;
    case SizeKind::min_content:
        text += "min-content";
        return;
    case SizeKind::max_content:
        text += "max-content";
        return;
    case SizeKind::content:
        text += "content";
        return;
    case SizeKind::fit_content:
        text += "fit-content(";
        append_length_percentage(text, value.value);
        text += ')';
        return;
    case SizeKind::length_percentage:
        append_length_percentage(text, value.value);
        return;
    default: // Not a SizeKind: no keyword names it, so nothing is written.
        return;
    }
}

void append_auto_length_percentage(std::string& text, const AutoLengthPercentage& value) {
    if (value.automatic) {
        text += "auto";
        return;
    }
    append_length_percentage(text, value.value);
}

template <typename Value> std::string_view keyword_name(std::span<const KeywordEntry<Value>> names, Value value) noexcept {
    for (const KeywordEntry<Value>& entry : names) {
        if (entry.value == value) return entry.name;
    }
    return "?";
}

void append_breadth(std::string& text, const TrackBreadth& breadth) {
    switch (breadth.kind) {
    case TrackBreadthKind::automatic:
        text += "auto";
        return;
    case TrackBreadthKind::min_content:
        text += "min-content";
        return;
    case TrackBreadthKind::max_content:
        text += "max-content";
        return;
    case TrackBreadthKind::fraction:
        append_number(text, breadth.fraction.value());
        text += "fr";
        return;
    case TrackBreadthKind::length_percentage:
        append_length_percentage(text, breadth.length);
        return;
    default: // Not a TrackBreadthKind: no keyword names it, so nothing is written.
        return;
    }
}

void append_track_size(std::string& text, const GridTrackSize& size) {
    if (size.fit_content) {
        text += "fit-content(";
        append_length_percentage(text, size.fit_content_limit);
        text += ')';
        return;
    }
    const bool lone_fraction = size.minimum.kind == TrackBreadthKind::automatic &&
                               size.maximum.kind == TrackBreadthKind::fraction;
    if (size.minimum == size.maximum || lone_fraction) {
        append_breadth(text, size.maximum);
        return;
    }
    text += "minmax(";
    append_breadth(text, size.minimum);
    text += ", ";
    append_breadth(text, size.maximum);
    text += ')';
}

void append_track_sizes(std::string& text, std::span<const GridTrackSize> tracks) {
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        if (index != 0U) text += ' ';
        append_track_size(text, tracks[index]);
    }
}

void append_track_list(std::string& text, const GridTrackList& list) {
    if (list.tracks.empty() && list.auto_repeat == AutoRepeatKind::none) {
        text += "none";
        return;
    }
    bool written = false;
    for (std::size_t index = 0U; index <= list.tracks.size(); ++index) {
        if (list.auto_repeat != AutoRepeatKind::none && index == list.auto_repeat_index) {
            if (written) text += ' ';
            text += list.auto_repeat == AutoRepeatKind::auto_fill ? "repeat(auto-fill, " : "repeat(auto-fit, ";
            append_track_sizes(text, list.auto_repeat_tracks);
            text += ')';
            written = true;
        }
        if (index == list.tracks.size()) break;
        if (written) text += ' ';
        append_track_size(text, list.tracks[index]);
        written = true;
    }
}

void append_template_areas(std::string& text, const GridTemplateAreas& areas) {
    if (areas.rows == 0U) {
        text += "none";
        return;
    }
    const std::size_t cell_count = static_cast<std::size_t>(areas.rows) * areas.columns;
    std::vector<std::string_view> cells(cell_count, ".");
    for (const GridNamedArea& area : areas.areas) {
        for (std::uint32_t row = area.row_start; row < area.row_end; ++row) {
            for (std::uint32_t column = area.column_start; column < area.column_end; ++column) {
                cells[static_cast<std::size_t>(row) * areas.columns + column] = area.name;
            }
        }
    }
    for (std::uint32_t row = 0U; row < areas.rows; ++row) {
        if (row != 0U) text += ' ';
        text += '"';
        for (std::uint32_t column = 0U; column < areas.columns; ++column) {
            if (column != 0U) text += ' ';
            text += cells[static_cast<std::size_t>(row) * areas.columns + column];
        }
        text += '"';
    }
}

void append_grid_line(std::string& text, const GridLine& line) {
    switch (line.kind) {
    case GridLineKind::automatic:
        text += "auto";
        return;
    case GridLineKind::line:
        text += std::to_string(line.number);
        return;
    case GridLineKind::span:
        text += "span ";
        text += std::to_string(line.number);
        return;
    case GridLineKind::area:
        text += line.area;
        return;
    default: // Not a GridLineKind: no keyword names it, so nothing is written.
        return;
    }
}

// Appends `name: ` and opens the declaration, separating it from the one
// before.
void begin_declaration(std::string& text, std::string_view name) {
    if (!text.empty()) text += "; ";
    text += name;
    text += ": ";
}

void describe_size(std::string& text, std::string_view name, const SizeValue& value, const SizeValue& base) {
    if (value == base) return;
    begin_declaration(text, name);
    append_size(text, value);
}

void describe_auto_length(std::string& text, std::string_view name, const AutoLengthPercentage& value,
                          const AutoLengthPercentage& base) {
    if (value == base) return;
    begin_declaration(text, name);
    append_auto_length_percentage(text, value);
}

void describe_length(std::string& text, std::string_view name, const LengthPercentage& value,
                     const LengthPercentage& base) {
    if (value == base) return;
    begin_declaration(text, name);
    append_length_percentage(text, value);
}

void describe_alignment(std::string& text, std::string_view name, LayoutAlignment value, LayoutAlignment base) {
    if (value == base) return;
    begin_declaration(text, name);
    text += keyword_name<LayoutAlignment>(alignment_names, value);
}

void describe_grid_line(std::string& text, std::string_view name, const GridLine& value, const GridLine& base) {
    if (value == base) return;
    begin_declaration(text, name);
    append_grid_line(text, value);
}

void describe_number(std::string& text, std::string_view name, double value, double base) {
    if (value == base) return;
    begin_declaration(text, name);
    append_number(text, value);
}

} // namespace

StyleReadOutcome LayoutStyleReader::read_inline_style(const ThemeSource& source, LayoutStyle& style,
                                                      ThemeDiagnosticList& diagnostics) {
    const std::size_t errors_before = diagnostics.error_count();
    const theme_language::TokenizeOutcome tokenized =
        theme_language::tokenize_theme_sheet(source, tokens_, lines_, diagnostics);
    if (tokenized == theme_language::TokenizeOutcome::refused_too_large) return StyleReadOutcome::refused;
    if (diagnostics.error_count() != errors_before) return StyleReadOutcome::refused;
    scratch_ = style;
    SourcePositionCursor positions(lines_, source.text);
    const std::vector<Token>& tokens = tokens_.tokens;
    const std::size_t token_count = tokens.size();
    std::size_t position = 0U;
    std::size_t declarations = 0U;
    // Openers on the stack, by value-token index; at most
    // maximum_nesting_depth deep (E504 beyond).
    std::array<std::uint32_t, theme_language::maximum_nesting_depth> open_blocks{};
    while (true) {
        while (position < token_count &&
               (insignificant_token(tokens[position].kind) || tokens[position].kind == TokenKind::semicolon)) {
            ++position;
        }
        if (position == token_count) break;
        const Token& name_token = tokens[position];
        const std::string_view name = token_text(source.text, name_token);
        if (name_token.kind != TokenKind::ident) {
            report(diagnostics, positions, source.file, DiagnosticCode::unexpected_token, name_token.offset, "", name);
            break;
        }
        ++declarations;
        if (declarations > theme_language::maximum_declarations_per_rule) {
            report(diagnostics, positions, source.file, DiagnosticCode::too_many_declarations, name_token.offset, "",
                   std::to_string(theme_language::maximum_declarations_per_rule));
            break;
        }
        ++position;
        while (position < token_count && insignificant_token(tokens[position].kind)) ++position;
        if (position == token_count || tokens[position].kind != TokenKind::colon) {
            const ByteOffset offset = position == token_count ? ByteOffset{static_cast<std::uint32_t>(source.text.size())}
                                                              : tokens[position].offset;
            const std::string_view detail =
                position == token_count ? std::string_view{"end of style"} : token_text(source.text, tokens[position]);
            report(diagnostics, positions, source.file, DiagnosticCode::unexpected_token, offset, name, detail);
            break;
        }
        ++position;
        // The value: significant tokens up to a semicolon outside brackets.
        value_tokens_.clear();
        value_closers_.clear();
        std::size_t depth = 0U;
        bool structure_failed = false;
        while (position < token_count && !(tokens[position].kind == TokenKind::semicolon && depth == 0U)) {
            const Token& token = tokens[position];
            if (insignificant_token(token.kind)) {
                ++position;
                continue;
            }
            const std::uint32_t value_index = static_cast<std::uint32_t>(value_tokens_.size());
            if (token.kind == TokenKind::left_brace || token.kind == TokenKind::right_brace) {
                report(diagnostics, positions, source.file, DiagnosticCode::unexpected_token, token.offset, name,
                       token_text(source.text, token));
                structure_failed = true;
                break;
            }
            value_tokens_.push_back(static_cast<std::uint32_t>(position));
            value_closers_.push_back(value_index);
            if (opens_block(token.kind)) {
                if (depth == open_blocks.size()) {
                    report(diagnostics, positions, source.file, DiagnosticCode::nesting_too_deep, token.offset, name,
                           std::to_string(theme_language::maximum_nesting_depth));
                    structure_failed = true;
                    break;
                }
                open_blocks[depth] = value_index;
                ++depth;
            } else if (closes_block(token.kind)) {
                const bool matched =
                    depth > 0U && closer_matches(tokens[value_tokens_[open_blocks[depth - 1U]]].kind, token.kind);
                if (!matched) {
                    report(diagnostics, positions, source.file, DiagnosticCode::unbalanced_block, token.offset, name,
                           token_text(source.text, token));
                    structure_failed = true;
                    break;
                }
                --depth;
                value_closers_[open_blocks[depth]] = value_index;
            }
            ++position;
        }
        if (structure_failed) break;
        if (depth > 0U) {
            const Token& opener = tokens[value_tokens_[open_blocks[depth - 1U]]];
            report(diagnostics, positions, source.file, DiagnosticCode::unbalanced_block, opener.offset, name,
                   token_text(source.text, opener));
            break;
        }
        PropertyId property = PropertyId::display;
        if (!find_property(name, property)) {
            const DiagnosticCode code = contains_folded(left_out_properties, name)
                                            ? DiagnosticCode::property_not_admitted
                                            : DiagnosticCode::unknown_property;
            report(diagnostics, positions, source.file, code, name_token.offset, name, "");
            continue;
        }
        if (value_tokens_.empty()) {
            const ByteOffset offset = position == token_count ? ByteOffset{static_cast<std::uint32_t>(source.text.size())}
                                                              : tokens[position].offset;
            report(diagnostics, positions, source.file, DiagnosticCode::value_syntax, offset, name, "an empty value");
            continue;
        }
        ValueReader reader{source.text, tokens,          tokens_.numbers, value_tokens_, value_closers_,
                           template_cells_, source.file, name,          positions,     diagnostics};
        static_cast<void>(read_declaration_value(reader, property, scratch_));
    }
    if (diagnostics.error_count() != errors_before) return StyleReadOutcome::refused;
    style = scratch_;
    return StyleReadOutcome::applied;
}

std::string describe_layout_style(const LayoutStyle& style, const LayoutStyle& base) {
    std::string text;
    if (style.display != base.display) {
        begin_declaration(text, "display");
        text += keyword_name<DisplayKind>(display_keywords, style.display);
    }
    if (style.position != base.position) {
        begin_declaration(text, "position");
        text += keyword_name<PositionKind>(position_keywords, style.position);
    }
    describe_auto_length(text, "top", style.inset.top, base.inset.top);
    describe_auto_length(text, "right", style.inset.right, base.inset.right);
    describe_auto_length(text, "bottom", style.inset.bottom, base.inset.bottom);
    describe_auto_length(text, "left", style.inset.left, base.inset.left);
    describe_size(text, "width", style.width, base.width);
    describe_size(text, "height", style.height, base.height);
    describe_size(text, "min-width", style.min_width, base.min_width);
    describe_size(text, "min-height", style.min_height, base.min_height);
    describe_size(text, "max-width", style.max_width, base.max_width);
    describe_size(text, "max-height", style.max_height, base.max_height);
    describe_auto_length(text, "margin-top", style.margin.top, base.margin.top);
    describe_auto_length(text, "margin-right", style.margin.right, base.margin.right);
    describe_auto_length(text, "margin-bottom", style.margin.bottom, base.margin.bottom);
    describe_auto_length(text, "margin-left", style.margin.left, base.margin.left);
    describe_length(text, "padding-top", style.padding.top, base.padding.top);
    describe_length(text, "padding-right", style.padding.right, base.padding.right);
    describe_length(text, "padding-bottom", style.padding.bottom, base.padding.bottom);
    describe_length(text, "padding-left", style.padding.left, base.padding.left);
    describe_length(text, "row-gap", style.row_gap, base.row_gap);
    describe_length(text, "column-gap", style.column_gap, base.column_gap);
    if (style.flex.direction != base.flex.direction) {
        begin_declaration(text, "flex-direction");
        text += keyword_name<FlexDirection>(flex_direction_keywords, style.flex.direction);
    }
    if (style.flex.wrap != base.flex.wrap) {
        begin_declaration(text, "flex-wrap");
        text += keyword_name<FlexWrap>(flex_wrap_keywords, style.flex.wrap);
    }
    describe_number(text, "flex-grow", style.flex_item.grow.value(), base.flex_item.grow.value());
    describe_number(text, "flex-shrink", style.flex_item.shrink.value(), base.flex_item.shrink.value());
    describe_size(text, "flex-basis", style.flex_item.basis, base.flex_item.basis);
    if (style.flex_item.order != base.flex_item.order) {
        begin_declaration(text, "order");
        text += std::to_string(style.flex_item.order);
    }
    describe_alignment(text, "justify-content", style.alignment.justify_content, base.alignment.justify_content);
    describe_alignment(text, "align-content", style.alignment.align_content, base.alignment.align_content);
    describe_alignment(text, "justify-items", style.alignment.justify_items, base.alignment.justify_items);
    describe_alignment(text, "align-items", style.alignment.align_items, base.alignment.align_items);
    describe_alignment(text, "justify-self", style.alignment.justify_self, base.alignment.justify_self);
    describe_alignment(text, "align-self", style.alignment.align_self, base.alignment.align_self);
    if (!(style.grid.template_columns == base.grid.template_columns)) {
        begin_declaration(text, "grid-template-columns");
        append_track_list(text, style.grid.template_columns);
    }
    if (!(style.grid.template_rows == base.grid.template_rows)) {
        begin_declaration(text, "grid-template-rows");
        append_track_list(text, style.grid.template_rows);
    }
    if (!(style.grid.template_areas == base.grid.template_areas)) {
        begin_declaration(text, "grid-template-areas");
        append_template_areas(text, style.grid.template_areas);
    }
    if (style.grid.auto_flow != base.grid.auto_flow) {
        begin_declaration(text, "grid-auto-flow");
        text += keyword_name<GridAutoFlow>(grid_auto_flow_names, style.grid.auto_flow);
    }
    if (!(style.grid.auto_columns == base.grid.auto_columns)) {
        begin_declaration(text, "grid-auto-columns");
        append_track_sizes(text, style.grid.auto_columns);
    }
    if (!(style.grid.auto_rows == base.grid.auto_rows)) {
        begin_declaration(text, "grid-auto-rows");
        append_track_sizes(text, style.grid.auto_rows);
    }
    describe_grid_line(text, "grid-column-start", style.grid_item.column_start, base.grid_item.column_start);
    describe_grid_line(text, "grid-column-end", style.grid_item.column_end, base.grid_item.column_end);
    describe_grid_line(text, "grid-row-start", style.grid_item.row_start, base.grid_item.row_start);
    describe_grid_line(text, "grid-row-end", style.grid_item.row_end, base.grid_item.row_end);
    if (style.text_align != base.text_align) {
        begin_declaration(text, "text-align");
        text += keyword_name<TextAlignment>(text_align_keywords, style.text_align);
    }
    if (style.vertical_align != base.vertical_align) {
        begin_declaration(text, "vertical-align");
        text += keyword_name<InlineVerticalAlignment>(vertical_align_keywords, style.vertical_align);
    }
    return text;
}

} // namespace gui_forms::layout_style
