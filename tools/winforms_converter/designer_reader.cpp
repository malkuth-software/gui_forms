#include "designer_reader.hpp"

#include <utility>

namespace gui_forms::winforms_converter {

namespace {

// The words that open a declaration's modifiers, never part of a type.
constexpr std::string_view modifier_words[] = {"private", "public",  "protected", "internal", "static",
                                               "readonly", "new",    "partial",   "sealed",   "abstract",
                                               "virtual", "override", "unsafe",   "extern",   "volatile"};

[[nodiscard]] bool modifier(std::string_view word) noexcept {
    for (const std::string_view candidate : modifier_words) {
        if (candidate == word) return true;
    }
    return false;
}

// A parse failure of one statement: where, and why.
struct StatementFailure final {
    bool failed{};
    bool too_deep{};
    std::uint32_t line{};
    std::uint32_t column{};
    std::string detail{};
};

// Reads statements and expressions over one token range, appending to a
// document under construction. Each statement either completes or leaves
// the document as it was before it began (its expressions are dropped).
class StatementReader final {
public:
    StatementReader(const CSharpSource& source, DesignerDocument& document, std::size_t begin, std::size_t end)
        : source_(source), document_(document), at_(begin), end_(end) {}

    // Every statement up to the range's end; a statement that fails is
    // kept as unread, reported, and skipped to its `;`.
    void run(ConversionDiagnosticList& diagnostics) {
        while (at_ < end_) {
            if (punctuation(";")) {
                at_ = at_ + 1U;
                continue;
            }
            if (document_.statements.size() >= maximum_designer_statements) {
                report(diagnostics, ConversionCode::limit_reached, token().line, token().column,
                       "more statements than the converter reads; the rest of InitializeComponent is skipped");
                return;
            }
            const std::size_t statement_begin = at_;
            const std::size_t expression_mark = document_.expressions.size();
            const std::size_t argument_mark = document_.arguments.size();
            StatementFailure failure{};
            DesignerStatement statement = read_statement(failure);
            if (!failure.failed && !punctuation(";")) {
                failure = fail_here("expected `;`");
            }
            if (failure.failed) {
                document_.expressions.resize(expression_mark);
                document_.arguments.resize(argument_mark);
                skip_statement(statement_begin);
                DesignerStatement unread{};
                unread.kind = DesignerStatementKind::unread;
                unread.line = source_.tokens[statement_begin].line;
                unread.column = source_.tokens[statement_begin].column;
                unread.text = quote(statement_begin, at_);
                const ConversionCode code =
                    failure.too_deep ? ConversionCode::expression_too_deep : ConversionCode::statement_skipped;
                report(diagnostics, code, unread.line, unread.column, failure.detail + ": " + unread.text);
                document_.statements.push_back(std::move(unread));
                continue;
            }
            at_ = at_ + 1U;
            document_.statements.push_back(std::move(statement));
        }
    }

private:
    [[nodiscard]] const CSharpToken& token() const { return source_.tokens[at_]; }
    [[nodiscard]] bool more() const noexcept { return at_ < end_; }
    [[nodiscard]] bool punctuation(std::string_view text) const noexcept {
        const bool matches = at_ < end_ && is_punctuation(source_, source_.tokens[at_], text);
        return matches;
    }
    [[nodiscard]] bool punctuation_at(std::size_t index, std::string_view text) const noexcept {
        const bool matches = index < end_ && is_punctuation(source_, source_.tokens[index], text);
        return matches;
    }
    [[nodiscard]] bool identifier() const noexcept {
        const bool matches = at_ < end_ && source_.tokens[at_].kind == CSharpTokenKind::identifier;
        return matches;
    }
    [[nodiscard]] bool word(std::string_view text) const noexcept {
        const bool matches = at_ < end_ && is_identifier(source_, source_.tokens[at_], text);
        return matches;
    }

    [[nodiscard]] StatementFailure fail_here(std::string detail) const {
        StatementFailure failure{};
        failure.failed = true;
        const std::size_t index = at_ < end_ ? at_ : end_ - 1U;
        failure.line = source_.tokens[index].line;
        failure.column = source_.tokens[index].column;
        failure.detail = std::move(detail);
        return failure;
    }

    void report(ConversionDiagnosticList& diagnostics, ConversionCode code, std::uint32_t line, std::uint32_t column,
                std::string detail) const {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = code;
        diagnostic.file = source_.file;
        diagnostic.line = line;
        diagnostic.column = column;
        diagnostic.detail = std::move(detail);
        diagnostics.add(std::move(diagnostic));
    }

    // Moves past the failed statement's `;` at depth zero, or to the end.
    void skip_statement(std::size_t statement_begin) {
        at_ = statement_begin;
        std::size_t depth = 0U;
        while (at_ < end_) {
            const CSharpToken& current = source_.tokens[at_];
            if (is_punctuation(source_, current, "(") || is_punctuation(source_, current, "[") ||
                is_punctuation(source_, current, "{")) {
                depth = depth + 1U;
            } else if (is_punctuation(source_, current, ")") || is_punctuation(source_, current, "]") ||
                       is_punctuation(source_, current, "}")) {
                if (depth > 0U) depth = depth - 1U;
            } else if (depth == 0U && is_punctuation(source_, current, ";")) {
                at_ = at_ + 1U;
                return;
            }
            at_ = at_ + 1U;
        }
    }

    // The source of tokens [first, last), bounded.
    [[nodiscard]] std::string quote(std::size_t first, std::size_t last) const {
        if (first >= last || first >= source_.tokens.size()) return std::string();
        const CSharpToken& head = source_.tokens[first];
        const CSharpToken& tail = source_.tokens[last - 1U];
        const std::size_t length = static_cast<std::size_t>(tail.offset) + tail.length - head.offset;
        std::string text = source_.text.substr(head.offset, length);
        // One line: the statement's line breaks and indentation as spaces.
        std::string single{};
        bool space = false;
        for (const char byte : text) {
            const bool blank = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
            if (blank) {
                space = !single.empty();
                continue;
            }
            if (space) single.push_back(' ');
            space = false;
            single.push_back(byte);
            if (single.size() >= maximum_quoted_statement_bytes) {
                single.append("...");
                break;
            }
        }
        return single;
    }

    // One statement up to (not past) its `;`.
    [[nodiscard]] DesignerStatement read_statement(StatementFailure& failure) {
        DesignerStatement statement{};
        statement.line = token().line;
        statement.column = token().column;
        // A local declaration: a type path, a name, then `=`.
        std::size_t type_end = at_;
        if (declaration_ahead(type_end)) {
            statement.kind = DesignerStatementKind::local_declaration;
            statement.type = path_text(at_, type_end - 1U);
            at_ = type_end - 1U;
            statement.target = std::string(source_.spelling(token()));
            at_ = at_ + 1U;
            at_ = at_ + 1U;
            statement.value = read_expression(0U, failure);
            return statement;
        }
        const std::uint32_t left = read_expression(0U, failure);
        if (failure.failed) return statement;
        const DesignerExpression& head = document_.expressions[left];
        if (punctuation("=") || punctuation("+=") || punctuation("-=")) {
            if (head.kind != DesignerExpressionKind::path) {
                failure = fail_here("an assignment to something that is not a name");
                return statement;
            }
            statement.kind = punctuation("=")    ? DesignerStatementKind::assignment
                             : punctuation("+=") ? DesignerStatementKind::subscription
                                                 : DesignerStatementKind::unsubscription;
            statement.target = head.text;
            at_ = at_ + 1U;
            statement.value = read_expression(0U, failure);
            return statement;
        }
        if (head.kind != DesignerExpressionKind::call) {
            failure = fail_here("an expression statement that is not a call");
            return statement;
        }
        statement.kind = DesignerStatementKind::call;
        statement.target = head.receiver == no_expression ? head.text : std::string();
        statement.value = left;
        return statement;
    }

    // Whether a local declaration begins here: identifiers joined by `.`
    // (a type), one identifier (the name), then `=`; `type_end` is then the
    // index past the name.
    [[nodiscard]] bool declaration_ahead(std::size_t& type_end) const {
        std::size_t index = at_;
        std::size_t names = 0U;
        while (index < end_ && source_.tokens[index].kind == CSharpTokenKind::identifier) {
            names = names + 1U;
            index = index + 1U;
            if (punctuation_at(index, ".") || punctuation_at(index, "::")) {
                index = index + 1U;
                continue;
            }
            break;
        }
        // `Type name =`: after the type's last identifier comes the name.
        if (names == 0U || index >= end_ || source_.tokens[index].kind != CSharpTokenKind::identifier) return false;
        if (!punctuation_at(index + 1U, "=")) return false;
        type_end = index + 1U;
        return true;
    }

    // The dotted text of identifier tokens [first, last), `global::` and
    // `this.` dropped.
    [[nodiscard]] std::string path_text(std::size_t first, std::size_t last) const {
        std::string text{};
        for (std::size_t index = first; index < last; ++index) {
            const CSharpToken& current = source_.tokens[index];
            if (current.kind != CSharpTokenKind::identifier) continue;
            const std::string_view spelled = source_.spelling(current);
            if (spelled == "global" && punctuation_at(index + 1U, "::")) continue;
            if (spelled == "this" && text.empty()) continue;
            if (!text.empty()) text.push_back('.');
            text.append(spelled);
        }
        return text;
    }

    [[nodiscard]] std::uint32_t add(DesignerExpression expression, StatementFailure& failure) {
        if (document_.expressions.size() >= maximum_designer_expressions) {
            failure = fail_here("more expressions than the converter reads");
            return no_expression;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(document_.expressions.size());
        document_.expressions.push_back(std::move(expression));
        return index;
    }

    [[nodiscard]] DesignerExpression located(DesignerExpressionKind kind) const {
        DesignerExpression expression{};
        expression.kind = kind;
        if (at_ < end_) {
            expression.line = token().line;
            expression.column = token().column;
        }
        return expression;
    }

    // Appends the argument indices as one contiguous run and records it.
    void attach(DesignerExpression& owner, const std::vector<std::uint32_t>& arguments) {
        owner.first_argument = static_cast<std::uint32_t>(document_.arguments.size());
        owner.argument_count = static_cast<std::uint32_t>(arguments.size());
        document_.arguments.insert(document_.arguments.end(), arguments.begin(), arguments.end());
    }

    // expression := bitwise_or. `depth` counts the levels above.
    [[nodiscard]] std::uint32_t read_expression(std::uint32_t depth, StatementFailure& failure) {
        if (depth >= maximum_expression_depth) {
            failure = fail_here("an expression nested deeper than the converter reads");
            failure.too_deep = true;
            return no_expression;
        }
        const std::uint32_t value = read_binary(depth + 1U, 0U, failure);
        return value;
    }

    // The binary operators by level: 0 `|`, 1 `&`, 2 `+`.
    [[nodiscard]] std::uint32_t read_binary(std::uint32_t depth, std::uint32_t level, StatementFailure& failure) {
        if (level > 2U) {
            const std::uint32_t operand = read_unary(depth, failure);
            return operand;
        }
        std::uint32_t left = read_binary(depth, level + 1U, failure);
        if (failure.failed) return no_expression;
        const std::string_view symbol = level == 0U ? "|" : (level == 1U ? "&" : "+");
        const DesignerExpressionKind kind = level == 0U   ? DesignerExpressionKind::bitwise_or
                                            : level == 1U ? DesignerExpressionKind::bitwise_and
                                                          : DesignerExpressionKind::addition;
        while (punctuation(symbol)) {
            DesignerExpression combined = located(kind);
            at_ = at_ + 1U;
            const std::uint32_t right = read_binary(depth, level + 1U, failure);
            if (failure.failed) return no_expression;
            const std::vector<std::uint32_t> pair{left, right};
            attach(combined, pair);
            left = add(std::move(combined), failure);
            if (failure.failed) return no_expression;
        }
        return left;
    }

    [[nodiscard]] std::uint32_t read_unary(std::uint32_t depth, StatementFailure& failure) {
        if (depth >= maximum_expression_depth) {
            failure = fail_here("an expression nested deeper than the converter reads");
            failure.too_deep = true;
            return no_expression;
        }
        if (punctuation("-") || punctuation("!") || punctuation("~")) {
            const DesignerExpressionKind kind = punctuation("-")   ? DesignerExpressionKind::negation
                                                : punctuation("!") ? DesignerExpressionKind::logical_not
                                                                   : DesignerExpressionKind::complement;
            DesignerExpression unary = located(kind);
            at_ = at_ + 1U;
            const std::uint32_t operand = read_unary(depth + 1U, failure);
            if (failure.failed) return no_expression;
            const std::vector<std::uint32_t> one{operand};
            attach(unary, one);
            const std::uint32_t index = add(std::move(unary), failure);
            return index;
        }
        std::size_t cast_end = at_;
        if (cast_ahead(cast_end)) {
            DesignerExpression cast = located(DesignerExpressionKind::cast);
            cast.text = path_text(at_ + 1U, cast_end - 1U);
            at_ = cast_end;
            const std::uint32_t operand = read_unary(depth + 1U, failure);
            if (failure.failed) return no_expression;
            const std::vector<std::uint32_t> one{operand};
            attach(cast, one);
            const std::uint32_t index = add(std::move(cast), failure);
            return index;
        }
        const std::uint32_t value = read_postfix(depth + 1U, failure);
        return value;
    }

    // Whether `(` Type `)` begins here and is followed by what a cast's
    // operand begins with; `cast_end` is then the index past the `)`.
    [[nodiscard]] bool cast_ahead(std::size_t& cast_end) const {
        if (!punctuation("(")) return false;
        std::size_t index = at_ + 1U;
        std::size_t names = 0U;
        while (index < end_ && source_.tokens[index].kind == CSharpTokenKind::identifier) {
            names = names + 1U;
            index = index + 1U;
            if (punctuation_at(index, ".") || punctuation_at(index, "::")) {
                index = index + 1U;
                continue;
            }
            break;
        }
        if (names == 0U || !punctuation_at(index, ")")) return false;
        const std::size_t next = index + 1U;
        if (next >= end_) return false;
        const CSharpToken& following = source_.tokens[next];
        // `(a) | b` and `(a).b` are a parenthesized name, not a cast.
        const bool operand = following.kind != CSharpTokenKind::punctuation || punctuation_at(next, "(");
        if (!operand) return false;
        // A one-word parenthesized name followed by a literal is still a
        // cast in C# only for a type; a designer writes casts with
        // qualified types or keywords, so accept both.
        cast_end = next;
        return true;
    }

    // primary, then `.name`, `(arguments)` and `[index]`.
    [[nodiscard]] std::uint32_t read_postfix(std::uint32_t depth, StatementFailure& failure) {
        std::uint32_t value = read_primary(depth, failure);
        if (failure.failed) return no_expression;
        while (more()) {
            if (punctuation(".") || punctuation("?.")) {
                at_ = at_ + 1U;
                if (!identifier()) {
                    failure = fail_here("expected a name after `.`");
                    return no_expression;
                }
                const std::string name(source_.spelling(token()));
                DesignerExpression& current = document_.expressions[value];
                if (current.kind == DesignerExpressionKind::path) {
                    if (!current.text.empty()) current.text.push_back('.');
                    current.text.append(name);
                    at_ = at_ + 1U;
                    continue;
                }
                DesignerExpression member = located(DesignerExpressionKind::member);
                member.text = name;
                member.receiver = value;
                at_ = at_ + 1U;
                value = add(std::move(member), failure);
                if (failure.failed) return no_expression;
                continue;
            }
            if (punctuation("(")) {
                std::vector<std::uint32_t> arguments{};
                const bool read = read_arguments(depth, ")", arguments, failure);
                if (!read) return no_expression;
                const DesignerExpression callee = document_.expressions[value];
                DesignerExpression call{};
                call.kind = DesignerExpressionKind::call;
                call.line = callee.line;
                call.column = callee.column;
                if (callee.kind == DesignerExpressionKind::path) {
                    call.text = callee.text;
                } else if (callee.kind == DesignerExpressionKind::member) {
                    call.text = callee.text;
                    call.receiver = callee.receiver;
                } else {
                    failure = fail_here("a call of something that is not a method");
                    return no_expression;
                }
                attach(call, arguments);
                value = add(std::move(call), failure);
                if (failure.failed) return no_expression;
                continue;
            }
            if (punctuation("[")) {
                std::vector<std::uint32_t> arguments{value};
                const bool read = read_arguments(depth, "]", arguments, failure);
                if (!read) return no_expression;
                DesignerExpression element = located(DesignerExpressionKind::element);
                attach(element, arguments);
                value = add(std::move(element), failure);
                if (failure.failed) return no_expression;
                continue;
            }
            break;
        }
        return value;
    }

    // `(` or `[` at the cursor through `closer`, arguments separated by
    // commas, appended to `arguments`. A named argument (`name: value`) and
    // `ref`/`out` are read by their value.
    [[nodiscard]] bool read_arguments(std::uint32_t depth, std::string_view closer, std::vector<std::uint32_t>& arguments,
                                      StatementFailure& failure) {
        at_ = at_ + 1U;
        if (punctuation(closer)) {
            at_ = at_ + 1U;
            return true;
        }
        while (true) {
            if (identifier() && punctuation_at(at_ + 1U, ":") && !punctuation_at(at_ + 1U, "::")) at_ = at_ + 2U;
            if (word("ref") || word("out") || word("in")) at_ = at_ + 1U;
            const std::uint32_t argument = read_expression(depth + 1U, failure);
            if (failure.failed) return false;
            arguments.push_back(argument);
            if (punctuation(",")) {
                at_ = at_ + 1U;
                continue;
            }
            if (punctuation(closer)) {
                at_ = at_ + 1U;
                return true;
            }
            failure = fail_here("expected `,` or `" + std::string(closer) + "`");
            return false;
        }
    }

    [[nodiscard]] std::uint32_t read_primary(std::uint32_t depth, StatementFailure& failure) {
        if (!more()) {
            failure = fail_here("the statement ends early");
            return no_expression;
        }
        const CSharpToken& current = token();
        if (current.kind == CSharpTokenKind::string || current.kind == CSharpTokenKind::number ||
            current.kind == CSharpTokenKind::character) {
            const DesignerExpressionKind kind = current.kind == CSharpTokenKind::string   ? DesignerExpressionKind::string
                                                : current.kind == CSharpTokenKind::number ? DesignerExpressionKind::number
                                                                                          : DesignerExpressionKind::character;
            DesignerExpression literal = located(kind);
            literal.text = current.value;
            at_ = at_ + 1U;
            const std::uint32_t index = add(std::move(literal), failure);
            return index;
        }
        if (punctuation("(")) {
            at_ = at_ + 1U;
            const std::uint32_t inner = read_expression(depth + 1U, failure);
            if (failure.failed) return no_expression;
            if (!punctuation(")")) {
                failure = fail_here("expected `)`");
                return no_expression;
            }
            at_ = at_ + 1U;
            return inner;
        }
        if (!identifier()) {
            failure = fail_here("an expression the converter does not read");
            return no_expression;
        }
        const std::string_view spelled = source_.spelling(current);
        if (spelled == "true" || spelled == "false" || spelled == "null") {
            const DesignerExpressionKind kind = spelled == "true"    ? DesignerExpressionKind::boolean_true
                                                : spelled == "false" ? DesignerExpressionKind::boolean_false
                                                                     : DesignerExpressionKind::null;
            DesignerExpression literal = located(kind);
            at_ = at_ + 1U;
            const std::uint32_t index = add(std::move(literal), failure);
            return index;
        }
        if (spelled == "new") {
            const std::uint32_t made = read_new(depth, failure);
            return made;
        }
        if (spelled == "typeof") {
            DesignerExpression type_of = located(DesignerExpressionKind::type_of);
            at_ = at_ + 1U;
            if (!punctuation("(")) {
                failure = fail_here("expected `(` after typeof");
                return no_expression;
            }
            const std::size_t first = at_ + 1U;
            std::size_t last = first;
            while (last < end_ && !punctuation_at(last, ")")) last = last + 1U;
            if (last >= end_) {
                failure = fail_here("expected `)`");
                return no_expression;
            }
            type_of.text = path_text(first, last);
            at_ = last + 1U;
            const std::uint32_t index = add(std::move(type_of), failure);
            return index;
        }
        if (punctuation_at(at_ + 1U, "=>")) {
            failure = fail_here("a lambda, which the converter does not read");
            return no_expression;
        }
        // A name: `this`, `global::`, identifiers; postfix `.` extends it.
        DesignerExpression path = located(DesignerExpressionKind::path);
        if (spelled == "global" && punctuation_at(at_ + 1U, "::")) {
            at_ = at_ + 2U;
            if (!identifier()) {
                failure = fail_here("expected a name after `global::`");
                return no_expression;
            }
        }
        const std::string_view name = source_.spelling(token());
        if (name != "this") path.text = std::string(name);
        at_ = at_ + 1U;
        const std::uint32_t index = add(std::move(path), failure);
        return index;
    }

    // `new T(arguments)`, `new T[] { elements }`, `new T[n]`, `new[] {}`.
    [[nodiscard]] std::uint32_t read_new(std::uint32_t depth, StatementFailure& failure) {
        DesignerExpression made = located(DesignerExpressionKind::construction);
        at_ = at_ + 1U;
        const std::size_t type_first = at_;
        while (identifier() || punctuation(".") || punctuation("::")) at_ = at_ + 1U;
        made.text = path_text(type_first, at_);
        if (punctuation("[")) {
            made.kind = DesignerExpressionKind::array;
            at_ = at_ + 1U;
            std::vector<std::uint32_t> elements{};
            if (!punctuation("]")) {
                // `new T[n]`: a sized array without elements.
                const std::uint32_t size = read_expression(depth + 1U, failure);
                if (failure.failed) return no_expression;
                static_cast<void>(size);
                if (!punctuation("]")) {
                    failure = fail_here("expected `]`");
                    return no_expression;
                }
            }
            at_ = at_ + 1U;
            if (punctuation("{")) {
                const bool read = read_arguments(depth, "}", elements, failure);
                if (!read) return no_expression;
            }
            attach(made, elements);
            const std::uint32_t index = add(std::move(made), failure);
            return index;
        }
        if (made.text.empty()) {
            failure = fail_here("expected a type after `new`");
            return no_expression;
        }
        std::vector<std::uint32_t> arguments{};
        if (punctuation("(")) {
            const bool read = read_arguments(depth, ")", arguments, failure);
            if (!read) return no_expression;
        }
        if (punctuation("{")) {
            failure = fail_here("an object initializer, which the converter does not read");
            return no_expression;
        }
        attach(made, arguments);
        const std::uint32_t index = add(std::move(made), failure);
        return index;
    }

    const CSharpSource& source_;
    DesignerDocument& document_;
    std::size_t at_{};
    std::size_t end_{};
};

// The token index of the `}` that closes the `{` at `open`, or the token
// count when none does.
[[nodiscard]] std::size_t closing_brace(const CSharpSource& source, std::size_t open) {
    std::size_t depth = 0U;
    for (std::size_t index = open; index < source.tokens.size(); ++index) {
        const CSharpToken& current = source.tokens[index];
        if (is_punctuation(source, current, "{")) {
            depth = depth + 1U;
        } else if (is_punctuation(source, current, "}")) {
            depth = depth - 1U;
            if (depth == 0U) return index;
        }
    }
    return source.tokens.size();
}

// Where the class body and InitializeComponent are, found in one pass.
struct DesignerLayout final {
    std::string namespace_name{};
    std::string class_name{};
    std::size_t class_open{};
    std::size_t class_close{};
    std::size_t method_open{};
    std::size_t method_close{};
    bool class_found{};
    bool method_found{};
};

void find_layout(const CSharpSource& source, DesignerLayout& layout) {
    const std::vector<CSharpToken>& tokens = source.tokens;
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        if (layout.namespace_name.empty() && is_identifier(source, tokens[index], "namespace")) {
            std::size_t name = index + 1U;
            while (name < tokens.size() && (tokens[name].kind == CSharpTokenKind::identifier ||
                                            is_punctuation(source, tokens[name], "."))) {
                layout.namespace_name.append(source.spelling(tokens[name]));
                name = name + 1U;
            }
            continue;
        }
        if (!layout.class_found && is_identifier(source, tokens[index], "class") && index + 1U < tokens.size() &&
            tokens[index + 1U].kind == CSharpTokenKind::identifier) {
            std::size_t open = index + 2U;
            while (open < tokens.size() && !is_punctuation(source, tokens[open], "{")) open = open + 1U;
            if (open >= tokens.size()) return;
            layout.class_name = std::string(source.spelling(tokens[index + 1U]));
            layout.class_open = open;
            layout.class_close = closing_brace(source, open);
            layout.class_found = true;
            index = open;
            continue;
        }
        const bool inside = layout.class_found && index > layout.class_open && index < layout.class_close;
        if (inside && !layout.method_found && is_identifier(source, tokens[index], "InitializeComponent") &&
            index + 3U < tokens.size() && is_punctuation(source, tokens[index + 1U], "(") &&
            is_punctuation(source, tokens[index + 2U], ")") && is_punctuation(source, tokens[index + 3U], "{")) {
            layout.method_open = index + 3U;
            layout.method_close = closing_brace(source, layout.method_open);
            layout.method_found = layout.method_close < tokens.size();
            index = layout.method_close;
        }
    }
}

// The class body's field declarations at its own depth, outside methods.
void read_fields(const CSharpSource& source, const DesignerLayout& layout, std::vector<DesignerField>& fields) {
    const std::vector<CSharpToken>& tokens = source.tokens;
    std::size_t statement = layout.class_open + 1U;
    std::size_t index = statement;
    while (index < layout.class_close) {
        const CSharpToken& current = tokens[index];
        if (is_punctuation(source, current, "{")) {
            // A method, property or nested type body: skip it, and what
            // led up to it is no field.
            index = closing_brace(source, index) + 1U;
            statement = index;
            continue;
        }
        if (is_punctuation(source, current, "[")) {
            // An attribute: `[System.ComponentModel.Browsable(false)]`.
            while (index < layout.class_close && !is_punctuation(source, tokens[index], "]")) index = index + 1U;
            index = index + 1U;
            statement = index;
            continue;
        }
        if (!is_punctuation(source, current, ";")) {
            index = index + 1U;
            continue;
        }
        // [modifiers] Type name [= initializer] ;
        std::size_t first = statement;
        while (first < index && tokens[first].kind == CSharpTokenKind::identifier &&
               modifier(source.spelling(tokens[first]))) {
            first = first + 1U;
        }
        std::size_t last = first;
        bool call = false;
        while (last < index && !is_punctuation(source, tokens[last], "=")) {
            if (is_punctuation(source, tokens[last], "(")) call = true;
            last = last + 1U;
        }
        if (!call && last >= first + 2U && tokens[last - 1U].kind == CSharpTokenKind::identifier) {
            DesignerField field{};
            field.name = std::string(source.spelling(tokens[last - 1U]));
            field.line = tokens[last - 1U].line;
            for (std::size_t part = first; part + 1U < last; ++part) {
                const std::string_view spelled = source.spelling(tokens[part]);
                if (spelled == "global" || spelled == "::") continue;
                field.type.append(spelled);
            }
            fields.push_back(std::move(field));
        }
        index = index + 1U;
        statement = index;
    }
}

} // namespace

const DesignerField* DesignerDocument::find_field(std::string_view name) const& noexcept {
    for (const DesignerField& field : fields) {
        if (field.name == name) return &field;
    }
    return nullptr;
}

DesignerReadOutcome read_designer_file(CSharpSource& source, DesignerDocument& document,
                                       ConversionDiagnosticList& diagnostics) {
    const CSharpLexOutcome lexed = lex_csharp(source, diagnostics);
    if (lexed != CSharpLexOutcome::lexed) return DesignerReadOutcome::refused;
    DesignerLayout layout{};
    find_layout(source, layout);
    if (!layout.class_found || !layout.method_found) {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = ConversionCode::initialize_component_missing;
        diagnostic.file = source.file;
        diagnostic.detail = layout.class_found ? "the class declares no `void InitializeComponent()` with a body"
                                               : "the file declares no class";
        diagnostics.add(std::move(diagnostic));
        return DesignerReadOutcome::refused;
    }
    DesignerDocument read{};
    read.file = source.file;
    read.namespace_name = layout.namespace_name;
    read.class_name = layout.class_name;
    read_fields(source, layout, read.fields);
    StatementReader reader(source, read, layout.method_open + 1U, layout.method_close);
    reader.run(diagnostics);
    document = std::move(read);
    return DesignerReadOutcome::read;
}

std::string_view last_segment(std::string_view dotted) noexcept {
    const std::size_t dot = dotted.rfind('.');
    if (dot == std::string_view::npos) return dotted;
    const std::string_view segment = dotted.substr(dot + 1U);
    return segment;
}

} // namespace gui_forms::winforms_converter
