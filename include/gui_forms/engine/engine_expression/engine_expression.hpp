#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {

// Bounded expressions (T2 plan, phase E5; brief item 6): the small programs
// theme data uses to derive a value from the engine's inputs, a part's
// states and its primitive's parameters, such as a sheen's position from
// the pointer. T3 parses them from theme documents; until then they are C++
// data.
//
// What an expression may do is fixed:
// - read a constant, an engine input, a state or a parameter, by index;
// - arithmetic (+ − × ÷ and negation), minimum and maximum;
// - clamp, mix and smoothstep;
// - comparisons, which give 1 or 0;
// - choose one of two values by a condition.
// There are no variables, loops, recursion or timers. A program is a
// postfix sequence of at most maximum_expression_operations instructions,
// checked once when it is compiled (at theme load): every index is in
// range, every operation finds its operands, and the stack stays within
// maximum_expression_stack. Which inputs, states and parameters it reads is
// known from that check, before it is ever evaluated, so the engine knows
// statically what a part depends on (E6).
//
// Evaluation computes in double, without fused multiply-add and without the
// host's libm, as the kernels do (T2 plan, "Numerics"): the same program and
// inputs give the same bits on every host. A division by zero, or a value
// that is not finite (an input, or any operation's result), ends the
// evaluation with the program's declared fallback.

// The most instructions a program may have: its fixed operation budget.
inline constexpr std::size_t maximum_expression_operations = 64;
// The deepest the evaluation stack may grow.
inline constexpr std::size_t maximum_expression_stack = 16;
// The most inputs and parameters an expression can address, so that what it
// reads fits a 64-bit and a 32-bit set.
inline constexpr std::size_t maximum_expression_inputs = 64;
inline constexpr std::size_t maximum_expression_parameters = 32;

// The position of an input in the inputs an expression is evaluated with.
// Distinct from a parameter's position and from a state.
struct GUI_FORMS_API ExpressionInputIndex final {
    std::uint8_t value{};

    friend constexpr bool operator==(ExpressionInputIndex left, ExpressionInputIndex right) noexcept {
        return left.value == right.value;
    }
};

// Where the engine puts its inputs (E6): first the engine inputs, in
// EngineInput order, then the values an application publishes that the
// theme names (ThemeRuleSheet::published_values), in the order the theme
// lists them. An input that is not published, such as the pointer of a part
// that is not hot, reads as NaN, so an expression reading it gives its
// fallback.
inline constexpr std::size_t maximum_published_values = 32;
static_assert(engine_input_count + maximum_published_values <= maximum_expression_inputs,
              "every engine input and published value has an expression input position");

[[nodiscard]] constexpr ExpressionInputIndex engine_input_position(EngineInput input) noexcept {
    const ExpressionInputIndex position{static_cast<std::uint8_t>(input)};
    return position;
}

// The position of the theme's published value at `value` in its list,
// counting from 0.
[[nodiscard]] constexpr ExpressionInputIndex published_value_position(std::size_t value) noexcept {
    const ExpressionInputIndex position{static_cast<std::uint8_t>(engine_input_count + value)};
    return position;
}

// One input's bit in a set of inputs read (ExpressionDependencies::inputs).
[[nodiscard]] constexpr std::uint64_t expression_input_bit(ExpressionInputIndex input) noexcept {
    if (input.value >= maximum_expression_inputs) return 0U;
    const std::uint64_t bit = std::uint64_t{1} << input.value;
    return bit;
}

// The position of a number parameter among a primitive's declared
// parameters (PrimitiveDescriptor::parameters).
struct GUI_FORMS_API ExpressionParameterIndex final {
    std::uint8_t value{};

    friend constexpr bool operator==(ExpressionParameterIndex left, ExpressionParameterIndex right) noexcept {
        return left.value == right.value;
    }
};

// One instruction's operation. The operand count is in parentheses; an
// operation pops its operands (the last pushed is the last operand) and
// pushes its result.
enum class ExpressionOperation : std::uint8_t {
    // Operands (0): push one value.
    constant,  // the instruction's constant
    input,     // the input at the instruction's index
    state,     // 1 when the part is in the state, else 0
    parameter, // the number parameter at the instruction's index
    // Arithmetic.
    negate,   // (1) −a
    add,      // (2) a + b
    subtract, // (2) a − b
    multiply, // (2) a × b
    divide,   // (2) a ÷ b; b = 0 gives the fallback
    minimum,  // (2) the smaller of a and b
    maximum,  // (2) the larger of a and b
    // Comparisons (2): 1 when the relation holds, else 0.
    less,
    less_equal,
    greater,
    greater_equal,
    equal,
    not_equal,
    // clamp(x, low, high) (3): min(max(x, low), high), so a low above high
    // gives high.
    clamp,
    // mix(a, b, t) (3): a + (b − a) × t; t is not clamped.
    mix,
    // smoothstep(edge0, edge1, x) (3): u = clamp((x − edge0) ÷ (edge1 −
    // edge0), 0, 1), then u²(3 − 2u). Equal edges divide by zero and give
    // the fallback.
    smoothstep,
    // choose(condition, if_true, if_false) (3): if_true when condition is
    // not 0, else if_false. Both are evaluated; neither has effects.
    choose,
    count,
};

inline constexpr std::size_t expression_operation_count = static_cast<std::size_t>(ExpressionOperation::count);

// Registered (snake_case) name of an operation, for diagnostics and the
// schema.
[[nodiscard]] GUI_FORMS_API const char* expression_operation_name(ExpressionOperation operation) noexcept;

// How many operands an operation pops.
[[nodiscard]] GUI_FORMS_API std::size_t expression_operand_count(ExpressionOperation operation) noexcept;

// One instruction. Make it with the named factories, which keep the
// operation and its operand together: `index` is read by input, state and
// parameter, `constant` by constant.
struct GUI_FORMS_API ExpressionInstruction final {
    ExpressionOperation operation{ExpressionOperation::constant};
    std::uint8_t index{};
    double constant{};

    [[nodiscard]] static constexpr ExpressionInstruction number(double value) noexcept {
        ExpressionInstruction result{};
        result.operation = ExpressionOperation::constant;
        result.constant = value;
        return result;
    }
    [[nodiscard]] static constexpr ExpressionInstruction input(ExpressionInputIndex input) noexcept {
        ExpressionInstruction result{};
        result.operation = ExpressionOperation::input;
        result.index = input.value;
        return result;
    }
    [[nodiscard]] static constexpr ExpressionInstruction state(AnatomyState state) noexcept {
        ExpressionInstruction result{};
        result.operation = ExpressionOperation::state;
        result.index = static_cast<std::uint8_t>(state);
        return result;
    }
    [[nodiscard]] static constexpr ExpressionInstruction parameter(ExpressionParameterIndex parameter) noexcept {
        ExpressionInstruction result{};
        result.operation = ExpressionOperation::parameter;
        result.index = parameter.value;
        return result;
    }
    // An operation that takes its operands from the stack.
    [[nodiscard]] static constexpr ExpressionInstruction apply(ExpressionOperation operation) noexcept {
        ExpressionInstruction result{};
        result.operation = operation;
        return result;
    }

    friend constexpr bool operator==(const ExpressionInstruction& left, const ExpressionInstruction& right) noexcept {
        return left.operation == right.operation && left.index == right.index && left.constant == right.constant;
    }
};

// What a program reads, found when it is compiled: bit i of `inputs` for
// the input at position i, bit i of `parameters` for the parameter at
// position i, and the standard states in `states`.
struct GUI_FORMS_API ExpressionDependencies final {
    std::uint64_t inputs{};
    std::uint32_t parameters{};
    AnatomyStateSet states{};

    [[nodiscard]] constexpr bool reads_input(ExpressionInputIndex input) const noexcept {
        if (input.value >= maximum_expression_inputs) return false;
        const std::uint64_t bit = std::uint64_t{1} << input.value;
        return (inputs & bit) != 0U;
    }
    [[nodiscard]] constexpr bool reads_parameter(ExpressionParameterIndex parameter) const noexcept {
        if (parameter.value >= maximum_expression_parameters) return false;
        const std::uint32_t bit = std::uint32_t{1} << parameter.value;
        return (parameters & bit) != 0U;
    }

    friend constexpr bool operator==(const ExpressionDependencies& left,
                                     const ExpressionDependencies& right) noexcept {
        return left.inputs == right.inputs && left.parameters == right.parameters && left.states == right.states;
    }
};

// How many inputs and parameters the program may address: an index at or
// beyond either count is refused when the program is compiled. Each is at
// most its maximum_expression_* bound.
struct GUI_FORMS_API ExpressionLimits final {
    std::size_t inputs{};
    std::size_t parameters{};
};

enum class ExpressionDefect : std::uint8_t {
    none,
    empty_program,
    // More than maximum_expression_operations instructions.
    over_budget,
    unknown_operation,
    constant_not_finite,
    fallback_not_finite,
    // The limits exceed maximum_expression_inputs or _parameters.
    limits_out_of_range,
    input_out_of_range,
    state_out_of_range,
    parameter_out_of_range,
    // An operation finds fewer operands than it takes.
    missing_operand,
    // The stack would grow past maximum_expression_stack.
    stack_too_deep,
    // The program leaves other than exactly one value.
    unbalanced,
};

[[nodiscard]] GUI_FORMS_API const char* expression_defect_name(ExpressionDefect defect) noexcept;

// What an evaluation used: the program's value, or its fallback and why.
enum class ExpressionFallback : std::uint8_t {
    none,
    division_by_zero,
    not_finite,
    // The environment holds fewer inputs or parameters than the program's
    // limits; nothing was read.
    environment_too_short,
};

[[nodiscard]] GUI_FORMS_API const char* expression_fallback_name(ExpressionFallback fallback) noexcept;

struct GUI_FORMS_API ExpressionResult final {
    double value{};
    ExpressionFallback fallback{ExpressionFallback::none};
};

// What one evaluation reads. Every view is borrowed for the call. `inputs`
// and `parameters` must hold at least the program's limits.
struct GUI_FORMS_API ExpressionEnvironment final {
    std::span<const double> inputs{};
    AnatomyStateSet states{};
    std::span<const double> parameters{};
};

// A compilation's outcome: the program when `defect` is none, otherwise the
// defect and the position of the instruction it concerns (0 for a defect of
// the whole program).
struct GUI_FORMS_API ExpressionCompilation final {
    ExpressionDefect defect{ExpressionDefect::none};
    std::size_t instruction{};
};

// A compiled program: immutable, a fixed-size value with no allocation, so
// it can be copied into a rule table and evaluated from any number of parts
// on the UI thread.
class GUI_FORMS_API EngineExpression final {
public:
    // The program `1` with fallback 0, which reads nothing.
    EngineExpression() noexcept;

    // Checks `program` against `limits` and the budget, then replaces this
    // expression with it. Anything but `none` leaves the expression
    // unchanged. `fallback` is the value an evaluation gives on a division
    // by zero or a value that is not finite.
    [[nodiscard]] ExpressionCompilation compile(std::span<const ExpressionInstruction> program, double fallback,
                                                ExpressionLimits limits) noexcept;

    // Evaluates the program. Its cost is one operation per instruction
    // (operation_count). Never fails: a defect of the inputs gives the
    // fallback, with the reason.
    [[nodiscard]] ExpressionResult evaluate(const ExpressionEnvironment& environment) const noexcept;

    [[nodiscard]] std::span<const ExpressionInstruction> instructions() const noexcept;
    [[nodiscard]] std::size_t operation_count() const noexcept { return count_; }
    [[nodiscard]] double fallback() const noexcept { return fallback_; }
    [[nodiscard]] const ExpressionDependencies& dependencies() const noexcept { return dependencies_; }
    [[nodiscard]] const ExpressionLimits& limits() const noexcept { return limits_; }

    friend GUI_FORMS_API bool operator==(const EngineExpression& left, const EngineExpression& right) noexcept;

private:
    std::array<ExpressionInstruction, maximum_expression_operations> instructions_{};
    // Live instructions, 1 to maximum_expression_operations.
    std::size_t count_{};
    double fallback_{};
    ExpressionLimits limits_{};
    ExpressionDependencies dependencies_{};
};

} // namespace gui_forms
