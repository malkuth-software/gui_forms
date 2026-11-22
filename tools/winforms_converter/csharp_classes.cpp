#include "csharp_classes.hpp"

#include "designer_reader.hpp"

#include <utility>

namespace gui_forms::winforms_converter {

namespace {

// An open scope while reading declarations: a namespace or class body, or
// another brace (a method body, an initializer) whose contents are skipped.
enum class ScopeKind : std::uint8_t { namespace_body, class_body, other };

struct Scope final {
    ScopeKind kind{ScopeKind::other};
    // For a class body: its record in the index.
    std::size_t class_record{};
    // For a namespace: its name's length before it, to restore on close.
    std::size_t namespace_length{};
};

// The most braces open at once that the reader follows; deeper source is
// refused as a lexical error would be (nothing is recursive here).
constexpr std::size_t maximum_scope_depth = 256U;

class ClassReader final {
public:
    ClassReader(const CSharpSource& source, CSharpClassIndex& index) : source_(source), index_(index) {}

    void run(ConversionDiagnosticList& diagnostics) {
        const std::vector<CSharpToken>& tokens = source_.tokens;
        std::size_t at = 0U;
        std::size_t declaration = 0U;
        while (at < tokens.size()) {
            const CSharpToken& current = tokens[at];
            if (is_identifier(source_, current, "namespace")) {
                at = namespace_declaration(at + 1U);
                declaration = at;
                continue;
            }
            if ((is_identifier(source_, current, "class") || is_identifier(source_, current, "struct") ||
                 is_identifier(source_, current, "interface")) &&
                at + 1U < tokens.size() && tokens[at + 1U].kind == CSharpTokenKind::identifier && !inside_other()) {
                at = class_declaration(at, declaration);
                declaration = at;
                continue;
            }
            if (is_punctuation(source_, current, "{")) {
                if (scopes_.size() >= maximum_scope_depth) {
                    ConversionDiagnostic diagnostic{};
                    diagnostic.code = ConversionCode::limit_reached;
                    diagnostic.file = source_.file;
                    diagnostic.line = current.line;
                    diagnostic.column = current.column;
                    diagnostic.detail = "braces nested deeper than the converter follows; the rest is not read";
                    diagnostics.add(std::move(diagnostic));
                    return;
                }
                if (in_class_body()) method_declaration(declaration, at);
                Scope scope{};
                scope.kind = ScopeKind::other;
                scopes_.push_back(scope);
                at = at + 1U;
                declaration = at;
                continue;
            }
            if (is_punctuation(source_, current, "}")) {
                if (!scopes_.empty()) {
                    if (scopes_.back().kind == ScopeKind::namespace_body) {
                        namespace_.resize(scopes_.back().namespace_length);
                    }
                    scopes_.pop_back();
                }
                at = at + 1U;
                declaration = at;
                continue;
            }
            if (is_punctuation(source_, current, ";")) {
                // An expression-bodied method ends here.
                if (in_class_body()) method_declaration(declaration, at);
                at = at + 1U;
                declaration = at;
                continue;
            }
            at = at + 1U;
        }
    }

private:
    [[nodiscard]] bool in_class_body() const noexcept {
        const bool inside = !scopes_.empty() && scopes_.back().kind == ScopeKind::class_body;
        return inside;
    }
    [[nodiscard]] bool inside_other() const noexcept {
        for (const Scope& scope : scopes_) {
            if (scope.kind == ScopeKind::other) return true;
        }
        return false;
    }

    // `namespace A.B {` or the file-scoped `namespace A.B;`.
    [[nodiscard]] std::size_t namespace_declaration(std::size_t at) {
        const std::vector<CSharpToken>& tokens = source_.tokens;
        std::string name{};
        while (at < tokens.size() &&
               (tokens[at].kind == CSharpTokenKind::identifier || is_punctuation(source_, tokens[at], "."))) {
            name.append(source_.spelling(tokens[at]));
            at = at + 1U;
        }
        Scope scope{};
        scope.kind = ScopeKind::namespace_body;
        scope.namespace_length = namespace_.size();
        if (!namespace_.empty()) namespace_.push_back('.');
        namespace_.append(name);
        if (at < tokens.size() && is_punctuation(source_, tokens[at], "{")) {
            scopes_.push_back(scope);
        }
        // A file-scoped namespace stays for the rest of the file.
        return at + 1U;
    }

    // `[modifiers] class Name [<T>] [: Base, IFace] [where ...] {`.
    [[nodiscard]] std::size_t class_declaration(std::size_t at, std::size_t declaration) {
        const std::vector<CSharpToken>& tokens = source_.tokens;
        CSharpClass record{};
        record.name = std::string(source_.spelling(tokens[at + 1U]));
        record.namespace_name = namespace_;
        record.file = source_.file;
        record.line = tokens[at].line;
        for (std::size_t modifier = declaration; modifier < at; ++modifier) {
            if (is_identifier(source_, tokens[modifier], "partial")) record.partial = true;
        }
        std::size_t cursor = at + 2U;
        std::size_t angle = 0U;
        while (cursor < tokens.size() && !is_punctuation(source_, tokens[cursor], "{") &&
               !is_punctuation(source_, tokens[cursor], ";")) {
            if (is_punctuation(source_, tokens[cursor], "<")) angle = angle + 1U;
            if (is_punctuation(source_, tokens[cursor], ">") && angle > 0U) angle = angle - 1U;
            if (angle == 0U && is_punctuation(source_, tokens[cursor], ":") && record.base.empty()) {
                std::size_t part = cursor + 1U;
                while (part < tokens.size() && (tokens[part].kind == CSharpTokenKind::identifier ||
                                                is_punctuation(source_, tokens[part], "."))) {
                    if (is_identifier(source_, tokens[part], "where")) break;
                    record.base.append(source_.spelling(tokens[part]));
                    part = part + 1U;
                }
            }
            cursor = cursor + 1U;
        }
        if (index_.classes.size() < maximum_csharp_classes) {
            index_.classes.push_back(std::move(record));
        }
        if (cursor < tokens.size() && is_punctuation(source_, tokens[cursor], "{")) {
            Scope scope{};
            scope.kind = ScopeKind::class_body;
            scope.class_record = index_.classes.size() - 1U;
            scopes_.push_back(scope);
        }
        return cursor + 1U;
    }

    // Tokens [declaration, end) in a class body, before a `{` or `;`: a
    // method when they end `name ( parameters )`, perhaps followed by `=>`
    // and its expression.
    void method_declaration(std::size_t declaration, std::size_t end) {
        const std::vector<CSharpToken>& tokens = source_.tokens;
        std::size_t close = end;
        // `=> expression ;`: the method's head is before the arrow.
        for (std::size_t arrow = declaration; arrow < end; ++arrow) {
            if (is_punctuation(source_, tokens[arrow], "=>")) {
                close = arrow;
                break;
            }
        }
        if (close <= declaration + 2U || !is_punctuation(source_, tokens[close - 1U], ")")) return;
        std::size_t depth = 0U;
        std::size_t open = close - 1U;
        while (true) {
            if (is_punctuation(source_, tokens[open], ")")) depth = depth + 1U;
            if (is_punctuation(source_, tokens[open], "(")) {
                depth = depth - 1U;
                if (depth == 0U) break;
            }
            if (open == declaration) return;
            open = open - 1U;
        }
        if (open == declaration || tokens[open - 1U].kind != CSharpTokenKind::identifier) return;
        // A field initialized by a call (`x = Make(...)`) is no method head.
        for (std::size_t check = declaration; check < open; ++check) {
            if (is_punctuation(source_, tokens[check], "=")) return;
        }
        CSharpMethod method{};
        method.name = std::string(source_.spelling(tokens[open - 1U]));
        method.line = tokens[open - 1U].line;
        // Parameters: split at depth-one commas; a parameter's type is
        // everything before its name (and before a default value).
        std::size_t parameter = open + 1U;
        std::size_t nested = 0U;
        for (std::size_t index = open + 1U; index < close; ++index) {
            const bool last = index + 1U == close;
            const CSharpToken& current = tokens[index];
            if (is_punctuation(source_, current, "(") || is_punctuation(source_, current, "<") ||
                is_punctuation(source_, current, "[")) {
                nested = nested + 1U;
            } else if ((is_punctuation(source_, current, ")") && !last) || is_punctuation(source_, current, ">") ||
                       is_punctuation(source_, current, "]")) {
                if (nested > 0U) nested = nested - 1U;
            }
            const bool separator = nested == 0U && is_punctuation(source_, current, ",");
            if (separator || last) {
                const std::string type = parameter_type(parameter, index);
                if (!type.empty()) method.parameter_types.push_back(type);
                parameter = index + 1U;
            }
        }
        index_.classes[scopes_.back().class_record].methods.push_back(std::move(method));
    }

    // The type of the parameter in tokens [first, last).
    [[nodiscard]] std::string parameter_type(std::size_t first, std::size_t last) const {
        const std::vector<CSharpToken>& tokens = source_.tokens;
        std::size_t name = last;
        for (std::size_t index = first; index < last; ++index) {
            if (is_punctuation(source_, tokens[index], "=")) {
                name = index;
                break;
            }
        }
        if (name <= first + 1U) return std::string();
        std::string type{};
        for (std::size_t index = first; index + 1U < name; ++index) {
            const std::string_view spelled = source_.spelling(tokens[index]);
            if (spelled == "this" || spelled == "ref" || spelled == "out" || spelled == "in" || spelled == "params") {
                continue;
            }
            type.append(spelled);
        }
        return type;
    }

    const CSharpSource& source_;
    CSharpClassIndex& index_;
    std::vector<Scope> scopes_{};
    std::string namespace_{};
};

} // namespace

const CSharpClass* CSharpClassIndex::find(std::string_view name) const& noexcept {
    const std::string_view short_name = last_segment(name);
    const CSharpClass* first = nullptr;
    for (const CSharpClass& record : classes) {
        if (record.name != short_name) continue;
        if (!record.base.empty()) return &record;
        if (first == nullptr) first = &record;
    }
    return first;
}

bool CSharpClassIndex::declares_method(std::string_view class_name, std::string_view name) const noexcept {
    const std::string_view short_name = last_segment(class_name);
    for (const CSharpClass& record : classes) {
        if (record.name != short_name) continue;
        for (const CSharpMethod& method : record.methods) {
            if (method.name == name) return true;
        }
    }
    return false;
}

std::string CSharpClassIndex::base_of(std::string_view class_name) const {
    const CSharpClass* const record = find(class_name);
    if (record == nullptr) return std::string();
    std::string base = (*record).base;
    return base;
}

void read_csharp_classes(CSharpSource& source, CSharpClassIndex& index, ConversionDiagnosticList& diagnostics) {
    const CSharpLexOutcome lexed = lex_csharp(source, diagnostics);
    if (lexed != CSharpLexOutcome::lexed) return;
    ClassReader reader(source, index);
    reader.run(diagnostics);
}

} // namespace gui_forms::winforms_converter
