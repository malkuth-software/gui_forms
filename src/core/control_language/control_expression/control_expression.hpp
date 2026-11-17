#pragma once

// The expressions a composite-control document writes in its attribute
// values (cp34, the composite-control language, design note section 3):
// conditions (`less(value, maximum) and not(read-only)`), geometry
// (`inset(track, 3) square align-inline(choose(checked, end, start))`),
// action calls (`step(value, calc(0 - step))`) and lists
// (`range="minimum maximum"`). Internal.
//
// The grammar, one value at a time:
//
//   sequence   := expression { expression }        terms separated by space
//   expression := conjunction { "or" conjunction }
//   conjunction:= sum { "and" sum }
//   sum        := product { ("+" | "-") product }
//   product    := unary { ("*" | "/") unary }
//   unary      := "not" unary | "-" unary | primary
//   primary    := number [unit] | string | word [ "(" arguments ")" ]
//               | "@" word [ "." word ] | "self" "." word
//               | "(" expression ")"
//   arguments  := [ expression { "," expression } ]
//
// - A word is kebab case: a lowercase letter, then lowercase letters,
//   digits and single hyphens (`step-up`, `align-inline`). So an operator
//   `-` is written with space around it (`calc(0 - step)`).
// - A number is digits with an optional fraction, and an optional unit of
//   lowercase letters written against it (`8`, `0.5`, `90ms`, `1px`).
// - A string is quoted as a form document's strings are
//   (form_model.hpp, read_form_string): `''`, `'Wide FM'`.
// - `not` written before `(` is the call `not(...)`; written before
//   anything else it is the prefix operator. The two mean the same.
//
// The parse is the checker's first step: it gives the tree the checks walk
// (which words are read, which functions called), the instruction count
// (one per node: the theme language's expression bytecode spends one
// instruction per operand and operator) and the evaluation depth. The
// budget is the theme language's: at most 64 instructions in one value and
// a depth of 16 (design note 4.1). The parser itself never nests deeper
// than that depth plus one: a value nested deeper is refused there.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::control_language {

// The most instructions (nodes) one value's expressions may compile to,
// and the deepest evaluation stack (nesting) they may need.
inline constexpr std::size_t maximum_expression_instructions = 64U;
inline constexpr std::size_t maximum_expression_depth = 16U;

enum class ExpressionNodeKind : std::uint8_t {
    // `text` is the number as written, without its unit; `unit` the unit.
    number,
    // `text` is the decoded string.
    string,
    // A bare word: `text`.
    word,
    // A call: `text` is the function's name, its arguments are
    // ParsedExpression::arguments[first_argument, + argument_count).
    call,
    // `@name` or `@name.member`: `text` is the name without `@`, `member`
    // the member or empty.
    local_reference,
    // `self.member`: `member`.
    self_reference,
    // A prefix operator over one operand (the first argument).
    logical_not,
    negate,
    // An infix operator over two operands (the first two arguments).
    binary,
};

enum class ExpressionOperator : std::uint8_t {
    none,
    add,
    subtract,
    multiply,
    divide,
    logical_and,
    logical_or,
};

// A node's position in ParsedExpression::nodes.
struct ExpressionNodeIndex final {
    std::uint32_t value{};

    friend constexpr bool operator==(ExpressionNodeIndex left, ExpressionNodeIndex right) noexcept {
        return left.value == right.value;
    }
};

struct ExpressionNode final {
    ExpressionNodeKind kind{ExpressionNodeKind::word};
    ExpressionOperator operation{ExpressionOperator::none};
    std::string text{};
    std::string member{};
    std::string unit{};
    // Into ParsedExpression::arguments: the operands of a call or an
    // operator, in order.
    std::uint32_t first_argument{};
    std::uint32_t argument_count{};
    // The byte offset of the node's first character in the parsed value.
    std::size_t offset{};
};

// One parsed value. Caller-owned and reused: parse_expressions replaces
// the contents and keeps the capacity.
struct ParsedExpression final {
    std::vector<ExpressionNode> nodes{};
    // The operands of every call and operator, as node indices, each node's
    // operands contiguous.
    std::vector<ExpressionNodeIndex> arguments{};
    // The sequence's terms, in order.
    std::vector<ExpressionNodeIndex> terms{};
    // The deepest nesting of the parsed terms: a word alone is 1, `f(x)` 2.
    std::size_t depth{};

    void clear() noexcept;
};

struct ExpressionParseResult final {
    bool valid{};
    // Where the refusal was found, as a byte offset in the value, and what
    // was found there; unset for a valid parse.
    std::size_t offset{};
    std::string detail{};
};

// Parses `text` as a sequence into `parsed`. An empty or blank value is a
// valid empty sequence. On a refusal `parsed` holds what was parsed before
// it, which the caller must not use.
[[nodiscard]] ExpressionParseResult parse_expressions(std::string_view text, ParsedExpression& parsed);

// The operands of node `node`, borrowed from `parsed`. A temporary is
// refused: its storage ends with the statement.
struct ExpressionOperands final {
    const ExpressionNodeIndex* first{};
    std::size_t count{};

    [[nodiscard]] ExpressionNodeIndex operator[](std::size_t index) const noexcept { return first[index]; }
};
[[nodiscard]] ExpressionOperands expression_operands(const ParsedExpression& parsed,
                                                     ExpressionNodeIndex node) noexcept;
ExpressionOperands expression_operands(const ParsedExpression&& parsed, ExpressionNodeIndex node) = delete;

} // namespace gui_forms::control_language
