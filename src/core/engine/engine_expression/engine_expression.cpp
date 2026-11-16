// Bounded expressions (T2 plan, phase E5): compiling a postfix program with
// its checks and dependencies, and evaluating it.

#include "gui_forms/engine/engine_expression/engine_expression.hpp"

#include <cmath>

// Identical bits on every host need the same operations in the same order:
// no fused multiply-add for this translation unit (T2 plan, "Numerics").
// Off in every build, whatever GUI_FORMS_REFERENCE_ARITHMETIC says
// (CMakeLists.txt, "Floating-point contraction").
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms {
namespace {

bool operand_operation(ExpressionOperation operation) noexcept {
    const bool operand = operation == ExpressionOperation::constant || operation == ExpressionOperation::input ||
                         operation == ExpressionOperation::state || operation == ExpressionOperation::parameter;
    return operand;
}

// The three operands of an operation, the first pushed first; operations
// taking fewer leave the rest 0.
struct Operands final {
    double a{};
    double b{};
    double c{};
};

// What one operation gave: its value, or that it divided by zero.
struct Applied final {
    double value{};
    bool division_by_zero{};
};

double smaller(double left, double right) noexcept {
    const double value = right < left ? right : left;
    return value;
}

double larger(double left, double right) noexcept {
    const double value = left < right ? right : left;
    return value;
}

double truth(bool holds) noexcept {
    const double value = holds ? 1.0 : 0.0;
    return value;
}

// One operation on its operands. Called only for the operations that take
// operands, with finite operands.
Applied apply_operation(ExpressionOperation operation, const Operands& operands) noexcept {
    Applied result{};
    const double a = operands.a;
    const double b = operands.b;
    const double c = operands.c;
    switch (operation) {
    case ExpressionOperation::negate: result.value = -a; return result;
    case ExpressionOperation::add: result.value = a + b; return result;
    case ExpressionOperation::subtract: result.value = a - b; return result;
    case ExpressionOperation::multiply: result.value = a * b; return result;
    case ExpressionOperation::divide:
        if (b == 0.0) {
            result.division_by_zero = true;
            return result;
        }
        result.value = a / b;
        return result;
    case ExpressionOperation::minimum: result.value = smaller(a, b); return result;
    case ExpressionOperation::maximum: result.value = larger(a, b); return result;
    case ExpressionOperation::less: result.value = truth(a < b); return result;
    case ExpressionOperation::less_equal: result.value = truth(a <= b); return result;
    case ExpressionOperation::greater: result.value = truth(a > b); return result;
    case ExpressionOperation::greater_equal: result.value = truth(a >= b); return result;
    case ExpressionOperation::equal: result.value = truth(a == b); return result;
    case ExpressionOperation::not_equal: result.value = truth(a != b); return result;
    case ExpressionOperation::clamp: {
        const double raised = larger(a, b);
        result.value = smaller(raised, c);
        return result;
    }
    case ExpressionOperation::mix: {
        const double span = b - a;
        const double travel = span * c;
        result.value = a + travel;
        return result;
    }
    case ExpressionOperation::smoothstep: {
        // smoothstep(edge0 = a, edge1 = b, x = c).
        const double width = b - a;
        if (width == 0.0) {
            result.division_by_zero = true;
            return result;
        }
        const double offset = c - a;
        const double ratio = offset / width;
        const double u = smaller(larger(ratio, 0.0), 1.0);
        const double rising = 3.0 - 2.0 * u;
        result.value = u * u * rising;
        return result;
    }
    case ExpressionOperation::choose: result.value = a != 0.0 ? b : c; return result;
    case ExpressionOperation::constant:
    case ExpressionOperation::input:
    case ExpressionOperation::state:
    case ExpressionOperation::parameter:
    case ExpressionOperation::count:
        // Leaves and the count sentinel take no operands; callers never
        // apply them. They yield zero.
        return result;
    }
    // An operation outside the enumeration yields zero.
    return result;
}

} // namespace

const char* expression_operation_name(ExpressionOperation operation) noexcept {
    switch (operation) {
    case ExpressionOperation::constant: return "constant";
    case ExpressionOperation::input: return "input";
    case ExpressionOperation::state: return "state";
    case ExpressionOperation::parameter: return "parameter";
    case ExpressionOperation::negate: return "negate";
    case ExpressionOperation::add: return "add";
    case ExpressionOperation::subtract: return "subtract";
    case ExpressionOperation::multiply: return "multiply";
    case ExpressionOperation::divide: return "divide";
    case ExpressionOperation::minimum: return "minimum";
    case ExpressionOperation::maximum: return "maximum";
    case ExpressionOperation::less: return "less";
    case ExpressionOperation::less_equal: return "less_equal";
    case ExpressionOperation::greater: return "greater";
    case ExpressionOperation::greater_equal: return "greater_equal";
    case ExpressionOperation::equal: return "equal";
    case ExpressionOperation::not_equal: return "not_equal";
    case ExpressionOperation::clamp: return "clamp";
    case ExpressionOperation::mix: return "mix";
    case ExpressionOperation::smoothstep: return "smoothstep";
    case ExpressionOperation::choose: return "choose";
    case ExpressionOperation::count: return "count";
    }
    return "unknown";
}

std::size_t expression_operand_count(ExpressionOperation operation) noexcept {
    switch (operation) {
    case ExpressionOperation::constant:
    case ExpressionOperation::input:
    case ExpressionOperation::state:
    case ExpressionOperation::parameter:
    case ExpressionOperation::count: return 0U;
    case ExpressionOperation::negate: return 1U;
    case ExpressionOperation::add:
    case ExpressionOperation::subtract:
    case ExpressionOperation::multiply:
    case ExpressionOperation::divide:
    case ExpressionOperation::minimum:
    case ExpressionOperation::maximum:
    case ExpressionOperation::less:
    case ExpressionOperation::less_equal:
    case ExpressionOperation::greater:
    case ExpressionOperation::greater_equal:
    case ExpressionOperation::equal:
    case ExpressionOperation::not_equal: return 2U;
    case ExpressionOperation::clamp:
    case ExpressionOperation::mix:
    case ExpressionOperation::smoothstep:
    case ExpressionOperation::choose: return 3U;
    }
    return 0U;
}

const char* expression_defect_name(ExpressionDefect defect) noexcept {
    switch (defect) {
    case ExpressionDefect::none: return "none";
    case ExpressionDefect::empty_program: return "empty_program";
    case ExpressionDefect::over_budget: return "over_budget";
    case ExpressionDefect::unknown_operation: return "unknown_operation";
    case ExpressionDefect::constant_not_finite: return "constant_not_finite";
    case ExpressionDefect::fallback_not_finite: return "fallback_not_finite";
    case ExpressionDefect::limits_out_of_range: return "limits_out_of_range";
    case ExpressionDefect::input_out_of_range: return "input_out_of_range";
    case ExpressionDefect::state_out_of_range: return "state_out_of_range";
    case ExpressionDefect::parameter_out_of_range: return "parameter_out_of_range";
    case ExpressionDefect::missing_operand: return "missing_operand";
    case ExpressionDefect::stack_too_deep: return "stack_too_deep";
    case ExpressionDefect::unbalanced: return "unbalanced";
    }
    return "unknown";
}

const char* expression_fallback_name(ExpressionFallback fallback) noexcept {
    switch (fallback) {
    case ExpressionFallback::none: return "none";
    case ExpressionFallback::division_by_zero: return "division_by_zero";
    case ExpressionFallback::not_finite: return "not_finite";
    case ExpressionFallback::environment_too_short: return "environment_too_short";
    }
    return "unknown";
}

EngineExpression::EngineExpression() noexcept {
    instructions_[0] = ExpressionInstruction::number(1.0);
    count_ = 1U;
}

ExpressionCompilation EngineExpression::compile(std::span<const ExpressionInstruction> program, double fallback,
                                                ExpressionLimits limits) noexcept {
    ExpressionCompilation result{};
    if (program.empty()) {
        result.defect = ExpressionDefect::empty_program;
        return result;
    }
    if (program.size() > maximum_expression_operations) {
        result.defect = ExpressionDefect::over_budget;
        return result;
    }
    if (!std::isfinite(fallback)) {
        result.defect = ExpressionDefect::fallback_not_finite;
        return result;
    }
    if (limits.inputs > maximum_expression_inputs || limits.parameters > maximum_expression_parameters) {
        result.defect = ExpressionDefect::limits_out_of_range;
        return result;
    }
    // Every instruction in order: its own operand, then its effect on the
    // stack's depth.
    ExpressionDependencies dependencies{};
    std::size_t depth = 0U;
    for (std::size_t index = 0; index < program.size(); ++index) {
        const ExpressionInstruction& instruction = program[index];
        result.instruction = index;
        const ExpressionOperation operation = instruction.operation;
        if (static_cast<std::size_t>(operation) >= expression_operation_count) {
            result.defect = ExpressionDefect::unknown_operation;
            return result;
        }
        if (operation == ExpressionOperation::constant && !std::isfinite(instruction.constant)) {
            result.defect = ExpressionDefect::constant_not_finite;
            return result;
        }
        if (operation == ExpressionOperation::input) {
            if (instruction.index >= limits.inputs) {
                result.defect = ExpressionDefect::input_out_of_range;
                return result;
            }
            dependencies.inputs = dependencies.inputs | (std::uint64_t{1} << instruction.index);
        }
        if (operation == ExpressionOperation::state) {
            if (instruction.index >= anatomy_state_count) {
                result.defect = ExpressionDefect::state_out_of_range;
                return result;
            }
            dependencies.states.insert(static_cast<AnatomyState>(instruction.index));
        }
        if (operation == ExpressionOperation::parameter) {
            if (instruction.index >= limits.parameters) {
                result.defect = ExpressionDefect::parameter_out_of_range;
                return result;
            }
            dependencies.parameters = dependencies.parameters | (std::uint32_t{1} << instruction.index);
        }
        const std::size_t operands = expression_operand_count(operation);
        if (depth < operands) {
            result.defect = ExpressionDefect::missing_operand;
            return result;
        }
        depth = depth - operands + 1U;
        if (depth > maximum_expression_stack) {
            result.defect = ExpressionDefect::stack_too_deep;
            return result;
        }
    }
    if (depth != 1U) {
        result.instruction = program.size() - 1U;
        result.defect = ExpressionDefect::unbalanced;
        return result;
    }
    // Checked; now commit.
    for (std::size_t index = 0; index < program.size(); ++index) instructions_[index] = program[index];
    for (std::size_t index = program.size(); index < instructions_.size(); ++index) instructions_[index] = {};
    count_ = program.size();
    fallback_ = fallback;
    limits_ = limits;
    dependencies_ = dependencies;
    result.instruction = 0U;
    return result;
}

ExpressionResult EngineExpression::evaluate(const ExpressionEnvironment& environment) const noexcept {
    ExpressionResult result{};
    result.value = fallback_;
    // The environment is checked once, before any instruction reads it.
    if (environment.inputs.size() < limits_.inputs || environment.parameters.size() < limits_.parameters) {
        result.fallback = ExpressionFallback::environment_too_short;
        return result;
    }
    // compile() proved that every operation finds its operands, that the
    // depth never passes maximum_expression_stack and that one value
    // remains, so the stack is indexed without checks.
    std::array<double, maximum_expression_stack> stack{};
    std::size_t depth = 0U;
    for (std::size_t index = 0; index < count_; ++index) {
        const ExpressionInstruction& instruction = instructions_[index];
        const ExpressionOperation operation = instruction.operation;
        double value = 0.0;
        if (operand_operation(operation)) {
            if (operation == ExpressionOperation::constant) {
                value = instruction.constant;
            } else if (operation == ExpressionOperation::input) {
                value = environment.inputs[instruction.index];
            } else if (operation == ExpressionOperation::state) {
                value = truth(environment.states.contains(static_cast<AnatomyState>(instruction.index)));
            } else {
                value = environment.parameters[instruction.index];
            }
        } else {
            const std::size_t operands = expression_operand_count(operation);
            depth = depth - operands;
            Operands taken{};
            taken.a = stack[depth];
            if (operands > 1U) taken.b = stack[depth + 1U];
            if (operands > 2U) taken.c = stack[depth + 2U];
            const Applied applied = apply_operation(operation, taken);
            if (applied.division_by_zero) {
                result.fallback = ExpressionFallback::division_by_zero;
                return result;
            }
            value = applied.value;
        }
        if (!std::isfinite(value)) {
            result.fallback = ExpressionFallback::not_finite;
            return result;
        }
        stack[depth] = value;
        depth = depth + 1U;
    }
    result.value = stack[0];
    result.fallback = ExpressionFallback::none;
    return result;
}

std::span<const ExpressionInstruction> EngineExpression::instructions() const noexcept {
    const std::span<const ExpressionInstruction> live(instructions_.data(), count_);
    return live;
}

bool operator==(const EngineExpression& left, const EngineExpression& right) noexcept {
    if (left.count_ != right.count_ || left.fallback_ != right.fallback_) return false;
    if (left.limits_.inputs != right.limits_.inputs || left.limits_.parameters != right.limits_.parameters) {
        return false;
    }
    if (!(left.dependencies_ == right.dependencies_)) return false;
    for (std::size_t index = 0; index < left.count_; ++index) {
        if (!(left.instructions_[index] == right.instructions_[index])) return false;
    }
    return true;
}

} // namespace gui_forms
