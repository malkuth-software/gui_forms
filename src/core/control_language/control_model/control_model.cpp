// A composite-control document's model and checker (control_model.hpp).

#include "control_model.hpp"

#include "../control_expression/control_expression.hpp"
#include "../../form_language/form_xml/form_xml.hpp"
#include "../../../platform/stxlib_bridge.hpp"

#include "gui_forms/theme/types/theme_types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace gui_forms::control_language {

namespace {

using theme_language::ByteOffset;
using theme_language::DiagnosticCode;
using theme_language::SourcePosition;
using theme_language::ThemeDiagnostic;

// The words of the closed vocabularies a document writes, kebab case.

constexpr std::array<std::string_view, 14> property_kind_words = {
    "boolean", "number", "signed-integer", "unsigned-integer", "text",      "enumeration", "color",
    "font",    "image",  "size",           "point",            "rectangle", "insets",      "command",
};

// The C++ payload spelling of each kind, in property_kind_words' order
// (control_payload_type_name): the BindingValue alternative its value is.
// An enumeration is a PropertyEnumValue; a command's payload is its
// command's name. Kept in tools/abi_event_catalog.py's COMPOSITE_PAYLOADS.
constexpr std::array<std::string_view, 14> payload_type_words = {
    "bool",     "double",  "std::int64_t", "std::uint64_t", "std::string", "PropertyEnumValue", "Color",
    "FontSpec", "ImageId", "Size",         "Point",         "Rect",        "Insets",            "std::string",
};

// The colour properties a composite may declare: the application's
// authored colours the theme admits (design note 3.6). No font property is
// admitted.
constexpr std::array<std::string_view, 3> admitted_colour_properties = {
    "authored-background",
    "authored-foreground",
    "content-color",
};

// The words of `affects`, and what each invalidates.
struct AffectsWord final {
    std::string_view word;
    Dirty dirty;
};
constexpr std::array<AffectsWord, 6> affects_words = {{
    {"measure", Dirty::measure},
    {"arrange", Dirty::arrange},
    {"layout", Dirty::layout},
    {"paint", Dirty::paint},
    {"hit-test", Dirty::hit_test},
    {"semantics", Dirty::semantics},
}};

// The semantic states a <semantics states="..."> list may name, as
// SemanticState spells them in kebab case.
constexpr std::array<std::string_view, 14> semantic_state_words = {
    "enabled", "visible",           "focusable", "focused",   "selected", "checked",   "mixed",
    "read-only", "expanded", "busy", "protected-content", "invalid", "checkable", "multiselectable",
};

// The rule triggers of an <on> (design note 3.8); a child's typed
// interaction by its schema name is admitted besides these.
constexpr std::array<std::string_view, 8> trigger_words = {
    "click",       "mouse-down", "mouse-up",    "activated",
    "focus-gained", "focus-lost", "mouse-wheel", "changed",
};

// Every function an expression may call (design note 3.2 to 3.11):
// geometry, conditions, values and actions.
constexpr std::array<std::string_view, 50> function_words = {
    // geometry
    "inset", "inset-inline", "inset-block", "width", "height", "align", "align-inline", "align-block", "split",
    "after-inline", "below", "union", "at", "at-fraction", "text-extent", "start-of", "end-of", "min", "max",
    "clamp", "calc", "choose",
    // conditions and values
    "property", "state", "state-of", "present", "equal", "less", "greater", "less-equal", "greater-equal", "not",
    "command-enabled", "enabled", "focused", "focus-within", "hovering", "pressed", "is-true", "equals", "child",
    "part",
    // actions
    "set", "toggle", "step", "run", "raise", "focus",
};

// Words an expression may write that are no declaration: positions,
// axes, halves and the firing fields.
constexpr std::array<std::string_view, 17> keyword_words = {
    "start",       "end",         "center",        "inline", "block", "first", "second", "square", "self",
    "frame-value", "frame-fraction", "wheel-notches", "true",   "false", "yes",   "no",     "none",
};

// The engine inputs: geometry and state programs may not read them
// (design note 3.2: layout is deterministic and the engine's).
constexpr std::array<std::string_view, 8> engine_input_words = {
    "pointer", "pointer-x", "pointer-y", "time", "time-in-state", "scale", "display-scale", "text-scale",
};

constexpr std::array<std::string_view, 2> physical_direction_words = {"left", "right"};

// The parts every control has, which a composite does not redeclare.
// The form element that declares a grid span, which is no object.
constexpr std::string_view span_element = "span";

constexpr std::array<std::string_view, 2> base_part_words = {"region", "focus"};

// Key names a gesture may end in, besides a-z, 0-9 and f1-f24.
constexpr std::array<std::string_view, 17> named_key_words = {
    "space",    "enter",      "escape",     "tab",       "backspace", "delete",     "insert",
    "home",     "end",        "page-up",    "page-down", "arrow-up",  "arrow-down", "arrow-left",
    "arrow-right", "plus",    "minus",
};
constexpr std::array<std::string_view, 4> modifier_words = {"ctrl", "shift", "alt", "meta"};

template <std::size_t Count>
bool listed(const std::array<std::string_view, Count>& words, const std::string_view word) noexcept {
    for (const std::string_view candidate : words) {
        if (candidate == word) return true;
    }
    return false;
}

// Whether `name` is kebab case: a lowercase letter, then lowercase
// letters, digits and single inner hyphens.
bool kebab_name(const std::string_view name) noexcept {
    if (name.empty() || name.front() < 'a' || name.front() > 'z' || name.back() == '-') return false;
    char previous = '\0';
    for (const char character : name) {
        const bool letter = character >= 'a' && character <= 'z';
        const bool number = character >= '0' && character <= '9';
        const bool hyphen = character == '-' && previous != '-';
        if (!letter && !number && !hyphen) return false;
        previous = character;
    }
    return true;
}

std::string snake_of(const std::string_view kebab) {
    std::string snake(kebab);
    std::replace(snake.begin(), snake.end(), '-', '_');
    return snake;
}

std::string kebab_of(const std::string_view snake) {
    std::string kebab(snake);
    std::replace(kebab.begin(), kebab.end(), '_', '-');
    return kebab;
}

bool blank(const std::string_view text) noexcept {
    for (const char character : text) {
        if (character != ' ' && character != '\t' && character != '\n' && character != '\r') return false;
    }
    return true;
}

// The whitespace-separated words of `text`, borrowed from it.
std::vector<std::string_view> words_of(const std::string_view text) {
    std::vector<std::string_view> words{};
    std::size_t at = 0U;
    while (at < text.size()) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
        const std::size_t start = at;
        while (at < text.size() && text[at] != ' ' && text[at] != '\t' && text[at] != '\n' && text[at] != '\r') {
            ++at;
        }
        if (at > start) words.push_back(text.substr(start, at - start));
    }
    return words;
}
std::vector<std::string_view> words_of(std::string&& text) = delete;

bool digit(const char character) noexcept {
    const bool result = character >= '0' && character <= '9';
    return result;
}

bool hex_digit(const char character) noexcept {
    const bool result = digit(character) || (character >= 'a' && character <= 'f') ||
                        (character >= 'A' && character <= 'F');
    return result;
}

// One number written whole: an optional '-', digits and an optional
// fraction of digits (no exponent, no spaces, no infinities).
std::optional<double> number_of(const std::string_view text) {
    std::size_t at = 0U;
    if (at < text.size() && text[at] == '-') ++at;
    const std::size_t integer_start = at;
    while (at < text.size() && digit(text[at])) ++at;
    const bool integer_digits = at > integer_start;
    bool fraction_digits = false;
    if (at < text.size() && text[at] == '.') {
        ++at;
        const std::size_t fraction_start = at;
        while (at < text.size() && digit(text[at])) ++at;
        fraction_digits = at > fraction_start;
        if (!fraction_digits) return std::nullopt;
    }
    if (at != text.size() || (!integer_digits && !fraction_digits)) return std::nullopt;
    const std::string copy(text);
    const double value = std::strtod(copy.c_str(), nullptr);
    return value;
}

// Whether word, which number_of read as number, is a default of an integer
// kind (A-gf-core-03-F2). Digits that parse exactly as the kind's integer
// are admitted, so 9223372036854775807 is a signed default although no
// double holds it. Any other number must be whole and inside the kind's
// range, tested as exact doubles before any conversion: -2^63 <= n < 2^63
// signed, 0 <= n < 2^64 unsigned.
bool integer_default_valid(const std::string_view word, const double number, const bool signed_kind) noexcept {
    const char* const first = word.data();
    const char* const last = word.data() + word.size();
    bool exact = false;
    if (signed_kind) {
        std::int64_t parsed = 0;
        const std::from_chars_result result = std::from_chars(first, last, parsed);
        exact = result.ec == std::errc{} && result.ptr == last;
    } else {
        std::uint64_t parsed = 0U;
        const std::from_chars_result result = std::from_chars(first, last, parsed);
        exact = result.ec == std::errc{} && result.ptr == last;
    }
    if (exact) return true;
    const bool in_range = signed_kind ? number >= -9223372036854775808.0 && number < 9223372036854775808.0
                                      : number >= 0.0 && number < 18446744073709551616.0;
    const bool whole = in_range && std::trunc(number) == number;
    return whole;
}

std::optional<ControlPropertyKind> kind_of(const std::string_view word) noexcept {
    for (std::size_t index = 0U; index < property_kind_words.size(); ++index) {
        if (property_kind_words[index] == word) return static_cast<ControlPropertyKind>(index);
    }
    return std::nullopt;
}

std::optional<SemanticRole> semantic_role_of(const std::string_view word) {
    const std::string snake = snake_of(word);
    for (std::size_t index = 0U; index < semantic_role_count; ++index) {
        const SemanticRole role = static_cast<SemanticRole>(index);
        if (snake == semantic_role_name(role)) return role;
    }
    return std::nullopt;
}

std::optional<SemanticAction> semantic_action_of(const std::string_view word) {
    const std::string snake = snake_of(word);
    for (std::size_t index = 0U; index < semantic_action_count; ++index) {
        const SemanticAction action = static_cast<SemanticAction>(index);
        if (snake == semantic_action_name(action)) return action;
    }
    return std::nullopt;
}

std::optional<ControlVisualRole> family_of(const std::string_view word) {
    const std::string snake = snake_of(word);
    for (std::size_t index = 0U; index < control_visual_role_count; ++index) {
        const ControlVisualRole role = static_cast<ControlVisualRole>(index);
        if (snake == control_visual_role_name(role)) return role;
    }
    return std::nullopt;
}

std::optional<PartMark> mark_of(const std::string_view word) {
    const std::string snake = snake_of(word);
    for (std::size_t index = 0U; index < part_mark_count; ++index) {
        const PartMark mark = static_cast<PartMark>(index);
        if (snake == part_mark_name(mark)) return mark;
    }
    return std::nullopt;
}

// Whether `gesture` is a key gesture: modifiers joined by '+' to one key.
bool key_gesture(const std::string_view gesture) {
    std::size_t start = 0U;
    while (true) {
        const std::size_t plus = gesture.find('+', start);
        const std::string_view piece = gesture.substr(start, plus == std::string_view::npos ? std::string_view::npos
                                                                                             : plus - start);
        if (plus == std::string_view::npos) {
            const bool letter = piece.size() == 1U && ((piece[0] >= 'a' && piece[0] <= 'z') ||
                                                       (piece[0] >= '0' && piece[0] <= '9'));
            bool function_key = false;
            if (piece.size() >= 2U && piece.size() <= 3U && piece[0] == 'f') {
                const std::optional<double> number = number_of(piece.substr(1U));
                function_key = number.has_value() && *number >= 1.0 && *number <= 24.0;
            }
            const bool named = listed(named_key_words, piece);
            return letter || function_key || named;
        }
        if (!listed(modifier_words, piece)) return false;
        start = plus + 1U;
    }
}

// The key a gesture ends in (after its last '+').
std::string_view gesture_key(const std::string_view gesture) noexcept {
    const std::size_t plus = gesture.rfind('+');
    const std::string_view key = plus == std::string_view::npos ? gesture : gesture.substr(plus + 1U);
    return key;
}

// One element in document order of start tags, which is the order of
// ControlSyntax::locations.
struct FlatElement final {
    const XmlElement* element{};
    // Its parent's position in the flat list; the root has none.
    std::optional<std::uint32_t> parent{};
};

// The admitted attributes of one element kind.
struct ElementShape final {
    std::string_view element;
    std::vector<std::string_view> admitted;
    std::vector<std::string_view> required;
};

// What an expression's words may name, by where the expression is written.
enum class ExpressionPlace : std::uint8_t {
    // A condition or value of the composite (`when`, `enabled`, semantics).
    condition,
    // A part's box, a drag's range: also refuses physical directions.
    geometry,
    // An action list (`run`, `preview`, an action's `to` or `by`).
    action,
    // A child's attribute, which reads the composite through `self.`.
    child,
};

class ModelBuilder final {
public:
    ModelBuilder(const theme_language::ThemeSource& source, const ControlSyntax& syntax,
                 const form_language::FormSchema& schema, theme_language::ThemeDiagnosticList& diagnostics) noexcept
        : source_(source), syntax_(syntax), schema_(schema), diagnostics_(diagnostics) {}

    [[nodiscard]] bool run(ControlModel& scratch);

private:
    // Positions.
    [[nodiscard]] SourcePosition position_at(std::size_t offset) const noexcept;
    [[nodiscard]] std::size_t element_offset(std::uint32_t index) const noexcept;
    [[nodiscard]] std::size_t attribute_offset(std::uint32_t index, std::string_view attribute) const noexcept;
    [[nodiscard]] std::size_t value_offset(std::uint32_t index, std::string_view attribute) const noexcept;
    [[nodiscard]] ControlSource source_of(std::uint32_t index) const noexcept;
    void report(DiagnosticCode code, std::size_t offset, std::string_view rule, std::string_view property,
                std::string detail);

    // The walk.
    void flatten();
    [[nodiscard]] std::uint32_t index_of(const XmlElement& element) const noexcept;
    void check_text(ControlModel& scratch);
    void check_attributes(std::uint32_t index, const ElementShape& shape, std::string_view rule);
    [[nodiscard]] std::string_view attribute(std::uint32_t index, std::string_view name) const noexcept;
    [[nodiscard]] bool has_attribute(std::uint32_t index, std::string_view name) const noexcept;
    [[nodiscard]] bool flag(std::uint32_t index, std::string_view name, std::string_view rule, bool fallback);

    void read_root(ControlModel& scratch);
    void read_declarations(ControlModel& scratch);
    void read_property(std::uint32_t index, ControlModel& scratch);
    void read_event(std::uint32_t index, ControlModel& scratch);
    void read_command(std::uint32_t index, ControlModel& scratch);
    void read_state(std::uint32_t index, bool custom, ControlModel& scratch);
    void read_part(std::uint32_t index, ControlModel& scratch);
    void read_drag(std::uint32_t index, std::string_view part, ControlModel& scratch);
    void read_children(std::uint32_t index, ControlModel& scratch);
    void read_child(std::uint32_t index, std::optional<std::size_t> parent_child,
                    std::optional<std::size_t> parent_type, ControlModel& scratch);
    void read_focus(std::uint32_t index, ControlModel& scratch);
    void read_rule(std::uint32_t index, bool key, ControlModel& scratch);
    void read_semantics(std::uint32_t index, ControlModel& scratch);
    void read_look(std::uint32_t index, ControlModel& scratch);

    // The checks that need every declaration.
    void check_names(const ControlModel& scratch);
    void check_references(const ControlModel& scratch);
    void check_part(const ControlPart& part, const ControlModel& scratch);
    void check_drag(const ControlDrag& drag, const ControlModel& scratch);
    void check_child_values(const ControlModel& scratch);
    void check_rules(const ControlModel& scratch);
    void check_semantics(const ControlModel& scratch);
    void check_reachability(const ControlModel& scratch);
    void check_cycles(const ControlModel& scratch);
    void check_look(const ControlModel& scratch);

    // Expressions.
    void check_expression(std::string_view text, std::uint32_t element, std::string_view attribute_name,
                          ExpressionPlace place, std::string_view rule, const ControlModel& scratch);
    void check_word(const ExpressionNode& node, std::size_t base, ExpressionPlace place, std::string_view rule,
                    std::string_view attribute_name, const ControlModel& scratch);
    // The properties a command's `run` or an action list writes.
    void written_properties(std::string_view actions, std::vector<std::string>& written) const;

    // Lookups.
    [[nodiscard]] const ControlProperty* find_property(const ControlModel& scratch,
                                                       std::string_view name) const noexcept;
    [[nodiscard]] bool base_property(std::string_view name) const;
    [[nodiscard]] bool any_property(const ControlModel& scratch, std::string_view name) const;
    [[nodiscard]] bool part_named(const ControlModel& scratch, std::string_view name) const noexcept;
    [[nodiscard]] bool command_named(const ControlModel& scratch, std::string_view name) const noexcept;
    [[nodiscard]] bool event_named(const ControlModel& scratch, std::string_view name) const noexcept;
    [[nodiscard]] bool state_named(const ControlModel& scratch, std::string_view name) const;
    [[nodiscard]] const ControlChild* child_named(const ControlModel& scratch, std::string_view name) const noexcept;
    [[nodiscard]] bool child_property(const ControlChild& child, std::string_view kebab_name) const;
    [[nodiscard]] std::optional<std::size_t> schema_type(std::string_view snake_name) const noexcept;

    const theme_language::ThemeSource& source_;
    const ControlSyntax& syntax_;
    const form_language::FormSchema& schema_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    std::vector<FlatElement> flat_{};
    std::optional<std::size_t> base_type_{};
    std::size_t first_record_{};
    ParsedExpression parsed_{};
};

SourcePosition ModelBuilder::position_at(const std::size_t offset) const noexcept {
    const SourcePosition position =
        syntax_.lines.position(source_.text, ByteOffset{static_cast<std::uint32_t>(offset)});
    return position;
}

std::size_t ModelBuilder::element_offset(const std::uint32_t index) const noexcept {
    if (index >= syntax_.locations.elements.size()) return 0U;
    return syntax_.locations.elements[index].start_offset;
}

std::size_t ModelBuilder::attribute_offset(const std::uint32_t index, const std::string_view name) const noexcept {
    if (index >= flat_.size() || index >= syntax_.locations.elements.size()) return element_offset(index);
    const XmlElement& element = *flat_[index].element;
    const detail::XmlElementLocation& location = syntax_.locations.elements[index];
    for (std::size_t at = 0U; at < element.attributes.size() && at < location.attributes.size(); ++at) {
        if (element.attributes[at].name == name) return location.attributes[at].name_offset;
    }
    return element_offset(index);
}

std::size_t ModelBuilder::value_offset(const std::uint32_t index, const std::string_view name) const noexcept {
    if (index >= flat_.size() || index >= syntax_.locations.elements.size()) return element_offset(index);
    const XmlElement& element = *flat_[index].element;
    const detail::XmlElementLocation& location = syntax_.locations.elements[index];
    for (std::size_t at = 0U; at < element.attributes.size() && at < location.attributes.size(); ++at) {
        if (element.attributes[at].name == name) return location.attributes[at].value_offset;
    }
    return element_offset(index);
}

ControlSource ModelBuilder::source_of(const std::uint32_t index) const noexcept {
    ControlSource result{};
    result.element = index;
    result.position = position_at(element_offset(index));
    return result;
}

void ModelBuilder::report(const DiagnosticCode code, const std::size_t offset, const std::string_view rule,
                          const std::string_view property, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = position_at(offset);
    diagnostic.rule = std::string(rule);
    diagnostic.property = std::string(property);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
}

void ModelBuilder::flatten() {
    // Pre-order with an explicit stack of (element, parent, next child),
    // so a deep document does not recurse.
    struct Frame final {
        const XmlElement* element{};
        std::uint32_t index{};
        std::size_t next_child{};
    };
    flat_.clear();
    std::vector<Frame> stack{};
    flat_.push_back(FlatElement{&syntax_.root, std::nullopt});
    stack.push_back(Frame{&syntax_.root, 0U, 0U});
    while (!stack.empty()) {
        Frame& top = stack.back();
        if (top.next_child >= (*top.element).children.size()) {
            stack.pop_back();
            continue;
        }
        const XmlElement* const child = &(*top.element).children[top.next_child];
        ++top.next_child;
        const std::uint32_t index = static_cast<std::uint32_t>(flat_.size());
        flat_.push_back(FlatElement{child, top.index});
        stack.push_back(Frame{child, index, 0U});
    }
}

std::uint32_t ModelBuilder::index_of(const XmlElement& element) const noexcept {
    for (std::uint32_t index = 0U; index < flat_.size(); ++index) {
        if (flat_[index].element == &element) return index;
    }
    return 0U;
}

std::string_view ModelBuilder::attribute(const std::uint32_t index, const std::string_view name) const noexcept {
    const XmlAttribute* const found = (*flat_[index].element).find_attribute(name);
    if (found == nullptr) return {};
    return (*found).value;
}

bool ModelBuilder::has_attribute(const std::uint32_t index, const std::string_view name) const noexcept {
    const bool present = (*flat_[index].element).find_attribute(name) != nullptr;
    return present;
}

bool ModelBuilder::flag(const std::uint32_t index, const std::string_view name, const std::string_view rule,
                        const bool fallback) {
    if (!has_attribute(index, name)) return fallback;
    const std::string_view value = attribute(index, name);
    if (value == "yes") return true;
    if (value == "no") return false;
    report(DiagnosticCode::control_value_invalid, value_offset(index, name), rule, name,
           "'" + std::string(value) + "' is not yes or no");
    return fallback;
}

void ModelBuilder::check_attributes(const std::uint32_t index, const ElementShape& shape,
                                    const std::string_view rule) {
    const XmlElement& element = *flat_[index].element;
    for (const XmlAttribute& written : element.attributes) {
        if (written.name == form_language::form_namespace_attribute || written.name.starts_with("xmlns:")) {
            if (index != 0U) {
                report(DiagnosticCode::control_xml_invalid, attribute_offset(index, written.name), rule, written.name,
                       "a namespace is declared only on the root");
            }
            continue;
        }
        bool admitted = false;
        for (const std::string_view name : shape.admitted) admitted = admitted || name == written.name;
        if (!admitted) {
            report(DiagnosticCode::control_attribute_unknown, attribute_offset(index, written.name), rule,
                   written.name, "<" + std::string(shape.element) + "> has no attribute " + written.name);
        }
    }
    for (const std::string_view name : shape.required) {
        if (!has_attribute(index, name)) {
            report(DiagnosticCode::control_attribute_missing, element_offset(index), rule, name,
                   "<" + std::string(shape.element) + "> needs " + std::string(name));
        }
    }
}

void ModelBuilder::check_text(ControlModel&) {
    for (std::uint32_t index = 0U; index < flat_.size(); ++index) {
        const XmlElement& element = *flat_[index].element;
        if (blank(element.text)) continue;
        if (element.name == "look" || element.name == "description") continue;
        report(DiagnosticCode::control_xml_invalid, element_offset(index), element.name, {},
               "<" + element.name + "> holds text");
    }
}

std::optional<std::size_t> ModelBuilder::schema_type(const std::string_view snake_name) const noexcept {
    for (std::size_t index = 0U; index < schema_.types.size(); ++index) {
        if (schema_.types[index].name == snake_name) return index;
    }
    return std::nullopt;
}

void ModelBuilder::read_root(ControlModel& scratch) {
    const XmlElement& root = syntax_.root;
    scratch.source = source_of(0U);
    if (root.name != "control") {
        report(DiagnosticCode::control_root_invalid, element_offset(0U), root.name, {},
               "the root is <" + root.name + ">, not <control>");
        return;
    }
    const XmlAttribute* const declared = root.find_attribute(form_language::form_namespace_attribute);
    if (declared == nullptr || (*declared).value != control_namespace) {
        report(DiagnosticCode::control_xml_invalid, element_offset(0U), "control", "xmlns",
               "the root declares xmlns=\"" + std::string(control_namespace) + "\"");
    }
    for (const XmlAttribute& written : root.attributes) {
        if (written.name.starts_with("xmlns:")) {
            report(DiagnosticCode::control_xml_invalid, attribute_offset(0U, written.name), "control", written.name,
                   "no prefixed namespace is admitted");
        }
    }
    const ElementShape shape{"control", {"name", "version", "base", "family"}, {"name", "version"}};
    check_attributes(0U, shape, "control");
    const std::string_view name = attribute(0U, "name");
    scratch.name = std::string(name);
    if (has_attribute(0U, "name")) {
        if (!kebab_name(name)) {
            report(DiagnosticCode::control_root_invalid, value_offset(0U, "name"), "control", "name",
                   "'" + std::string(name) + "' is not a kebab-case name");
        } else if (schema_type(snake_of(name)).has_value()) {
            report(DiagnosticCode::control_root_invalid, value_offset(0U, "name"), "control", "name",
                   "a type named " + std::string(name) + " is already registered");
        }
    }
    if (has_attribute(0U, "version")) {
        const std::string_view version = attribute(0U, "version");
        const std::size_t dot = version.find('.');
        bool valid = dot != std::string_view::npos && dot > 0U && dot + 1U < version.size();
        if (valid) {
            const std::string_view major = version.substr(0U, dot);
            const std::string_view minor = version.substr(dot + 1U);
            valid = std::all_of(major.begin(), major.end(), digit) && std::all_of(minor.begin(), minor.end(), digit) &&
                    major.size() <= 6U && minor.size() <= 6U;
            if (valid) {
                scratch.version_major = static_cast<std::uint32_t>(std::strtoul(std::string(major).c_str(), nullptr, 10));
                scratch.version_minor = static_cast<std::uint32_t>(std::strtoul(std::string(minor).c_str(), nullptr, 10));
            }
        }
        if (!valid) {
            report(DiagnosticCode::control_root_invalid, value_offset(0U, "version"), "control", "version",
                   "'" + std::string(version) + "' is not <major>.<minor>");
        }
    }
    const std::string_view base = has_attribute(0U, "base") ? attribute(0U, "base") : std::string_view("user-control");
    if (base == "user-control") {
        scratch.base = CompositeBase::user_control;
    } else if (base == "scrollable-control") {
        scratch.base = CompositeBase::scrollable_control;
    } else {
        report(DiagnosticCode::control_vocabulary_unknown, value_offset(0U, "base"), "control", "base",
               "base '" + std::string(base) + "' (user-control or scrollable-control)");
    }
    base_type_ = schema_type(snake_of(base));
    if (has_attribute(0U, "family")) {
        const std::string_view family = attribute(0U, "family");
        scratch.family = family_of(family);
        if (!scratch.family) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(0U, "family"), "control", "family",
                   "family '" + std::string(family) + "'");
        }
    }
}

void ModelBuilder::read_declarations(ControlModel& scratch) {
    bool children_seen = false;
    for (const XmlElement& child : syntax_.root.children) {
        const std::uint32_t index = index_of(child);
        const std::string& name = child.name;
        if (name == "property") {
            read_property(index, scratch);
        } else if (name == "event") {
            read_event(index, scratch);
        } else if (name == "command") {
            read_command(index, scratch);
        } else if (name == "publish") {
            read_state(index, false, scratch);
        } else if (name == "state") {
            read_state(index, true, scratch);
        } else if (name == "part") {
            read_part(index, scratch);
        } else if (name == "children") {
            if (children_seen) {
                report(DiagnosticCode::control_element_unknown, element_offset(index), "children", {},
                       "a document has one <children>");
                continue;
            }
            children_seen = true;
            read_children(index, scratch);
        } else if (name == "focus") {
            if (scratch.focus) {
                report(DiagnosticCode::control_element_unknown, element_offset(index), "focus", {},
                       "a document has one <focus>");
                continue;
            }
            read_focus(index, scratch);
        } else if (name == "key") {
            read_rule(index, true, scratch);
        } else if (name == "on") {
            read_rule(index, false, scratch);
        } else if (name == "semantics") {
            if (scratch.semantics) {
                report(DiagnosticCode::control_element_unknown, element_offset(index), "semantics", {},
                       "a document has one <semantics>");
                continue;
            }
            read_semantics(index, scratch);
        } else if (name == "look") {
            if (!scratch.look.empty()) {
                report(DiagnosticCode::control_element_unknown, element_offset(index), "look", {},
                       "a document has one <look>");
                continue;
            }
            read_look(index, scratch);
        } else if (name == "style") {
            report(DiagnosticCode::control_style_outside_look, element_offset(index), "style", {},
                   "style belongs in <look>");
        } else {
            report(DiagnosticCode::control_element_unknown, element_offset(index), name, {},
                   "<control> has no element <" + name + ">");
        }
    }
}

void ModelBuilder::read_property(const std::uint32_t index, ControlModel& scratch) {
    const ElementShape shape{"property",
                             {"name", "kind", "default", "range", "bindable", "category", "changed", "affects",
                              "localizable", "presentational", "alias"},
                             {"name"}};
    const std::string_view name = attribute(index, "name");
    check_attributes(index, shape, name);
    ControlProperty property{};
    property.name = std::string(name);
    property.source = source_of(index);
    if (has_attribute(index, "name") && !kebab_name(name)) {
        report(DiagnosticCode::control_value_invalid, value_offset(index, "name"), name, "name",
               "'" + std::string(name) + "' is not a kebab-case name");
    }
    property.alias = std::string(attribute(index, "alias"));
    if (has_attribute(index, "kind")) {
        const std::string_view kind = attribute(index, "kind");
        property.kind = kind_of(kind);
        if (!property.kind) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "kind"), name, "kind",
                   "kind '" + std::string(kind) + "'");
        }
        if (!property.alias.empty()) {
            report(DiagnosticCode::control_attribute_unknown, attribute_offset(index, "kind"), name, "kind",
                   "an alias takes its target's kind");
        }
    } else if (property.alias.empty()) {
        report(DiagnosticCode::control_attribute_missing, element_offset(index), name, "kind",
               "<property> needs kind (or alias)");
    }
    property.default_value = std::string(attribute(index, "default"));
    property.range = std::string(attribute(index, "range"));
    property.category = has_attribute(index, "category") ? std::string(attribute(index, "category")) : "Behavior";
    property.changed = std::string(attribute(index, "changed"));
    if (has_attribute(index, "bindable")) {
        const std::string_view bindable = attribute(index, "bindable");
        if (bindable == "no") {
            property.bindable = ControlBindable::no;
        } else if (bindable == "one-way") {
            property.bindable = ControlBindable::one_way;
        } else if (bindable == "two-way") {
            property.bindable = ControlBindable::two_way;
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "bindable"), name, "bindable",
                   "bindable '" + std::string(bindable) + "' (no, one-way or two-way)");
        }
    }
    for (const std::string_view word : words_of(attribute(index, "affects"))) {
        bool known = false;
        for (const AffectsWord& affects : affects_words) {
            if (affects.word != word) continue;
            known = true;
            property.affects |= affects.dirty;
        }
        if (!known) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "affects"), name, "affects",
                   "affects '" + std::string(word) + "'");
        }
    }
    property.localizable = flag(index, "localizable", name, false);
    property.presentational = flag(index, "presentational", name, false);
    if (property.presentational && (property.bindable != ControlBindable::no || !property.changed.empty())) {
        report(DiagnosticCode::control_value_invalid, element_offset(index), name, "presentational",
               "a presentational property raises no event and takes no binding");
    }
    // Its own elements: a description and, for an enumeration, choices.
    for (const XmlElement& child : (*flat_[index].element).children) {
        const std::uint32_t child_index = index_of(child);
        if (child.name == "description") {
            check_attributes(child_index, ElementShape{"description", {}, {}}, name);
            property.description = child.text;
        } else if (child.name == "choice") {
            check_attributes(child_index, ElementShape{"choice", {"name"}, {"name"}}, name);
            const std::string_view choice = attribute(child_index, "name");
            if (!kebab_name(choice)) {
                report(DiagnosticCode::control_value_invalid, value_offset(child_index, "name"), name, "name",
                       "'" + std::string(choice) + "' is not a kebab-case name");
            }
            if (std::find(property.choices.begin(), property.choices.end(), choice) != property.choices.end()) {
                report(DiagnosticCode::control_name_repeated, element_offset(child_index), name, "name",
                       "choice " + std::string(choice) + " is written twice");
            }
            property.choices.emplace_back(choice);
        } else if (child.name == "style") {
            report(DiagnosticCode::control_style_outside_look, element_offset(child_index), name, {},
                   "style belongs in <look>");
        } else {
            report(DiagnosticCode::control_element_unknown, element_offset(child_index), name, {},
                   "<property> has no element <" + child.name + ">");
        }
    }
    if (property.kind == ControlPropertyKind::enumeration && property.choices.empty()) {
        report(DiagnosticCode::control_value_invalid, element_offset(index), name, "kind",
               "an enumeration lists its <choice> elements");
    }
    if (property.kind != ControlPropertyKind::enumeration && !property.choices.empty()) {
        report(DiagnosticCode::control_element_unknown, element_offset(index), name, {},
               "only an enumeration has <choice> elements");
    }
    const bool colour_or_font = property.kind == ControlPropertyKind::color || property.kind == ControlPropertyKind::font;
    if (colour_or_font && !(property.kind == ControlPropertyKind::color && listed(admitted_colour_properties, name))) {
        report(DiagnosticCode::control_presentation_property, value_offset(index, "kind"), name, "kind",
               "a look belongs to the theme; only authored-background, authored-foreground and content-color are "
               "admitted");
    }
    // The default, read by the property's kind as a form writes it.
    if (has_attribute(index, "default") && property.kind) {
        const std::string_view value = property.default_value;
        const std::vector<std::string_view> numbers = words_of(value);
        bool numeric = true;
        for (const std::string_view word : numbers) numeric = numeric && number_of(word).has_value();
        bool valid = true;
        switch (*property.kind) {
        case ControlPropertyKind::boolean: valid = value == "true" || value == "false"; break;
        case ControlPropertyKind::number:
        case ControlPropertyKind::signed_integer:
        case ControlPropertyKind::unsigned_integer: {
            valid = numbers.size() == 1U && numeric;
            if (valid && *property.kind != ControlPropertyKind::number) {
                const bool signed_kind = *property.kind == ControlPropertyKind::signed_integer;
                valid = integer_default_valid(numbers[0], *number_of(numbers[0]), signed_kind);
            }
            break;
        }
        case ControlPropertyKind::text: valid = true; break;
        case ControlPropertyKind::enumeration:
            valid = std::find(property.choices.begin(), property.choices.end(), value) != property.choices.end();
            break;
        case ControlPropertyKind::size:
        case ControlPropertyKind::point: valid = numbers.size() == 2U && numeric; break;
        case ControlPropertyKind::rectangle: valid = numbers.size() == 4U && numeric; break;
        case ControlPropertyKind::insets:
            valid = (numbers.size() == 1U || numbers.size() == 2U || numbers.size() == 4U) && numeric;
            break;
        case ControlPropertyKind::color: {
            const std::size_t digits = value.size() - 1U;
            valid = value.size() > 1U && value.front() == '#' &&
                    (digits == 3U || digits == 4U || digits == 6U || digits == 8U) &&
                    std::all_of(value.begin() + 1, value.end(), hex_digit);
            break;
        }
        case ControlPropertyKind::font:
        case ControlPropertyKind::image:
        case ControlPropertyKind::command:
        // An out-of-range kind admits no default value, as these kinds do not.
        default: valid = false; break;
        }
        if (!valid) {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "default"), name, "default",
                   "'" + std::string(value) + "' is not a default of kind " +
                       std::string(control_property_kind_name(*property.kind)));
        }
    }
    scratch.properties.push_back(std::move(property));
}

void ModelBuilder::read_event(const std::uint32_t index, ControlModel& scratch) {
    const std::string_view name = attribute(index, "name");
    check_attributes(index, ElementShape{"event", {"name", "payload"}, {"name"}}, name);
    ControlEvent event{};
    event.name = std::string(name);
    event.source = source_of(index);
    if (has_attribute(index, "name") && !kebab_name(name)) {
        report(DiagnosticCode::control_value_invalid, value_offset(index, "name"), name, "name",
               "'" + std::string(name) + "' is not a kebab-case name");
    }
    if (has_attribute(index, "payload")) {
        const std::string_view payload = attribute(index, "payload");
        event.payload = kind_of(payload);
        if (!event.payload) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "payload"), name, "payload",
                   "kind '" + std::string(payload) + "'");
        }
    }
    for (const XmlElement& child : (*flat_[index].element).children) {
        report(DiagnosticCode::control_element_unknown, element_offset(index_of(child)), name, {},
               "<event> has no element <" + child.name + ">");
    }
    scratch.events.push_back(std::move(event));
}

void ModelBuilder::read_command(const std::uint32_t index, ControlModel& scratch) {
    const std::string_view name = attribute(index, "name");
    check_attributes(index, ElementShape{"command", {"name", "enabled", "run", "semantic-action"}, {"name"}}, name);
    ControlCommand command{};
    command.name = std::string(name);
    command.enabled = std::string(attribute(index, "enabled"));
    command.run = std::string(attribute(index, "run"));
    command.source = source_of(index);
    if (has_attribute(index, "name") && !kebab_name(name)) {
        report(DiagnosticCode::control_value_invalid, value_offset(index, "name"), name, "name",
               "'" + std::string(name) + "' is not a kebab-case name");
    }
    if (has_attribute(index, "semantic-action")) {
        const std::string_view action = attribute(index, "semantic-action");
        command.semantic_action = semantic_action_of(action);
        if (!command.semantic_action) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "semantic-action"), name,
                   "semantic-action", "semantic action '" + std::string(action) + "'");
        }
    }
    for (const XmlElement& child : (*flat_[index].element).children) {
        report(DiagnosticCode::control_element_unknown, element_offset(index_of(child)), name, {},
               "<command> has no element <" + child.name + ">");
    }
    scratch.commands.push_back(std::move(command));
}

void ModelBuilder::read_state(const std::uint32_t index, const bool custom, ControlModel& scratch) {
    ControlState state{};
    state.custom = custom;
    state.source = source_of(index);
    state.when = std::string(attribute(index, "when"));
    if (custom) {
        const std::string_view name = attribute(index, "name");
        check_attributes(index, ElementShape{"state", {"name", "when"}, {"name", "when"}}, name);
        state.name = std::string(name);
        if (has_attribute(index, "name") && !kebab_name(name)) {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "name"), name, "name",
                   "'" + std::string(name) + "' is not a kebab-case name");
        }
        if (anatomy_state_from_name(snake_of(name)).present()) {
            report(DiagnosticCode::control_state_shadows_standard, value_offset(index, "name"), name, "name",
                   std::string(name) + " is a standard state: publish it with <publish>");
        }
    } else {
        const std::string_view name = attribute(index, "state");
        check_attributes(index, ElementShape{"publish", {"state", "when"}, {"state", "when"}}, name);
        state.name = std::string(name);
        state.standard = platform::standard_optional(anatomy_state_from_name(snake_of(name)));
        if (has_attribute(index, "state") && !state.standard) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "state"), name, "state",
                   "standard state '" + std::string(name) + "'");
        }
    }
    for (const XmlElement& child : (*flat_[index].element).children) {
        report(DiagnosticCode::control_element_unknown, element_offset(index_of(child)), state.name, {},
               "<" + (*flat_[index].element).name + "> has no element <" + child.name + ">");
    }
    scratch.states.push_back(std::move(state));
}

void ModelBuilder::read_part(const std::uint32_t index, ControlModel& scratch) {
    const ElementShape shape{"part",
                             {"name", "role", "composed-from", "states", "shape", "corner-radius", "box", "present",
                              "hit", "press", "activates", "disabled-when", "mark", "paint"},
                             {"name", "role"}};
    const std::string_view name = attribute(index, "name");
    check_attributes(index, shape, name);
    ControlPart part{};
    part.name = std::string(name);
    part.source = source_of(index);
    if (has_attribute(index, "name") && !kebab_name(name)) {
        report(DiagnosticCode::control_value_invalid, value_offset(index, "name"), name, "name",
               "'" + std::string(name) + "' is not a kebab-case name");
    }
    if (has_attribute(index, "role")) {
        const std::string_view role = attribute(index, "role");
        const stxlib::Optional<PartRole> known = part_role_from_name(snake_of(role));
        if (known.present()) {
            part.role = *known.value();
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "role"), name, "role",
                   "part role '" + std::string(role) + "'");
        }
    }
    part.composed_from = std::string(attribute(index, "composed-from"));
    if (!part.composed_from.empty() && !family_of(part.composed_from)) {
        report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "composed-from"), name,
               "composed-from", "family '" + part.composed_from + "'");
    }
    for (const std::string_view word : words_of(attribute(index, "states"))) {
        const stxlib::Optional<AnatomyState> state = anatomy_state_from_name(snake_of(word));
        if (state.present()) {
            part.states.push_back(*state.value());
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "states"), name, "states",
                   "standard state '" + std::string(word) + "'");
        }
    }
    if (has_attribute(index, "shape")) {
        const std::string_view written = attribute(index, "shape");
        if (written == "rounded-rectangle") {
            part.shape = ControlPartShape::rounded_rectangle;
        } else if (written == "pill") {
            part.shape = ControlPartShape::pill;
        } else if (written == "circle") {
            part.shape = ControlPartShape::circle;
        } else if (written == "cap-start") {
            part.shape = ControlPartShape::cap_start;
        } else if (written == "cap-end") {
            part.shape = ControlPartShape::cap_end;
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "shape"), name, "shape",
                   "shape '" + std::string(written) + "'");
        }
    }
    if (has_attribute(index, "corner-radius")) {
        const std::vector<std::string_view> radii = words_of(attribute(index, "corner-radius"));
        bool valid = radii.size() == 1U || radii.size() == 4U;
        for (const std::string_view radius : radii) {
            const std::optional<double> value = number_of(radius);
            valid = valid && value.has_value() && *value >= 0.0;
            if (value) part.corner_radius.push_back(*value);
        }
        if (!valid) {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "corner-radius"), name, "corner-radius",
                   "a corner radius is one or four numbers, none negative");
        }
    }
    part.box = std::string(attribute(index, "box"));
    part.present = std::string(attribute(index, "present"));
    part.activates = std::string(attribute(index, "activates"));
    part.disabled_when = std::string(attribute(index, "disabled-when"));
    for (const std::string_view word : words_of(attribute(index, "press"))) {
        ControlPress bit = ControlPress::none;
        if (word == "click") bit = ControlPress::click;
        if (word == "down") bit = ControlPress::down;
        if (word == "repeat") bit = ControlPress::repeat;
        if (word == "drag") bit = ControlPress::drag;
        if (bit == ControlPress::none) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "press"), name, "press",
                   "press '" + std::string(word) + "' (click, down, repeat or drag)");
            continue;
        }
        part.press = static_cast<std::uint8_t>(part.press | static_cast<std::uint8_t>(bit));
    }
    const std::size_t press_words = words_of(attribute(index, "press")).size();
    const bool click_drag = part.press == (static_cast<std::uint8_t>(ControlPress::click) |
                                           static_cast<std::uint8_t>(ControlPress::drag));
    if (press_words > 2U || (press_words == 2U && !click_drag)) {
        report(DiagnosticCode::control_value_invalid, value_offset(index, "press"), name, "press",
               "press is one of click, down, repeat and drag, or click drag");
    }
    // The hit region: the shape by default for a part that presses.
    part.hit = part.press != 0U ? ControlPartHit::shape : ControlPartHit::none;
    if (has_attribute(index, "hit")) {
        const std::string_view hit = attribute(index, "hit");
        if (hit == "shape") {
            part.hit = ControlPartHit::shape;
        } else if (hit == "box") {
            part.hit = ControlPartHit::box;
        } else if (hit == "none") {
            part.hit = ControlPartHit::none;
        } else if (hit.starts_with("expand(") && hit.ends_with(")")) {
            const std::optional<double> length = number_of(hit.substr(7U, hit.size() - 8U));
            part.hit = ControlPartHit::expand;
            if (length && *length >= 0.0) {
                part.hit_expand = *length;
            } else {
                report(DiagnosticCode::control_value_invalid, value_offset(index, "hit"), name, "hit",
                       "expand() takes one length, not negative");
            }
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "hit"), name, "hit",
                   "hit '" + std::string(hit) + "' (shape, box, expand(<length>) or none)");
        }
    }
    if (has_attribute(index, "mark")) {
        const std::string_view mark = attribute(index, "mark");
        const std::optional<PartMark> known = mark_of(mark);
        if (known) {
            part.mark = *known;
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "mark"), name, "mark",
                   "mark '" + std::string(mark) + "'");
        }
    }
    if (has_attribute(index, "paint")) {
        const std::string_view paint = attribute(index, "paint");
        if (paint == "hook") {
            part.paint_hook = true;
        } else if (paint != "theme") {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "paint"), name, "paint",
                   "paint '" + std::string(paint) + "' (theme or hook)");
        }
        // The region and the focus cue are the composite's whole box and its
        // focus presentation, never a typed class's drawing.
        if (part.paint_hook && (part.role == PartRole::region || part.role == PartRole::focus)) {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "paint"), name, "paint",
                   "a region or focus part is painted by the theme");
        }
    }
    const std::size_t part_position = scratch.parts.size();
    scratch.parts.push_back(std::move(part));
    for (const XmlElement& child : (*flat_[index].element).children) {
        const std::uint32_t child_index = index_of(child);
        if (child.name == "drag" && !scratch.parts[part_position].drag) {
            scratch.parts[part_position].drag = scratch.drags.size();
            read_drag(child_index, name, scratch);
        } else if (child.name == "style") {
            report(DiagnosticCode::control_style_outside_look, element_offset(child_index), name, {},
                   "style belongs in <look>");
        } else {
            report(DiagnosticCode::control_element_unknown, element_offset(child_index), name, {},
                   child.name == "drag" ? std::string("a part holds one <drag>")
                                        : "<part> has no element <" + child.name + ">");
        }
    }
}

void ModelBuilder::read_drag(const std::uint32_t index, const std::string_view part, ControlModel& scratch) {
    const ElementShape shape{"drag",
                             {"kind", "mode", "axis", "range", "maps-to", "grid", "targets", "slots", "threshold",
                              "settles", "keyboard", "preview"},
                             {"kind", "mode", "maps-to"}};
    check_attributes(index, shape, part);
    ControlDrag drag{};
    drag.source = source_of(index);
    const std::string_view kind = attribute(index, "kind");
    if (kind == "placement") {
        drag.kind = ControlDragKind::placement;
    } else if (kind == "reorder") {
        drag.kind = ControlDragKind::reorder;
    } else if (kind == "value" || !has_attribute(index, "kind")) {
        drag.kind = ControlDragKind::value;
    } else {
        report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "kind"), part, "kind",
               "drag kind '" + std::string(kind) + "' (value, placement or reorder)");
    }
    const std::string_view mode = attribute(index, "mode");
    if (mode == "live") {
        drag.mode = ControlDragMode::live;
    } else if (mode == "commit") {
        drag.mode = ControlDragMode::commit;
    } else if (has_attribute(index, "mode")) {
        report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "mode"), part, "mode",
               "drag mode '" + std::string(mode) + "' (live or commit)");
    }
    if (has_attribute(index, "axis")) {
        const std::string_view axis = attribute(index, "axis");
        const bool placement = drag.kind == ControlDragKind::placement;
        if (axis == "left" || axis == "right") {
            report(DiagnosticCode::control_physical_direction, value_offset(index, "axis"), part, "axis",
                   "an axis is inline or block, which mirror under right-to-left");
        } else if (axis == "inline") {
            drag.axis = ControlDragAxis::inline_axis;
        } else if (axis == "block") {
            drag.axis = ControlDragAxis::block_axis;
        } else if (axis == "free" && placement) {
            drag.axis = ControlDragAxis::free;
        } else if (axis == "dominant" && placement) {
            drag.axis = ControlDragAxis::dominant;
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "axis"), part, "axis",
                   "axis '" + std::string(axis) + "' for a " + std::string(kind) + " drag");
        }
    }
    drag.range = std::string(attribute(index, "range"));
    drag.maps_to = std::string(attribute(index, "maps-to"));
    drag.grid = std::string(attribute(index, "grid"));
    drag.targets = std::string(attribute(index, "targets"));
    drag.slots = std::string(attribute(index, "slots"));
    drag.preview = std::string(attribute(index, "preview"));
    if (has_attribute(index, "threshold")) {
        const std::optional<double> threshold = number_of(attribute(index, "threshold"));
        if (threshold && *threshold >= 0.0) {
            drag.threshold = *threshold;
        } else {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "threshold"), part, "threshold",
                   "a threshold is one length, not negative");
        }
    }
    drag.settles = flag(index, "settles", part, false);
    const std::string_view keyboard = attribute(index, "keyboard");
    if (has_attribute(index, "keyboard") && keyboard != "none") {
        if (key_gesture(keyboard)) {
            drag.keyboard = std::string(keyboard);
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "keyboard"), part, "keyboard",
                   "key gesture '" + std::string(keyboard) + "'");
        }
    }
    for (const XmlElement& child : (*flat_[index].element).children) {
        report(DiagnosticCode::control_element_unknown, element_offset(index_of(child)), part, {},
               "<drag> has no element <" + child.name + ">");
    }
    scratch.drags.push_back(std::move(drag));
}

void ModelBuilder::read_children(const std::uint32_t index, ControlModel& scratch) {
    check_attributes(index, ElementShape{"children", {}, {}}, "children");
    for (const XmlElement& child : (*flat_[index].element).children) {
        read_child(index_of(child), std::nullopt, base_type_, scratch);
    }
}

void ModelBuilder::read_child(const std::uint32_t index, const std::optional<std::size_t> parent_child,
                              const std::optional<std::size_t> parent_type, ControlModel& scratch) {
    // Children are walked with an explicit stack: a form's nesting is not
    // bounded by the composite language.
    struct Pending final {
        std::uint32_t index{};
        std::optional<std::size_t> parent_child{};
        std::optional<std::size_t> parent_type{};
    };
    std::vector<Pending> pending{};
    pending.push_back(Pending{index, parent_child, parent_type});
    std::size_t slots = 0U;
    for (const ControlChild& existing : scratch.children) slots += existing.slot ? 1U : 0U;
    while (!pending.empty()) {
        const Pending item = pending.back();
        pending.pop_back();
        const XmlElement& element = *flat_[item.index].element;
        const std::string& written_type = element.name;
        if (written_type == "style") {
            report(DiagnosticCode::control_style_outside_look, element_offset(item.index), "style", {},
                   "style belongs in <look>");
            continue;
        }
        if (written_type == span_element) {
            const bool holds = item.parent_type && schema_.types[*item.parent_type].holds_spans;
            if (!holds) {
                report(DiagnosticCode::control_child_invalid, element_offset(item.index), "span", {},
                       "a span is written directly inside a type that holds spans");
            }
            check_attributes(item.index, ElementShape{"span", {"name", "anchor", "rows", "columns"}, {"anchor"}},
                             "span");
            continue;
        }
        ControlChild child{};
        child.parent = item.parent_child;
        child.source = source_of(item.index);
        const std::string_view id = attribute(item.index, form_language::form_identity_attribute);
        const bool local = id.size() > 1U && id.front() == '@' && kebab_name(id.substr(1U));
        if (!local) {
            report(DiagnosticCode::control_child_invalid, has_attribute(item.index, "id") ?
                       value_offset(item.index, "id") : element_offset(item.index),
                   written_type, "id",
                   has_attribute(item.index, "id") ? "'" + std::string(id) + "' is not a local ID (@name)"
                                                   : std::string("a child needs a local ID (id=\"@name\")"));
        } else {
            child.local_id = std::string(id.substr(1U));
        }
        if (written_type == "slot") {
            child.slot = true;
            ++slots;
            if (slots > 1U) {
                report(DiagnosticCode::control_child_invalid, element_offset(item.index), "slot", {},
                       "a composite has at most one <slot>");
            }
            if (item.parent_child) {
                report(DiagnosticCode::control_child_invalid, element_offset(item.index), "slot", {},
                       "the slot is a direct child of <children>");
            }
        } else {
            child.schema_type = schema_type(snake_of(written_type));
            if (!child.schema_type || !kebab_name(written_type)) {
                child.schema_type.reset();
                report(DiagnosticCode::control_child_invalid, element_offset(item.index), written_type, {},
                       "the schema has no type " + written_type);
            }
        }
        if (item.parent_type && !schema_.types[*item.parent_type].holds_children && item.parent_child) {
            report(DiagnosticCode::control_child_invalid, element_offset(item.index), written_type, {},
                   "its parent's type holds no children");
        }
        // The attributes: properties of the child's type (the base type's
        // for the slot), or properties its parent provides.
        const std::optional<std::size_t> own_type = child.slot ? base_type_ : child.schema_type;
        for (const XmlAttribute& written : element.attributes) {
            if (written.name == form_language::form_identity_attribute) continue;
            if (written.name == form_language::form_namespace_attribute || written.name.starts_with("xmlns:")) {
                report(DiagnosticCode::control_xml_invalid, attribute_offset(item.index, written.name), written_type,
                       written.name, "a namespace is declared only on the root");
                continue;
            }
            ControlChildAttribute value{};
            value.name = snake_of(written.name);
            value.value = written.value;
            value.bound = written.value.starts_with("bind(");
            const PropertyDescriptor* descriptor = nullptr;
            if (own_type) {
                for (const PropertyDescriptor& candidate : schema_.types[*own_type].properties) {
                    if (candidate.name == value.name) descriptor = &candidate;
                }
            }
            if (descriptor == nullptr && item.parent_type) {
                for (const PropertyDescriptor& candidate : schema_.types[*item.parent_type].child_properties) {
                    if (candidate.name == value.name) descriptor = &candidate;
                }
            }
            if (!kebab_name(written.name)) descriptor = nullptr;
            if (descriptor == nullptr) {
                if (own_type || child.slot) {
                    report(DiagnosticCode::control_child_invalid, attribute_offset(item.index, written.name),
                           written_type, written.name, written_type + " has no property " + written.name);
                }
            } else {
                const bool presentation = ((*descriptor).kind == BindingValueKind::color ||
                                           (*descriptor).kind == BindingValueKind::font) &&
                                          !(*descriptor).content_value;
                if (presentation) {
                    report(DiagnosticCode::control_presentation_property, attribute_offset(item.index, written.name),
                           written_type, written.name, "a look belongs to the theme, reached by classes");
                }
                if (!(*descriptor).writable) {
                    report(DiagnosticCode::control_child_invalid, attribute_offset(item.index, written.name),
                           written_type, written.name, written.name + " cannot be set by a document");
                }
            }
            child.attributes.push_back(std::move(value));
        }
        const std::size_t position = scratch.children.size();
        scratch.children.push_back(std::move(child));
        // Its own children, kept in document order on the stack.
        for (std::size_t at = element.children.size(); at > 0U; --at) {
            const XmlElement& nested = element.children[at - 1U];
            pending.push_back(Pending{index_of(nested), position, own_type});
        }
    }
}

void ModelBuilder::read_focus(const std::uint32_t index, ControlModel& scratch) {
    const ElementShape shape{"focus", {"target", "tab-stop", "visible-cue", "roving", "activation", "mnemonic"},
                             {"target"}};
    check_attributes(index, shape, "focus");
    ControlFocus focus{};
    focus.source = source_of(index);
    const std::string_view target = attribute(index, "target");
    if (target == "self") {
        focus.target = ControlFocusTarget::self;
        focus.tab_stop = true;
    } else if (target == "none" || !has_attribute(index, "target")) {
        focus.target = ControlFocusTarget::none;
    } else if (target.starts_with("child(@") && target.ends_with(")")) {
        focus.target = ControlFocusTarget::child;
        focus.child = std::string(target.substr(7U, target.size() - 8U));
    } else {
        report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "target"), "focus", "target",
               "focus target '" + std::string(target) + "' (self, child(@name) or none)");
    }
    focus.tab_stop = flag(index, "tab-stop", "focus", focus.tab_stop);
    const std::string_view cue = attribute(index, "visible-cue");
    if (has_attribute(index, "visible-cue")) {
        if (cue.starts_with("part(") && cue.ends_with(")")) {
            focus.visible_cue = std::string(cue.substr(5U, cue.size() - 6U));
        } else {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "visible-cue"), "focus", "visible-cue",
                   "a cue is part(<name>)");
        }
    }
    if (has_attribute(index, "roving")) {
        const std::string_view roving = attribute(index, "roving");
        focus.roving = roving == "arrows";
        if (!focus.roving && roving != "none") {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "roving"), "focus", "roving",
                   "roving '" + std::string(roving) + "' (arrows or none)");
        }
    }
    if (has_attribute(index, "activation")) {
        const std::string_view activation = attribute(index, "activation");
        if (activation != "standard" && activation != "one-click" && activation != "two-click") {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "activation"), "focus",
                   "activation", "activation '" + std::string(activation) + "'");
        }
        focus.activation = std::string(activation);
    }
    const std::string_view mnemonic = attribute(index, "mnemonic");
    if (has_attribute(index, "mnemonic")) {
        if (mnemonic.starts_with("property(") && mnemonic.ends_with(")")) {
            focus.mnemonic = std::string(mnemonic.substr(9U, mnemonic.size() - 10U));
        } else {
            report(DiagnosticCode::control_value_invalid, value_offset(index, "mnemonic"), "focus", "mnemonic",
                   "a mnemonic is property(<name>)");
        }
    }
    for (const XmlElement& child : (*flat_[index].element).children) {
        report(DiagnosticCode::control_element_unknown, element_offset(index_of(child)), "focus", {},
               "<focus> has no element <" + child.name + ">");
    }
    scratch.focus = std::move(focus);
}

void ModelBuilder::read_rule(const std::uint32_t index, const bool key, ControlModel& scratch) {
    ControlRule rule{};
    rule.key = key;
    rule.source = source_of(index);
    if (key) {
        check_attributes(index, ElementShape{"key", {"gesture", "when", "phase", "claims"}, {"gesture"}}, "key");
        rule.trigger = std::string(attribute(index, "gesture"));
        if (has_attribute(index, "gesture") && !key_gesture(rule.trigger)) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "gesture"), rule.trigger,
                   "gesture", "key gesture '" + rule.trigger + "'");
        }
        const std::string_view phase = attribute(index, "phase");
        // Space and Enter bound to an activating command act after KeyUp
        // by default (ADR-003); the rest on KeyDown.
        const std::string_view pressed = gesture_key(rule.trigger);
        rule.phase = (pressed == "space" || pressed == "enter") ? ControlKeyPhase::key_up : ControlKeyPhase::key_down;
        if (phase == "key-up") {
            rule.phase = ControlKeyPhase::key_up;
        } else if (phase == "key-down") {
            rule.phase = ControlKeyPhase::key_down;
        } else if (has_attribute(index, "phase")) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "phase"), rule.trigger, "phase",
                   "phase '" + std::string(phase) + "' (key-down or key-up)");
        }
        const std::string_view claims = attribute(index, "claims");
        rule.claims_dialog_key = claims == "dialog-key";
        if (has_attribute(index, "claims") && !rule.claims_dialog_key) {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "claims"), rule.trigger, "claims",
                   "claims '" + std::string(claims) + "' (dialog-key)");
        }
    } else {
        check_attributes(index, ElementShape{"on", {"trigger", "when"}, {"trigger"}}, "on");
        rule.trigger = std::string(attribute(index, "trigger"));
    }
    rule.when = std::string(attribute(index, "when"));
    rule.first_action = scratch.actions.size();
    for (const XmlElement& child : (*flat_[index].element).children) {
        const std::uint32_t child_index = index_of(child);
        ControlAction action{};
        action.source = source_of(child_index);
        const std::string& name = child.name;
        std::string_view target_attribute{};
        std::string_view value_attribute{};
        if (name == "set") {
            action.kind = ControlActionKind::set;
            target_attribute = "property";
            value_attribute = "to";
        } else if (name == "toggle") {
            action.kind = ControlActionKind::toggle;
            target_attribute = "property";
        } else if (name == "step") {
            action.kind = ControlActionKind::step;
            target_attribute = "property";
            value_attribute = "by";
        } else if (name == "run") {
            action.kind = ControlActionKind::run;
            target_attribute = "command";
        } else if (name == "raise") {
            action.kind = ControlActionKind::raise;
            target_attribute = "event";
        } else if (name == "focus") {
            action.kind = ControlActionKind::focus;
            target_attribute = "target";
        } else if (name == "invoke") {
            action.kind = ControlActionKind::invoke;
            target_attribute = "property";
        } else if (name == "open") {
            action.kind = ControlActionKind::open;
            target_attribute = "target";
        } else if (name == "close") {
            action.kind = ControlActionKind::close;
            target_attribute = "target";
        } else if (name == "style") {
            report(DiagnosticCode::control_style_outside_look, element_offset(child_index), rule.trigger, {},
                   "style belongs in <look>");
            continue;
        } else {
            report(DiagnosticCode::control_element_unknown, element_offset(child_index), rule.trigger, {},
                   "action <" + name + "> (set, toggle, step, run, raise, focus, invoke, open or close)");
            continue;
        }
        ElementShape shape{"", {}, {}};
        shape.element = name;
        shape.admitted.push_back(target_attribute);
        shape.required.push_back(target_attribute);
        if (!value_attribute.empty()) {
            shape.admitted.push_back(value_attribute);
            shape.required.push_back(value_attribute);
        }
        check_attributes(child_index, shape, rule.trigger);
        action.target = std::string(attribute(child_index, target_attribute));
        if (!value_attribute.empty()) action.value = std::string(attribute(child_index, value_attribute));
        for (const XmlElement& nested : child.children) {
            report(DiagnosticCode::control_element_unknown, element_offset(index_of(nested)), rule.trigger, {},
                   "an action holds no element");
        }
        scratch.actions.push_back(std::move(action));
    }
    rule.action_count = scratch.actions.size() - rule.first_action;
    scratch.rules.push_back(std::move(rule));
}

void ModelBuilder::read_semantics(const std::uint32_t index, ControlModel& scratch) {
    // `role` is required unless `from` hands the node to a child, checked
    // below with the other attributes `from` excludes.
    const ElementShape shape{"semantics",
                             {"role", "name", "value", "range", "states", "actions", "children", "from"},
                             {}};
    check_attributes(index, shape, "semantics");
    ControlSemantics semantics{};
    semantics.source = source_of(index);
    if (has_attribute(index, "from")) {
        const std::string_view from = attribute(index, "from");
        if (from.starts_with("child(@") && from.ends_with(")") && from.size() > 8U) {
            semantics.from_child = std::string(from.substr(7U, from.size() - 8U));
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "from"), "semantics", "from",
                   "semantics source '" + std::string(from) + "' (child(@name))");
        }
        const std::array<std::string_view, 7> own{"role", "name", "value", "range", "states", "actions", "children"};
        for (const std::string_view written : own) {
            if (!has_attribute(index, written)) continue;
            report(DiagnosticCode::control_value_invalid, attribute_offset(index, written), "semantics",
                   written, "a node taken from a child declares no " + std::string(written) + " of its own");
        }
    } else if (!has_attribute(index, "role")) {
        report(DiagnosticCode::control_attribute_missing, element_offset(index), "semantics", "role",
               "<semantics> needs role (or from)");
    }
    const std::string_view role = attribute(index, "role");
    if (has_attribute(index, "role")) {
        const std::optional<SemanticRole> known = semantic_role_of(role);
        if (known) {
            semantics.role = *known;
        } else {
            report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "role"), "semantics", "role",
                   "semantic role '" + std::string(role) + "'");
        }
    }
    semantics.name = std::string(attribute(index, "name"));
    semantics.value = std::string(attribute(index, "value"));
    semantics.range = std::string(attribute(index, "range"));
    semantics.states = std::string(attribute(index, "states"));
    semantics.actions = std::string(attribute(index, "actions"));
    const std::string_view children = attribute(index, "children");
    semantics.children_exposed = children == "exposed";
    if (has_attribute(index, "children") && children != "exposed" && children != "hidden") {
        report(DiagnosticCode::control_vocabulary_unknown, value_offset(index, "children"), "semantics", "children",
               "children '" + std::string(children) + "' (hidden or exposed)");
    }
    for (const XmlElement& child : (*flat_[index].element).children) {
        report(DiagnosticCode::control_element_unknown, element_offset(index_of(child)), "semantics", {},
               "<semantics> has no element <" + child.name + ">");
    }
    scratch.semantics = std::move(semantics);
}

void ModelBuilder::read_look(const std::uint32_t index, ControlModel& scratch) {
    check_attributes(index, ElementShape{"look", {}, {}}, "look");
    const XmlElement& element = *flat_[index].element;
    for (const XmlElement& child : element.children) {
        report(DiagnosticCode::control_xml_invalid, element_offset(index_of(child)), "look", {},
               "<look> holds a theme sheet's text, not elements (write < as &lt;)");
    }
    scratch.look = element.text;
}

// Lookups.

const ControlProperty* ModelBuilder::find_property(const ControlModel& scratch,
                                                   const std::string_view name) const noexcept {
    for (const ControlProperty& property : scratch.properties) {
        if (property.name == name) return &property;
    }
    return nullptr;
}

bool ModelBuilder::base_property(const std::string_view name) const {
    if (!base_type_) return false;
    const std::string snake = snake_of(name);
    for (const PropertyDescriptor& property : schema_.types[*base_type_].properties) {
        if (property.name == snake) return true;
    }
    return false;
}

bool ModelBuilder::any_property(const ControlModel& scratch, const std::string_view name) const {
    const bool found = find_property(scratch, name) != nullptr || base_property(name);
    return found;
}

bool ModelBuilder::part_named(const ControlModel& scratch, const std::string_view name) const noexcept {
    if (listed(base_part_words, name)) return true;
    for (const ControlPart& part : scratch.parts) {
        if (part.name == name) return true;
    }
    return false;
}

bool ModelBuilder::command_named(const ControlModel& scratch, const std::string_view name) const noexcept {
    for (const ControlCommand& command : scratch.commands) {
        if (command.name == name) return true;
    }
    return false;
}

bool ModelBuilder::event_named(const ControlModel& scratch, const std::string_view name) const noexcept {
    for (const ControlEvent& event : scratch.events) {
        if (event.name == name) return true;
    }
    return false;
}

bool ModelBuilder::state_named(const ControlModel& scratch, const std::string_view name) const {
    if (anatomy_state_from_name(snake_of(name)).present()) return true;
    for (const ControlState& state : scratch.states) {
        if (state.name == name) return true;
    }
    return false;
}

const ControlChild* ModelBuilder::child_named(const ControlModel& scratch,
                                              const std::string_view name) const noexcept {
    for (const ControlChild& child : scratch.children) {
        if (child.local_id == name) return &child;
    }
    return nullptr;
}

bool ModelBuilder::child_property(const ControlChild& child, const std::string_view kebab_name) const {
    const std::optional<std::size_t> type = child.slot ? base_type_ : child.schema_type;
    if (!type) return true;
    const std::string snake = snake_of(kebab_name);
    for (const PropertyDescriptor& property : schema_.types[*type].properties) {
        if (property.name == snake) return true;
    }
    return false;
}

// The checks that need every declaration.

void ModelBuilder::check_names(const ControlModel& scratch) {
    struct Named final {
        std::string_view name;
        std::uint32_t element;
    };
    struct Space final {
        std::string_view what;
        std::vector<Named> names;
    };
    std::array<Space, 6> spaces{};
    spaces[0].what = "property";
    spaces[1].what = "event";
    spaces[2].what = "command";
    spaces[3].what = "state";
    spaces[4].what = "part";
    spaces[5].what = "child";
    for (const ControlProperty& property : scratch.properties) {
        spaces[0].names.push_back(Named{property.name, property.source.element});
    }
    for (const ControlEvent& event : scratch.events) spaces[1].names.push_back(Named{event.name, event.source.element});
    for (const ControlCommand& command : scratch.commands) {
        spaces[2].names.push_back(Named{command.name, command.source.element});
    }
    for (const ControlState& state : scratch.states) spaces[3].names.push_back(Named{state.name, state.source.element});
    for (const std::string_view base : base_part_words) spaces[4].names.push_back(Named{base, 0U});
    for (const ControlPart& part : scratch.parts) spaces[4].names.push_back(Named{part.name, part.source.element});
    for (const ControlChild& child : scratch.children) {
        if (!child.local_id.empty()) spaces[5].names.push_back(Named{child.local_id, child.source.element});
    }
    for (const Space& space : spaces) {
        for (std::size_t at = 1U; at < space.names.size(); ++at) {
            for (std::size_t before = 0U; before < at; ++before) {
                if (space.names[before].name != space.names[at].name || space.names[at].name.empty()) continue;
                const std::string detail = space.names[before].element == 0U && space.what == "part"
                                               ? std::string(space.names[at].name) + " is a part every control has"
                                               : std::string(space.what) + " " + std::string(space.names[at].name) +
                                                     " is declared twice";
                report(DiagnosticCode::control_name_repeated, element_offset(space.names[at].element),
                       space.names[at].name, {}, detail);
                break;
            }
        }
    }
    // A property shares no name with an inherited one.
    for (const ControlProperty& property : scratch.properties) {
        if (base_property(property.name)) {
            report(DiagnosticCode::control_name_repeated, element_offset(property.source.element), property.name, {},
                   property.name + " is a property of the base; it is not redeclared");
        }
    }
    if (scratch.parts.size() > maximum_control_parts) {
        report(DiagnosticCode::control_limit_exceeded, element_offset(scratch.parts[maximum_control_parts].source.element),
               scratch.parts[maximum_control_parts].name, {}, "more than 30 parts (with region and focus, the anatomy's 32)");
    }
    std::size_t custom = 0U;
    for (const ControlState& state : scratch.states) {
        if (!state.custom) continue;
        ++custom;
        if (custom == maximum_custom_states + 1U) {
            report(DiagnosticCode::control_limit_exceeded, element_offset(state.source.element), state.name, {},
                   "more than 16 custom states");
        }
    }
}

void ModelBuilder::check_word(const ExpressionNode& node, const std::size_t base, const ExpressionPlace place,
                              const std::string_view rule, const std::string_view attribute_name,
                              const ControlModel& scratch) {
    const std::size_t offset = base + node.offset;
    if (node.kind == ExpressionNodeKind::word) {
        const std::string& word = node.text;
        if (listed(physical_direction_words, word)) {
            report(DiagnosticCode::control_physical_direction, offset, rule, attribute_name,
                   "'" + word + "': positions are start and end, which mirror under right-to-left");
            return;
        }
        if (listed(engine_input_words, word)) {
            if (place == ExpressionPlace::geometry || place == ExpressionPlace::condition) {
                report(DiagnosticCode::control_geometry_engine_input, offset, rule, attribute_name,
                       "'" + word + "' is an engine input, which layout and states may not read");
            }
            return;
        }
        const bool known = listed(keyword_words, word) || any_property(scratch, word) || part_named(scratch, word) ||
                           command_named(scratch, word) || state_named(scratch, word) || event_named(scratch, word) ||
                           listed(function_words, word);
        if (!known) {
            report(DiagnosticCode::control_reference_unknown, offset, rule, attribute_name,
                   "'" + word + "' names no property, part, command or state");
        }
        return;
    }
    if (node.kind == ExpressionNodeKind::call) {
        const std::string& function = node.text;
        const bool semantic_list = attribute_name == "states" || attribute_name == "actions";
        bool known = listed(function_words, function);
        if (semantic_list && attribute_name == "states") known = known || listed(semantic_state_words, function);
        if (semantic_list && attribute_name == "actions") known = known || semantic_action_of(function).has_value();
        if (!known) {
            report(DiagnosticCode::control_vocabulary_unknown, offset, rule, attribute_name,
                   "function '" + function + "'");
        }
        return;
    }
    if (node.kind == ExpressionNodeKind::local_reference) {
        const ControlChild* const child = child_named(scratch, node.text);
        if (child == nullptr) {
            report(DiagnosticCode::control_reference_unknown, offset, rule, attribute_name,
                   "@" + node.text + " names no child");
            return;
        }
        if (!node.member.empty() && !child_property(*child, node.member)) {
            report(DiagnosticCode::control_reference_unknown, offset, rule, attribute_name,
                   "@" + node.text + " has no property " + node.member);
        }
        return;
    }
    if (node.kind == ExpressionNodeKind::self_reference) {
        if (place != ExpressionPlace::child) {
            report(DiagnosticCode::control_reference_unknown, offset, rule, attribute_name,
                   "self." + node.member + " is written in a child's attribute only");
            return;
        }
        if (!any_property(scratch, node.member)) {
            report(DiagnosticCode::control_reference_unknown, offset, rule, attribute_name,
                   "self." + node.member + " names no property");
        }
    }
}

void ModelBuilder::check_expression(const std::string_view text, const std::uint32_t element,
                                    const std::string_view attribute_name, const ExpressionPlace place,
                                    const std::string_view rule, const ControlModel& scratch) {
    const std::size_t base = value_offset(element, attribute_name);
    const ExpressionParseResult parse = parse_expressions(text, parsed_);
    if (!parse.valid) {
        const bool too_deep = parse.detail.starts_with("nested deeper");
        report(too_deep ? DiagnosticCode::control_expression_over_budget : DiagnosticCode::control_expression_invalid,
               base + parse.offset, rule, attribute_name, parse.detail);
        return;
    }
    if (parsed_.nodes.size() > maximum_expression_instructions) {
        report(DiagnosticCode::control_expression_over_budget, base, rule, attribute_name,
               std::to_string(parsed_.nodes.size()) + " instructions, more than 64");
    }
    for (const ExpressionNode& node : parsed_.nodes) check_word(node, base, place, rule, attribute_name, scratch);
}

void ModelBuilder::written_properties(const std::string_view actions, std::vector<std::string>& written) const {
    ParsedExpression parsed{};
    const ExpressionParseResult parse = parse_expressions(actions, parsed);
    if (!parse.valid) return;
    for (const ExpressionNodeIndex term : parsed.terms) {
        const ExpressionNode& node = parsed.nodes[term.value];
        const bool writes = node.kind == ExpressionNodeKind::call &&
                            (node.text == "set" || node.text == "toggle" || node.text == "step");
        if (!writes || node.argument_count == 0U) continue;
        const ExpressionOperands operands = expression_operands(parsed, term);
        const ExpressionNode& target = parsed.nodes[operands[0].value];
        if (target.kind == ExpressionNodeKind::word) written.push_back(target.text);
    }
}

void ModelBuilder::check_references(const ControlModel& scratch) {
    for (const ControlProperty& property : scratch.properties) {
        const std::uint32_t element = property.source.element;
        if (!property.changed.empty() && !event_named(scratch, property.changed)) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "changed"), property.name,
                   "changed", "event " + property.changed + " is not declared");
        }
        if (!property.range.empty()) {
            const std::vector<std::string_view> bounds = words_of(property.range);
            if (bounds.size() != 2U) {
                report(DiagnosticCode::control_value_invalid, value_offset(element, "range"), property.name, "range",
                       "a range is two bounds");
            }
            for (const std::string_view bound : bounds) {
                if (number_of(bound).has_value()) continue;
                const ControlProperty* const target = find_property(scratch, bound);
                if (target == nullptr) {
                    report(DiagnosticCode::control_reference_unknown, value_offset(element, "range"), property.name,
                           "range", "range bound " + std::string(bound) + " is not a property");
                }
            }
        }
        if (!property.alias.empty()) {
            if (property.alias.front() == '@') {
                const std::size_t dot = property.alias.find('.');
                const std::string child = property.alias.substr(1U, dot == std::string::npos ? std::string::npos : dot - 1U);
                const ControlChild* const target = child_named(scratch, child);
                if (dot == std::string::npos || target == nullptr ||
                    !child_property(*target, property.alias.substr(dot + 1U))) {
                    report(DiagnosticCode::control_reference_unknown, value_offset(element, "alias"), property.name,
                           "alias", "alias " + property.alias + " names no child property");
                }
            } else if (find_property(scratch, property.alias) == nullptr) {
                report(DiagnosticCode::control_reference_unknown, value_offset(element, "alias"), property.name,
                       "alias", "alias " + property.alias + " names no property");
            }
        }
    }
    for (const ControlCommand& command : scratch.commands) {
        if (!command.enabled.empty()) {
            check_expression(command.enabled, command.source.element, "enabled", ExpressionPlace::condition,
                             command.name, scratch);
        }
        if (!command.run.empty()) {
            check_expression(command.run, command.source.element, "run", ExpressionPlace::action, command.name,
                             scratch);
        }
    }
    for (const ControlState& state : scratch.states) {
        if (!state.when.empty()) {
            check_expression(state.when, state.source.element, "when", ExpressionPlace::condition, state.name,
                             scratch);
        }
    }
    for (const ControlPart& part : scratch.parts) check_part(part, scratch);
    for (const ControlDrag& drag : scratch.drags) check_drag(drag, scratch);
    if (scratch.focus) {
        const ControlFocus& focus = *scratch.focus;
        const std::uint32_t element = focus.source.element;
        if (focus.target == ControlFocusTarget::child && child_named(scratch, focus.child) == nullptr) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "target"), "focus", "target",
                   "@" + focus.child + " names no child");
        }
        if (!focus.visible_cue.empty() && !part_named(scratch, focus.visible_cue)) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "visible-cue"), "focus",
                   "visible-cue", "part " + focus.visible_cue + " is not declared");
        }
        if (!focus.mnemonic.empty()) {
            const ControlProperty* const property = find_property(scratch, focus.mnemonic);
            const bool text = (property != nullptr && (*property).kind == ControlPropertyKind::text) ||
                              base_property(focus.mnemonic);
            if (!text) {
                report(DiagnosticCode::control_reference_unknown, value_offset(element, "mnemonic"), "focus",
                       "mnemonic", "the mnemonic names a text property");
            }
        }
    }
    check_child_values(scratch);
    check_rules(scratch);
    check_semantics(scratch);
}

void ModelBuilder::check_part(const ControlPart& part, const ControlModel& scratch) {
    const std::uint32_t element = part.source.element;
    if (!part.box.empty()) {
        check_expression(part.box, element, "box", ExpressionPlace::geometry, part.name, scratch);
    }
    if (!part.present.empty()) {
        check_expression(part.present, element, "present", ExpressionPlace::condition, part.name, scratch);
    }
    if (!part.disabled_when.empty()) {
        check_expression(part.disabled_when, element, "disabled-when", ExpressionPlace::condition, part.name, scratch);
    }
    if (!part.activates.empty() && !command_named(scratch, part.activates)) {
        report(DiagnosticCode::control_reference_unknown, value_offset(element, "activates"), part.name, "activates",
               "command " + part.activates + " is not declared");
    }
    const bool drags = (part.press & static_cast<std::uint8_t>(ControlPress::drag)) != 0U;
    const bool active = part.press != 0U || part.hit != ControlPartHit::none;
    if (active && part.activates.empty() && !drags) {
        report(DiagnosticCode::control_part_inactive, element_offset(element), part.name, "press",
               "an active part activates a command or starts a drag");
    }
    if (drags && !part.drag) {
        report(DiagnosticCode::control_drag_invalid, element_offset(element), part.name, "press",
               "a part pressing drag holds a <drag>");
    }
    if (!drags && part.drag) {
        report(DiagnosticCode::control_drag_invalid, element_offset(element), part.name, "press",
               "a part with a <drag> presses drag");
    }
}

void ModelBuilder::check_drag(const ControlDrag& drag, const ControlModel& scratch) {
    const std::uint32_t element = drag.source.element;
    std::string part_name{};
    for (const ControlPart& part : scratch.parts) {
        if (part.drag && &scratch.drags[*part.drag] == &drag) part_name = part.name;
    }
    if (!drag.range.empty()) {
        check_expression(drag.range, element, "range", ExpressionPlace::geometry, part_name, scratch);
    }
    const ControlProperty* const target = find_property(scratch, drag.maps_to);
    if (!drag.maps_to.empty() && target == nullptr) {
        report(DiagnosticCode::control_reference_unknown, value_offset(element, "maps-to"), part_name, "maps-to",
               "property " + drag.maps_to + " is not declared");
    }
    if (target != nullptr && (*target).kind) {
        const ControlPropertyKind kind = *(*target).kind;
        bool fits = true;
        switch (drag.kind) {
        case ControlDragKind::value:
            fits = kind == ControlPropertyKind::number || kind == ControlPropertyKind::boolean ||
                   kind == ControlPropertyKind::signed_integer || kind == ControlPropertyKind::unsigned_integer;
            break;
        case ControlDragKind::placement: fits = kind == ControlPropertyKind::point; break;
        case ControlDragKind::reorder:
            fits = kind == ControlPropertyKind::signed_integer || kind == ControlPropertyKind::unsigned_integer;
            break;
        // An out-of-range drag kind keeps fits as set above (true): no constraint to check.
        default: break;
        }
        if (!fits) {
            report(DiagnosticCode::control_drag_invalid, value_offset(element, "maps-to"), part_name, "maps-to",
                   drag.maps_to + " is of kind " + std::string(control_property_kind_name(kind)) +
                       ", which this drag's kind cannot set");
        }
    }
    if (!drag.preview.empty()) {
        if (drag.mode == ControlDragMode::live) {
            report(DiagnosticCode::control_drag_invalid, value_offset(element, "preview"), part_name, "preview",
                   "a live drag writes its property; preview is for commit mode");
        }
        check_expression(drag.preview, element, "preview", ExpressionPlace::action, part_name, scratch);
        std::vector<std::string> written{};
        written_properties(drag.preview, written);
        for (const std::string& name : written) {
            const ControlProperty* const property = find_property(scratch, name);
            if (property != nullptr && !(*property).presentational) {
                report(DiagnosticCode::control_drag_invalid, value_offset(element, "preview"), part_name, "preview",
                       "preview writes " + name + ", which is not presentational");
            }
        }
    }
    for (const std::string_view word : words_of(drag.targets)) {
        const bool known = (word.size() > 1U && word.front() == '@' && child_named(scratch, word.substr(1U)) != nullptr) ||
                           part_named(scratch, word);
        if (!known) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "targets"), part_name, "targets",
                   std::string(word) + " names no part or child");
        }
    }
    for (const std::string_view word : words_of(drag.slots)) {
        const bool known = (word.size() > 1U && word.front() == '@' && child_named(scratch, word.substr(1U)) != nullptr) ||
                           part_named(scratch, word);
        if (!known) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "slots"), part_name, "slots",
                   std::string(word) + " names no part or child");
        }
    }
    if (!drag.grid.empty() && !number_of(drag.grid).has_value()) {
        report(DiagnosticCode::control_value_invalid, value_offset(element, "grid"), part_name, "grid",
               "a grid step is one number");
    }
}

void ModelBuilder::check_child_values(const ControlModel& scratch) {
    for (const ControlChild& child : scratch.children) {
        const std::uint32_t element = child.source.element;
        const std::string rule = child.local_id.empty() ? std::string("child") : "@" + child.local_id;
        for (const ControlChildAttribute& attribute_value : child.attributes) {
            const std::string written_name = kebab_of(attribute_value.name);
            if (attribute_value.bound) {
                // bind(self.<property>[, option]...): a composite's child
                // binds to the composite's own properties.
                const std::string_view value = attribute_value.value;
                const std::size_t close = value.rfind(')');
                const std::string_view inside = close == std::string_view::npos ? std::string_view{}
                                                                                : value.substr(5U, close - 5U);
                const std::size_t comma = inside.find(',');
                std::string_view source_name = inside.substr(0U, comma);
                while (!source_name.empty() && source_name.back() == ' ') source_name.remove_suffix(1U);
                while (!source_name.empty() && source_name.front() == ' ') source_name.remove_prefix(1U);
                if (close == std::string_view::npos || close + 1U != value.size() || !source_name.starts_with("self.")) {
                    report(DiagnosticCode::form_binding_invalid, value_offset(element, written_name), rule,
                           written_name, "a child binds to the composite: bind(self.<property>[, option]...)");
                    continue;
                }
                const std::string_view property = source_name.substr(5U);
                if (!any_property(scratch, property)) {
                    report(DiagnosticCode::control_reference_unknown, value_offset(element, written_name), rule,
                           written_name, "self." + std::string(property) + " names no property");
                }
                const std::string_view options = comma == std::string_view::npos ? std::string_view{}
                                                                                 : inside.substr(comma + 1U);
                std::size_t start = 0U;
                while (start <= options.size() && !options.empty()) {
                    const std::size_t next = options.find(',', start);
                    std::string_view option = options.substr(start, next == std::string_view::npos ? std::string_view::npos
                                                                                                   : next - start);
                    while (!option.empty() && option.front() == ' ') option.remove_prefix(1U);
                    while (!option.empty() && option.back() == ' ') option.remove_suffix(1U);
                    if (option.starts_with("convert")) {
                        report(DiagnosticCode::not_yet_built, value_offset(element, written_name), rule, written_name,
                               "converters are reserved (convert)");
                    } else if (option != "one_way" && option != "update on-change" && option != "update on-validate") {
                        report(DiagnosticCode::form_binding_option_invalid, value_offset(element, written_name), rule,
                               written_name, "binding option '" + std::string(option) + "'");
                    }
                    if (next == std::string_view::npos) break;
                    start = next + 1U;
                }
                continue;
            }
            if (attribute_value.value.find("self.") != std::string::npos) {
                check_expression(attribute_value.value, element, written_name, ExpressionPlace::child, rule, scratch);
            }
        }
    }
}

void ModelBuilder::check_rules(const ControlModel& scratch) {
    for (const ControlRule& rule : scratch.rules) {
        const std::uint32_t element = rule.source.element;
        if (rule.key) {
            const std::string_view key = gesture_key(rule.trigger);
            if (key == "tab") {
                report(DiagnosticCode::control_key_not_admitted, value_offset(element, "gesture"), rule.trigger,
                       "gesture", "Tab moves focus; a composite may not claim it");
            }
            if ((key == "enter" || key == "escape") && !rule.claims_dialog_key) {
                report(DiagnosticCode::control_key_not_admitted, value_offset(element, "gesture"), rule.trigger,
                       "gesture", "Enter and Escape are the dialog's keys unless claims=\"dialog-key\"");
            }
        } else {
            // trigger(<target>): a known trigger, or a child's interaction.
            ParsedExpression parsed{};
            const ExpressionParseResult parse = parse_expressions(rule.trigger, parsed);
            const bool one_call = parse.valid && parsed.terms.size() == 1U &&
                                  parsed.nodes[parsed.terms[0].value].kind == ExpressionNodeKind::call &&
                                  parsed.nodes[parsed.terms[0].value].argument_count == 1U;
            if (!one_call) {
                report(DiagnosticCode::control_expression_invalid, value_offset(element, "trigger"), rule.trigger,
                       "trigger", "a trigger is <trigger>(<target>)");
            } else {
                const ExpressionNode& call = parsed.nodes[parsed.terms[0].value];
                const ExpressionNode& target = parsed.nodes[expression_operands(parsed, parsed.terms[0])[0].value];
                const bool self_target = target.kind == ExpressionNodeKind::word && target.text == "self";
                const ControlChild* const child =
                    target.kind == ExpressionNodeKind::local_reference ? child_named(scratch, target.text) : nullptr;
                const bool property_target = target.kind == ExpressionNodeKind::word && any_property(scratch, target.text);
                bool known = listed(trigger_words, call.text);
                if (!known && child != nullptr && (*child).schema_type) {
                    const std::string snake = snake_of(call.text);
                    for (const std::string& interaction : schema_.types[*(*child).schema_type].interactions) {
                        known = known || interaction == snake;
                    }
                }
                if (!known) {
                    report(DiagnosticCode::control_vocabulary_unknown, value_offset(element, "trigger"), rule.trigger,
                           "trigger", "trigger '" + call.text + "'");
                }
                const bool target_known = call.text == "changed" ? property_target : (self_target || child != nullptr);
                if (!target_known) {
                    report(DiagnosticCode::control_reference_unknown, value_offset(element, "trigger"), rule.trigger,
                           "trigger", call.text == "changed" ? "changed() names a property"
                                                             : "a trigger's target is self or a child");
                }
            }
        }
        if (!rule.when.empty()) {
            check_expression(rule.when, element, "when", ExpressionPlace::condition, rule.trigger, scratch);
        }
        for (std::size_t at = rule.first_action; at < rule.first_action + rule.action_count; ++at) {
            const ControlAction& action = scratch.actions[at];
            const std::uint32_t action_element = action.source.element;
            std::string_view target_attribute = "property";
            bool known = true;
            switch (action.kind) {
            case ControlActionKind::set:
            case ControlActionKind::toggle:
            case ControlActionKind::step:
                known = any_property(scratch, action.target);
                break;
            case ControlActionKind::invoke: {
                const ControlProperty* const property = find_property(scratch, action.target);
                known = property != nullptr && (*property).kind == ControlPropertyKind::command;
                break;
            }
            case ControlActionKind::run:
                target_attribute = "command";
                known = command_named(scratch, action.target);
                break;
            case ControlActionKind::raise:
                target_attribute = "event";
                known = event_named(scratch, action.target);
                break;
            case ControlActionKind::focus:
            case ControlActionKind::open:
            case ControlActionKind::close:
                target_attribute = "target";
                known = action.target == "self" ||
                        (action.target.size() > 1U && action.target.front() == '@' &&
                         child_named(scratch, std::string_view(action.target).substr(1U)) != nullptr);
                break;
            // An out-of-range action kind keeps known as set above (true): nothing to look up.
            default: break;
            }
            if (!known && !action.target.empty()) {
                report(DiagnosticCode::control_reference_unknown, value_offset(action_element, target_attribute),
                       rule.trigger, target_attribute, "'" + action.target + "' names nothing this action can take");
            }
            if (!action.value.empty()) {
                const std::string_view value_attribute = action.kind == ControlActionKind::set ? "to" : "by";
                check_expression(action.value, action_element, value_attribute, ExpressionPlace::action, rule.trigger,
                                 scratch);
            }
        }
    }
}

void ModelBuilder::check_semantics(const ControlModel& scratch) {
    if (!scratch.semantics) return;
    const ControlSemantics& semantics = *scratch.semantics;
    const std::uint32_t element = semantics.source.element;
    if (!semantics.from_child.empty()) {
        // The node handed over is the focus target's: the focused node and
        // the composite's node are then one node.
        if (child_named(scratch, semantics.from_child) == nullptr) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "from"), "semantics", "from",
                   "@" + semantics.from_child + " names no child");
        }
        const bool focus_child = scratch.focus && (*scratch.focus).target == ControlFocusTarget::child &&
                                 (*scratch.focus).child == semantics.from_child;
        if (!focus_child) {
            report(DiagnosticCode::control_value_invalid, value_offset(element, "from"), "semantics", "from",
                   "the semantics come from the focus target: declare <focus target=\"child(@" +
                       semantics.from_child + ")\">");
        }
        return;
    }
    if (!semantics.name.empty()) {
        check_expression(semantics.name, element, "name", ExpressionPlace::condition, "semantics", scratch);
    }
    if (!semantics.value.empty()) {
        check_expression(semantics.value, element, "value", ExpressionPlace::condition, "semantics", scratch);
    }
    for (const std::string_view bound : words_of(semantics.range)) {
        if (!any_property(scratch, bound) && !number_of(bound).has_value()) {
            report(DiagnosticCode::control_reference_unknown, value_offset(element, "range"), "semantics", "range",
                   std::string(bound) + " is not a property");
        }
    }
    const std::size_t range_count = words_of(semantics.range).size();
    if (range_count != 0U && range_count != 2U && range_count != 3U) {
        report(DiagnosticCode::control_value_invalid, value_offset(element, "range"), "semantics", "range",
               "a semantic range is minimum maximum [step]");
    }
    if (!semantics.states.empty()) {
        check_expression(semantics.states, element, "states", ExpressionPlace::condition, "semantics", scratch);
    }
    if (!semantics.actions.empty()) {
        check_expression(semantics.actions, element, "actions", ExpressionPlace::condition, "semantics", scratch);
    }
}

void ModelBuilder::check_reachability(const ControlModel& scratch) {
    // The commands something runs.
    std::vector<std::string_view> reached{};
    for (const ControlPart& part : scratch.parts) {
        if (!part.activates.empty()) reached.push_back(part.activates);
    }
    for (const ControlAction& action : scratch.actions) {
        if (action.kind == ControlActionKind::run) reached.push_back(action.target);
    }
    for (const ControlCommand& command : scratch.commands) {
        if (command.semantic_action) reached.push_back(command.name);
    }
    // The semantics' actions map an action to a command or a property.
    std::vector<std::string> semantic_targets{};
    if (scratch.semantics && !(*scratch.semantics).actions.empty()) {
        ParsedExpression parsed{};
        if (parse_expressions((*scratch.semantics).actions, parsed).valid) {
            for (const ExpressionNodeIndex term : parsed.terms) {
                const ExpressionNode& node = parsed.nodes[term.value];
                if (node.kind != ExpressionNodeKind::call || node.argument_count != 1U) continue;
                const ExpressionNode& target = parsed.nodes[expression_operands(parsed, term)[0].value];
                if (target.kind == ExpressionNodeKind::word) semantic_targets.push_back(target.text);
            }
        }
    }
    for (const std::string& target : semantic_targets) reached.push_back(target);
    for (const ControlCommand& command : scratch.commands) {
        if (std::find(reached.begin(), reached.end(), command.name) == reached.end()) {
            report(DiagnosticCode::control_command_unreachable, element_offset(command.source.element), command.name,
                   {}, "no part, key, rule or semantic action runs " + command.name);
        }
    }
    // A drag's result is reachable from the keyboard: a key or a semantic
    // action writes its property, directly or through a command.
    std::vector<std::string> keyboard_written{};
    for (const ControlRule& rule : scratch.rules) {
        if (!rule.key) continue;
        for (std::size_t at = rule.first_action; at < rule.first_action + rule.action_count; ++at) {
            const ControlAction& action = scratch.actions[at];
            const bool writes = action.kind == ControlActionKind::set || action.kind == ControlActionKind::toggle ||
                                action.kind == ControlActionKind::step;
            if (writes) keyboard_written.push_back(action.target);
            if (action.kind != ControlActionKind::run) continue;
            for (const ControlCommand& command : scratch.commands) {
                if (command.name == action.target) written_properties(command.run, keyboard_written);
            }
        }
    }
    for (const std::string& target : semantic_targets) {
        keyboard_written.push_back(target);
        for (const ControlCommand& command : scratch.commands) {
            if (command.name == target) written_properties(command.run, keyboard_written);
        }
    }
    for (const ControlDrag& drag : scratch.drags) {
        if (!drag.keyboard.empty() || drag.maps_to.empty()) continue;
        const bool reachable =
            std::find(keyboard_written.begin(), keyboard_written.end(), drag.maps_to) != keyboard_written.end();
        if (!reachable) {
            report(DiagnosticCode::control_drag_invalid, element_offset(drag.source.element), drag.maps_to, "keyboard",
                   "the keyboard cannot reach " + drag.maps_to + ": give the drag a keyboard gesture, or a key that "
                   "writes it");
        }
    }
    // An active or focusable composite has a semantic role, a name source
    // and a visible focus cue.
    bool active = false;
    for (const ControlPart& part : scratch.parts) active = active || part.press != 0U || part.hit != ControlPartHit::none;
    const bool focusable = scratch.focus && (*scratch.focus).target != ControlFocusTarget::none;
    // A composite whose node is its focus target's (semantics from=) takes
    // that child's role, name and focus presentation.
    const bool delegated = scratch.semantics && !(*scratch.semantics).from_child.empty();
    if ((active || focusable) && !delegated) {
        const bool role = scratch.semantics && (*scratch.semantics).role != SemanticRole::generic;
        const bool name = scratch.semantics && !(*scratch.semantics).name.empty();
        if (!role || !name) {
            report(DiagnosticCode::control_semantics_missing, element_offset(0U), scratch.name, "semantics",
                   "an active or focusable composite declares a semantic role and a name source");
        }
        if (focusable && (*scratch.focus).visible_cue.empty()) {
            report(DiagnosticCode::control_semantics_missing, element_offset((*scratch.focus).source.element), "focus",
                   "visible-cue", "a focusable composite shows a focus cue");
        }
    }
}

// Finds a cycle in a graph given as edges from name to name; reports each
// node on a cycle once.
void ModelBuilder::check_cycles(const ControlModel& scratch) {
    struct Edge final {
        std::string from;
        std::string to;
        std::uint32_t element;
        std::string_view what;
    };
    std::vector<Edge> edges{};
    for (const ControlProperty& property : scratch.properties) {
        if (!property.alias.empty() && property.alias.front() != '@') {
            edges.push_back(Edge{"property:" + property.name, "property:" + property.alias, property.source.element,
                                 "alias"});
        }
        for (const std::string_view bound : words_of(property.range)) {
            if (find_property(scratch, bound) != nullptr) {
                edges.push_back(Edge{"property:" + property.name, "property:" + std::string(bound),
                                     property.source.element, "range"});
            }
        }
    }
    for (const ControlState& state : scratch.states) {
        ParsedExpression parsed{};
        if (!parse_expressions(state.when, parsed).valid) continue;
        for (std::size_t at = 0U; at < parsed.nodes.size(); ++at) {
            const ExpressionNode& node = parsed.nodes[at];
            if (node.kind != ExpressionNodeKind::call || node.text != "state" || node.argument_count != 1U) continue;
            const ExpressionNode& target =
                parsed.nodes[expression_operands(parsed, ExpressionNodeIndex{static_cast<std::uint32_t>(at)})[0].value];
            if (target.kind == ExpressionNodeKind::word) {
                edges.push_back(Edge{"state:" + state.name, "state:" + target.text, state.source.element, "state"});
            }
        }
    }
    // A range edge from a property to itself is its own bound, also a cycle.
    std::vector<std::string> reported{};
    for (const Edge& start : edges) {
        // Follow edges from start.to looking for start.from, breadth first
        // with a visited list (the graph has at most a few dozen nodes).
        std::vector<std::string> frontier{start.to};
        std::vector<std::string> visited{};
        bool cycle = start.to == start.from;
        while (!frontier.empty() && !cycle) {
            const std::string node = frontier.back();
            frontier.pop_back();
            if (std::find(visited.begin(), visited.end(), node) != visited.end()) continue;
            visited.push_back(node);
            for (const Edge& edge : edges) {
                if (edge.from != node) continue;
                if (edge.to == start.from) cycle = true;
                frontier.push_back(edge.to);
            }
        }
        if (!cycle || std::find(reported.begin(), reported.end(), start.from) != reported.end()) continue;
        reported.push_back(start.from);
        report(DiagnosticCode::control_cycle, element_offset(start.element), start.from.substr(start.from.find(':') + 1U),
               start.what, start.from + " depends on itself through " + std::string(start.what));
    }
}

void ModelBuilder::check_look(const ControlModel& scratch) {
    if (scratch.look.empty()) {
        for (const ControlState& state : scratch.states) {
            if (!state.custom) continue;
            report(DiagnosticCode::control_state_without_cue, element_offset(state.source.element), state.name, {},
                   "custom state " + state.name + " has no rule in <look> and no role cue");
        }
        return;
    }
    std::uint32_t look_element = 0U;
    for (std::uint32_t index = 0U; index < flat_.size(); ++index) {
        if ((*flat_[index].element).name == "look" && flat_[index].parent == 0U) look_element = index;
    }
    const std::size_t base = syntax_.locations.elements.size() > look_element
                                 ? syntax_.locations.elements[look_element].start_tag_end
                                 : 0U;
    // The rules' preludes at depth 0: each selector's first compound names
    // this type, or no type (`::part(x)`, `:state(y)` alone).
    const std::string& text = scratch.look;
    std::size_t depth = 0U;
    std::size_t prelude = 0U;
    bool in_comment = false;
    for (std::size_t at = 0U; at < text.size(); ++at) {
        if (in_comment) {
            if (text[at] == '*' && at + 1U < text.size() && text[at + 1U] == '/') {
                in_comment = false;
                ++at;
                prelude = at + 1U;
            }
            continue;
        }
        if (text[at] == '/' && at + 1U < text.size() && text[at + 1U] == '*') {
            in_comment = true;
            continue;
        }
        if (text[at] == '{') {
            if (depth == 0U) {
                const std::string_view selectors = std::string_view(text).substr(prelude, at - prelude);
                std::size_t start = 0U;
                const bool at_rule = selectors.find('@') != std::string_view::npos;
                while (!at_rule && start <= selectors.size()) {
                    const std::size_t comma = selectors.find(',', start);
                    std::string_view selector =
                        selectors.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
                    std::size_t lead = 0U;
                    while (lead < selector.size() && (selector[lead] == ' ' || selector[lead] == '\n' ||
                                                      selector[lead] == '\t' || selector[lead] == '\r')) {
                        ++lead;
                    }
                    std::size_t end = lead;
                    while (end < selector.size() && ((selector[end] >= 'a' && selector[end] <= 'z') ||
                                                     (selector[end] >= '0' && selector[end] <= '9') ||
                                                     selector[end] == '-')) {
                        ++end;
                    }
                    const std::string_view type = selector.substr(lead, end - lead);
                    if (!type.empty() && type != scratch.name) {
                        report(DiagnosticCode::control_look_foreign_type, base + prelude + start + lead, "look", {},
                               "a <look> rule styles " + scratch.name + ", not " + std::string(type));
                    }
                    if (comma == std::string_view::npos) break;
                    start = comma + 1U;
                }
            }
            ++depth;
            continue;
        }
        if (text[at] == '}') {
            if (depth > 0U) --depth;
            if (depth == 0U) prelude = at + 1U;
        }
    }
    // State completeness (W950): a custom state the look styles somewhere.
    for (const ControlState& state : scratch.states) {
        if (!state.custom) continue;
        const std::string written = ":state(" + state.name + ")";
        if (text.find(written) == std::string::npos) {
            report(DiagnosticCode::control_state_without_cue, element_offset(state.source.element), state.name, {},
                   "custom state " + state.name + " has no rule in <look> and no role cue");
        }
    }
}

bool ModelBuilder::run(ControlModel& scratch) {
    first_record_ = diagnostics_.error_count();
    flatten();
    read_root(scratch);
    if (syntax_.root.name != "control") return false;
    check_text(scratch);
    read_declarations(scratch);
    check_names(scratch);
    check_references(scratch);
    check_reachability(scratch);
    check_cycles(scratch);
    check_look(scratch);
    const bool clean = diagnostics_.error_count() == first_record_;
    return clean;
}

} // namespace

std::string_view control_property_kind_name(const ControlPropertyKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= property_kind_words.size()) return "unknown";
    return property_kind_words[index];
}

std::string_view control_payload_type_name(const ControlPropertyKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= payload_type_words.size()) return "unknown";
    return payload_type_words[index];
}

ControlSyntaxOutcome read_control_syntax(const theme_language::ThemeSource& source, ControlSyntax& syntax,
                                         theme_language::ThemeDiagnosticList& diagnostics) {
    const XmlLimits limits = form_language::form_xml_limits();
    XmlReadResult read = detail::read_xml_located(source.text, limits, syntax.locations);
    if (!read) {
        ThemeDiagnostic diagnostic{};
        diagnostic.code = DiagnosticCode::control_xml_invalid;
        diagnostic.file = std::string(source.file);
        diagnostic.position.offset = ByteOffset{static_cast<std::uint32_t>(read.position.byte_offset)};
        diagnostic.position.line = theme_language::LineNumber{read.position.line};
        diagnostic.position.column = theme_language::ColumnNumber{read.position.column};
        diagnostic.detail = std::string(xml_error_name(read.error));
        diagnostics.add(std::move(diagnostic));
        syntax.root = XmlElement{};
        syntax.lines.clear();
        return ControlSyntaxOutcome::refused;
    }
    // The reader admitted the input, so it is within the line table's bound.
    syntax.lines.reset(source.text);
    syntax.root = std::move(read.root);
    return ControlSyntaxOutcome::read;
}

ControlModelOutcome build_control_model(const theme_language::ThemeSource& source, const ControlSyntax& syntax,
                                        const form_language::FormSchema& schema, ControlModel& model,
                                        theme_language::ThemeDiagnosticList& diagnostics) {
    ModelBuilder builder(source, syntax, schema, diagnostics);
    ControlModel scratch{};
    const bool built = builder.run(scratch);
    if (!built) return ControlModelOutcome::refused;
    model = std::move(scratch);
    return ControlModelOutcome::model_built;
}

ControlModelOutcome read_control_model(const theme_language::ThemeSource& source, ControlSyntax& syntax,
                                       const form_language::FormSchema& schema, ControlModel& model,
                                       theme_language::ThemeDiagnosticList& diagnostics) {
    const ControlSyntaxOutcome read = read_control_syntax(source, syntax, diagnostics);
    if (read == ControlSyntaxOutcome::refused) return ControlModelOutcome::refused;
    const ControlModelOutcome outcome = build_control_model(source, syntax, schema, model, diagnostics);
    return outcome;
}

} // namespace gui_forms::control_language
