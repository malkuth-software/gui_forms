#include "winforms_conversion.hpp"

#include "winforms_mapping.hpp"

#include "../../src/core/form_language/form_xml/form_xml.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace gui_forms::winforms_converter {

namespace {

using form_language::FormSchema;
using gui_forms::BindingValueKind;
using gui_forms::PropertyDescriptor;
using gui_forms::PropertyEnumChoice;
using gui_forms::PropertyEnumDescriptor;
using gui_forms::PropertySerializationVisibility;

constexpr std::size_t no_object = std::numeric_limits<std::size_t>::max();
constexpr std::size_t no_type = std::numeric_limits<std::size_t>::max();
// The longest attribute line written on the element's own line; longer
// start tags put each attribute on a line of its own.
constexpr std::size_t maximum_inline_tag = 100U;

// One attribute of a converted object, in the order the designer set it.
struct ConvertedAttribute final {
    std::string name{};
    std::string value{};
};

struct ConvertedBinding final {
    std::string property{};
    std::string to{};
    std::string field{};
    std::string update{};
};

struct ConvertedEvent final {
    std::string winforms_event{};
    std::string interaction{};
    std::string handler{};
    // How the C# binding calls the handler: its second argument.
    std::string arguments{};
};

// Location and Size, gathered until the object is written: one `bounds`.
struct PendingGeometry final {
    bool location{};
    bool size{};
    double x{};
    double y{};
    double width{};
    double height{};
    // The attribute slot `bounds` takes: where the first of the two was set.
    std::size_t slot{};
};

struct ConvertedObject final {
    // The path the designer reaches it by (`button1`,
    // `splitContainer1.Panel1`); empty for the root.
    std::string path{};
    std::string field{};
    std::string winforms_type{};
    const WinFormsTypeRow* winforms_row{};
    // The registry type written (snake case) and its schema entry.
    std::string type{};
    std::size_t schema_type{no_type};
    std::string stable_id{};
    std::size_t parent{no_object};
    // In the order the designer added them (Controls.Add).
    std::vector<std::size_t> added{};
    std::vector<ConvertedAttribute> attributes{};
    std::vector<ConvertedBinding> bindings{};
    std::vector<ConvertedEvent> events{};
    std::vector<std::string> theme_declarations{};
    std::vector<std::string> code_lines{};
    // A text collection's items (`items`), written as one attribute.
    std::vector<std::string> items{};
    PendingGeometry geometry{};
    bool unmapped{};
    // A component: never a control's child; written inside the root.
    bool component{};
    std::uint32_t line{};
    std::uint32_t column{};
};

// The value a property is given: a designer expression, or a resource
// entry's text (ApplyResources).
struct SourceValue final {
    std::uint32_t expression{no_expression};
    const ResxEntry* resource{};
    std::uint32_t line{};
    std::uint32_t column{};
};

// A converted value, or why there is none.
struct ValueText final {
    bool converted{};
    std::string text{};
    std::string problem{};
};

[[nodiscard]] ValueText value_text(std::string text) {
    ValueText value{};
    value.converted = true;
    value.text = std::move(text);
    return value;
}

[[nodiscard]] ValueText value_problem(std::string problem) {
    ValueText value{};
    value.problem = std::move(problem);
    return value;
}

// A number as a form document writes it: an integer without a fraction,
// otherwise the shortest text that reads back as the same double.
[[nodiscard]] std::string number_text(double value) {
    if (value == 0.0) return "0";
    char buffer[64]{};
    const double rounded = std::nearbyint(value);
    if (rounded == value && std::fabs(value) < 9.0e15) {
        const std::to_chars_result written =
            std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(rounded));
        std::string text(buffer, written.ptr);
        return text;
    }
    const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string text(buffer, written.ptr);
    return text;
}

[[nodiscard]] std::optional<double> parse_number(std::string_view digits) {
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        unsigned long long hexadecimal = 0U;
        const std::from_chars_result read =
            std::from_chars(digits.data() + 2, digits.data() + digits.size(), hexadecimal, 16);
        if (read.ec != std::errc() || read.ptr != digits.data() + digits.size()) return std::nullopt;
        return static_cast<double>(hexadecimal);
    }
    double value = 0.0;
    const std::from_chars_result read = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (read.ec != std::errc() || read.ptr != digits.data() + digits.size()) return std::nullopt;
    return value;
}

// The numbers of a resource's text: "12, 12" gives 12 and 12.
[[nodiscard]] std::vector<double> resource_numbers(std::string_view text) {
    std::vector<double> numbers{};
    std::size_t start = 0U;
    while (start <= text.size()) {
        std::size_t comma = text.find(',', start);
        if (comma == std::string_view::npos) comma = text.size();
        std::string_view part = text.substr(start, comma - start);
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1U);
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1U);
        while (!part.empty() && (part.back() == 'F' || part.back() == 'f')) part.remove_suffix(1U);
        if (!part.empty()) {
            const std::optional<double> number = parse_number(part);
            if (!number) return std::vector<double>();
            numbers.push_back(*number);
        }
        start = comma + 1U;
    }
    return numbers;
}

// Whether a word may go into a text collection unquoted.
[[nodiscard]] bool plain_item(std::string_view item) noexcept {
    if (item.empty() || item.front() == '\'' || item.front() == '"') return false;
    for (const char byte : item) {
        const bool blank = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
        if (blank || byte == '\\') return false;
    }
    return true;
}

// A colour of .NET's known colours that CSS shares, by Windows Forms name.
struct KnownColour final {
    std::string_view name{};
    std::string_view hex{};
};

constexpr KnownColour known_colours[] = {
    {"Black", "#000000"},   {"Blue", "#0000FF"},     {"Brown", "#A52A2A"},       {"Cyan", "#00FFFF"},
    {"DarkBlue", "#00008B"}, {"DarkGray", "#A9A9A9"}, {"DarkGreen", "#006400"},  {"DarkRed", "#8B0000"},
    {"Gold", "#FFD700"},    {"Gray", "#808080"},     {"Green", "#008000"},       {"LightBlue", "#ADD8E6"},
    {"LightGray", "#D3D3D3"}, {"LightGreen", "#90EE90"}, {"LightYellow", "#FFFFE0"}, {"Lime", "#00FF00"},
    {"Magenta", "#FF00FF"}, {"Maroon", "#800000"},   {"Navy", "#000080"},        {"Olive", "#808000"},
    {"Orange", "#FFA500"},  {"Pink", "#FFC0CB"},     {"Purple", "#800080"},      {"Red", "#FF0000"},
    {"Silver", "#C0C0C0"},  {"Teal", "#008080"},     {"Transparent", "#00000000"}, {"White", "#FFFFFF"},
    {"WhiteSmoke", "#F5F5F5"}, {"Yellow", "#FFFF00"},
};

[[nodiscard]] std::string hex_colour(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) {
    char buffer[16]{};
    if (alpha == 255U) {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", red & 0xFFU, green & 0xFFU, blue & 0xFFU);
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", red & 0xFFU, green & 0xFFU, blue & 0xFFU,
                      alpha & 0xFFU);
    }
    std::string text(buffer);
    return text;
}

// The CSS property a colour property of GUI.Forms is written as.
[[nodiscard]] std::string theme_colour_property(std::string_view gui_name) {
    if (gui_name == "back_color") return "background-color";
    if (gui_name == "fore_color") return "color";
    std::string kebab(gui_name);
    std::replace(kebab.begin(), kebab.end(), '_', '-');
    return kebab;
}

// The C# binding's text of one handler call argument by parameter type.
[[nodiscard]] std::string handler_arguments(const std::vector<std::string>& parameters) {
    if (parameters.size() != 2U) return std::string();
    const std::string_view argument = last_segment(parameters[1]);
    if (argument == "EventArgs") return "System.EventArgs.Empty";
    if (argument == "MouseEventArgs") return "GuiForms.Forms.MouseEventArgs.from_native(@event)";
    return std::string();
}

[[nodiscard]] bool type_is_form_root(std::string_view type) noexcept {
    const bool root = type == form_language::form_root_type_name;
    return root;
}

class Converter final {
public:
    Converter(const DesignerDocument& designer, const ResxDocument* resx, const CSharpClassIndex& classes,
              const FormSchema& schema, const ConversionOptions& options, ConversionDiagnosticList& diagnostics)
        : designer_(designer), resx_(resx), classes_(classes), schema_(schema), options_(options),
          diagnostics_(diagnostics) {}

    [[nodiscard]] ConversionOutcome run(ConvertedForm& form) {
        form_id_ = options_.form_id.empty() ? stable_id_segment_of(designer_.class_name) : options_.form_id;
        if (form_id_.empty()) form_id_ = "form";
        const bool rooted = make_root();
        if (!rooted) return ConversionOutcome::refused;
        for (const DesignerStatement& statement : designer_.statements) read_statement(statement);
        place_unadded();
        assign_identities();
        ConvertedForm made{};
        made.class_name = designer_.class_name;
        made.form_id = form_id_;
        made.form_file = options_.form_file.empty() ? form_id_ + ".form" : options_.form_file;
        made.binding_file = options_.binding_file.empty() ? designer_.class_name + ".GuiForms.g.cs" : options_.binding_file;
        write_document(made.form_document);
        write_theme(made.form_file, made.theme_sheet);
        if (!made.theme_sheet.empty()) {
            made.theme_file = options_.theme_file.empty() ? form_id_ + ".theme.css" : options_.theme_file;
        }
        write_binding(made);
        made.sources = sources_;
        made.counts = counts_;
        made.counts.objects = objects_.size();
        form = std::move(made);
        return ConversionOutcome::converted;
    }

private:
    // ----- Diagnostics -----

    void report(ConversionCode code, std::uint32_t line, std::uint32_t column, std::string subject, std::string detail) {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = code;
        diagnostic.file = designer_.file;
        diagnostic.line = line;
        diagnostic.column = column;
        diagnostic.subject = std::move(subject);
        diagnostic.detail = std::move(detail);
        diagnostics_.add(std::move(diagnostic));
    }

    // ----- Schema -----

    [[nodiscard]] std::size_t schema_type_named(std::string_view name) const noexcept {
        for (std::size_t index = 0U; index < schema_.types.size(); ++index) {
            if (schema_.types[index].name == name) return index;
        }
        return no_type;
    }

    [[nodiscard]] const PropertyDescriptor* schema_property(std::size_t schema_type, std::string_view name) const {
        if (schema_type == no_type) return nullptr;
        for (const PropertyDescriptor& descriptor : schema_.types[schema_type].properties) {
            if (descriptor.name == name) return &descriptor;
        }
        return nullptr;
    }

    [[nodiscard]] const PropertyDescriptor* child_property(std::size_t parent, std::string_view name) const {
        if (parent == no_object || objects_[parent].schema_type == no_type) return nullptr;
        for (const PropertyDescriptor& descriptor : schema_.types[objects_[parent].schema_type].child_properties) {
            if (descriptor.name == name) return &descriptor;
        }
        return nullptr;
    }

    [[nodiscard]] bool has_interaction(std::size_t schema_type, std::string_view name) const {
        if (schema_type == no_type) return false;
        for (const std::string& interaction : schema_.types[schema_type].interactions) {
            if (interaction == name) return true;
        }
        return false;
    }

    // ----- Objects -----

    // The registry type a Windows Forms type becomes, following the
    // project's own classes to a known base; empty when none.
    [[nodiscard]] std::string registry_type_of(std::string_view winforms_type, std::string& converted_as) const {
        std::string current(last_segment(winforms_type));
        for (std::size_t depth = 0U; depth < maximum_winforms_base_depth && !current.empty(); ++depth) {
            const WinFormsTypeRow* const row = find_winforms_type(current);
            if (row != nullptr && !(*row).registry_id.empty() &&
                schema_type_named((*row).registry_id) != no_type) {
                converted_as = current;
                return std::string((*row).registry_id);
            }
            if (row != nullptr) return std::string();
            const std::string base = classes_.base_of(current);
            current = std::string(last_segment(base));
        }
        return std::string();
    }

    [[nodiscard]] bool make_root() {
        std::string base = classes_.base_of(designer_.class_name);
        if (base.empty()) base = "Form";
        std::string converted_as{};
        const std::string root_type = registry_type_of(base, converted_as);
        const bool form = last_segment(base) == "Form" || (root_type.empty() && find_winforms_type(base) != nullptr &&
                                                            last_segment(base) != "UserControl");
        ConvertedObject root{};
        root.winforms_type = form ? "Form" : converted_as;
        root.winforms_row = find_winforms_type(root.winforms_type);
        if (form) {
            root.type = std::string(form_language::form_root_type_name);
            root.schema_type = schema_type_named(root.type);
        } else if (!root_type.empty()) {
            root.type = root_type;
            root.schema_type = schema_type_named(root_type);
        } else {
            report(ConversionCode::type_unmapped, 0U, 0U, designer_.class_name,
                   "the form's base `" + base + "` has no GUI.Forms counterpart; converted as a user control");
            root.type = "user_control";
            root.schema_type = schema_type_named(root.type);
            root.winforms_type = "UserControl";
            root.winforms_row = find_winforms_type("UserControl");
        }
        if (root.schema_type == no_type) {
            report(ConversionCode::type_unmapped, 0U, 0U, designer_.class_name,
                   "the form schema has no `" + root.type + "` type; nothing can be converted");
            return false;
        }
        objects_.push_back(std::move(root));
        content_ = 0U;
        if (form) {
            // The window's single root control holds the form's controls.
            ConvertedObject panel{};
            panel.path = std::string();
            panel.field = std::string();
            panel.winforms_type = "Panel";
            panel.winforms_row = find_winforms_type("Panel");
            panel.type = "panel";
            panel.schema_type = schema_type_named("panel");
            panel.parent = 0U;
            objects_.push_back(std::move(panel));
            content_ = 1U;
            objects_[0].added.push_back(content_);
        }
        return true;
    }

    // The object a path names: the longest prefix that is an object's path.
    // `rest` receives what follows it. The root is the empty path.
    [[nodiscard]] std::size_t object_of(std::string_view path, std::string_view& rest) {
        std::string_view prefix = path;
        while (true) {
            for (std::size_t index = 0U; index < objects_.size(); ++index) {
                if (!objects_[index].path.empty() && objects_[index].path == prefix) {
                    rest = prefix.size() < path.size() ? path.substr(prefix.size() + 1U) : std::string_view();
                    return index;
                }
            }
            // A split container's panels are reached through it.
            const std::size_t dot = prefix.rfind('.');
            if (dot == std::string_view::npos) break;
            const std::string_view tail = prefix.substr(dot + 1U);
            const std::string_view head = prefix.substr(0U, dot);
            if (tail == "Panel1" || tail == "Panel2") {
                std::string_view inner{};
                const std::size_t container = object_of(head, inner);
                if (container != no_object && inner.empty() && objects_[container].winforms_type == "SplitContainer") {
                    const std::size_t panel = make_split_panel(container, std::string(prefix), tail);
                    rest = prefix.size() < path.size() ? path.substr(prefix.size() + 1U) : std::string_view();
                    return panel;
                }
            }
            prefix = head;
        }
        rest = path;
        return 0U;
    }

    [[nodiscard]] std::size_t make_split_panel(std::size_t container, std::string path, std::string_view which) {
        ConvertedObject panel{};
        panel.path = std::move(path);
        panel.field = objects_[container].field + std::string(which);
        panel.winforms_type = "SplitterPanel";
        panel.winforms_row = find_winforms_type("SplitterPanel");
        panel.type = "splitter_panel";
        panel.schema_type = schema_type_named(panel.type);
        if (panel.schema_type == no_type) {
            panel.type = "panel";
            panel.schema_type = schema_type_named("panel");
        }
        panel.line = objects_[container].line;
        panel.column = objects_[container].column;
        const std::size_t index = objects_.size();
        objects_.push_back(std::move(panel));
        objects_[container].added.push_back(index);
        objects_[index].parent = container;
        report(ConversionCode::layout_approximated, objects_[container].line, objects_[container].column,
               objects_[container].field,
               std::string("the split container's ") + std::string(which) +
                   " is written as a splitter panel child; check that GUI.Forms' split container takes it");
        return index;
    }

    void create_object(std::string field, std::string_view type_text, std::uint32_t line, std::uint32_t column) {
        ConvertedObject made{};
        made.path = field;
        made.field = std::move(field);
        made.winforms_type = std::string(last_segment(type_text));
        made.winforms_row = find_winforms_type(made.winforms_type);
        made.line = line;
        made.column = column;
        std::string converted_as{};
        made.type = registry_type_of(type_text, converted_as);
        if (made.type.empty()) {
            made.unmapped = true;
            made.type = "panel";
            counts_.unmapped_objects = counts_.unmapped_objects + 1U;
            report(ConversionCode::type_unmapped, line, column, made.field,
                   "`" + made.winforms_type +
                       "` has no GUI.Forms counterpart; written as a panel with the class winforms-unmapped");
        } else if (converted_as != made.winforms_type) {
            report(ConversionCode::type_converted_as_base, line, column, made.field,
                   "the project's `" + made.winforms_type + "` is converted as its base `" + converted_as + "`");
            made.winforms_type = converted_as;
            made.winforms_row = find_winforms_type(converted_as);
        }
        made.schema_type = schema_type_named(made.type);
        made.component = made.schema_type != no_type && !schema_.types[made.schema_type].holds_children;
        objects_.push_back(std::move(made));
    }

    // ----- Statements -----

    void read_statement(const DesignerStatement& statement) {
        switch (statement.kind) {
        case DesignerStatementKind::local_declaration:
            if (statement.type.ends_with("ResourceManager")) resources_ = statement.target;
            return;
        case DesignerStatementKind::assignment:
            assignment(statement);
            return;
        case DesignerStatementKind::subscription:
            subscription(statement);
            return;
        case DesignerStatementKind::unsubscription:
            return;
        case DesignerStatementKind::call:
            call(statement);
            return;
        case DesignerStatementKind::unread:
            objects_[0].code_lines.push_back("// Not read (WF103): " + statement.text);
            counts_.code_items = counts_.code_items + 1U;
            return;
        }
        // Any other kind is one this reader does not produce; skip it.
    }

    void assignment(const DesignerStatement& statement) {
        const DesignerExpression& value = designer_.expression(statement.value);
        const bool single = statement.target.find('.') == std::string::npos;
        // `this.button1 = new System.Windows.Forms.Button();`
        if (single && value.kind == DesignerExpressionKind::construction &&
            (designer_.find_field(statement.target) != nullptr ||
             (!is_root_property(statement.target) && statement.target.front() >= 'a' &&
              statement.target.front() <= 'z'))) {
            if (value.text.ends_with("ComponentModel.Container")) return;
            const std::string_view type_name = last_segment(value.text);
            if (type_name == "BindingSource") {
                add_source(statement.target, ModelSourceKind::binding_source);
                return;
            }
            create_object(statement.target, value.text, statement.line, statement.column);
            return;
        }
        std::string_view property{};
        const std::size_t object = object_of(statement.target, property);
        if (property.empty()) return;
        SourceValue source{};
        source.expression = statement.value;
        source.line = statement.line;
        source.column = statement.column;
        set_property(object, property, source);
    }

    // Whether `name` is a property the form root sets on itself, not a
    // field the designer creates.
    [[nodiscard]] bool is_root_property(std::string_view name) const {
        if (objects_.empty() || objects_[0].winforms_row == nullptr) return false;
        const WinFormsMemberMatch match =
            find_winforms_member(*objects_[0].winforms_row, name, WinFormsMemberKind::property);
        const bool known = match.member != nullptr;
        return known;
    }

    void add_source(std::string_view field, ModelSourceKind kind) {
        const std::string name = member_identifier_of(field);
        for (const ModelSource& existing : sources_) {
            if (existing.name == name) return;
        }
        ModelSource source{};
        source.name = name;
        source.kind = kind;
        source.winforms_field = std::string(field);
        sources_.push_back(std::move(source));
    }

    void subscription(const DesignerStatement& statement) {
        std::string_view event{};
        const std::size_t object = object_of(statement.target, event);
        if (event.empty() || event.find('.') != std::string_view::npos) {
            report(ConversionCode::event_unmapped, statement.line, statement.column, statement.target,
                   "an event of a nested member; its handler is not attached");
            return;
        }
        // The handler: `new System.EventHandler(this.button1_Click)` or a
        // method group `this.button1_Click`.
        std::uint32_t handler_index = statement.value;
        const DesignerExpression& written = designer_.expression(handler_index);
        if (written.kind == DesignerExpressionKind::construction && written.argument_count == 1U) {
            handler_index = designer_.argument(written, 0U);
        }
        const DesignerExpression& handler = designer_.expression(handler_index);
        if (handler.kind != DesignerExpressionKind::path || handler.text.find('.') != std::string::npos) {
            report(ConversionCode::event_unmapped, statement.line, statement.column, statement.target,
                   "a handler that is not a method of the form; not attached");
            return;
        }
        ConvertedObject& target = objects_[object];
        const std::string interaction = map_event(target, event);
        if (interaction.empty()) {
            report(ConversionCode::event_unmapped, statement.line, statement.column, statement.target,
                   "GUI.Forms has no interaction for `" + std::string(event) + "` on " + target.type +
                       "; `" + handler.text + "` is not attached");
            target.code_lines.push_back("// Not attached (WF208): " + statement.target + " += " + handler.text);
            counts_.code_items = counts_.code_items + 1U;
            return;
        }
        ConvertedEvent converted{};
        converted.winforms_event = std::string(event);
        converted.interaction = interaction;
        converted.handler = handler.text;
        const CSharpClass* const form_class = classes_.find(designer_.class_name);
        const CSharpMethod* method = nullptr;
        if (form_class != nullptr) {
            for (const CSharpClass& record : classes_.classes) {
                if (record.name != designer_.class_name) continue;
                for (const CSharpMethod& candidate : record.methods) {
                    if (candidate.name == handler.text) method = &candidate;
                }
            }
        }
        if (method == nullptr) {
            report(ConversionCode::handler_missing, statement.line, statement.column, statement.target,
                   "the form class declares no `" + handler.text + "`; the binding calls it anyway");
            converted.arguments = "System.EventArgs.Empty";
        } else {
            converted.arguments = handler_arguments((*method).parameter_types);
            if (converted.arguments.empty()) {
                const std::string taken =
                    (*method).parameter_types.size() == 2U ? (*method).parameter_types[1] : std::string("?");
                report(ConversionCode::event_unmapped, statement.line, statement.column, statement.target,
                       "`" + handler.text + "` takes " + taken +
                           ", which the binding cannot make from a GUI.Forms event; not attached");
                target.code_lines.push_back("// Not attached (WF208): " + statement.target + " += " + handler.text +
                                            " takes " + taken);
                counts_.code_items = counts_.code_items + 1U;
                return;
            }
        }
        target.events.push_back(std::move(converted));
        counts_.events = counts_.events + 1U;
    }

    [[nodiscard]] std::string map_event(const ConvertedObject& target, std::string_view event) const {
        std::string candidate{};
        if (target.winforms_row != nullptr) {
            const WinFormsMemberMatch match = find_winforms_member(*target.winforms_row, event, WinFormsMemberKind::event);
            if (match.member != nullptr) candidate = std::string((*match.member).gui_forms);
        }
        if (candidate.empty()) candidate = snake_case_of(event);
        if (has_interaction(target.schema_type, candidate)) return candidate;
        return std::string();
    }

    void call(const DesignerStatement& statement) {
        const DesignerExpression& expression = designer_.expression(statement.value);
        if (statement.target.empty()) {
            // A call on a cast: `((ISupportInitialize)(x)).BeginInit()`.
            if (expression.text == "BeginInit" || expression.text == "EndInit") return;
            objects_[0].code_lines.push_back("// Not converted (WF215): a call of " + expression.text);
            counts_.code_items = counts_.code_items + 1U;
            return;
        }
        const std::string_view method = last_segment(statement.target);
        const std::string_view receiver =
            statement.target.size() > method.size() ? std::string_view(statement.target).substr(
                                                          0U, statement.target.size() - method.size() - 1U)
                                                    : std::string_view();
        if (method == "SuspendLayout" || method == "ResumeLayout" || method == "PerformLayout") return;
        if (!resources_.empty() && receiver == resources_) {
            resource_call(statement, expression, method);
            return;
        }
        if (receiver == "Controls" || receiver.ends_with(".Controls")) {
            const std::string_view owner_path =
                receiver == "Controls" ? std::string_view() : receiver.substr(0U, receiver.size() - 9U);
            add_children(statement, expression, owner_path, method);
            return;
        }
        if (receiver.ends_with(".Items") && (method == "AddRange" || method == "Add")) {
            add_items(statement, expression, receiver.substr(0U, receiver.size() - 6U));
            return;
        }
        if (receiver.ends_with(".DataBindings") && method == "Add") {
            add_binding(statement, expression, receiver.substr(0U, receiver.size() - 13U));
            return;
        }
        if (method == "SetToolTip" && expression.argument_count == 2U) {
            SourceValue text{};
            text.expression = designer_.argument(expression, 1U);
            text.line = statement.line;
            text.column = statement.column;
            const std::size_t target = object_of_argument(designer_.argument(expression, 0U));
            if (target != no_object) {
                set_property(target, "ToolTipText", text);
                return;
            }
        }
        if ((method == "SetColumnSpan" || method == "SetRowSpan" || method == "SetColumn" || method == "SetRow" ||
             method == "SetCellPosition") &&
            expression.argument_count == 2U) {
            cell_call(statement, expression, method);
            return;
        }
        std::string_view rest{};
        const std::size_t object = object_of(receiver, rest);
        objects_[object].code_lines.push_back("// Not converted (WF215): " + statement.target + "(...)");
        counts_.code_items = counts_.code_items + 1U;
        report(ConversionCode::call_to_code, statement.line, statement.column, statement.target,
               "a call the form cannot hold; listed in the C# binding");
    }

    // The object an argument names (`this.button1`), or no_object.
    [[nodiscard]] std::size_t object_of_argument(std::uint32_t argument) {
        const DesignerExpression& written = designer_.expression(argument);
        if (written.kind != DesignerExpressionKind::path) return no_object;
        std::string_view rest{};
        const std::size_t object = object_of(written.text, rest);
        if (!rest.empty()) return no_object;
        if (object == 0U && !written.text.empty()) return no_object;
        return object;
    }

    void add_children(const DesignerStatement& statement, const DesignerExpression& expression,
                      std::string_view owner_path, std::string_view method) {
        std::size_t owner = content_;
        if (!owner_path.empty()) {
            std::string_view rest{};
            owner = object_of(owner_path, rest);
            if (!rest.empty()) {
                report(ConversionCode::layout_approximated, statement.line, statement.column, statement.target,
                       "children added to a nested member; added to its owner instead");
            }
            if (owner == 0U) owner = content_;
        }
        std::vector<std::uint32_t> children{};
        if (method == "Add" && expression.argument_count >= 1U) {
            children.push_back(designer_.argument(expression, 0U));
        } else if (method == "AddRange" && expression.argument_count == 1U) {
            const DesignerExpression& array = designer_.expression(designer_.argument(expression, 0U));
            for (std::uint32_t position = 0U; position < array.argument_count; ++position) {
                children.push_back(designer_.argument(array, position));
            }
        } else {
            return;
        }
        for (const std::uint32_t argument : children) {
            const std::size_t child = object_of_argument(argument);
            if (child == no_object || child == owner || objects_[child].parent != no_object) continue;
            objects_[child].parent = owner;
            objects_[owner].added.push_back(child);
        }
        // TableLayoutPanel's `Controls.Add(control, column, row)`.
        if (method == "Add" && expression.argument_count == 3U && !children.empty()) {
            const std::size_t child = object_of_argument(children[0]);
            const std::optional<double> column = number_of(designer_.argument(expression, 1U));
            const std::optional<double> row = number_of(designer_.argument(expression, 2U));
            if (child != no_object && column && row) {
                set_child_property(child, "cell", number_text(*column) + " " + number_text(*row), statement);
            }
        }
    }

    void cell_call(const DesignerStatement& statement, const DesignerExpression& expression, std::string_view method) {
        const std::size_t child = object_of_argument(designer_.argument(expression, 0U));
        if (child == no_object) return;
        const std::uint32_t argument = designer_.argument(expression, 1U);
        if (method == "SetCellPosition") {
            const DesignerExpression& position = designer_.expression(argument);
            if (position.kind == DesignerExpressionKind::construction && position.argument_count == 2U) {
                const std::optional<double> column = number_of(designer_.argument(position, 0U));
                const std::optional<double> row = number_of(designer_.argument(position, 1U));
                if (column && row) {
                    set_child_property(child, "cell", number_text(*column) + " " + number_text(*row), statement);
                }
            }
            return;
        }
        const std::optional<double> number = number_of(argument);
        if (!number) return;
        if (method == "SetColumnSpan") set_child_property(child, "column_span", number_text(*number), statement);
        if (method == "SetRowSpan") set_child_property(child, "row_span", number_text(*number), statement);
        if (method == "SetColumn" || method == "SetRow") {
            // One axis of `cell`; the other keeps what it had, zero first.
            pending_cells_.resize(std::max(pending_cells_.size(), objects_.size()));
            CellPosition& cell = pending_cells_[child];
            if (method == "SetColumn") cell.column = *number;
            if (method == "SetRow") cell.row = *number;
            set_child_property(child, "cell", number_text(cell.column) + " " + number_text(cell.row), statement);
        }
    }

    void set_child_property(std::size_t child, std::string_view name, std::string value,
                            const DesignerStatement& statement) {
        const std::size_t parent = objects_[child].parent;
        const PropertyDescriptor* const descriptor = child_property(parent, name);
        if (descriptor == nullptr) {
            report(ConversionCode::layout_approximated, statement.line, statement.column, objects_[child].field,
                   "the container provides no `" + std::string(name) + "` to its children; left out");
            return;
        }
        std::string kebab(name);
        std::replace(kebab.begin(), kebab.end(), '_', '-');
        put_attribute(objects_[child], kebab, std::move(value));
    }

    void add_items(const DesignerStatement& statement, const DesignerExpression& expression, std::string_view owner_path) {
        std::string_view rest{};
        const std::size_t owner = object_of(owner_path, rest);
        std::vector<std::uint32_t> values{};
        for (std::uint32_t position = 0U; position < expression.argument_count; ++position) {
            const std::uint32_t argument = designer_.argument(expression, position);
            const DesignerExpression& written = designer_.expression(argument);
            if (written.kind == DesignerExpressionKind::array) {
                for (std::uint32_t element = 0U; element < written.argument_count; ++element) {
                    values.push_back(designer_.argument(written, element));
                }
            } else {
                values.push_back(argument);
            }
        }
        const PropertyDescriptor* const descriptor = schema_property(objects_[owner].schema_type, "items");
        if (descriptor == nullptr || (*descriptor).kind != BindingValueKind::collection) {
            objects_[owner].code_lines.push_back("// Not converted (WF205): " + statement.target + "(...)");
            counts_.code_items = counts_.code_items + 1U;
            report(ConversionCode::property_to_code, statement.line, statement.column, objects_[owner].field,
                   "items a form document cannot list for " + objects_[owner].type + "; listed in the C# binding");
            return;
        }
        for (const std::uint32_t value : values) {
            const std::optional<std::string> text = text_of(value);
            if (!text) {
                report(ConversionCode::value_unconverted, statement.line, statement.column, objects_[owner].field,
                       "an item that is not text; left out");
                continue;
            }
            objects_[owner].items.push_back(*text);
        }
    }

    // `x.DataBindings.Add(new Binding("Text", source, "Field", true,
    // DataSourceUpdateMode.OnPropertyChanged))` or
    // `x.DataBindings.Add("Text", source, "Field")`.
    void add_binding(const DesignerStatement& statement, const DesignerExpression& expression, std::string_view owner_path) {
        std::string_view rest{};
        const std::size_t owner = object_of(owner_path, rest);
        const DesignerExpression* call = &expression;
        if (expression.argument_count == 1U) {
            const DesignerExpression& inner = designer_.expression(designer_.argument(expression, 0U));
            if (inner.kind == DesignerExpressionKind::construction) call = &inner;
        }
        if ((*call).argument_count < 3U) {
            report(ConversionCode::binding_unconverted, statement.line, statement.column, objects_[owner].field,
                   "a data binding without a property, a source and a member");
            return;
        }
        const std::optional<std::string> property = text_of(designer_.argument(*call, 0U));
        const DesignerExpression& source = designer_.expression(designer_.argument(*call, 1U));
        const std::optional<std::string> member = text_of(designer_.argument(*call, 2U));
        if (!property || !member || source.kind != DesignerExpressionKind::path || source.text.empty()) {
            report(ConversionCode::binding_unconverted, statement.line, statement.column, objects_[owner].field,
                   "a data binding whose property, source or member is not written literally");
            return;
        }
        std::string gui_name = map_property_name(objects_[owner], *property);
        const PropertyDescriptor* const descriptor = schema_property(objects_[owner].schema_type, gui_name);
        if (descriptor == nullptr || !(*descriptor).bindable) {
            report(ConversionCode::binding_unconverted, statement.line, statement.column, objects_[owner].field,
                   "`" + *property + "` is not a property a form document binds on " + objects_[owner].type);
            return;
        }
        ConvertedBinding binding{};
        std::replace(gui_name.begin(), gui_name.end(), '_', '-');
        binding.property = gui_name;
        binding.to = member_identifier_of(last_segment(source.text));
        binding.field = *member;
        for (std::uint32_t position = 3U; position < (*call).argument_count; ++position) {
            const DesignerExpression& option = designer_.expression(designer_.argument(*call, position));
            if (option.kind == DesignerExpressionKind::path && option.text.ends_with("OnPropertyChanged")) {
                binding.update = "on-change";
            }
        }
        add_source(last_segment(source.text), ModelSourceKind::binding_source);
        objects_[owner].bindings.push_back(std::move(binding));
        counts_.bindings = counts_.bindings + 1U;
    }

    // `resources.ApplyResources(this.button1, "button1")`: every entry
    // `button1.<Property>` is that property's value.
    void resource_call(const DesignerStatement& statement, const DesignerExpression& expression, std::string_view method) {
        if (method != "ApplyResources" || expression.argument_count < 2U) return;
        if (resx_ == nullptr) {
            report(ConversionCode::resource_missing, statement.line, statement.column, statement.target,
                   "the form reads its resources, but no .resx was found beside it");
            return;
        }
        const DesignerExpression& target = designer_.expression(designer_.argument(expression, 0U));
        const std::optional<std::string> prefix = text_of(designer_.argument(expression, 1U));
        if (target.kind != DesignerExpressionKind::path || !prefix) return;
        std::string_view rest{};
        const std::size_t object = target.text.empty() ? 0U : object_of(target.text, rest);
        const std::string lead = *prefix + ".";
        for (const ResxEntry& entry : (*resx_).entries) {
            if (!entry.name.starts_with(lead)) continue;
            const std::string_view property = std::string_view(entry.name).substr(lead.size());
            if (property.empty() || property.find('.') != std::string_view::npos ||
                property.starts_with(">>") || property == "Name") {
                continue;
            }
            SourceValue source{};
            source.resource = &entry;
            source.line = statement.line;
            source.column = statement.column;
            set_property(object, property, source);
        }
    }

    // ----- Properties -----

    // The GUI.Forms name of a Windows Forms property of `target`: the member
    // manifest's, or the snake case when it names none.
    [[nodiscard]] std::string map_property_name(const ConvertedObject& target, std::string_view property) const {
        if (target.winforms_row != nullptr) {
            const WinFormsMemberMatch match =
                find_winforms_member(*target.winforms_row, property, WinFormsMemberKind::property);
            if (match.member != nullptr && !(*match.member).gui_forms.empty()) {
                return std::string((*match.member).gui_forms);
            }
        }
        std::string snake = snake_case_of(property);
        return snake;
    }

    void set_property(std::size_t object, std::string_view property, const SourceValue& value) {
        ConvertedObject& target = objects_[object];
        const std::string subject = target.field.empty() ? designer_.class_name + "." + std::string(property)
                                                         : target.field + "." + std::string(property);
        if (property.find('.') != std::string_view::npos) {
            note_code(target, subject, value, ConversionCode::property_unmapped,
                      "a nested property, which a form document cannot set");
            return;
        }
        if (property == "Name") return;
        if (property == "AutoScaleDimensions" || property == "AutoScaleMode" || property == "AutoScaleBaseSize") {
            if (!auto_scale_reported_) {
                report(ConversionCode::auto_scale_dropped, value.line, value.column, subject,
                       "Windows Forms' automatic scaling is left out: GUI.Forms lays out in logical units");
                auto_scale_reported_ = true;
            }
            return;
        }
        if (object == 0U && type_is_form_root(target.type)) {
            form_root_property(property, value, subject);
            return;
        }
        if (property == "Location" || property == "Size") {
            geometry(target, property, value, subject);
            return;
        }
        if (value.resource != nullptr && (*value.resource).binary()) {
            report(ConversionCode::resource_binary, value.line, value.column, subject,
                   "a binary resource (" + (*value.resource).type + "); not carried into the form");
            counts_.dropped = counts_.dropped + 1U;
            return;
        }
        if (value.expression != no_expression && reads_binary_resource(value.expression)) {
            report(ConversionCode::resource_binary, value.line, value.column, subject,
                   "an image or object resource; not carried into the form");
            counts_.dropped = counts_.dropped + 1U;
            return;
        }
        // The manifest's view of the member.
        WinFormsMemberMatch match{};
        if (target.winforms_row != nullptr) {
            match = find_winforms_member(*target.winforms_row, property, WinFormsMemberKind::property);
        }
        const std::string gui_name = map_property_name(target, property);
        const PropertyDescriptor* descriptor = schema_property(target.schema_type, gui_name);
        if (descriptor == nullptr) {
            if (match.member != nullptr && ((*match.member).status == WinFormsMemberStatus::excluded ||
                                            (*match.member).status == WinFormsMemberStatus::deferred)) {
                report(ConversionCode::property_not_admitted, value.line, value.column, subject,
                       std::string("the member manifest marks it ") +
                           ((*match.member).status == WinFormsMemberStatus::excluded ? "excluded" : "deferred") +
                           "; left out");
                counts_.dropped = counts_.dropped + 1U;
                return;
            }
            if (match.member != nullptr && !(*match.member).gui_forms.empty()) {
                note_code(target, subject, value, ConversionCode::property_to_code,
                          "GUI.Forms has `" + std::string((*match.member).gui_forms) +
                              "`, which a form document does not set; listed in the C# binding");
                return;
            }
            report(ConversionCode::property_unmapped, value.line, value.column, subject,
                   "GUI.Forms has no member for it on " + target.type + "; left out");
            counts_.dropped = counts_.dropped + 1U;
            return;
        }
        const bool presentation = ((*descriptor).kind == BindingValueKind::color ||
                                   (*descriptor).kind == BindingValueKind::font) &&
                                  !(*descriptor).content_value;
        if (presentation) {
            theme_property(target, *descriptor, value, subject);
            return;
        }
        if (!(*descriptor).writable ||
            (*descriptor).serialization_visibility == PropertySerializationVisibility::hidden) {
            note_code(target, subject, value, ConversionCode::property_to_code,
                      "`" + gui_name + "` is not a property a form document sets; listed in the C# binding");
            return;
        }
        const ValueText converted = convert_value(*descriptor, value);
        if (!converted.converted) {
            report(ConversionCode::value_unconverted, value.line, value.column, subject, converted.problem);
            counts_.dropped = counts_.dropped + 1U;
            return;
        }
        std::string kebab = gui_name;
        std::replace(kebab.begin(), kebab.end(), '_', '-');
        put_attribute(target, kebab, converted.text);
        note_divergence(target, gui_name, value, subject);
    }

    void note_divergence(const ConvertedObject& target, std::string_view gui_name, const SourceValue& value,
                         const std::string& subject) {
        const std::string_view gui_type = target.winforms_row != nullptr ? (*target.winforms_row).gui_forms_type
                                                                         : std::string_view();
        const WinFormsDivergenceRow* const divergence = find_winforms_divergence(gui_type, gui_name);
        if (divergence == nullptr) return;
        report(ConversionCode::known_divergence, value.line, value.column, subject,
               std::string((*divergence).gui_forms));
    }

    void put_attribute(ConvertedObject& target, std::string name, std::string value) {
        for (ConvertedAttribute& existing : target.attributes) {
            if (existing.name == name) {
                existing.value = std::move(value);
                return;
            }
        }
        ConvertedAttribute attribute{};
        attribute.name = std::move(name);
        attribute.value = std::move(value);
        target.attributes.push_back(std::move(attribute));
        counts_.properties = counts_.properties + 1U;
    }

    void note_code(ConvertedObject& target, const std::string& subject, const SourceValue& value, ConversionCode code,
                   std::string detail) {
        report(code, value.line, value.column, subject, detail);
        std::string line = "// " + subject + " = " + source_text(value) + ";  ";
        line.append(conversion_code_label(code));
        line.append(": ");
        line.append(detail);
        target.code_lines.push_back(std::move(line));
        counts_.code_items = counts_.code_items + 1U;
    }

    // The value as the source wrote it, for a comment: a resource's text, a
    // literal's, or the expression's kind.
    [[nodiscard]] std::string source_text(const SourceValue& value) const {
        if (value.resource != nullptr) return "resource \"" + (*value.resource).value + "\"";
        if (value.expression == no_expression) return std::string();
        const DesignerExpression& written = designer_.expression(value.expression);
        switch (written.kind) {
        case DesignerExpressionKind::string: return "\"" + written.text + "\"";
        case DesignerExpressionKind::number: return written.text;
        case DesignerExpressionKind::boolean_true: return "true";
        case DesignerExpressionKind::boolean_false: return "false";
        case DesignerExpressionKind::path: return written.text;
        case DesignerExpressionKind::construction: return "new " + written.text + "(...)";
        default: return "...";
        }
    }

    // Text, client size and the accept and cancel buttons stay on the form
    // root; layout and presentation of the client area go to its root
    // panel; the window's other settings go to the C# binding.
    void form_root_property(std::string_view property, const SourceValue& value, const std::string& subject) {
        ConvertedObject& root = objects_[0];
        if (property == "Text") {
            const std::optional<std::string> text = value_as_text(value);
            if (text) {
                put_attribute(root, "text", *text);
                return;
            }
        }
        if (property == "ClientSize") {
            const std::optional<std::pair<double, double>> pair = pair_of(value, "Size");
            if (pair) {
                put_attribute(root, "client-size", number_text((*pair).first) + " " + number_text((*pair).second));
                return;
            }
        }
        if (property == "AcceptButton" || property == "CancelButton") {
            if (value.expression != no_expression) {
                const std::size_t button = object_of_argument(value.expression);
                if (button != no_object && button != 0U) {
                    accept_cancel_.push_back(
                        AcceptCancel{property == "AcceptButton" ? "accept-button" : "cancel-button", button});
                    return;
                }
            }
        }
        const bool client_area = property == "Padding" || property == "BackColor" || property == "ForeColor" ||
                                 property == "Font" || property == "AutoScroll" || property == "RightToLeft";
        if (client_area && content_ != 0U) {
            set_property(content_, property, value);
            return;
        }
        std::string gui_name = map_property_name(root, property);
        note_code(root, subject, value, ConversionCode::property_to_code,
                  "a window setting (`" + gui_name + "`) outside the form root's text, client size and buttons");
    }

    void geometry(ConvertedObject& target, std::string_view property, const SourceValue& value,
                  const std::string& subject) {
        const std::optional<std::pair<double, double>> pair = pair_of(value, property == "Location" ? "Point" : "Size");
        if (!pair) {
            report(ConversionCode::value_unconverted, value.line, value.column, subject,
                   "a position or size that is not two literal numbers");
            return;
        }
        if (!target.geometry.location && !target.geometry.size) {
            // Reserve the slot `bounds` is written in.
            target.geometry.slot = target.attributes.size();
            ConvertedAttribute slot{};
            slot.name = "bounds";
            target.attributes.push_back(std::move(slot));
            counts_.properties = counts_.properties + 1U;
        }
        if (property == "Location") {
            target.geometry.location = true;
            target.geometry.x = (*pair).first;
            target.geometry.y = (*pair).second;
        } else {
            target.geometry.size = true;
            target.geometry.width = (*pair).first;
            target.geometry.height = (*pair).second;
        }
        target.attributes[target.geometry.slot].value =
            number_text(target.geometry.x) + " " + number_text(target.geometry.y) + " " +
            number_text(target.geometry.width) + " " + number_text(target.geometry.height);
    }

    void theme_property(ConvertedObject& target, const PropertyDescriptor& descriptor, const SourceValue& value,
                        const std::string& subject) {
        if (descriptor.kind == BindingValueKind::color) {
            const ValueText colour = colour_of(value);
            if (!colour.converted) {
                report(ConversionCode::value_unconverted, value.line, value.column, subject, colour.problem);
                counts_.dropped = counts_.dropped + 1U;
                return;
            }
            target.theme_declarations.push_back(theme_colour_property(descriptor.name) + ": " + colour.text);
            counts_.theme_declarations = counts_.theme_declarations + 1U;
            report(ConversionCode::presentation_to_theme, value.line, value.column, subject,
                   "written into the theme sheet as " + theme_colour_property(descriptor.name));
            return;
        }
        // A font: `new System.Drawing.Font("Segoe UI", 9F, FontStyle.Bold, ...)`.
        if (value.expression == no_expression) {
            report(ConversionCode::value_unconverted, value.line, value.column, subject,
                   "a font from a resource; left to the theme");
            return;
        }
        const DesignerExpression& written = designer_.expression(unwrap(value.expression));
        if (written.kind != DesignerExpressionKind::construction || last_segment(written.text) != "Font" ||
            written.argument_count < 2U) {
            report(ConversionCode::value_unconverted, value.line, value.column, subject,
                   "a font that is not `new Font(family, size, ...)`; left to the theme");
            return;
        }
        const std::optional<double> points = number_of(designer_.argument(written, 1U));
        if (!points) {
            report(ConversionCode::value_unconverted, value.line, value.column, subject, "a font size that is not a number");
            return;
        }
        // Points to logical pixels (96 per inch), unless the font says Pixel.
        bool pixels = false;
        bool bold = false;
        bool italic = false;
        for (std::uint32_t position = 2U; position < written.argument_count; ++position) {
            const std::vector<std::string> words = enumeration_words(designer_.argument(written, position));
            for (const std::string& word : words) {
                if (word == "Pixel") pixels = true;
                if (word == "Bold") bold = true;
                if (word == "Italic") italic = true;
            }
        }
        const double size = pixels ? *points : *points * 96.0 / 72.0;
        const double rounded = std::round(size * 100.0) / 100.0;
        target.theme_declarations.push_back("font-size: " + number_text(rounded) + "px");
        if (bold) target.theme_declarations.push_back("font-weight: 700");
        if (italic) target.theme_declarations.push_back("font-style: italic");
        counts_.theme_declarations = counts_.theme_declarations + 1U;
        const std::optional<std::string> family = text_of(designer_.argument(written, 0U));
        report(ConversionCode::presentation_to_theme, value.line, value.column, subject,
               "written into the theme sheet as its size and weight" +
                   (family ? "; the family `" + *family + "` is left to the theme's faces" : std::string()));
    }

    // ----- Values -----

    // The expression under any casts and parentheses.
    [[nodiscard]] std::uint32_t unwrap(std::uint32_t index) const {
        std::uint32_t current = index;
        for (std::uint32_t depth = 0U; depth < maximum_expression_depth; ++depth) {
            const DesignerExpression& written = designer_.expression(current);
            if (written.kind != DesignerExpressionKind::cast || written.argument_count != 1U) return current;
            current = designer_.argument(written, 0U);
        }
        return current;
    }

    [[nodiscard]] bool reads_binary_resource(std::uint32_t index) const {
        const DesignerExpression& written = designer_.expression(unwrap(index));
        const bool binary = written.kind == DesignerExpressionKind::call && written.text.ends_with(".GetObject");
        return binary;
    }

    // A number an expression evaluates to: literals, negation and casts.
    [[nodiscard]] std::optional<double> number_of(std::uint32_t index) const {
        const DesignerExpression& written = designer_.expression(unwrap(index));
        if (written.kind == DesignerExpressionKind::number) return parse_number(written.text);
        if (written.kind == DesignerExpressionKind::negation && written.argument_count == 1U) {
            const std::optional<double> operand = number_of(designer_.argument(written, 0U));
            if (!operand) return std::nullopt;
            return -*operand;
        }
        // `new decimal(new int[] { lo, mid, hi, flags })`.
        if (written.kind == DesignerExpressionKind::construction && last_segment(written.text) == "decimal" &&
            written.argument_count == 1U) {
            const DesignerExpression& parts = designer_.expression(designer_.argument(written, 0U));
            if (parts.kind != DesignerExpressionKind::array || parts.argument_count != 4U) return std::nullopt;
            const std::optional<double> low = number_of(designer_.argument(parts, 0U));
            const std::optional<double> flags = number_of(designer_.argument(parts, 3U));
            if (!low || !flags) return std::nullopt;
            const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::int64_t>(*flags));
            const std::uint32_t scale = (bits >> 16U) & 0xFFU;
            const bool negative = (bits & 0x80000000U) != 0U;
            const double magnitude = static_cast<double>(static_cast<std::uint32_t>(*low)) / std::pow(10.0, scale);
            return negative ? -magnitude : magnitude;
        }
        return std::nullopt;
    }

    // Text an expression evaluates to: literals, concatenation and
    // `resources.GetString("name")`.
    [[nodiscard]] std::optional<std::string> text_of(std::uint32_t index) const {
        const DesignerExpression& written = designer_.expression(unwrap(index));
        if (written.kind == DesignerExpressionKind::string || written.kind == DesignerExpressionKind::character) {
            return written.text;
        }
        if (written.kind == DesignerExpressionKind::addition && written.argument_count == 2U) {
            const std::optional<std::string> left = text_of(designer_.argument(written, 0U));
            const std::optional<std::string> right = text_of(designer_.argument(written, 1U));
            if (!left || !right) return std::nullopt;
            return *left + *right;
        }
        if (written.kind == DesignerExpressionKind::call && written.text.ends_with(".GetString") &&
            written.argument_count == 1U && resx_ != nullptr) {
            const std::optional<std::string> name = text_of(designer_.argument(written, 0U));
            if (!name) return std::nullopt;
            const ResxEntry* const entry = (*resx_).find(*name);
            if (entry == nullptr) return std::nullopt;
            return (*entry).value;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> value_as_text(const SourceValue& value) const {
        if (value.resource != nullptr) return (*value.resource).value;
        if (value.expression == no_expression) return std::nullopt;
        const std::optional<std::string> text = text_of(value.expression);
        return text;
    }

    // Two numbers: `new Point(x, y)`, `new Size(w, h)`, `new SizeF(...)` or a
    // resource's "x, y".
    [[nodiscard]] std::optional<std::pair<double, double>> pair_of(const SourceValue& value, std::string_view type) const {
        if (value.resource != nullptr) {
            const std::vector<double> numbers = resource_numbers((*value.resource).value);
            if (numbers.size() != 2U) return std::nullopt;
            return std::make_pair(numbers[0], numbers[1]);
        }
        if (value.expression == no_expression) return std::nullopt;
        const DesignerExpression& written = designer_.expression(unwrap(value.expression));
        if (written.kind != DesignerExpressionKind::construction || written.argument_count != 2U) return std::nullopt;
        const std::string_view made = last_segment(written.text);
        if (!made.starts_with(type)) return std::nullopt;
        const std::optional<double> first = number_of(designer_.argument(written, 0U));
        const std::optional<double> second = number_of(designer_.argument(written, 1U));
        if (!first || !second) return std::nullopt;
        return std::make_pair(*first, *second);
    }

    // The last segments of an enumeration value: `AnchorStyles.Top |
    // AnchorStyles.Left` gives Top and Left.
    [[nodiscard]] std::vector<std::string> enumeration_words(std::uint32_t index) const {
        std::vector<std::string> words{};
        std::vector<std::uint32_t> pending{index};
        while (!pending.empty() && words.size() < 64U) {
            const std::uint32_t current = unwrap(pending.back());
            pending.pop_back();
            const DesignerExpression& written = designer_.expression(current);
            if (written.kind == DesignerExpressionKind::bitwise_or && written.argument_count == 2U) {
                pending.push_back(designer_.argument(written, 1U));
                pending.push_back(designer_.argument(written, 0U));
            } else if (written.kind == DesignerExpressionKind::path) {
                words.emplace_back(last_segment(written.text));
            }
        }
        return words;
    }

    [[nodiscard]] ValueText convert_value(const PropertyDescriptor& descriptor, const SourceValue& value) const {
        switch (descriptor.kind) {
        case BindingValueKind::boolean: {
            if (value.resource != nullptr) {
                const std::string& text = (*value.resource).value;
                if (text == "True" || text == "true") return value_text("true");
                if (text == "False" || text == "false") return value_text("false");
                return value_problem("a resource that is not a boolean");
            }
            const DesignerExpression& written = designer_.expression(unwrap(value.expression));
            if (written.kind == DesignerExpressionKind::boolean_true) return value_text("true");
            if (written.kind == DesignerExpressionKind::boolean_false) return value_text("false");
            return value_problem("a boolean that is not written true or false");
        }
        case BindingValueKind::signed_integer:
        case BindingValueKind::unsigned_integer:
        case BindingValueKind::number: {
            std::optional<double> number{};
            if (value.resource != nullptr) {
                const std::vector<double> numbers = resource_numbers((*value.resource).value);
                if (numbers.size() == 1U) number = numbers[0];
            } else {
                number = number_of(value.expression);
            }
            if (!number) return value_problem("a number the converter does not evaluate");
            if (descriptor.kind != BindingValueKind::number && std::nearbyint(*number) != *number) {
                return value_problem("a fraction for an integer property");
            }
            if (descriptor.kind == BindingValueKind::unsigned_integer && *number < 0.0) {
                return value_problem("a negative value for an unsigned property");
            }
            return value_text(number_text(*number));
        }
        case BindingValueKind::text: {
            const std::optional<std::string> text = value_as_text(value);
            if (!text) return value_problem("text the converter does not evaluate");
            return value_text(*text);
        }
        case BindingValueKind::point:
        case BindingValueKind::size: {
            const std::string_view type = descriptor.kind == BindingValueKind::point ? "Point" : "Size";
            const std::optional<std::pair<double, double>> pair = pair_of(value, type);
            if (!pair) return value_problem("not two literal numbers");
            return value_text(number_text((*pair).first) + " " + number_text((*pair).second));
        }
        case BindingValueKind::rectangle: {
            if (value.expression == no_expression) return value_problem("a rectangle from a resource");
            const DesignerExpression& written = designer_.expression(unwrap(value.expression));
            if (written.kind != DesignerExpressionKind::construction || written.argument_count != 4U) {
                return value_problem("a rectangle that is not `new Rectangle(x, y, width, height)`");
            }
            std::string text{};
            for (std::uint32_t position = 0U; position < 4U; ++position) {
                const std::optional<double> number = number_of(designer_.argument(written, position));
                if (!number) return value_problem("a rectangle with an operand that is not a number");
                if (!text.empty()) text.push_back(' ');
                text.append(number_text(*number));
            }
            return value_text(text);
        }
        case BindingValueKind::insets: {
            std::vector<double> numbers{};
            if (value.resource != nullptr) {
                numbers = resource_numbers((*value.resource).value);
            } else {
                const DesignerExpression& written = designer_.expression(unwrap(value.expression));
                if (written.kind != DesignerExpressionKind::construction) {
                    return value_problem("insets that are not `new Padding(...)`");
                }
                for (std::uint32_t position = 0U; position < written.argument_count; ++position) {
                    const std::optional<double> number = number_of(designer_.argument(written, position));
                    if (!number) return value_problem("insets with an operand that is not a number");
                    numbers.push_back(*number);
                }
            }
            if (numbers.size() == 1U) return value_text(number_text(numbers[0]));
            if (numbers.size() == 4U) {
                // Windows Forms: left, top, right, bottom. CSS: top right
                // bottom left.
                return value_text(number_text(numbers[1]) + " " + number_text(numbers[2]) + " " +
                                  number_text(numbers[3]) + " " + number_text(numbers[0]));
            }
            return value_problem("insets of neither one nor four numbers");
        }
        case BindingValueKind::enumeration: {
            if (!descriptor.enumeration) return value_problem("an enumeration the schema does not describe");
            std::vector<std::string> words{};
            if (value.resource != nullptr) {
                const std::string& text = (*value.resource).value;
                std::size_t start = 0U;
                while (start <= text.size()) {
                    std::size_t comma = text.find(',', start);
                    if (comma == std::string::npos) comma = text.size();
                    std::string word = text.substr(start, comma - start);
                    word.erase(0U, word.find_first_not_of(' '));
                    while (!word.empty() && word.back() == ' ') word.pop_back();
                    if (!word.empty()) words.push_back(std::move(word));
                    start = comma + 1U;
                }
            } else {
                words = enumeration_words(value.expression);
            }
            const PropertyEnumDescriptor& enumeration = *descriptor.enumeration;
            if (words.size() > 1U && !enumeration.flags) return value_problem("several choices for a single choice");
            std::string text{};
            for (const std::string& word : words) {
                const std::string snake = snake_case_of(word);
                // `AnchorStyles.None` is the empty set of a flags type.
                if (enumeration.flags && snake == "none") continue;
                bool found = false;
                for (const PropertyEnumChoice& choice : enumeration.choices) {
                    if (choice.name == snake) found = true;
                }
                if (!found) {
                    std::string choices{};
                    for (const PropertyEnumChoice& choice : enumeration.choices) {
                        if (choices.size() > 120U) {
                            choices.append(", ...");
                            break;
                        }
                        if (!choices.empty()) choices.append(", ");
                        choices.append(choice.name);
                    }
                    return value_problem("`" + word + "` is not one of " + enumeration.type_name + "'s choices (" +
                                         choices + ")");
                }
                if (!text.empty()) text.push_back(' ');
                std::string kebab = snake;
                std::replace(kebab.begin(), kebab.end(), '_', '-');
                text.append(kebab);
            }
            if (text.empty() && !enumeration.flags) return value_problem("no choice written");
            return value_text(text);
        }
        case BindingValueKind::collection: {
            std::vector<std::string> items{};
            if (value.expression == no_expression) return value_problem("a collection from a resource");
            const DesignerExpression& written = designer_.expression(unwrap(value.expression));
            if (written.kind != DesignerExpressionKind::array) return value_problem("a collection that is not an array");
            std::string text{};
            for (std::uint32_t position = 0U; position < written.argument_count; ++position) {
                const std::optional<std::string> item = text_of(designer_.argument(written, position));
                if (!item) return value_problem("a collection item that is not text");
                if (!text.empty()) text.push_back(' ');
                text.append(plain_item(*item) ? *item : form_language::quote_form_string(*item));
            }
            return value_text(text);
        }
        case BindingValueKind::null:
        case BindingValueKind::color:
        case BindingValueKind::font:
        case BindingValueKind::image:
        case BindingValueKind::object:
            return value_problem("a kind a form document cannot write");
        }
        return value_problem("a kind a form document cannot write");
    }

    [[nodiscard]] ValueText colour_of(const SourceValue& value) const {
        if (value.resource != nullptr) {
            const std::vector<double> numbers = resource_numbers((*value.resource).value);
            if (numbers.size() == 3U) {
                return value_text(hex_colour(static_cast<std::uint32_t>(numbers[0]), static_cast<std::uint32_t>(numbers[1]),
                                             static_cast<std::uint32_t>(numbers[2]), 255U));
            }
            return value_problem("a colour resource that is not three numbers");
        }
        const DesignerExpression& written = designer_.expression(unwrap(value.expression));
        if (written.kind == DesignerExpressionKind::call && written.text.ends_with("FromArgb")) {
            std::vector<std::uint32_t> channels{};
            for (std::uint32_t position = 0U; position < written.argument_count; ++position) {
                const std::optional<double> number = number_of(designer_.argument(written, position));
                if (!number) return value_problem("a colour channel that is not a number");
                channels.push_back(static_cast<std::uint32_t>(static_cast<std::int64_t>(*number)));
            }
            if (channels.size() == 3U) return value_text(hex_colour(channels[0], channels[1], channels[2], 255U));
            if (channels.size() == 4U) return value_text(hex_colour(channels[1], channels[2], channels[3], channels[0]));
            if (channels.size() == 1U) {
                const std::uint32_t argb = channels[0];
                return value_text(hex_colour((argb >> 16U) & 0xFFU, (argb >> 8U) & 0xFFU, argb & 0xFFU, argb >> 24U));
            }
            return value_problem("FromArgb with a number of channels the converter does not read");
        }
        if (written.kind == DesignerExpressionKind::path) {
            if (written.text.find("SystemColors.") != std::string::npos) {
                return value_problem("a system colour: the theme's own, so left out");
            }
            const std::string_view name = last_segment(written.text);
            for (const KnownColour& known : known_colours) {
                if (known.name == name) return value_text(std::string(known.hex));
            }
            return value_problem("the named colour `" + std::string(name) + "`, which the converter does not know");
        }
        return value_problem("a colour expression the converter does not evaluate");
    }

    // ----- Placement and identities -----

    // Components go inside the root; a control nobody added is reported.
    void place_unadded() {
        for (std::size_t index = 1U; index < objects_.size(); ++index) {
            ConvertedObject& current = objects_[index];
            if (current.parent != no_object) continue;
            if (current.component) {
                current.parent = 0U;
                objects_[0].added.push_back(index);
                continue;
            }
            report(ConversionCode::layout_approximated, current.line, current.column, current.field,
                   "not added to any container (an item of a collection?); left out of the form");
            current.parent = no_object;
        }
    }

    void assign_identities() {
        objects_[0].stable_id = form_id_;
        if (content_ != 0U) objects_[content_].stable_id = form_id_ + ".root";
        std::vector<std::string> taken{objects_[0].stable_id};
        if (content_ != 0U) taken.push_back(objects_[content_].stable_id);
        for (std::size_t index = 1U; index < objects_.size(); ++index) {
            ConvertedObject& current = objects_[index];
            if (index == content_) continue;
            std::string segment = stable_id_segment_of(current.field.empty() ? current.winforms_type : current.field);
            if (segment.empty()) segment = "object";
            if (current.path.find('.') != std::string::npos) {
                // A split container's panel: under its container's segment.
                segment = stable_id_segment_of(current.path);
                std::replace(segment.begin(), segment.end(), '.', '-');
            }
            std::string identity = form_id_ + "." + segment;
            std::size_t suffix = 2U;
            while (std::find(taken.begin(), taken.end(), identity) != taken.end()) {
                identity = form_id_ + "." + segment + "-" + std::to_string(suffix);
                suffix = suffix + 1U;
            }
            if (!current.field.empty() && segment != current.field && current.path.find('.') == std::string::npos) {
                report(ConversionCode::identity_respelled, current.line, current.column, current.field,
                       "its stable ID is `" + identity + "`");
            }
            current.stable_id = identity;
            taken.push_back(identity);
        }
        for (const AcceptCancel& entry : accept_cancel_) {
            put_attribute(objects_[0], entry.attribute, objects_[entry.button].stable_id);
        }
    }

    // ----- Writing -----

    void write_document(std::string& out) const {
        out.clear();
        out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        out.append("<!-- Converted from ");
        out.append(escape_comment(last_segment_path(designer_.file)));
        out.append(" by gui_forms_winforms_converter. -->\n");
        // Depth-first in explicit storage: an entry per open element.
        struct Frame final {
            std::size_t object{};
            std::size_t next_child{};
            bool opened{};
        };
        std::vector<Frame> stack{};
        stack.push_back(Frame{0U, 0U, false});
        while (!stack.empty()) {
            Frame& frame = stack.back();
            const ConvertedObject& current = objects_[frame.object];
            const std::size_t depth = stack.size() - 1U;
            const std::vector<std::size_t> children = painter_order(frame.object);
            if (!frame.opened) {
                frame.opened = true;
                open_element(out, frame.object, depth, children.empty() && current.bindings.empty());
                if (children.empty() && current.bindings.empty()) {
                    stack.pop_back();
                    continue;
                }
                for (const ConvertedBinding& binding : current.bindings) write_binding_element(out, binding, depth + 1U);
            }
            if (frame.next_child < children.size()) {
                const std::size_t child = children[frame.next_child];
                frame.next_child = frame.next_child + 1U;
                stack.push_back(Frame{child, 0U, false});
                continue;
            }
            out.append(indent(depth));
            out.append("</");
            out.append(element_name(current));
            out.append(">\n");
            stack.pop_back();
        }
    }

    // GUI.Forms' child order: components first, then controls later on top,
    // the reverse of the order the designer added them.
    [[nodiscard]] std::vector<std::size_t> painter_order(std::size_t object) const {
        std::vector<std::size_t> order{};
        const std::vector<std::size_t>& added = objects_[object].added;
        for (const std::size_t child : added) {
            if (objects_[child].component) order.push_back(child);
        }
        for (std::vector<std::size_t>::const_reverse_iterator child = added.rbegin(); child != added.rend(); ++child) {
            if (!objects_[*child].component) order.push_back(*child);
        }
        return order;
    }

    [[nodiscard]] static std::string indent(std::size_t depth) {
        std::string spaces(depth * 2U, ' ');
        return spaces;
    }

    [[nodiscard]] static std::string element_name(const ConvertedObject& object) {
        std::string name = object.type;
        std::replace(name.begin(), name.end(), '_', '-');
        return name;
    }

    [[nodiscard]] static std::string escape_comment(std::string_view text) {
        std::string escaped(text);
        std::size_t found = escaped.find("--");
        while (found != std::string::npos) {
            escaped.replace(found, 2U, "- -");
            found = escaped.find("--", found + 3U);
        }
        return escaped;
    }

    [[nodiscard]] static std::string last_segment_path(std::string_view path) {
        const std::size_t separator = path.find_last_of("/\\");
        if (separator == std::string_view::npos) return std::string(path);
        std::string name(path.substr(separator + 1U));
        return name;
    }

    void open_element(std::string& out, std::size_t index, std::size_t depth, bool empty) const {
        const ConvertedObject& current = objects_[index];
        if (current.unmapped) {
            out.append(indent(depth));
            out.append("<!-- ");
            out.append(escape_comment(current.winforms_type));
            out.append(" ");
            out.append(escape_comment(current.field));
            out.append(": no GUI.Forms counterpart (WF201). -->\n");
        }
        std::vector<ConvertedAttribute> attributes{};
        if (index == 0U) {
            ConvertedAttribute space{};
            space.name = "xmlns";
            space.value = std::string(form_language::form_namespace);
            attributes.push_back(std::move(space));
        }
        ConvertedAttribute identity{};
        identity.name = "id";
        identity.value = current.stable_id;
        attributes.push_back(std::move(identity));
        if (current.unmapped) {
            ConvertedAttribute classes{};
            classes.name = "classes";
            classes.value = "winforms-unmapped";
            attributes.push_back(std::move(classes));
        }
        for (const ConvertedAttribute& attribute : current.attributes) {
            if (attribute.value.empty() && attribute.name == "bounds") continue;
            attributes.push_back(attribute);
        }
        if (!current.items.empty()) {
            ConvertedAttribute items{};
            items.name = "items";
            for (const std::string& item : current.items) {
                if (!items.value.empty()) items.value.push_back(' ');
                items.value.append(plain_item(item) ? item : form_language::quote_form_string(item));
            }
            attributes.push_back(std::move(items));
        }
        std::string inline_tag = "<" + element_name(current);
        for (const ConvertedAttribute& attribute : attributes) {
            inline_tag.append(" ");
            inline_tag.append(attribute.name);
            inline_tag.append("=\"");
            inline_tag.append(escape_form_attribute(attribute.value));
            inline_tag.append("\"");
        }
        const std::string close = empty ? "/>\n" : ">\n";
        if (inline_tag.size() + depth * 2U <= maximum_inline_tag) {
            out.append(indent(depth));
            out.append(inline_tag);
            out.append(close);
            return;
        }
        // One attribute per line, four spaces deeper than the element.
        out.append(indent(depth));
        out.append("<");
        out.append(element_name(current));
        for (std::size_t position = 0U; position < attributes.size(); ++position) {
            out.append(position == 0U ? " " : "\n" + indent(depth) + "    ");
            out.append(attributes[position].name);
            out.append("=\"");
            out.append(escape_form_attribute(attributes[position].value));
            out.append("\"");
        }
        out.append(close);
    }

    void write_binding_element(std::string& out, const ConvertedBinding& binding, std::size_t depth) const {
        out.append(indent(depth));
        out.append("<bind property=\"");
        out.append(escape_form_attribute(binding.property));
        out.append("\" to=\"");
        out.append(escape_form_attribute(binding.to));
        out.append("\"");
        if (!binding.field.empty()) {
            out.append(" field=\"");
            out.append(escape_form_attribute(binding.field));
            out.append("\"");
        }
        if (!binding.update.empty()) {
            out.append(" update=\"");
            out.append(binding.update);
            out.append("\"");
        }
        out.append("/>\n");
    }

    void write_theme(std::string_view form_file, std::string& out) const {
        out.clear();
        bool any = false;
        for (const ConvertedObject& current : objects_) {
            if (!current.theme_declarations.empty()) any = true;
        }
        if (!any) return;
        out.append("/* The colours and fonts of ");
        out.append(designer_.class_name);
        out.append(", converted from ");
        out.append(last_segment_path(designer_.file));
        out.append(" for ");
        out.append(form_file);
        out.append(" by gui_forms_winforms_converter. */\n");
        for (const ConvertedObject& current : objects_) {
            if (current.theme_declarations.empty() || current.stable_id.empty()) continue;
            out.append("\n#");
            for (const char byte : current.stable_id) {
                if (byte == '.') out.push_back('\\');
                out.push_back(byte);
            }
            out.append(" {\n");
            for (const std::string& declaration : current.theme_declarations) {
                out.append("    ");
                out.append(declaration);
                out.append(";\n");
            }
            out.append("}\n");
        }
    }

    void write_binding(ConvertedForm& made) const {
        std::string& out = made.csharp_binding;
        const std::string name_space =
            options_.csharp_namespace.empty() ? designer_.namespace_name : options_.csharp_namespace;
        out.append("// Generated by gui_forms_winforms_converter from ");
        out.append(last_segment_path(designer_.file));
        out.append(": ");
        out.append(designer_.class_name);
        out.append("'s handlers attached to\n// the GUI.Forms form ");
        out.append(made.form_file);
        out.append(", and what that form cannot hold, as comments. Convert\n");
        out.append("// again rather than edit; the form class keeps its handlers.\n");
        out.append("#nullable enable\n\nusing System;\nusing GuiForms.Interop;\n\n");
        if (!name_space.empty()) {
            out.append("namespace ");
            out.append(name_space);
            out.append(";\n\n");
        }
        out.append("partial class ");
        out.append(designer_.class_name);
        out.append("\n{\n");
        for (const ConvertedObject& current : objects_) {
            if (current.field.empty() || current.parent == no_object) continue;
            // Prefixed: the designer file's own field of the name may still
            // be compiled beside the binding while the port goes on.
            out.append("    private ObjectHandle? gui_forms_");
            out.append(current.field);
            out.append(";\n");
        }
        out.append("    private readonly FormSubscriptions gui_forms_subscriptions = new FormSubscriptions();\n\n");
        out.append("    /// <summary>\n    /// Finds the objects of ");
        out.append(made.form_file);
        out.append(" under root (the form's root control) and\n");
        out.append("    /// attaches the handlers. Call once, after the form is loaded.\n    /// </summary>\n");
        out.append("    private unsafe void bind_gui_forms(ObjectHandle root)\n    {\n");
        for (const ConvertedObject& current : objects_) {
            if (current.field.empty() || current.parent == no_object) continue;
            out.append("        gui_forms_");
            out.append(current.field);
            out.append(" = FormTree.find(root, \"");
            out.append(current.stable_id);
            out.append("\");\n");
        }
        for (const ConvertedObject& current : objects_) {
            const std::string handle = current.field.empty() ? std::string("root") : "gui_forms_" + current.field + "!";
            for (const ConvertedEvent& event : current.events) {
                out.append("        gui_forms_subscriptions.add(FormTree.subscribe(");
                out.append(handle);
                out.append(", \"");
                out.append(event.interaction);
                out.append("\", ");
                out.append(handler_trampoline(current, event));
                out.append("));\n");
            }
        }
        bool any_code = false;
        for (const ConvertedObject& current : objects_) {
            if (!current.code_lines.empty()) any_code = true;
        }
        if (any_code) {
            out.append("\n        // What the form document cannot hold:\n");
            for (const ConvertedObject& current : objects_) {
                for (const std::string& line : current.code_lines) {
                    out.append("        ");
                    out.append(line);
                    out.push_back('\n');
                }
            }
        }
        out.append("    }\n");
        for (const ConvertedObject& current : objects_) {
            for (const ConvertedEvent& event : current.events) {
                out.append("\n    private unsafe void ");
                out.append(handler_trampoline(current, event));
                out.append("(gf_event* @event)\n    {\n        ");
                out.append(event.handler);
                out.append("(this, ");
                out.append(event.arguments);
                out.append(");\n    }\n");
            }
        }
        out.append("}\n");
    }

    [[nodiscard]] static std::string handler_trampoline(const ConvertedObject& object, const ConvertedEvent& event) {
        std::string name = "on_";
        name.append(object.field.empty() ? std::string("form") : member_identifier_of(object.field));
        name.push_back('_');
        name.append(event.interaction);
        return name;
    }

    struct AcceptCancel final {
        std::string attribute{};
        std::size_t button{};
    };

    struct CellPosition final {
        double column{};
        double row{};
    };

    const DesignerDocument& designer_;
    const ResxDocument* resx_;
    const CSharpClassIndex& classes_;
    const FormSchema& schema_;
    const ConversionOptions& options_;
    ConversionDiagnosticList& diagnostics_;
    std::string form_id_{};
    std::vector<ConvertedObject> objects_{};
    // The object the form's own controls go into: the root panel of a form
    // root, the root itself otherwise.
    std::size_t content_{};
    std::string resources_{};
    std::vector<ModelSource> sources_{};
    std::vector<AcceptCancel> accept_cancel_{};
    std::vector<CellPosition> pending_cells_{};
    ConversionCounts counts_{};
    bool auto_scale_reported_{};
};

} // namespace

ConversionOutcome convert_winforms_form(const DesignerDocument& designer, const ResxDocument* resx,
                                        const CSharpClassIndex& classes, const form_language::FormSchema& schema,
                                        const ConversionOptions& options, ConvertedForm& form,
                                        ConversionDiagnosticList& diagnostics) {
    Converter converter(designer, resx, classes, schema, options, diagnostics);
    const ConversionOutcome outcome = converter.run(form);
    return outcome;
}

std::string escape_form_attribute(std::string_view text) {
    std::string escaped{};
    escaped.reserve(text.size() + 8U);
    for (const char byte : text) {
        switch (byte) {
        case '&': escaped.append("&amp;"); break;
        case '<': escaped.append("&lt;"); break;
        case '>': escaped.append("&gt;"); break;
        case '"': escaped.append("&quot;"); break;
        case '\n': escaped.append("&#10;"); break;
        case '\r': escaped.append("&#13;"); break;
        case '\t': escaped.append("&#9;"); break;
        default: escaped.push_back(byte); break;
        }
    }
    return escaped;
}

} // namespace gui_forms::winforms_converter
