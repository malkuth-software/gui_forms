#pragma once

// A Windows Forms designer file read as data (stage MCP G7): the fields a
// `*.Designer.cs` declares and the statements of its InitializeComponent,
// each an assignment, an event subscription, a call or a local declaration
// over a small expression language. Nothing is evaluated: the converter
// (winforms_conversion.hpp) gives the statements their meaning.
//
// What the designer writes, and how it is read:
//
//   this.button1 = new System.Windows.Forms.Button();     assignment
//   this.button1.Location = new System.Drawing.Point(12, 12);
//   this.button1.Click += new System.EventHandler(this.button1_Click);
//                                                          subscription
//   this.Controls.Add(this.button1);                       call
//   ((System.ComponentModel.ISupportInitialize)(this.grid)).BeginInit();
//                                                          call on a cast
//   System.ComponentModel.ComponentResourceManager resources =
//       new System.ComponentModel.ComponentResourceManager(typeof(Form1));
//                                                          local declaration
//
// Paths drop `this.` and `global::`: `this.button1.Text` is the path
// `button1.Text`, and `this.Text` is `Text`, a property of the form itself.
//
// A statement the reader cannot read (a lambda, an object initializer, a
// `foreach`) is kept as `unread` with its source text, and reported as
// statement_skipped; the rest of the method is still read.
//
// Expressions nest at most maximum_expression_depth levels: the reader
// descends recursively, one frame of a few hundred bytes per level, and
// refuses deeper input (expression_too_deep) before descending further.

#include "conversion_diagnostics.hpp"
#include "csharp_lexer.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

inline constexpr std::uint32_t maximum_expression_depth = 64U;
// The most statements and expressions one InitializeComponent may hold.
inline constexpr std::size_t maximum_designer_statements = 100000U;
inline constexpr std::size_t maximum_designer_expressions = 1000000U;
// The most bytes of a skipped statement's source kept for its report.
inline constexpr std::size_t maximum_quoted_statement_bytes = 160U;

// No expression: an index past every expression.
inline constexpr std::uint32_t no_expression = std::numeric_limits<std::uint32_t>::max();

enum class DesignerExpressionKind : std::uint8_t {
    // Literals: `text` holds the decoded string, the number's digits, the
    // character, or nothing.
    string,
    number,
    character,
    boolean_true,
    boolean_false,
    null,
    // A dotted name: `button1.Text`, `System.Drawing.Color.Red`,
    // `resources`. `text` holds it with `this.` and `global::` dropped; a
    // bare `this` is the empty path.
    path,
    // `callee(arguments)`: `text` is the callee's path, or, for a call on
    // something that is not a path (a cast, a construction), the method's
    // name with `receiver` the expression it is called on.
    call,
    // `receiver.name` on something that is not a path: `text` is the name.
    member,
    // `new T(arguments)`: `text` is T.
    construction,
    // `new T[] { elements }` or `new[] { elements }`: `text` is T (empty
    // for `new[]`), the arguments are the elements.
    array,
    // `(T)operand`: `text` is T, one argument.
    cast,
    // `typeof(T)`: `text` is T.
    type_of,
    // Unary `-`, `!` and `~`, one argument.
    negation,
    logical_not,
    complement,
    // Binary `|`, `&` and `+`, two arguments.
    bitwise_or,
    bitwise_and,
    addition,
    // `receiver[index]`: two arguments, the receiver and the index.
    element,
};

struct DesignerExpression final {
    DesignerExpressionKind kind{DesignerExpressionKind::null};
    std::string text{};
    // The arguments' expression indices are arguments[first_argument] to
    // arguments[first_argument + argument_count - 1].
    std::uint32_t first_argument{};
    std::uint32_t argument_count{};
    // For a call or member on something that is not a path.
    std::uint32_t receiver{no_expression};
    std::uint32_t line{};
    std::uint32_t column{};
};

enum class DesignerStatementKind : std::uint8_t {
    // `target = value`.
    assignment,
    // `target += value` (an event subscription) and `target -= value`.
    subscription,
    unsubscription,
    // `value;` where value is a call; `target` is its callee path (empty
    // for a call on something that is not a path).
    call,
    // `Type target = value;`: `type` is Type.
    local_declaration,
    // Not read: `text` quotes its source.
    unread,
};

struct DesignerStatement final {
    DesignerStatementKind kind{DesignerStatementKind::unread};
    std::string target{};
    std::string type{};
    std::uint32_t value{no_expression};
    std::uint32_t line{};
    std::uint32_t column{};
    std::string text{};
};

// A field the designer file declares: `private System.Windows.Forms.Button
// button1;`.
struct DesignerField final {
    std::string name{};
    // As written: `System.Windows.Forms.Button`.
    std::string type{};
    std::uint32_t line{};
};

// One read designer file. Caller-owned; replaced whole by a read that
// succeeds, left as it was by one that refuses.
struct DesignerDocument final {
    std::string file{};
    std::string namespace_name{};
    std::string class_name{};
    std::vector<DesignerField> fields{};
    std::vector<DesignerStatement> statements{};
    std::vector<DesignerExpression> expressions{};
    std::vector<std::uint32_t> arguments{};

    // The expression an index names; the reference borrows the document.
    [[nodiscard]] const DesignerExpression& expression(std::uint32_t index) const& {
        const DesignerExpression& found = expressions.at(index);
        return found;
    }
    const DesignerExpression& expression(std::uint32_t index) const&& = delete;
    // The index of argument `position` of `expression`.
    [[nodiscard]] std::uint32_t argument(const DesignerExpression& owner, std::uint32_t position) const {
        const std::uint32_t index = arguments.at(static_cast<std::size_t>(owner.first_argument) + position);
        return index;
    }
    // The field declared with `name`, or null.
    [[nodiscard]] const DesignerField* find_field(std::string_view name) const& noexcept;
    const DesignerField* find_field(std::string_view name) const&& = delete;
};

enum class DesignerReadOutcome : std::uint8_t {
    read,
    // A lexical refusal, or no InitializeComponent with a body: the error
    // is reported and `document` is unchanged.
    refused,
};

// Reads the designer file `source` (lexed here) into `document`. Skipped
// statements are warnings; the read still succeeds.
[[nodiscard]] DesignerReadOutcome read_designer_file(CSharpSource& source, DesignerDocument& document,
                                                     ConversionDiagnosticList& diagnostics);

// The last segment of a dotted name: `System.Windows.Forms.Button` gives
// `Button`; a name without dots is itself.
[[nodiscard]] std::string_view last_segment(std::string_view dotted) noexcept;

} // namespace gui_forms::winforms_converter
