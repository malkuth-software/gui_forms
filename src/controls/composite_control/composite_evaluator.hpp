#pragma once

// The composite control's expression evaluator (cp34, phase 2; design note
// sections 3.2 to 3.11): conditions, values and part geometry over one
// instance. Internal to src/controls/composite_control.
//
// Values are small: a boolean, a number, a text or a bare word (a position
// such as `start`, a part or child name). A property reads as its kind: a
// boolean as a boolean, the numeric kinds as a number, text as text, an
// enumeration as the word of its choice; other kinds read as no value.
// Truth: a boolean is itself, a number is true when not zero, a text or a
// word when not empty; no value is false.
//
// Names: a bare word is first a keyword (`true`, `start`, `frame-value`),
// then a declared property, then one of the control's own properties by
// registered name (`accessible-name`), else the word itself.
//
// Geometry (section 3.2) is evaluated term by term into a box, from the
// client rectangle, in logical coordinates (inline start on the left);
// CompositeControl mirrors the boxes under right-to-left afterwards.
// Geometry never reads the pointer, time or scale (the checker refused
// those), so it is evaluated on layout and on changes only.

#include "../../core/control_language/composite_definition/composite_definition.hpp"

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms {

enum class CompositeValueKind : std::uint8_t {
    none,
    boolean,
    number,
    text,
    word,
};

struct CompositeValue final {
    CompositeValueKind kind{CompositeValueKind::none};
    bool boolean{};
    double number{};
    std::string text{};
};

// The inputs of one firing that expressions may read: a drag frame's
// value and its fraction of the range, and a wheel's notches.
struct CompositeFiring final {
    double frame_value{};
    double frame_fraction{};
    double wheel_notches{};
};

class CompositeEvaluator final {
public:
    // Both borrowed for the evaluator's life, which is one call's.
    CompositeEvaluator(const CompositeControl& control, CompositeFiring firing) noexcept;

    // A node's value.
    [[nodiscard]] CompositeValue evaluate(const control_language::CompositeExpression& expression,
                                          control_language::ExpressionNodeIndex node) const;
    // The first term's value (none for an empty expression).
    [[nodiscard]] CompositeValue first(const control_language::CompositeExpression& expression) const;
    // Whether every term holds; true for an empty expression.
    [[nodiscard]] bool holds(const control_language::CompositeExpression& expression) const;
    [[nodiscard]] static bool truthy(const CompositeValue& value) noexcept;
    // The first term as a number (0 for none) and as text.
    [[nodiscard]] double number(const control_language::CompositeExpression& expression) const;
    [[nodiscard]] std::string text(const control_language::CompositeExpression& expression) const;

    // Fills every part's logical box, presence and disabled flag, in
    // declaration order, from the client rectangle `client`.
    void evaluate_geometry(Rect client, CompositeControl::Geometry& geometry) const;

    // A property's value as an expression reads it.
    [[nodiscard]] static CompositeValue value_of(const BindingValue& value);

private:
    [[nodiscard]] CompositeValue evaluate_call(const control_language::CompositeExpression& expression,
                                               const control_language::ExpressionNode& node) const;
    [[nodiscard]] CompositeValue evaluate_word(std::string_view word) const;
    // The raw text of a call's argument `index` (a word, `@name` or
    // `self.name`), unevaluated: the name a function takes.
    [[nodiscard]] std::string_view argument_name(const control_language::CompositeExpression& expression,
                                                 const control_language::ExpressionNode& node,
                                                 std::uint32_t index) const;
    [[nodiscard]] CompositeValue argument(const control_language::CompositeExpression& expression,
                                          const control_language::ExpressionNode& node, std::uint32_t index) const;
    [[nodiscard]] std::optional<std::size_t> part_named(std::string_view name) const noexcept;
    // A part's logical box: one computed earlier in this geometry pass, or
    // the cached geometry outside a pass.
    [[nodiscard]] Rect part_box(std::string_view name) const;
    [[nodiscard]] Rect subject_box(std::string_view name) const;
    // Whether the subject (`self` or a child's local ID) is in a state.
    [[nodiscard]] bool subject_state(std::string_view subject, std::string_view state) const;
    // Evaluates one part's box.
    [[nodiscard]] Rect evaluate_box(const control_language::CompositeExpression& box, Rect client) const;

    const CompositeControl& control_;
    const CompositeDefinition& definition_;
    CompositeFiring firing_;
    // The geometry pass in progress, if any.
    mutable const CompositeControl::Geometry* pass_{};
};

} // namespace gui_forms
