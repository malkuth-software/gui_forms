// A composite control's compiled definition (cp34 composite controls,
// phase 2). The model was checked whole (control_model.cpp), so every name
// resolves and every expression parses here; what is refused is what the
// run time cannot run yet (E999), and the children's document if the form
// front end refuses it.

#include "composite_definition.hpp"

#include "../../form_language/form_xml/form_xml.hpp"
#include "../../theme/theme_compiler/composite_look_compile.hpp"
#include "../../theme_language/compiler/theme_compiler.hpp"

#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/xml/xml_writer/xml_writer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace gui_forms::control_language {

namespace {

// Whether `look` reads, through var(), a custom property name outside
// `defined` (sorted), the names it defines itself: a name a theme may
// define. Read from the text: a var() inside a comment counts too, which
// at worst compiles a theme-independent look once per theme.
bool reads_undefined_name(std::string_view look, std::span<const std::string> defined) {
    std::size_t at = look.find("var(");
    while (at != std::string_view::npos) {
        std::size_t first = at + 4U;
        while (first < look.size() && (look[first] == ' ' || look[first] == '\t' || look[first] == '\n')) ++first;
        std::size_t last = first;
        while (last < look.size()) {
            const char character = look[last];
            const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
            const bool digit = character >= '0' && character <= '9';
            if (!letter && !digit && character != '-' && character != '_') break;
            ++last;
        }
        const std::string name(look.substr(first, last - first));
        if (!name.empty() && !std::binary_search(defined.begin(), defined.end(), name)) return true;
        at = look.find("var(", last);
    }
    return false;
}

// Whether every error of a look compiled alone names something a theme may
// supply: an unknown keyword (an application colour role) or an undefined
// custom property.
bool deferrable_to_theme(const theme_language::ThemeDiagnosticList& found) noexcept {
    for (const theme_language::ThemeDiagnostic& diagnostic : found.records()) {
        const bool error = theme_language::diagnostic_severity(diagnostic.code) ==
                           theme_language::DiagnosticSeverity::error;
        if (!error) continue;
        const bool deferrable = diagnostic.code == theme_language::DiagnosticCode::keyword_unknown ||
                                diagnostic.code == theme_language::DiagnosticCode::variable_undefined;
        if (!deferrable) return false;
    }
    return true;
}

using theme_language::DiagnosticCode;
using theme_language::ThemeDiagnostic;

constexpr std::string_view slot_element = "slot";
constexpr std::string_view span_element = "span";
constexpr std::string_view bind_prefix = "bind(";
constexpr std::string_view self_prefix = "self.";
// The type of the children's wrapping root and of the slot.
constexpr std::string_view slot_type = "user-control";

struct NamedKey final {
    std::string_view name;
    std::uint32_t key;
};

constexpr std::array<NamedKey, 17> named_keys{{
    {"space", PhysicalKey::space},
    {"enter", PhysicalKey::enter},
    {"escape", PhysicalKey::escape},
    {"tab", PhysicalKey::tab},
    {"backspace", PhysicalKey::backspace},
    {"delete", PhysicalKey::delete_forward},
    {"insert", PhysicalKey::insert},
    {"home", PhysicalKey::home},
    {"end", PhysicalKey::end},
    {"page-up", PhysicalKey::page_up},
    {"page-down", PhysicalKey::page_down},
    {"arrow-up", PhysicalKey::up},
    {"arrow-down", PhysicalKey::down},
    {"arrow-left", PhysicalKey::left},
    {"arrow-right", PhysicalKey::right},
    {"plus", PhysicalKey::keypad_plus},
    {"minus", PhysicalKey::minus},
}};

// HID usages: digits 1-9 then 0, and F13-F24 after the keypad.
constexpr std::uint32_t first_digit_key = 0x1EU;
constexpr std::uint32_t zero_key = 0x27U;
constexpr std::uint32_t f13_key = 0x68U;

double number_of(const std::string_view text) noexcept {
    std::array<char, 64> copy{};
    const std::size_t length = std::min(text.size(), copy.size() - 1U);
    std::copy_n(text.begin(), length, copy.begin());
    const double value = std::strtod(copy.data(), nullptr);
    return value;
}

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

BindingValueKind binding_kind_of(const ControlPropertyKind kind) noexcept {
    switch (kind) {
    case ControlPropertyKind::boolean: return BindingValueKind::boolean;
    case ControlPropertyKind::number: return BindingValueKind::number;
    case ControlPropertyKind::signed_integer: return BindingValueKind::signed_integer;
    case ControlPropertyKind::unsigned_integer: return BindingValueKind::unsigned_integer;
    case ControlPropertyKind::text: return BindingValueKind::text;
    case ControlPropertyKind::enumeration: return BindingValueKind::enumeration;
    case ControlPropertyKind::color: return BindingValueKind::color;
    case ControlPropertyKind::font: return BindingValueKind::font;
    case ControlPropertyKind::image: return BindingValueKind::image;
    case ControlPropertyKind::size: return BindingValueKind::size;
    case ControlPropertyKind::point: return BindingValueKind::point;
    case ControlPropertyKind::rectangle: return BindingValueKind::rectangle;
    case ControlPropertyKind::insets: return BindingValueKind::insets;
    case ControlPropertyKind::command: return BindingValueKind::object;
    default: return BindingValueKind::text;
    }
}

// A hex digit's value, or 16 when character is not one.
std::uint32_t hex_nibble(const char character) noexcept {
    if (character >= '0' && character <= '9') return static_cast<std::uint32_t>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<std::uint32_t>(character - 'a') + 10U;
    if (character >= 'A' && character <= 'F') return static_cast<std::uint32_t>(character - 'A') + 10U;
    return 16U;
}

// `#rgb`, `#rgba`, `#rrggbb` or `#rrggbbaa`, the forms the model's checker
// admits (A-gf-core-03-F3); none otherwise. A short form's digit d stands
// for the channel dd, as in CSS.
std::optional<Color> color_of(const std::string_view text) noexcept {
    if (text.empty() || text.front() != '#') return std::nullopt;
    const std::size_t digits = text.size() - 1U;
    if (digits != 3U && digits != 4U && digits != 6U && digits != 8U) return std::nullopt;
    const std::size_t per_channel = digits <= 4U ? 1U : 2U;
    const std::size_t channel_count = digits / per_channel;
    std::array<std::uint8_t, 4> channels{0U, 0U, 0U, 255U};
    for (std::size_t channel = 0U; channel < channel_count; ++channel) {
        std::uint32_t value = 0U;
        for (std::size_t digit = 0U; digit < per_channel; ++digit) {
            const std::uint32_t nibble = hex_nibble(text[1U + channel * per_channel + digit]);
            if (nibble > 15U) return std::nullopt;
            value = value * 16U + nibble;
        }
        if (per_channel == 1U) value = value * 17U;
        channels[channel] = static_cast<std::uint8_t>(value);
    }
    const Color color{channels[0], channels[1], channels[2], channels[3]};
    return color;
}

// A signed default as the model's checker admits it (A-gf-core-03-F2):
// digits that parse exactly, else a whole number inside [-2^63, 2^63),
// converted only after that test. Zero for anything else, which the checker
// has refused.
std::int64_t signed_default(const std::string_view written) noexcept {
    std::int64_t exact = 0;
    const std::from_chars_result result = std::from_chars(written.data(), written.data() + written.size(), exact);
    if (result.ec == std::errc{} && result.ptr == written.data() + written.size()) return exact;
    const double number = number_of(written);
    if (!(number >= -9223372036854775808.0 && number < 9223372036854775808.0)) return 0;
    const std::int64_t converted = static_cast<std::int64_t>(number);
    return converted;
}

// An unsigned default, as signed_default with the range [0, 2^64).
std::uint64_t unsigned_default(const std::string_view written) noexcept {
    std::uint64_t exact = 0U;
    const std::from_chars_result result = std::from_chars(written.data(), written.data() + written.size(), exact);
    if (result.ec == std::errc{} && result.ptr == written.data() + written.size()) return exact;
    const double number = number_of(written);
    if (!(number >= 0.0 && number < 18446744073709551616.0)) return 0U;
    const std::uint64_t converted = static_cast<std::uint64_t>(number);
    return converted;
}

// The value a property starts with: its default read by kind (checked by
// the model), else the kind's zero. Null for a presentational property
// without a default and for a command.
BindingValue initial_value(const ControlProperty& property, const ControlPropertyKind kind,
                           const std::shared_ptr<const PropertyEnumDescriptor>& enumeration) {
    const std::string_view written = property.default_value;
    const bool given = !written.empty();
    if (kind == ControlPropertyKind::command) return BindingValue{};
    if (property.presentational && !given) return BindingValue{};
    const std::vector<std::string_view> numbers = words_of(written);
    switch (kind) {
    case ControlPropertyKind::boolean: return BindingValue{written == "true"};
    case ControlPropertyKind::number: return BindingValue{given ? number_of(written) : 0.0};
    case ControlPropertyKind::signed_integer: return BindingValue{given ? signed_default(written) : std::int64_t{0}};
    case ControlPropertyKind::unsigned_integer:
        return BindingValue{given ? unsigned_default(written) : std::uint64_t{0U}};
    case ControlPropertyKind::text: return BindingValue{std::string(written)};
    case ControlPropertyKind::enumeration: {
        PropertyEnumValue choice{};
        choice.type_name = (*enumeration).type_name;
        const std::size_t count = (*enumeration).choices.size();
        std::size_t chosen = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            if ((*enumeration).choices[index].name == written) chosen = index;
        }
        choice.name = (*enumeration).choices[chosen].name;
        choice.value = (*enumeration).choices[chosen].value;
        return BindingValue{choice};
    }
    case ControlPropertyKind::color: {
        const std::optional<Color> color = color_of(written);
        return BindingValue{color.value_or(Color{})};
    }
    case ControlPropertyKind::image: return BindingValue{ImageId{}};
    case ControlPropertyKind::size:
        if (numbers.size() == 2U) return BindingValue{Size{number_of(numbers[0]), number_of(numbers[1])}};
        return BindingValue{Size{}};
    case ControlPropertyKind::point:
        if (numbers.size() == 2U) return BindingValue{Point{number_of(numbers[0]), number_of(numbers[1])}};
        return BindingValue{Point{}};
    case ControlPropertyKind::rectangle:
        if (numbers.size() == 4U) {
            return BindingValue{Rect{number_of(numbers[0]), number_of(numbers[1]), number_of(numbers[2]),
                                     number_of(numbers[3])}};
        }
        return BindingValue{Rect{}};
    case ControlPropertyKind::insets: {
        Insets insets{};
        if (numbers.size() == 1U) {
            const double all = number_of(numbers[0]);
            insets = Insets{all, all, all, all};
        } else if (numbers.size() == 2U) {
            // Two numbers are vertical then horizontal, as CSS writes them
            // (A-gf-core-03-F3: the model's checker admits this form).
            const double vertical = number_of(numbers[0]);
            const double horizontal = number_of(numbers[1]);
            insets.top = vertical;
            insets.bottom = vertical;
            insets.left = horizontal;
            insets.right = horizontal;
        } else if (numbers.size() == 4U) {
            // A form writes insets top right bottom left, as CSS does.
            insets.top = number_of(numbers[0]);
            insets.right = number_of(numbers[1]);
            insets.bottom = number_of(numbers[2]);
            insets.left = number_of(numbers[3]);
        }
        return BindingValue{insets};
    }
    // font and command store no default value: none.
    default: return BindingValue{};
    }
}

class DefinitionCompiler final {
public:
    DefinitionCompiler(const theme_language::ThemeSource& source, const ControlSyntax& syntax, const ControlModel& model,
                       CompositeDefinition& definition, theme_language::ThemeDiagnosticList& diagnostics) noexcept
        : source_(source), syntax_(syntax), model_(model), definition_(definition), diagnostics_(diagnostics) {}

    // Fills the definition; false when anything was refused.
    [[nodiscard]] bool run();

private:
    void compile_identity();
    void compile_properties();
    void compile_events_and_commands();
    void compile_states();
    void compile_parts();
    void compile_children();
    void compile_rules();
    void compile_focus_and_semantics();
    void build_anatomy();
    void compile_look();

    void parse(const std::string& text, CompositeExpression& expression, const ControlSource& where,
               const std::string& rule);
    [[nodiscard]] std::optional<std::size_t> property_index(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::size_t> command_index(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::size_t> event_index(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::size_t> part_index(std::string_view name) const noexcept;
    [[nodiscard]] const PropertyDescriptor* child_descriptor(std::string_view local_id,
                                                             std::string_view property) const noexcept;
    // Copies `element` (a form element inside <children>) into `made`
    // with its local ID prefixed and its bound and `self.` attributes taken
    // out into the definition.
    void copy_child(const XmlElement& element, XmlElement& made);
    void refuse(const ControlSource& where, const std::string& rule, std::string detail);

    const theme_language::ThemeSource& source_;
    const ControlSyntax& syntax_;
    const ControlModel& model_;
    CompositeDefinition& definition_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    bool refused_{};
};

bool DefinitionCompiler::run() {
    compile_identity();
    compile_properties();
    compile_events_and_commands();
    compile_states();
    compile_parts();
    compile_children();
    compile_rules();
    compile_focus_and_semantics();
    if (refused_) return false;
    build_anatomy();
    compile_look();
    return !refused_;
}

void DefinitionCompiler::compile_identity() {
    definition_.name = model_.name;
    definition_.registered = composite_registered_name(model_.name);
    definition_.version_major = model_.version_major;
    definition_.version_minor = model_.version_minor;
    definition_.base = model_.base;
    definition_.family = model_.family.value_or(ControlVisualRole::panel);
    definition_.look = model_.look;
}

void DefinitionCompiler::compile_properties() {
    // Names first, so a range or an alias may name a later property.
    definition_.properties.resize(model_.properties.size());
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        definition_.properties[index].name = model_.properties[index].name;
    }
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        const ControlProperty& written = model_.properties[index];
        CompositePropertyDefinition& made = definition_.properties[index];
        made.presentational = written.presentational;
        made.affects = written.affects;
        PropertyDescriptor descriptor{};
        descriptor.name = composite_registered_name(written.name);
        descriptor.category = written.category;
        descriptor.description = written.description;
        descriptor.invalidation_effects = written.affects;
        descriptor.change_notifications = true;
        descriptor.bindable = written.bindable != ControlBindable::no;
        descriptor.binding_direction = written.bindable == ControlBindable::two_way
            ? PropertyBindingDirection::two_way
            : PropertyBindingDirection::one_way;
        if (!written.alias.empty()) {
            if (!written.alias.starts_with("@")) {
                refuse(written.source, written.name, "an alias of another property of the composite");
                continue;
            }
            const std::string_view target = std::string_view(written.alias).substr(1U);
            const std::size_t dot = target.find('.');
            made.alias_child = std::string(target.substr(0U, dot));
            made.alias_property =
                dot == std::string_view::npos ? std::string{} : composite_registered_name(target.substr(dot + 1U));
            const PropertyDescriptor* aliased = child_descriptor(made.alias_child, made.alias_property);
            if (aliased == nullptr) {
                refuse(written.source, written.name, "the aliased child property is not found");
                continue;
            }
            descriptor.kind = (*aliased).kind;
            descriptor.enumeration = (*aliased).enumeration;
            descriptor.default_value = (*aliased).default_value;
            made.kind = ControlPropertyKind::text;
            made.descriptor = std::move(descriptor);
            continue;
        }
        const ControlPropertyKind kind = written.kind.value_or(ControlPropertyKind::text);
        made.kind = kind;
        made.command = kind == ControlPropertyKind::command;
        descriptor.kind = binding_kind_of(kind);
        if (kind == ControlPropertyKind::enumeration) {
            std::shared_ptr<PropertyEnumDescriptor> enumeration = std::make_shared<PropertyEnumDescriptor>();
            (*enumeration).type_name = definition_.registered + "." + descriptor.name;
            for (std::size_t choice = 0U; choice < written.choices.size(); ++choice) {
                PropertyEnumChoice entry{};
                entry.name = written.choices[choice];
                entry.value = static_cast<std::int64_t>(choice);
                (*enumeration).choices.push_back(std::move(entry));
            }
            descriptor.enumeration = std::move(enumeration);
        }
        made.initial = initial_value(written, kind, descriptor.enumeration);
        if (!made.presentational && !made.command) descriptor.default_value = made.initial;
        if (made.presentational) descriptor.nullable = true;
        made.descriptor = std::move(descriptor);
        if (!written.range.empty()) parse(written.range, made.range, written.source, written.name);
    }
}

void DefinitionCompiler::compile_events_and_commands() {
    for (const ControlEvent& written : model_.events) {
        CompositeEventDefinition made{};
        made.name = written.name;
        made.payload = written.payload;
        definition_.events.push_back(std::move(made));
        definition_.interaction_names.push_back(composite_registered_name(written.name));
    }
    for (std::size_t index = 0U; index < model_.properties.size(); ++index) {
        const std::string& changed = model_.properties[index].changed;
        if (!changed.empty()) definition_.properties[index].changed_event = event_index(changed);
    }
    for (const ControlCommand& written : model_.commands) {
        CompositeCommandDefinition made{};
        made.name = written.name;
        made.semantic_action = written.semantic_action;
        parse(written.enabled, made.enabled, written.source, written.name);
        parse(written.run, made.run, written.source, written.name);
        definition_.commands.push_back(std::move(made));
    }
}

void DefinitionCompiler::compile_states() {
    std::uint8_t custom = 0U;
    for (const ControlState& written : model_.states) {
        CompositeStateDefinition made{};
        made.name = written.name;
        made.registered = composite_registered_name(written.name);
        if (written.custom) {
            made.custom_index = CustomStateIndex{custom};
            ++custom;
        } else {
            made.standard = written.standard;
        }
        parse(written.when, made.when, written.source, written.name);
        definition_.states.push_back(std::move(made));
    }
}

void DefinitionCompiler::compile_parts() {
    CompositePartDefinition region{};
    region.name = "region";
    region.registered = "region";
    region.role = PartRole::region;
    definition_.parts.push_back(std::move(region));
    CompositePartDefinition focus{};
    focus.name = "focus";
    focus.registered = "focus";
    focus.role = PartRole::focus;
    definition_.parts.push_back(std::move(focus));
    // Names first, so a box may name a later part and a drag its range's.
    for (const ControlPart& written : model_.parts) {
        CompositePartDefinition made{};
        made.name = written.name;
        made.registered = composite_registered_name(written.name);
        definition_.parts.push_back(std::move(made));
    }
    for (std::size_t index = 0U; index < model_.parts.size(); ++index) {
        const ControlPart& written = model_.parts[index];
        CompositePartDefinition& made = definition_.parts[index + 2U];
        made.role = written.role;
        made.composed_from = composite_registered_name(written.composed_from);
        made.states = written.states;
        made.shape = written.shape;
        made.corner_radius = written.corner_radius;
        made.hit = written.hit;
        made.hit_expand = written.hit_expand;
        made.press = written.press;
        made.mark = written.mark;
        made.paint_hook = written.paint_hook;
        parse(written.box, made.box, written.source, written.name);
        parse(written.present, made.present, written.source, written.name);
        parse(written.disabled_when, made.disabled_when, written.source, written.name);
        if (!written.activates.empty()) made.activates = command_index(written.activates);
        // The text a label shows: the property its box measures.
        for (const ExpressionNode& node : made.box.parsed.nodes) {
            if (node.kind != ExpressionNodeKind::call || node.text != "text-extent" || node.argument_count != 1U) {
                continue;
            }
            const ExpressionNode& argument = made.box.parsed.nodes[made.box.parsed.arguments[node.first_argument].value];
            if (argument.kind == ExpressionNodeKind::word) made.text_property = property_index(argument.text);
        }
        if (!written.drag) continue;
        const ControlDrag& drag = model_.drags[*written.drag];
        if (drag.kind != ControlDragKind::value) {
            refuse(drag.source, written.name, "placement and reorder drags are not run yet");
            continue;
        }
        if (drag.axis != ControlDragAxis::inline_axis && drag.axis != ControlDragAxis::block_axis) {
            refuse(drag.source, written.name, "a value drag runs along inline or block");
            continue;
        }
        CompositeDragDefinition made_drag{};
        made_drag.mode = drag.mode;
        made_drag.axis = drag.axis;
        made_drag.threshold = drag.threshold;
        made_drag.settles = drag.settles;
        const std::optional<std::size_t> target = property_index(drag.maps_to);
        made_drag.maps_to = target.value_or(0U);
        parse(drag.range, made_drag.range, drag.source, written.name);
        parse(drag.preview, made_drag.preview, drag.source, written.name);
        if (!drag.keyboard.empty()) {
            const std::optional<CompositeKeyGesture> gesture = composite_key_gesture(drag.keyboard);
            if (gesture) {
                made_drag.keyboard_key = (*gesture).key;
                made_drag.keyboard_modifiers = (*gesture).modifiers;
            }
        }
        made.drag = definition_.drags.size();
        definition_.drags.push_back(std::move(made_drag));
    }
}

void DefinitionCompiler::copy_child(const XmlElement& element, XmlElement& made) {
    const bool slot = element.name == slot_element;
    // The slot is a container of the base type: both bases run on
    // UserControl, which scrolls.
    made.name = slot ? std::string(slot_type) : element.name;
    const std::string registered_type = composite_registered_name(made.name);
    std::optional<std::size_t> own_type{};
    for (std::size_t index = 0U; index < definition_.schema.types.size(); ++index) {
        if (definition_.schema.types[index].name == registered_type) own_type = index;
    }
    std::string local_id{};
    const XmlAttribute* identity = element.find_attribute(form_language::form_identity_attribute);
    if (identity != nullptr && (*identity).value.starts_with("@")) local_id = (*identity).value.substr(1U);
    if (slot) definition_.slot = local_id;
    for (const XmlAttribute& attribute : element.attributes) {
        if (attribute.name == form_language::form_identity_attribute) {
            XmlAttribute renamed{};
            renamed.name = attribute.name;
            renamed.value = std::string(composite_child_prefix) + "." + local_id;
            made.attributes.push_back(std::move(renamed));
            continue;
        }
        const std::string property = composite_registered_name(attribute.name);
        if (attribute.value.starts_with(bind_prefix)) {
            // bind(self.<property>[, option]...), as the model checked it.
            const std::string_view value = attribute.value;
            const std::size_t close = value.rfind(')');
            const std::string_view inside = value.substr(bind_prefix.size(), close - bind_prefix.size());
            const std::size_t comma = inside.find(',');
            std::string_view source_name = inside.substr(0U, comma);
            while (!source_name.empty() && source_name.back() == ' ') source_name.remove_suffix(1U);
            while (!source_name.empty() && source_name.front() == ' ') source_name.remove_prefix(1U);
            const std::string_view options =
                comma == std::string_view::npos ? std::string_view{} : inside.substr(comma + 1U);
            const bool narrowed = options.find("one_way") != std::string_view::npos;
            CompositeChildBinding binding{};
            binding.child = local_id;
            binding.property = property;
            // The checker admits a base type's property too (any_property);
            // the composite reads and writes its own by index, so a name it
            // does not declare is refused, never compiled to index 0
            // (A-gf-core-03-F1).
            const std::string_view source_property = source_name.substr(self_prefix.size());
            const std::optional<std::size_t> source_index = property_index(source_property);
            if (!source_index) {
                refuse(model_.source, property,
                       "a binding to '" + std::string(source_property) + "', not a property the composite declares");
                continue;
            }
            binding.source = *source_index;
            const PropertyDescriptor* descriptor = nullptr;
            if (own_type) {
                for (const PropertyDescriptor& candidate : definition_.schema.types[*own_type].properties) {
                    if (candidate.name == property) descriptor = &candidate;
                }
            }
            binding.two_way = descriptor != nullptr &&
                              (*descriptor).binding_direction == PropertyBindingDirection::two_way && !narrowed;
            definition_.child_bindings.push_back(std::move(binding));
            continue;
        }
        if (attribute.value.find(self_prefix) != std::string::npos) {
            CompositeChildExpression expression{};
            expression.child = local_id;
            expression.property = property;
            ControlSource where{};
            where.position = model_.source.position;
            parse(attribute.value, expression.expression, where, "@" + local_id);
            definition_.child_expressions.push_back(std::move(expression));
            continue;
        }
        made.attributes.push_back(attribute);
    }
    for (const XmlElement& child : element.children) {
        XmlElement copied{};
        if (child.name == span_element) {
            copied = child;
        } else {
            copy_child(child, copied);
        }
        made.children.push_back(std::move(copied));
    }
}

void DefinitionCompiler::compile_children() {
    const XmlElement* written = nullptr;
    for (const XmlElement& element : syntax_.root.children) {
        if (element.name == "children") written = &element;
    }
    if (written == nullptr || (*written).children.empty()) return;
    XmlElement root{};
    root.name = std::string(slot_type);
    XmlAttribute space{};
    space.name = std::string(form_language::form_namespace_attribute);
    space.value = std::string(form_language::form_namespace);
    root.attributes.push_back(std::move(space));
    XmlAttribute identity{};
    identity.name = std::string(form_language::form_identity_attribute);
    identity.value = std::string(composite_child_prefix);
    root.attributes.push_back(std::move(identity));
    for (const XmlElement& child : (*written).children) {
        XmlElement copied{};
        if (child.name == span_element) {
            copied = child;
        } else {
            copy_child(child, copied);
        }
        root.children.push_back(std::move(copied));
    }
    const XmlWriteResult document = write_xml(root);
    if (!document) {
        refuse(model_.source, model_.name, "the children's document cannot be written");
        return;
    }
    definition_.children_file = std::string(source_.file) + " <children>";
    definition_.children_document = document.text;
    theme_language::ThemeSource children_source{};
    children_source.file = definition_.children_file;
    children_source.text = definition_.children_document;
    form_language::FormSyntax children_syntax{};
    const form_language::FormModelOutcome built = form_language::read_form_model(
        children_source, children_syntax, definition_.schema, definition_.children, diagnostics_);
    if (built != form_language::FormModelOutcome::model_built) refused_ = true;
    const std::string prefix = std::string(composite_child_prefix) + ".";
    for (const form_language::FormObject& object : definition_.children.objects) {
        const std::string& stable_id = object.stable_id;
        std::string local = stable_id.starts_with(prefix) ? stable_id.substr(prefix.size()) : std::string{};
        definition_.local_ids.push_back(std::move(local));
    }
}

void DefinitionCompiler::compile_rules() {
    for (const ControlRule& written : model_.rules) {
        CompositeRuleDefinition made{};
        made.phase = written.phase;
        made.claims_dialog_key = written.claims_dialog_key;
        parse(written.when, made.when, written.source, written.trigger);
        if (written.key) {
            made.trigger = CompositeTrigger::key;
            const std::optional<CompositeKeyGesture> gesture = composite_key_gesture(written.trigger);
            if (!gesture) {
                refuse(written.source, written.trigger, "the gesture names no key");
                continue;
            }
            made.key = (*gesture).key;
            made.modifiers = (*gesture).modifiers;
        } else {
            CompositeExpression trigger{};
            parse(written.trigger, trigger, written.source, written.trigger);
            if (trigger.empty()) continue;
            const ExpressionNode& term = trigger.parsed.nodes[trigger.parsed.terms[0].value];
            std::string subject{};
            std::string argument_word{};
            if (term.kind == ExpressionNodeKind::call && term.argument_count == 1U) {
                const ExpressionNode& argument = trigger.parsed.nodes[trigger.parsed.arguments[term.first_argument].value];
                if (argument.kind == ExpressionNodeKind::local_reference) subject = argument.text;
                if (argument.kind == ExpressionNodeKind::word && argument.text != "self") argument_word = argument.text;
            }
            const std::string& name = term.text;
            if (name == "click") {
                made.trigger = CompositeTrigger::click;
            } else if (name == "mouse-down") {
                made.trigger = CompositeTrigger::mouse_down;
            } else if (name == "mouse-up") {
                made.trigger = CompositeTrigger::mouse_up;
            } else if (name == "activated") {
                made.trigger = CompositeTrigger::activated;
            } else if (name == "focus-gained") {
                made.trigger = CompositeTrigger::focus_gained;
            } else if (name == "focus-lost") {
                made.trigger = CompositeTrigger::focus_lost;
            } else if (name == "mouse-wheel") {
                made.trigger = CompositeTrigger::mouse_wheel;
            } else if (name == "changed") {
                made.trigger = CompositeTrigger::changed;
                made.property = property_index(argument_word);
            } else if (name.ends_with("-changed") && !subject.empty()) {
                made.trigger = CompositeTrigger::child_property_changed;
                made.child_property = composite_registered_name(name.substr(0U, name.size() - 8U));
            } else {
                refuse(written.source, written.trigger, "a child's typed interaction other than a property change");
                continue;
            }
            made.child = subject;
        }
        made.first_action = definition_.actions.size();
        for (std::size_t index = 0U; index < written.action_count; ++index) {
            const ControlAction& action = model_.actions[written.first_action + index];
            CompositeActionDefinition compiled{};
            compiled.kind = action.kind;
            parse(action.value, compiled.value, action.source, written.trigger);
            std::optional<std::size_t> found{};
            switch (action.kind) {
            case ControlActionKind::set:
            case ControlActionKind::toggle:
            case ControlActionKind::step:
            case ControlActionKind::invoke:
                // As for a child binding: a base type's property, or any
                // name the composite does not declare, is refused rather
                // than compiled to index 0 (A-gf-core-03-F1).
                found = property_index(action.target);
                if (!found) refuse(action.source, written.trigger, "the action's property is not one the composite declares");
                break;
            case ControlActionKind::run:
                found = command_index(action.target);
                if (!found) refuse(action.source, written.trigger, "the action's command is not one the composite declares");
                break;
            case ControlActionKind::raise:
                found = event_index(action.target);
                if (!found) refuse(action.source, written.trigger, "the action's event is not one the composite declares");
                break;
            case ControlActionKind::focus: {
                std::string_view target = action.target;
                if (target.starts_with("child(")) target = target.substr(6U, target.size() - 7U);
                if (target.starts_with("@")) compiled.child = std::string(target.substr(1U));
                break;
            }
            case ControlActionKind::open:
            case ControlActionKind::close:
            default:
                refuse(action.source, written.trigger, "open and close wait for overlays in composites");
                break;
            }
            if (found) compiled.target = *found;
            definition_.actions.push_back(std::move(compiled));
        }
        made.action_count = definition_.actions.size() - made.first_action;
        definition_.rules.push_back(std::move(made));
    }
}

void DefinitionCompiler::compile_focus_and_semantics() {
    if (model_.focus) {
        const ControlFocus& written = *model_.focus;
        definition_.focus.target = written.target;
        definition_.focus.child = written.child;
        definition_.focus.tab_stop = written.tab_stop || written.target == ControlFocusTarget::self;
        if (!written.visible_cue.empty()) definition_.focus.visible_cue = part_index(written.visible_cue);
        if (written.mnemonic.starts_with("property(") && written.mnemonic.ends_with(")")) {
            const std::string_view inside =
                std::string_view(written.mnemonic).substr(9U, written.mnemonic.size() - 10U);
            definition_.focus.mnemonic = property_index(inside);
        }
    }
    if (!model_.semantics) return;
    const ControlSemantics& written = *model_.semantics;
    CompositeSemanticsDefinition made{};
    made.role = written.role;
    made.children_exposed = written.children_exposed;
    made.from_child = written.from_child;
    parse(written.name, made.name, written.source, "semantics");
    parse(written.value, made.value, written.source, "semantics");
    parse(written.range, made.range, written.source, "semantics");
    parse(written.states, made.states_expression, written.source, "semantics");
    for (const ExpressionNodeIndex term : made.states_expression.parsed.terms) {
        const ExpressionNode& node = made.states_expression.parsed.nodes[term.value];
        if (node.kind != ExpressionNodeKind::call || node.argument_count != 1U) continue;
        const std::string state = composite_registered_name(node.text);
        for (const SemanticState candidate : semantic_states) {
            if (state != semantic_state_name(candidate)) continue;
            CompositeSemanticState entry{};
            entry.state = candidate;
            entry.condition = made.states_expression.parsed.arguments[node.first_argument];
            made.states.push_back(entry);
        }
    }
    CompositeExpression actions{};
    parse(written.actions, actions, written.source, "semantics");
    for (const ExpressionNodeIndex term : actions.parsed.terms) {
        const ExpressionNode& node = actions.parsed.nodes[term.value];
        if (node.kind != ExpressionNodeKind::call || node.argument_count != 1U) continue;
        const ExpressionNode& argument = actions.parsed.nodes[actions.parsed.arguments[node.first_argument].value];
        const std::string action = composite_registered_name(node.text);
        for (std::size_t index = 0U; index < semantic_action_count; ++index) {
            const SemanticAction candidate = static_cast<SemanticAction>(index);
            if (action != semantic_action_name(candidate)) continue;
            CompositeSemanticAction entry{};
            entry.action = candidate;
            entry.command = command_index(argument.text);
            if (!entry.command) entry.property = property_index(argument.text);
            made.actions.push_back(entry);
        }
    }
    definition_.semantics = std::move(made);
}

void DefinitionCompiler::build_anatomy() {
    // The states the composite shows: every control's, hot and pressed for
    // its parts, captured for a drag, then the published ones.
    const std::array<AnatomyState, 8> shown{AnatomyState::normal,  AnatomyState::enabled,   AnatomyState::disabled,
                                            AnatomyState::focused, AnatomyState::focus_visible, AnatomyState::hot,
                                            AnatomyState::pressed, AnatomyState::captured};
    for (const AnatomyState state : shown) {
        if (state == AnatomyState::captured && definition_.drags.empty()) continue;
        definition_.anatomy_states.push_back(state);
    }
    for (const CompositeStateDefinition& state : definition_.states) {
        if (!state.standard) continue;
        const bool listed =
            std::find(definition_.anatomy_states.begin(), definition_.anatomy_states.end(), *state.standard) !=
            definition_.anatomy_states.end();
        if (!listed) definition_.anatomy_states.push_back(*state.standard);
    }
    for (const CompositeStateDefinition& state : definition_.states) {
        if (!state.custom_index) continue;
        CustomStateDeclaration declaration{};
        declaration.name = state.registered;
        definition_.custom_states.push_back(declaration);
    }
    definition_.part_declarations.reserve(definition_.parts.size());
    for (const CompositePartDefinition& part : definition_.parts) {
        PartDeclaration declaration{};
        declaration.name = part.registered;
        declaration.role = part.role;
        declaration.composed_from = part.composed_from;
        declaration.states = std::span<const AnatomyState>(part.states.data(), part.states.size());
        definition_.part_declarations.push_back(declaration);
    }
    definition_.anatomy.control_id = definition_.registered;
    definition_.anatomy.parts = definition_.part_declarations;
    definition_.anatomy.states = definition_.anatomy_states;
    definition_.anatomy.custom_states = definition_.custom_states;
}

// The <look>, compiled alone once for the definition against the stock
// anatomies and the composite's own, so its rules name the composite's
// type, parts and states and the stock types of its children. Its findings
// name `<file> <look>`, at positions in the look's text.
// - It compiles and reads no custom property it does not define: it is
//   theme-independent, and those rules serve every theme.
// - It reads a custom property it does not define, or is refused only for
//   names a theme may supply (an unknown keyword, such as an application
//   colour role, or an undefined custom property): it is theme-bound,
//   compiled with each theme's text before resolution
//   (detail::CompositeLook).
// - Any other error refuses the definition.
void DefinitionCompiler::compile_look() {
    if (definition_.look.empty()) return;
    std::vector<ControlAnatomy> controls{};
    const std::span<const ControlAnatomy> stock = stock_control_anatomies();
    controls.reserve(stock.size() + 1U);
    controls.insert(controls.end(), stock.begin(), stock.end());
    controls.push_back(definition_.anatomy);
    std::string file = std::string(source_.file) + " <look>";
    const theme_language::ThemeSource look_source{.file = file, .text = definition_.look};
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const theme_language::ThemeCompileInput input{.source = look_source,
                                                  .registry = registry,
                                                  .vocabulary = theme_language::SelectorVocabulary{controls},
                                                  // A look may name the application's font families.
                                                  .admit_app_fonts = true};
    ThemeRuleSheet sheet{};
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList found{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, sheet, found);
    definition_.look_compiles = definition_.look_compiles + 1U;
    std::vector<std::string> defined{};
    compiler.append_defined_names(defined);
    std::sort(defined.begin(), defined.end());
    const bool compiled = outcome == theme_language::CompileOutcome::compiled;
    if (compiled && !reads_undefined_name(definition_.look, defined)) {
        for (const theme_language::ThemeDiagnostic& diagnostic : found.records()) diagnostics_.add(diagnostic);
        std::shared_ptr<const CompiledThemeRules> rules =
            std::make_shared<const CompiledThemeRules>(CompiledThemeRules::compile_look(sheet, registry));
        definition_.compiled_look =
            std::make_shared<detail::CompositeLook>(definition_.look, std::move(file), std::move(rules));
        return;
    }
    if (!compiled && !deferrable_to_theme(found)) {
        for (const theme_language::ThemeDiagnostic& diagnostic : found.records()) diagnostics_.add(diagnostic);
        refused_ = true;
        return;
    }
    // Its findings come again from each theme's compilation, placed alike.
    definition_.compiled_look =
        std::make_shared<detail::CompositeLook>(definition_.look, std::move(file), std::move(controls));
}

void DefinitionCompiler::parse(const std::string& text, CompositeExpression& expression, const ControlSource& where,
                               const std::string& rule) {
    expression.text = text;
    const ExpressionParseResult result = parse_expressions(text, expression.parsed);
    if (result.valid) return;
    // The model parsed every expression; one refused here is an internal
    // mismatch, reported rather than run.
    expression.parsed.clear();
    refuse(where, rule, "the expression '" + text + "' does not parse: " + result.detail);
}

std::optional<std::size_t> DefinitionCompiler::property_index(const std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < definition_.properties.size(); ++index) {
        if (definition_.properties[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> DefinitionCompiler::command_index(const std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < model_.commands.size(); ++index) {
        if (model_.commands[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> DefinitionCompiler::event_index(const std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < model_.events.size(); ++index) {
        if (model_.events[index].name == name) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> DefinitionCompiler::part_index(const std::string_view name) const noexcept {
    for (std::size_t index = 0U; index < definition_.parts.size(); ++index) {
        if (definition_.parts[index].name == name) return index;
    }
    return std::nullopt;
}

const PropertyDescriptor* DefinitionCompiler::child_descriptor(const std::string_view local_id,
                                                               const std::string_view property) const noexcept {
    for (const ControlChild& child : model_.children) {
        if (child.local_id != local_id || !child.schema_type) continue;
        for (const PropertyDescriptor& candidate : definition_.schema.types[*child.schema_type].properties) {
            if (candidate.name == property) return &candidate;
        }
    }
    return nullptr;
}

void DefinitionCompiler::refuse(const ControlSource& where, const std::string& rule, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = DiagnosticCode::not_yet_built;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = where.position;
    diagnostic.rule = rule;
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    refused_ = true;
}

} // namespace

std::string composite_registered_name(const std::string_view written) {
    std::string registered(written);
    std::replace(registered.begin(), registered.end(), '-', '_');
    return registered;
}

std::optional<CompositeKeyGesture> composite_key_gesture(const std::string_view gesture) noexcept {
    CompositeKeyGesture made{};
    std::string_view rest = gesture;
    std::size_t plus = rest.find('+');
    while (plus != std::string_view::npos && plus + 1U < rest.size()) {
        const std::string_view modifier = rest.substr(0U, plus);
        if (modifier == "ctrl") {
            made.modifiers = made.modifiers | Modifier::control;
        } else if (modifier == "shift") {
            made.modifiers = made.modifiers | Modifier::shift;
        } else if (modifier == "alt") {
            made.modifiers = made.modifiers | Modifier::alt;
        } else if (modifier == "meta") {
            made.modifiers = made.modifiers | Modifier::meta;
        } else {
            return std::nullopt;
        }
        rest = rest.substr(plus + 1U);
        plus = rest.find('+');
    }
    for (const NamedKey& named : named_keys) {
        if (named.name == rest) {
            made.key = named.key;
            return made;
        }
    }
    if (rest.size() == 1U && rest[0] >= 'a' && rest[0] <= 'z') {
        made.key = PhysicalKey::a + static_cast<std::uint32_t>(rest[0] - 'a');
        return made;
    }
    if (rest.size() == 1U && rest[0] >= '1' && rest[0] <= '9') {
        made.key = first_digit_key + static_cast<std::uint32_t>(rest[0] - '1');
        return made;
    }
    if (rest == "0") {
        made.key = zero_key;
        return made;
    }
    if (rest.size() >= 2U && rest.size() <= 3U && rest[0] == 'f') {
        std::uint32_t number = 0U;
        for (std::size_t index = 1U; index < rest.size(); ++index) {
            if (rest[index] < '0' || rest[index] > '9') return std::nullopt;
            number = number * 10U + static_cast<std::uint32_t>(rest[index] - '0');
        }
        if (number >= 1U && number <= 12U) {
            made.key = PhysicalKey::f1 + number - 1U;
            return made;
        }
        if (number >= 13U && number <= 24U) {
            made.key = f13_key + number - 13U;
            return made;
        }
    }
    return std::nullopt;
}

std::shared_ptr<const CompositeDefinition> compile_composite_definition(
    const theme_language::ThemeSource& source, const ControlSyntax& syntax, const ControlModel& model,
    form_language::FormSchema schema, theme_language::ThemeDiagnosticList& diagnostics) {
    std::shared_ptr<CompositeDefinition> definition = std::make_shared<CompositeDefinition>();
    (*definition).schema = std::move(schema);
    DefinitionCompiler compiler(source, syntax, model, *definition, diagnostics);
    if (!compiler.run()) return nullptr;
    std::shared_ptr<const CompositeDefinition> compiled = std::move(definition);
    return compiled;
}

} // namespace gui_forms::control_language

namespace gui_forms::detail {

CompositeLook::CompositeLook(std::string text, std::string file, std::shared_ptr<const CompiledThemeRules> rules)
    : text_(std::move(text)), file_(std::move(file)), independent_(std::move(rules)) {}

CompositeLook::CompositeLook(std::string text, std::string file, std::vector<ControlAnatomy> controls)
    : text_(std::move(text)), file_(std::move(file)), controls_(std::move(controls)), theme_bound_(true) {
    entries_.reserve(maximum_composite_look_entries + 1U);
}

CompositeLook::~CompositeLook() = default;

void CompositeLook::prepare(const Theme& theme, std::uint64_t& held) {
    if (!theme_bound_) return;
    const std::uint64_t serial = theme.engine().rules().serial();
    use_clock_ = use_clock_ + 1U;
    if (held != serial) release(held);
    for (ThemeEntry& entry : entries_) {
        if (entry.theme_serial != serial) continue;
        entry.used = use_clock_;
        if (held != serial) {
            entry.holders = entry.holders + 1U;
            held = serial;
        }
        if (last_serial_ != 0U && last_serial_ != serial) theme_reuses_ = theme_reuses_ + 1U;
        last_serial_ = serial;
        return;
    }
    // First preparation under this theme: compiled with its text.
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::shared_ptr<const CompiledThemeRules> compiled = compile_theme_look(text_, file_, controls_, theme, diagnostics);
    theme_compiles_ = theme_compiles_ + 1U;
    if (compiled == nullptr) {
        theme_refusals_ = theme_refusals_ + 1U;
        // Kept as refused, with one log line naming the first error.
        for (const ThemeCompileDiagnostic& diagnostic : diagnostics) {
            if (diagnostic.severity != ThemeDiagnosticSeverity::error) continue;
            const std::string line = format_theme_diagnostic(diagnostic);
            std::fprintf(stderr, "%s: look refused under theme %.*s: %s\n", file_.c_str(),
                         static_cast<int>(theme.id().size()), theme.id().data(), line.c_str());
            break;
        }
    }
    ThemeEntry made{};
    made.theme_serial = serial;
    made.rules = std::move(compiled);
    made.used = use_clock_;
    made.holders = 1U;
    entries_.push_back(std::move(made));
    held = serial;
    last_serial_ = serial;
    evict_unheld();
}

void CompositeLook::release(std::uint64_t& held) noexcept {
    if (held == 0U) return;
    for (ThemeEntry& entry : entries_) {
        if (entry.theme_serial != held || entry.holders == 0U) continue;
        entry.holders = entry.holders - 1U;
        break;
    }
    held = 0U;
    evict_unheld();
}

void CompositeLook::evict_unheld() noexcept {
    std::size_t unheld = 0U;
    for (const ThemeEntry& entry : entries_) {
        if (entry.holders == 0U) unheld = unheld + 1U;
    }
    while (unheld > maximum_composite_look_entries) {
        std::size_t oldest = entries_.size();
        for (std::size_t index = 0U; index < entries_.size(); ++index) {
            if (entries_[index].holders != 0U) continue;
            if (oldest == entries_.size() || entries_[index].used < entries_[oldest].used) oldest = index;
        }
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(oldest));
        unheld = unheld - 1U;
    }
}

std::size_t CompositeLook::held_count() const noexcept {
    std::size_t held = 0U;
    for (const ThemeEntry& entry : entries_) {
        if (entry.holders != 0U) held = held + 1U;
    }
    return held;
}

const CompiledThemeRules* CompositeLook::rules_for(const Theme& theme) noexcept {
    if (!theme_bound_) return independent_.get();
    const std::uint64_t serial = theme.engine().rules().serial();
    for (const ThemeEntry& entry : entries_) {
        if (entry.theme_serial == serial) return entry.rules.get();
    }
    // A defect: no fixed point prepared this theme. Nothing is compiled
    // here; the look draws nothing for the part.
    missing_ = missing_ + 1U;
    return nullptr;
}

} // namespace gui_forms::detail
