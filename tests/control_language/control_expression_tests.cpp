// The composite-control language's expressions (cp34, phase 1;
// control_expression.hpp). Gate: every primary and operator parses to its
// node with its operands and offset; precedence and sequences hold; the
// 16-deep budget is refused in the parser without deeper recursion; every
// malformed value is refused at the byte where it goes wrong.

#include "../../src/core/control_language/control_expression/control_expression.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using gui_forms::control_language::ExpressionNode;
using gui_forms::control_language::ExpressionNodeIndex;
using gui_forms::control_language::ExpressionNodeKind;
using gui_forms::control_language::ExpressionOperands;
using gui_forms::control_language::ExpressionOperator;
using gui_forms::control_language::ExpressionParseResult;
using gui_forms::control_language::ParsedExpression;
using gui_forms::control_language::expression_operands;
using gui_forms::control_language::maximum_expression_depth;
using gui_forms::control_language::parse_expressions;

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const ExpressionNode& term_node(const ParsedExpression& parsed, const std::size_t term) {
    const ExpressionNode& node = parsed.nodes[parsed.terms[term].value];
    return node;
}
const ExpressionNode& term_node(const ParsedExpression&& parsed, std::size_t term) = delete;

const ExpressionNode& operand_node(const ParsedExpression& parsed, const ExpressionNodeIndex node,
                                   const std::size_t operand) {
    const ExpressionOperands operands = expression_operands(parsed, node);
    const ExpressionNode& result = parsed.nodes[operands[operand].value];
    return result;
}
const ExpressionNode& operand_node(const ParsedExpression&& parsed, ExpressionNodeIndex node,
                                   std::size_t operand) = delete;

void test_primaries() {
    ParsedExpression parsed{};
    const ExpressionParseResult result =
        parse_expressions("8 0.5 90ms 'Wide FM' step-up @field @title.text self.action-text", parsed);
    require(result.valid, "primaries parse");
    require(parsed.terms.size() == 8U, "eight terms");
    require(term_node(parsed, 0U).kind == ExpressionNodeKind::number && term_node(parsed, 0U).text == "8",
            "a number");
    require(term_node(parsed, 1U).text == "0.5", "a fraction");
    require(term_node(parsed, 2U).text == "90" && term_node(parsed, 2U).unit == "ms", "a number with its unit");
    require(term_node(parsed, 3U).kind == ExpressionNodeKind::string && term_node(parsed, 3U).text == "Wide FM",
            "a string is decoded");
    require(term_node(parsed, 4U).kind == ExpressionNodeKind::word && term_node(parsed, 4U).text == "step-up",
            "a kebab word");
    require(term_node(parsed, 5U).kind == ExpressionNodeKind::local_reference && term_node(parsed, 5U).text ==
                "field" && term_node(parsed, 5U).member.empty(),
            "a local reference");
    require(term_node(parsed, 6U).member == "text", "a local reference's member");
    require(term_node(parsed, 7U).kind == ExpressionNodeKind::self_reference &&
                term_node(parsed, 7U).member == "action-text",
            "self.member");
    require(term_node(parsed, 4U).offset == 21U, "a node's offset is its first byte");
}

void test_calls_and_operators() {
    ParsedExpression parsed{};
    ExpressionParseResult result = parse_expressions("less(value, maximum) and not(read-only)", parsed);
    require(result.valid && parsed.terms.size() == 1U, "a condition is one term");
    const ExpressionNode& conjunction = term_node(parsed, 0U);
    require(conjunction.kind == ExpressionNodeKind::binary &&
                conjunction.operation == ExpressionOperator::logical_and && conjunction.argument_count == 2U,
            "and joins two operands");
    const ExpressionNode& less = operand_node(parsed, parsed.terms[0], 0U);
    require(less.kind == ExpressionNodeKind::call && less.text == "less" && less.argument_count == 2U,
            "a call with two arguments");
    const ExpressionNode& negation = operand_node(parsed, parsed.terms[0], 1U);
    require(negation.kind == ExpressionNodeKind::call && negation.text == "not",
            "not before ( is the call not()");

    result = parse_expressions("not read-only", parsed);
    require(result.valid && term_node(parsed, 0U).kind == ExpressionNodeKind::logical_not,
            "not before a word is the prefix operator");

    // Precedence: or < and < + - < * /.
    result = parse_expressions("calc(0 - step * 10)", parsed);
    require(result.valid, "arithmetic parses");
    const ExpressionNode& difference = operand_node(parsed, parsed.terms[0], 0U);
    require(difference.operation == ExpressionOperator::subtract, "subtraction is the outer operator");
    const ExpressionNodeIndex difference_index = expression_operands(parsed, parsed.terms[0])[0];
    require(operand_node(parsed, difference_index, 1U).operation == ExpressionOperator::multiply,
            "multiplication binds tighter");

    result = parse_expressions("a or b and c", parsed);
    require(result.valid && term_node(parsed, 0U).operation == ExpressionOperator::logical_or,
            "or binds loosest");

    result = parse_expressions("- step", parsed);
    require(result.valid && term_node(parsed, 0U).kind == ExpressionNodeKind::negate, "a prefix minus");

    result = parse_expressions("f()", parsed);
    require(result.valid && term_node(parsed, 0U).argument_count == 0U, "a call without arguments");

    result = parse_expressions("inset(track, 3) square align-inline(choose(checked, end, start))", parsed);
    require(result.valid && parsed.terms.size() == 3U, "a geometry sequence has its terms");
    require(parsed.depth == 3U, "the depth counts nesting: align-inline(choose(word))");

    result = parse_expressions("  ", parsed);
    require(result.valid && parsed.terms.empty() && parsed.nodes.empty(), "a blank value is an empty sequence");
}

void test_depth_budget() {
    // Seventeen nested calls: one deeper than the budget.
    std::string deep{};
    for (std::size_t level = 0U; level < maximum_expression_depth; ++level) deep += "f(";
    deep += "x";
    for (std::size_t level = 0U; level < maximum_expression_depth; ++level) deep += ")";
    ParsedExpression parsed{};
    ExpressionParseResult result = parse_expressions(deep, parsed);
    require(!result.valid && result.detail == "nested deeper than 16", "seventeen levels are refused");

    std::string allowed{};
    for (std::size_t level = 0U; level + 1U < maximum_expression_depth; ++level) allowed += "f(";
    allowed += "x";
    for (std::size_t level = 0U; level + 1U < maximum_expression_depth; ++level) allowed += ")";
    result = parse_expressions(allowed, parsed);
    require(result.valid && parsed.depth == maximum_expression_depth, "sixteen levels are admitted");

    // A deep value far past the budget is refused without recursing per
    // level: the parser stops at the budget.
    std::string far{};
    for (std::size_t level = 0U; level < 100000U; ++level) far += "(";
    result = parse_expressions(far, parsed);
    require(!result.valid && result.detail == "nested deeper than 16", "a far deeper value is refused at once");
}

void test_refusals() {
    ParsedExpression parsed{};
    ExpressionParseResult result = parse_expressions("less(value, maximum", parsed);
    require(!result.valid && result.offset == 19U, "an unclosed call is refused at the end");
    result = parse_expressions("@field.", parsed);
    require(!result.valid && result.offset == 6U, "a member is expected after the dot, refused at the dot");
    result = parse_expressions("self", parsed);
    require(result.valid && term_node(parsed, 0U).kind == ExpressionNodeKind::word,
            "self alone is a word (a trigger's target)");
    result = parse_expressions("self.", parsed);
    require(!result.valid, "self. needs a member");
    result = parse_expressions("'unterminated", parsed);
    require(!result.valid, "an unterminated string is refused");
    result = parse_expressions("'\\q'", parsed);
    require(!result.valid && result.detail.find("\\q") != std::string::npos, "an unknown escape is refused");
    result = parse_expressions("Value", parsed);
    require(!result.valid && result.offset == 0U, "an uppercase word is refused");
    result = parse_expressions("step-", parsed);
    require(!result.valid && result.detail == "the value ends where an operand is expected",
            "a trailing hyphen joins nothing: it is a minus without its operand");
    result = parse_expressions("f(a,)", parsed);
    require(!result.valid, "a missing argument is refused");
    result = parse_expressions(")", parsed);
    require(!result.valid && result.offset == 0U, "a stray ) is refused");
    result = parse_expressions("a +", parsed);
    require(!result.valid && result.detail == "the value ends where an operand is expected",
            "a missing operand is refused at the end");
}

void test_reuse() {
    ParsedExpression parsed{};
    ExpressionParseResult result = parse_expressions("f(a, b, c) g(d)", parsed);
    require(result.valid && parsed.nodes.size() == 6U, "six nodes");
    result = parse_expressions("x", parsed);
    require(result.valid && parsed.nodes.size() == 1U && parsed.arguments.empty() && parsed.depth == 1U,
            "a reparse replaces the contents");
}

} // namespace

int main() {
    test_primaries();
    test_calls_and_operators();
    test_depth_budget();
    test_refusals();
    test_reuse();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "control expression tests passed\n";
    return 0;
}
