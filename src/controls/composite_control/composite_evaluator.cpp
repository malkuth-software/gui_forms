// The composite control's expression evaluator (cp34, phase 2). See
// composite_evaluator.hpp for the value model and the name order.

#include "composite_evaluator.hpp"

#include "gui_forms/surface_root/surface_root.hpp"
#include "../basic/number_text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <variant>

namespace gui_forms {

namespace {

using control_language::CompositeExpression;
using control_language::ExpressionNode;
using control_language::ExpressionNodeIndex;
using control_language::ExpressionNodeKind;
using control_language::ExpressionOperator;

// Counts one nested custom-state or command-enabled condition for a scope.
class ConditionDepthScope final {
public:
    explicit ConditionDepthScope(std::size_t& depth) noexcept : depth_(depth) { ++depth_; }
    ~ConditionDepthScope() { --depth_; }
    ConditionDepthScope(const ConditionDepthScope&) = delete;
    ConditionDepthScope& operator=(const ConditionDepthScope&) = delete;

private:
    std::size_t& depth_;
};

// Two numbers within this are equal (properties hold doubles written as
// decimal text).
constexpr double number_tolerance = 1e-9;

// Number literals and numbers held as text, read in the C locale whatever
// LC_NUMERIC says (A-gf-controls-02-F4).
double parsed_number(const std::string& text) noexcept {
    const double value = number_text::leading_decimal(text);
    return value;
}

CompositeValue boolean_value(const bool truth) {
    CompositeValue value{};
    value.kind = CompositeValueKind::boolean;
    value.boolean = truth;
    return value;
}

CompositeValue number_value(const double number) {
    CompositeValue value{};
    value.kind = CompositeValueKind::number;
    value.number = number;
    return value;
}

CompositeValue word_value(const std::string_view word) {
    CompositeValue value{};
    value.kind = CompositeValueKind::word;
    value.text = std::string(word);
    return value;
}

double number_of(const CompositeValue& value) noexcept {
    switch (value.kind) {
    case CompositeValueKind::number: return value.number;
    case CompositeValueKind::boolean: return value.boolean ? 1.0 : 0.0;
    case CompositeValueKind::text:
    case CompositeValueKind::word: return parsed_number(value.text);
    case CompositeValueKind::none:
    default: return 0.0;
    }
}

// The value as text, for comparing a text with a number or a word.
std::string text_of(const CompositeValue& value) {
    switch (value.kind) {
    case CompositeValueKind::text:
    case CompositeValueKind::word: return value.text;
    case CompositeValueKind::boolean: return value.boolean ? "true" : "false";
    case CompositeValueKind::number: {
        // printf's "%.17g" in the C locale, whatever LC_NUMERIC says.
        std::string text{};
        number_text::append_number_text(text, value.number, std::chars_format::general, 17);
        return text;
    }
    case CompositeValueKind::none:
    default: return {};
    }
}

bool values_equal(const CompositeValue& left, const CompositeValue& right) {
    const bool numeric = (left.kind == CompositeValueKind::number || left.kind == CompositeValueKind::boolean) &&
                         (right.kind == CompositeValueKind::number || right.kind == CompositeValueKind::boolean);
    if (numeric) {
        const double difference = std::fabs(number_of(left) - number_of(right));
        return difference <= number_tolerance;
    }
    const std::string left_text = text_of(left);
    const std::string right_text = text_of(right);
    return left_text == right_text;
}

// The x of a box `width` wide placed at `position` (start, end, center, or
// a number: the box's centre) inside `reference`'s inline extent; the same
// for the block axis with y and height.
double placed(const CompositeValue& position, const double start, const double extent, const double size) {
    if (position.kind == CompositeValueKind::word) {
        if (position.text == "end") return start + extent - size;
        if (position.text == "center") return start + (extent - size) * 0.5;
        return start;
    }
    if (position.kind != CompositeValueKind::number) return start;
    const double centred = position.number - size * 0.5;
    const double last = start + extent - size;
    const double clamped = last >= start ? std::clamp(centred, start, last) : start;
    return clamped;
}

Rect inset_rect(const Rect base, const double top, const double right, const double bottom, const double left) {
    Rect inset{base.x + left, base.y + top, base.width - left - right, base.height - top - bottom};
    inset.width = std::max(inset.width, 0.0);
    inset.height = std::max(inset.height, 0.0);
    return inset;
}

} // namespace

CompositeEvaluator::CompositeEvaluator(const CompositeControl& control, const CompositeFiring firing) noexcept
    : control_(control), definition_(*control.definition_), firing_(firing) {}

bool CompositeEvaluator::truthy(const CompositeValue& value) noexcept {
    switch (value.kind) {
    case CompositeValueKind::boolean: return value.boolean;
    case CompositeValueKind::number: return value.number != 0.0;
    case CompositeValueKind::text:
    case CompositeValueKind::word: return !value.text.empty();
    case CompositeValueKind::none:
    default: return false;
    }
}

CompositeValue CompositeEvaluator::value_of(const BindingValue& value) {
    if (const bool* boolean = std::get_if<bool>(&value)) return boolean_value(*boolean);
    if (const double* number = std::get_if<double>(&value)) return number_value(*number);
    if (const std::int64_t* signed_number = std::get_if<std::int64_t>(&value)) {
        return number_value(static_cast<double>(*signed_number));
    }
    if (const std::uint64_t* unsigned_number = std::get_if<std::uint64_t>(&value)) {
        return number_value(static_cast<double>(*unsigned_number));
    }
    if (const std::string* text = std::get_if<std::string>(&value)) {
        CompositeValue made{};
        made.kind = CompositeValueKind::text;
        made.text = *text;
        return made;
    }
    if (const PropertyEnumValue* choice = std::get_if<PropertyEnumValue>(&value)) return word_value((*choice).name);
    return CompositeValue{};
}

CompositeValue CompositeEvaluator::first(const CompositeExpression& expression) const {
    if (expression.empty()) return CompositeValue{};
    const CompositeValue value = evaluate(expression, expression.parsed.terms[0]);
    return value;
}

bool CompositeEvaluator::holds(const CompositeExpression& expression) const {
    for (const ExpressionNodeIndex term : expression.parsed.terms) {
        if (!truthy(evaluate(expression, term))) return false;
    }
    return true;
}

double CompositeEvaluator::number(const CompositeExpression& expression) const {
    const double value = number_of(first(expression));
    return value;
}

std::string CompositeEvaluator::text(const CompositeExpression& expression) const {
    std::string value = text_of(first(expression));
    return value;
}

CompositeValue CompositeEvaluator::evaluate(const CompositeExpression& expression, const ExpressionNodeIndex index) const {
    const ExpressionNode& node = expression.parsed.nodes[index.value];
    switch (node.kind) {
    case ExpressionNodeKind::number: return number_value(parsed_number(node.text));
    case ExpressionNodeKind::string: {
        CompositeValue value{};
        value.kind = CompositeValueKind::text;
        value.text = node.text;
        return value;
    }
    case ExpressionNodeKind::word: return evaluate_word(node.text);
    case ExpressionNodeKind::call: return evaluate_call(expression, node);
    case ExpressionNodeKind::self_reference: return evaluate_word(node.member);
    case ExpressionNodeKind::local_reference: {
        if (node.member.empty()) return word_value(node.text);
        const Control::Ptr child = control_.child(node.text);
        if (!child) return CompositeValue{};
        const std::optional<BindingValue> read =
            (*child).property_value(control_language::composite_registered_name(node.member));
        if (!read) return CompositeValue{};
        return value_of(*read);
    }
    case ExpressionNodeKind::logical_not: return boolean_value(!truthy(argument(expression, node, 0U)));
    case ExpressionNodeKind::negate: return number_value(-number_of(argument(expression, node, 0U)));
    case ExpressionNodeKind::binary: {
        if (node.operation == ExpressionOperator::logical_and) {
            if (!truthy(argument(expression, node, 0U))) return boolean_value(false);
            return boolean_value(truthy(argument(expression, node, 1U)));
        }
        if (node.operation == ExpressionOperator::logical_or) {
            if (truthy(argument(expression, node, 0U))) return boolean_value(true);
            return boolean_value(truthy(argument(expression, node, 1U)));
        }
        const double left = number_of(argument(expression, node, 0U));
        const double right = number_of(argument(expression, node, 1U));
        switch (node.operation) {
        case ExpressionOperator::add: return number_value(left + right);
        case ExpressionOperator::subtract: return number_value(left - right);
        case ExpressionOperator::multiply: return number_value(left * right);
        case ExpressionOperator::divide: return number_value(right == 0.0 ? 0.0 : left / right);
        // none, logical_and and logical_or are not arithmetic: no number value.
        default: return CompositeValue{};
        }
    }
    default: return CompositeValue{};
    }
}

CompositeValue CompositeEvaluator::evaluate_word(const std::string_view word) const {
    if (word == "true" || word == "yes") return boolean_value(true);
    if (word == "false" || word == "no") return boolean_value(false);
    if (word == "frame-value") return number_value(firing_.frame_value);
    if (word == "frame-fraction") return number_value(firing_.frame_fraction);
    if (word == "wheel-notches") return number_value(firing_.wheel_notches);
    for (std::size_t index = 0U; index < definition_.properties.size(); ++index) {
        if (definition_.properties[index].name == word) return value_of(control_.read_property(index));
    }
    // A keyword or another name: one of the control's own properties when
    // it has one (accessible-name), else the word.
    static constexpr std::array<std::string_view, 9> keywords{"start", "end",   "center", "inline", "block",
                                                              "first", "second", "self",  "none"};
    for (const std::string_view keyword : keywords) {
        if (keyword == word) return word_value(word);
    }
    const std::string registered = control_language::composite_registered_name(word);
    const std::optional<BindingValue> own = control_.property_value(registered);
    if (own) return value_of(*own);
    return word_value(word);
}

std::string_view CompositeEvaluator::argument_name(const CompositeExpression& expression, const ExpressionNode& node,
                                                   const std::uint32_t index) const {
    if (index >= node.argument_count) return {};
    const ExpressionNode& argument =
        expression.parsed.nodes[expression.parsed.arguments[node.first_argument + index].value];
    if (argument.kind == ExpressionNodeKind::self_reference) return argument.member;
    if (argument.kind == ExpressionNodeKind::call && argument.argument_count == 1U &&
        (argument.text == "child" || argument.text == "part")) {
        return argument_name(expression, argument, 0U);
    }
    return argument.text;
}

CompositeValue CompositeEvaluator::argument(const CompositeExpression& expression, const ExpressionNode& node,
                                            const std::uint32_t index) const {
    if (index >= node.argument_count) return CompositeValue{};
    const CompositeValue value = evaluate(expression, expression.parsed.arguments[node.first_argument + index]);
    return value;
}

CompositeValue CompositeEvaluator::evaluate_call(const CompositeExpression& expression, const ExpressionNode& node) const {
    const std::string& name = node.text;
    if (name == "calc" || name == "is-true") {
        const CompositeValue value = argument(expression, node, 0U);
        if (name == "is-true") return boolean_value(truthy(value));
        return value;
    }
    if (name == "not") return boolean_value(!truthy(argument(expression, node, 0U)));
    if (name == "choose") {
        const bool chosen = truthy(argument(expression, node, 0U));
        return argument(expression, node, chosen ? 1U : 2U);
    }
    if (name == "min" || name == "max") {
        if (node.argument_count == 0U) return CompositeValue{};
        double result = number_of(argument(expression, node, 0U));
        for (std::uint32_t index = 1U; index < node.argument_count; ++index) {
            const double next = number_of(argument(expression, node, index));
            result = name == "min" ? std::min(result, next) : std::max(result, next);
        }
        return number_value(result);
    }
    if (name == "clamp") {
        // CSS order: clamp(minimum, value, maximum).
        const double low = number_of(argument(expression, node, 0U));
        const double value = number_of(argument(expression, node, 1U));
        const double high = number_of(argument(expression, node, 2U));
        return number_value(std::max(low, std::min(value, high)));
    }
    if (name == "equal" || name == "equals") {
        return boolean_value(values_equal(argument(expression, node, 0U), argument(expression, node, 1U)));
    }
    if (name == "less" || name == "greater" || name == "less-equal" || name == "greater-equal") {
        const double left = number_of(argument(expression, node, 0U));
        const double right = number_of(argument(expression, node, 1U));
        if (name == "less") return boolean_value(left < right);
        if (name == "greater") return boolean_value(left > right);
        if (name == "less-equal") return boolean_value(left <= right);
        return boolean_value(left >= right);
    }
    if (name == "property") return evaluate_word(argument_name(expression, node, 0U));
    if (name == "at") return number_value(number_of(evaluate_word(argument_name(expression, node, 0U))));
    if (name == "state") return boolean_value(subject_state("self", argument_name(expression, node, 0U)));
    if (name == "state-of") {
        return boolean_value(subject_state(argument_name(expression, node, 0U), argument_name(expression, node, 1U)));
    }
    if (name == "enabled" || name == "focused" || name == "focus-within" || name == "hovering" || name == "pressed") {
        const std::string_view subject = node.argument_count == 0U ? std::string_view("self")
                                                                   : argument_name(expression, node, 0U);
        return boolean_value(subject_state(subject, name));
    }
    if (name == "present") {
        const std::string_view target = argument_name(expression, node, 0U);
        for (std::size_t index = 0U; index < definition_.properties.size(); ++index) {
            if (definition_.properties[index].name != target) continue;
            const BindingValue value = control_.read_property(index);
            return boolean_value(!std::holds_alternative<std::monostate>(value));
        }
        const std::optional<std::size_t> part = part_named(target);
        if (!part) return boolean_value(false);
        const CompositeControl::Geometry* geometry = pass_ != nullptr ? pass_ : &control_.geometry_;
        return boolean_value(*part < (*geometry).count && (*geometry).present[*part]);
    }
    if (name == "command-enabled") {
        const std::string_view command = argument_name(expression, node, 0U);
        for (std::size_t index = 0U; index < definition_.commands.size(); ++index) {
            if (definition_.commands[index].name != command) continue;
            // Within the condition depth, so a command and a custom state
            // that read each other end (A-gf-controls-02-F1).
            if (control_.condition_depth_ >= maximum_composite_condition_depth) return boolean_value(false);
            const ConditionDepthScope depth(control_.condition_depth_);
            return boolean_value(control_.command_enabled_at(index));
        }
        return boolean_value(false);
    }
    if (name == "child" || name == "part") return word_value(argument_name(expression, node, 0U));
    if (name == "start-of" || name == "end-of") {
        const Rect box = subject_box(argument_name(expression, node, 0U));
        return number_value(name == "start-of" ? box.x : box.right());
    }
    // Geometry outside a box and the action calls read as no value; the
    // actions are run by the control (composite_control.cpp).
    return CompositeValue{};
}

std::optional<std::size_t> CompositeEvaluator::part_named(const std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < definition_.parts.size(); ++index) {
        if (definition_.parts[index].name == name) return index;
    }
    return std::nullopt;
}

Rect CompositeEvaluator::part_box(const std::string_view name) const {
    const std::optional<std::size_t> part = part_named(name);
    if (!part) return Rect{};
    if (pass_ == nullptr) control_.ensure_geometry();
    const CompositeControl::Geometry& geometry = pass_ != nullptr ? *pass_ : control_.geometry_;
    if (*part >= geometry.count) return Rect{};
    return geometry.logical[*part];
}

Rect CompositeEvaluator::subject_box(const std::string_view name) const {
    if (!name.starts_with("@")) return part_box(name);
    const Control::Ptr child = control_.child(name.substr(1U));
    if (!child) return Rect{};
    const Rect child_bounds = (*child).absolute_bounds();
    const Rect own = control_.absolute_bounds();
    const Rect relative{child_bounds.x - own.x, child_bounds.y - own.y, child_bounds.width, child_bounds.height};
    return relative;
}

bool CompositeEvaluator::subject_state(const std::string_view subject, const std::string_view state) const {
    const bool self = subject.empty() || subject == "self";
    const std::string_view local = subject.starts_with("@") ? subject.substr(1U) : subject;
    const Control::Ptr child = self ? Control::Ptr{} : control_.child(local);
    if (!self && !child) return false;
    const Control& target = self ? static_cast<const Control&>(control_) : *child;
    if (state == "enabled") return target.effectively_enabled();
    if (state == "focus-within") {
        const BorrowedPointer<SurfaceRoot> root = target.attached_surface_root();
        if (root == nullptr) return false;
        const Control::Ptr focused = (*root).focused_control();
        return focused != nullptr && (focused.get() == &target || target.contains(*focused));
    }
    // A custom state of the composite: its condition, within the condition
    // depth (A-gf-controls-02-F1).
    if (self) {
        for (const control_language::CompositeStateDefinition& declared : definition_.states) {
            if (!declared.custom_index || declared.name != state) continue;
            if (control_.condition_depth_ >= maximum_composite_condition_depth) return false;
            const ConditionDepthScope depth(control_.condition_depth_);
            return holds(declared.when);
        }
    }
    std::string_view standard = state;
    if (state == "hovering") standard = "hot";
    const stxlib::Optional<AnatomyState> known =
        anatomy_state_from_name(control_language::composite_registered_name(standard));
    if (known.absent()) return false;
    const AnatomyStateSet states = target.anatomy_states();
    const bool contained = states.contains(*known.value());
    return contained;
}

void CompositeEvaluator::evaluate_geometry(const Rect client, CompositeControl::Geometry& geometry) const {
    pass_ = &geometry;
    geometry.count = std::min(definition_.parts.size(), maximum_control_parts);
    for (std::size_t index = 0U; index < geometry.count; ++index) {
        const control_language::CompositePartDefinition& part = definition_.parts[index];
        geometry.disabled[index] = !part.disabled_when.empty() && holds(part.disabled_when);
        if (part.box.empty()) {
            // Region and focus, and a declared part without a box: the
            // client rectangle.
            geometry.logical[index] = client;
            geometry.present[index] = !client.empty();
            continue;
        }
        const bool present = part.present.empty() || holds(part.present);
        geometry.logical[index] = present ? evaluate_box(part.box, client) : Rect{};
        geometry.present[index] = present && !geometry.logical[index].empty();
    }
    pass_ = nullptr;
}

Rect CompositeEvaluator::evaluate_box(const CompositeExpression& box, const Rect client) const {
    Rect current = client;
    Rect reference = client;
    Rect first_half{};
    Rect second_half{};
    for (const ExpressionNodeIndex term : box.parsed.terms) {
        const ExpressionNode& node = box.parsed.nodes[term.value];
        if (node.kind == ExpressionNodeKind::word) {
            if (node.text == "square") {
                const double side = std::min(current.width, current.height);
                current.width = side;
                current.height = side;
            } else if (node.text == "first" || node.text == "second") {
                current = node.text == "first" ? first_half : second_half;
                reference = current;
            }
            continue;
        }
        if (node.kind != ExpressionNodeKind::call) continue;
        const std::string& name = node.text;
        if (name == "inset") {
            const Rect base = subject_box(argument_name(box, node, 0U));
            const std::uint32_t values = node.argument_count - 1U;
            const double a = number_of(argument(box, node, 1U));
            const double b = values >= 2U ? number_of(argument(box, node, 2U)) : a;
            Rect inset{};
            if (values >= 4U) {
                inset = inset_rect(base, a, b, number_of(argument(box, node, 3U)), number_of(argument(box, node, 4U)));
            } else {
                // One value: all sides; two: block, then inline.
                inset = inset_rect(base, a, b, a, b);
            }
            current = inset;
            reference = inset;
        } else if (name == "inset-inline" || name == "inset-block") {
            const Rect base = subject_box(argument_name(box, node, 0U));
            const double amount = number_of(argument(box, node, 1U));
            if (name == "inset-inline") {
                current.x = base.x + amount;
                current.width = std::max(base.width - amount * 2.0, 0.0);
                reference.x = current.x;
                reference.width = current.width;
            } else {
                current.y = base.y + amount;
                current.height = std::max(base.height - amount * 2.0, 0.0);
                reference.y = current.y;
                reference.height = current.height;
            }
        } else if (name == "width") {
            current.width = std::max(number_of(argument(box, node, 0U)), 0.0);
        } else if (name == "height") {
            current.height = std::max(number_of(argument(box, node, 0U)), 0.0);
        } else if (name == "align" || name == "align-inline" || name == "align-block") {
            const CompositeValue inline_position = argument(box, node, 0U);
            const CompositeValue block_position = node.argument_count >= 2U ? argument(box, node, 1U) : inline_position;
            if (name != "align-block") current.x = placed(inline_position, reference.x, reference.width, current.width);
            if (name == "align-block") current.y = placed(inline_position, reference.y, reference.height, current.height);
            if (name == "align") current.y = placed(block_position, reference.y, reference.height, current.height);
        } else if (name == "split") {
            const Rect base = subject_box(argument_name(box, node, 0U));
            const bool block = argument_name(box, node, 1U) == "block";
            const double share = std::clamp(number_of(argument(box, node, 2U)), 0.0, 1.0);
            first_half = base;
            second_half = base;
            if (block) {
                first_half.height = base.height * share;
                second_half.y = base.y + first_half.height;
                second_half.height = base.height - first_half.height;
            } else {
                first_half.width = base.width * share;
                second_half.x = base.x + first_half.width;
                second_half.width = base.width - first_half.width;
            }
            current = first_half;
            reference = first_half;
        } else if (name == "after-inline") {
            const Rect base = subject_box(argument_name(box, node, 0U));
            const double gap = number_of(argument(box, node, 1U));
            current.x = base.right() + gap;
            current.width = std::max(reference.right() - current.x, 0.0);
        } else if (name == "below") {
            const Rect base = subject_box(argument_name(box, node, 0U));
            const double gap = number_of(argument(box, node, 1U));
            current.y = base.bottom() + gap;
            current.height = std::max(reference.bottom() - current.y, 0.0);
        } else if (name == "union") {
            const Rect left = subject_box(argument_name(box, node, 0U));
            const Rect right = subject_box(argument_name(box, node, 1U));
            Rect joined = left.empty() ? right : left;
            if (!left.empty() && !right.empty()) joined = Rect::united(left, right);
            current = joined;
            reference = joined;
        } else if (name == "text-extent") {
            const std::string shown = text_of(evaluate_word(argument_name(box, node, 0U)));
            const TextExtent extent = control_.measure_text_extent_utf8(shown, control_.text_font(FontSpec{}));
            current.width = shown.empty() ? 0.0 : extent.logical_size.width;
            current.height = extent.logical_size.height;
        }
        // Expressions (calc, choose) as whole terms place nothing.
    }
    return current;
}

} // namespace gui_forms
