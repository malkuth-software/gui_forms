// Composite-control expressions (control_expression.hpp).

#include "control_expression.hpp"

#include "../../form_language/form_model/form_model.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms::control_language {

namespace {

bool lowercase_letter(const char character) noexcept {
    const bool result = character >= 'a' && character <= 'z';
    return result;
}

bool digit(const char character) noexcept {
    const bool result = character >= '0' && character <= '9';
    return result;
}

bool space(const char character) noexcept {
    const bool result = character == ' ' || character == '\t' || character == '\n' || character == '\r';
    return result;
}

enum class TokenKind : std::uint8_t {
    end,
    number,
    string,
    word,
    local,
    open,
    close,
    comma,
    dot,
    plus,
    minus,
    star,
    slash,
    invalid,
};

struct Token final {
    TokenKind kind{TokenKind::end};
    // The token as written, and where it starts.
    std::string_view text{};
    std::size_t offset{};
};

// Reads tokens from one value, skipping the space between them.
class Tokenizer final {
public:
    explicit Tokenizer(std::string_view text) noexcept : text_(text) {}

    // The next token, without taking it.
    [[nodiscard]] Token peek() const noexcept;
    // Takes the token peek gives.
    Token take() noexcept;

private:
    [[nodiscard]] std::size_t skip_space(std::size_t from) const noexcept;
    [[nodiscard]] Token read_at(std::size_t start) const noexcept;
    // The end of a word starting at `start` (a lowercase letter).
    [[nodiscard]] std::size_t word_end(std::size_t start) const noexcept;

    std::string_view text_{};
    std::size_t position_{};
};

std::size_t Tokenizer::skip_space(std::size_t from) const noexcept {
    std::size_t at = from;
    while (at < text_.size() && space(text_[at])) ++at;
    return at;
}

std::size_t Tokenizer::word_end(const std::size_t start) const noexcept {
    std::size_t at = start + 1U;
    while (at < text_.size()) {
        const char character = text_[at];
        if (lowercase_letter(character) || digit(character)) {
            ++at;
            continue;
        }
        // A hyphen joins two parts of a word: it is followed by a letter
        // or a digit, and not by another hyphen.
        const bool joins = character == '-' && at + 1U < text_.size() &&
                           (lowercase_letter(text_[at + 1U]) || digit(text_[at + 1U]));
        if (!joins) break;
        ++at;
    }
    return at;
}

Token Tokenizer::read_at(const std::size_t start) const noexcept {
    Token token{};
    token.offset = start;
    if (start >= text_.size()) {
        token.kind = TokenKind::end;
        return token;
    }
    const char first = text_[start];
    std::size_t end = start + 1U;
    if (lowercase_letter(first)) {
        end = word_end(start);
        token.kind = TokenKind::word;
    } else if (first == '@') {
        if (start + 1U < text_.size() && lowercase_letter(text_[start + 1U])) {
            end = word_end(start + 1U);
            token.kind = TokenKind::local;
        } else {
            token.kind = TokenKind::invalid;
        }
    } else if (digit(first)) {
        while (end < text_.size() && digit(text_[end])) ++end;
        if (end + 1U < text_.size() && text_[end] == '.' && digit(text_[end + 1U])) {
            end += 1U;
            while (end < text_.size() && digit(text_[end])) ++end;
        }
        while (end < text_.size() && lowercase_letter(text_[end])) ++end;
        token.kind = TokenKind::number;
    } else if (first == '\'' || first == '"') {
        // To the matching quote, stepping over escaped characters; the
        // escapes are read by read_form_string.
        bool closed = false;
        while (end < text_.size()) {
            const char character = text_[end];
            ++end;
            if (character == '\\' && end < text_.size()) {
                ++end;
                continue;
            }
            if (character == first) {
                closed = true;
                break;
            }
        }
        token.kind = closed ? TokenKind::string : TokenKind::invalid;
    } else {
        switch (first) {
        case '(': token.kind = TokenKind::open; break;
        case ')': token.kind = TokenKind::close; break;
        case ',': token.kind = TokenKind::comma; break;
        case '.': token.kind = TokenKind::dot; break;
        case '+': token.kind = TokenKind::plus; break;
        case '-': token.kind = TokenKind::minus; break;
        case '*': token.kind = TokenKind::star; break;
        case '/': token.kind = TokenKind::slash; break;
        default: token.kind = TokenKind::invalid; break;
        }
    }
    token.text = text_.substr(start, end - start);
    return token;
}

Token Tokenizer::peek() const noexcept {
    const std::size_t start = skip_space(position_);
    const Token token = read_at(start);
    return token;
}

Token Tokenizer::take() noexcept {
    const Token token = peek();
    position_ = token.offset + token.text.size();
    return token;
}

// A recursive descent over one value. Each level of nesting is one call
// deeper; `depth_` stops it past maximum_expression_depth, so the parser's
// own stack is bounded by the expression budget.
class Parser final {
public:
    Parser(std::string_view text, ParsedExpression& parsed) noexcept : tokens_(text), parsed_(parsed) {}

    [[nodiscard]] ExpressionParseResult run();

private:
    // Each returns the node it parsed, or none after recording a refusal.
    [[nodiscard]] bool expression(ExpressionNodeIndex& result);
    [[nodiscard]] bool conjunction(ExpressionNodeIndex& result);
    [[nodiscard]] bool sum(ExpressionNodeIndex& result);
    [[nodiscard]] bool product(ExpressionNodeIndex& result);
    [[nodiscard]] bool unary(ExpressionNodeIndex& result);
    [[nodiscard]] bool primary(ExpressionNodeIndex& result);
    [[nodiscard]] bool call_arguments(ExpressionNode& call);

    // Whether the next token begins a primary (so a new sequence term).
    [[nodiscard]] bool starts_term(const Token& token) const noexcept;
    [[nodiscard]] ExpressionNodeIndex add_node(ExpressionNode node);
    [[nodiscard]] ExpressionNodeIndex add_operator(ExpressionNodeKind kind, ExpressionOperator operation,
                                                   std::size_t offset, ExpressionNodeIndex left,
                                                   ExpressionNodeIndex right, bool binary);
    bool refuse(std::size_t offset, std::string detail);
    // Enters one level of nesting; false (after a refusal) past the budget.
    [[nodiscard]] bool enter(std::size_t offset);
    void leave() noexcept { --depth_; }

    Tokenizer tokens_;
    ParsedExpression& parsed_;
    std::size_t depth_{};
    ExpressionParseResult result_{};
};

bool Parser::refuse(const std::size_t offset, std::string detail) {
    if (result_.detail.empty()) {
        result_.offset = offset;
        result_.detail = std::move(detail);
    }
    return false;
}

bool Parser::enter(const std::size_t offset) {
    ++depth_;
    parsed_.depth = std::max(parsed_.depth, depth_);
    if (depth_ > maximum_expression_depth) {
        static_cast<void>(refuse(offset, "nested deeper than 16"));
        return false;
    }
    return true;
}

ExpressionNodeIndex Parser::add_node(ExpressionNode node) {
    const ExpressionNodeIndex index{static_cast<std::uint32_t>(parsed_.nodes.size())};
    parsed_.nodes.push_back(std::move(node));
    return index;
}

ExpressionNodeIndex Parser::add_operator(const ExpressionNodeKind kind, const ExpressionOperator operation,
                                         const std::size_t offset, const ExpressionNodeIndex left,
                                         const ExpressionNodeIndex right, const bool binary) {
    ExpressionNode node{};
    node.kind = kind;
    node.operation = operation;
    node.offset = offset;
    node.first_argument = static_cast<std::uint32_t>(parsed_.arguments.size());
    parsed_.arguments.push_back(left);
    node.argument_count = 1U;
    if (binary) {
        parsed_.arguments.push_back(right);
        node.argument_count = 2U;
    }
    const ExpressionNodeIndex index = add_node(std::move(node));
    return index;
}

bool Parser::starts_term(const Token& token) const noexcept {
    switch (token.kind) {
    case TokenKind::number:
    case TokenKind::string:
    case TokenKind::word:
    case TokenKind::local:
    case TokenKind::open:
        return true;
    case TokenKind::end:
    case TokenKind::close:
    case TokenKind::comma:
    case TokenKind::dot:
    case TokenKind::plus:
    case TokenKind::minus:
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::invalid:
        return false;
    }
    return false;
}

ExpressionParseResult Parser::run() {
    parsed_.clear();
    while (true) {
        const Token next = tokens_.peek();
        if (next.kind == TokenKind::end) break;
        if (!starts_term(next) && next.kind != TokenKind::minus) {
            static_cast<void>(refuse(next.offset, "unexpected '" + std::string(next.text) + "'"));
            return result_;
        }
        ExpressionNodeIndex term{};
        if (!expression(term)) return result_;
        parsed_.terms.push_back(term);
    }
    result_.valid = true;
    return result_;
}

bool Parser::expression(ExpressionNodeIndex& result) {
    ExpressionNodeIndex left{};
    if (!conjunction(left)) return false;
    while (true) {
        const Token next = tokens_.peek();
        if (next.kind != TokenKind::word || next.text != "or") break;
        static_cast<void>(tokens_.take());
        ExpressionNodeIndex right{};
        if (!conjunction(right)) return false;
        left = add_operator(ExpressionNodeKind::binary, ExpressionOperator::logical_or, next.offset, left, right, true);
    }
    result = left;
    return true;
}

bool Parser::conjunction(ExpressionNodeIndex& result) {
    ExpressionNodeIndex left{};
    if (!sum(left)) return false;
    while (true) {
        const Token next = tokens_.peek();
        if (next.kind != TokenKind::word || next.text != "and") break;
        static_cast<void>(tokens_.take());
        ExpressionNodeIndex right{};
        if (!sum(right)) return false;
        left = add_operator(ExpressionNodeKind::binary, ExpressionOperator::logical_and, next.offset, left, right,
                            true);
    }
    result = left;
    return true;
}

bool Parser::sum(ExpressionNodeIndex& result) {
    ExpressionNodeIndex left{};
    if (!product(left)) return false;
    while (true) {
        const Token next = tokens_.peek();
        ExpressionOperator operation = ExpressionOperator::none;
        if (next.kind == TokenKind::plus) operation = ExpressionOperator::add;
        if (next.kind == TokenKind::minus) operation = ExpressionOperator::subtract;
        if (operation == ExpressionOperator::none) break;
        static_cast<void>(tokens_.take());
        ExpressionNodeIndex right{};
        if (!product(right)) return false;
        left = add_operator(ExpressionNodeKind::binary, operation, next.offset, left, right, true);
    }
    result = left;
    return true;
}

bool Parser::product(ExpressionNodeIndex& result) {
    ExpressionNodeIndex left{};
    if (!unary(left)) return false;
    while (true) {
        const Token next = tokens_.peek();
        ExpressionOperator operation = ExpressionOperator::none;
        if (next.kind == TokenKind::star) operation = ExpressionOperator::multiply;
        if (next.kind == TokenKind::slash) operation = ExpressionOperator::divide;
        if (operation == ExpressionOperator::none) break;
        static_cast<void>(tokens_.take());
        ExpressionNodeIndex right{};
        if (!unary(right)) return false;
        left = add_operator(ExpressionNodeKind::binary, operation, next.offset, left, right, true);
    }
    result = left;
    return true;
}

bool Parser::unary(ExpressionNodeIndex& result) {
    const Token next = tokens_.peek();
    const bool prefix_not = next.kind == TokenKind::word && next.text == "not";
    const bool prefix_minus = next.kind == TokenKind::minus;
    if (prefix_not) {
        // `not(` is the call form, read as a primary.
        Tokenizer ahead = tokens_;
        static_cast<void>(ahead.take());
        const Token after = ahead.peek();
        const bool call_form = after.kind == TokenKind::open && after.offset == next.offset + next.text.size();
        if (call_form) {
            const bool read = primary(result);
            return read;
        }
    }
    if (!prefix_not && !prefix_minus) {
        const bool read = primary(result);
        return read;
    }
    static_cast<void>(tokens_.take());
    if (!enter(next.offset)) return false;
    ExpressionNodeIndex operand{};
    const bool read = unary(operand);
    leave();
    if (!read) return false;
    const ExpressionNodeKind kind = prefix_not ? ExpressionNodeKind::logical_not : ExpressionNodeKind::negate;
    result = add_operator(kind, ExpressionOperator::none, next.offset, operand, operand, false);
    return true;
}

bool Parser::call_arguments(ExpressionNode& call) {
    // Each argument is parsed before the call's operands are stored, so
    // the operands are gathered here and appended contiguously.
    std::vector<ExpressionNodeIndex> operands{};
    const Token first = tokens_.peek();
    if (first.kind == TokenKind::close) {
        static_cast<void>(tokens_.take());
    } else {
        while (true) {
            ExpressionNodeIndex argument{};
            if (!expression(argument)) return false;
            operands.push_back(argument);
            const Token separator = tokens_.take();
            if (separator.kind == TokenKind::close) break;
            if (separator.kind != TokenKind::comma) {
                const std::string found = separator.kind == TokenKind::end ? "the end" : std::string(separator.text);
                return refuse(separator.offset, "expected ',' or ')' in " + call.text + "(), found " + found);
            }
        }
    }
    call.first_argument = static_cast<std::uint32_t>(parsed_.arguments.size());
    call.argument_count = static_cast<std::uint32_t>(operands.size());
    parsed_.arguments.insert(parsed_.arguments.end(), operands.begin(), operands.end());
    return true;
}

bool Parser::primary(ExpressionNodeIndex& result) {
    const Token token = tokens_.take();
    if (!enter(token.offset)) return false;
    ExpressionNode node{};
    node.offset = token.offset;
    bool read = true;
    switch (token.kind) {
    case TokenKind::number: {
        std::size_t unit_start = 0U;
        while (unit_start < token.text.size() && !lowercase_letter(token.text[unit_start])) ++unit_start;
        node.kind = ExpressionNodeKind::number;
        node.text = std::string(token.text.substr(0U, unit_start));
        node.unit = std::string(token.text.substr(unit_start));
        result = add_node(std::move(node));
        break;
    }
    case TokenKind::string: {
        const form_language::FormStringReading reading = form_language::read_form_string(token.text);
        if (!reading.valid) {
            read = refuse(token.offset, "string escape " + reading.escape + " is not admitted");
            break;
        }
        node.kind = ExpressionNodeKind::string;
        node.text = reading.text;
        result = add_node(std::move(node));
        break;
    }
    case TokenKind::local: {
        node.kind = ExpressionNodeKind::local_reference;
        node.text = std::string(token.text.substr(1U));
        const Token dot = tokens_.peek();
        if (dot.kind == TokenKind::dot && dot.offset == token.offset + token.text.size()) {
            static_cast<void>(tokens_.take());
            const Token member = tokens_.take();
            if (member.kind != TokenKind::word || member.offset != dot.offset + 1U) {
                read = refuse(dot.offset, "expected a property name after '@" + node.text + ".'");
                break;
            }
            node.member = std::string(member.text);
        }
        result = add_node(std::move(node));
        break;
    }
    case TokenKind::word: {
        const Token next = tokens_.peek();
        const bool adjacent = next.offset == token.offset + token.text.size();
        if (token.text == "self" && next.kind == TokenKind::dot && adjacent) {
            static_cast<void>(tokens_.take());
            const Token member = tokens_.take();
            if (member.kind != TokenKind::word || member.offset != next.offset + 1U) {
                read = refuse(next.offset, "expected a property name after 'self.'");
                break;
            }
            node.kind = ExpressionNodeKind::self_reference;
            node.member = std::string(member.text);
            result = add_node(std::move(node));
            break;
        }
        node.text = std::string(token.text);
        if (next.kind == TokenKind::open && adjacent) {
            static_cast<void>(tokens_.take());
            node.kind = ExpressionNodeKind::call;
            if (!call_arguments(node)) {
                read = false;
                break;
            }
        } else {
            node.kind = ExpressionNodeKind::word;
        }
        result = add_node(std::move(node));
        break;
    }
    case TokenKind::open: {
        read = expression(result);
        if (!read) break;
        const Token close = tokens_.take();
        if (close.kind != TokenKind::close) read = refuse(close.offset, "expected ')'");
        break;
    }
    case TokenKind::end:
        read = refuse(token.offset, "the value ends where an operand is expected");
        break;
    case TokenKind::close:
    case TokenKind::comma:
    case TokenKind::dot:
    case TokenKind::plus:
    case TokenKind::minus:
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::invalid:
        read = refuse(token.offset, "unexpected '" + std::string(token.text) + "'");
        break;
    }
    leave();
    return read;
}

} // namespace

void ParsedExpression::clear() noexcept {
    nodes.clear();
    arguments.clear();
    terms.clear();
    depth = 0U;
}

ExpressionParseResult parse_expressions(const std::string_view text, ParsedExpression& parsed) {
    Parser parser(text, parsed);
    const ExpressionParseResult result = parser.run();
    return result;
}

ExpressionOperands expression_operands(const ParsedExpression& parsed, const ExpressionNodeIndex node) noexcept {
    const ExpressionNode& record = parsed.nodes[node.value];
    ExpressionOperands operands{};
    operands.count = record.argument_count;
    if (record.argument_count != 0U) operands.first = parsed.arguments.data() + record.first_argument;
    return operands;
}

} // namespace gui_forms::control_language
