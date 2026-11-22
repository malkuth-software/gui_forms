#pragma once

// What the WinForms converter reports (stage MCP G7). Every record names a
// file, a line and a column of the Windows Forms source it is about, the
// object or member it concerns, and what was done instead, so that a model
// driving a conversion through MCP can act on each one.
//
// The codes are the converter's own (WF, for Windows Forms), apart from the
// form compiler's E8xx: a converted document is checked by the form
// compiler afterwards (`validate`), and its diagnostics keep their own
// codes.
//
// Severity:
// - an error stops the conversion of its form: nothing is written for it;
// - a warning says that something of the source was left out or changed in
//   meaning (a type with no counterpart, a property GUI.Forms lacks);
// - a note says where something went that is not in the form document (a
//   colour into the theme sheet, a property into the C# binding).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

enum class ConversionSeverity : std::uint8_t { error, warning, note };

// The codes. The hundreds digit is the class: WF0xx files and projects,
// WF1xx reading C# and .resx, WF2xx mapping onto GUI.Forms, WF3xx layout.
enum class ConversionCode : std::uint16_t {
    // WF0xx: files and projects.
    // A file that cannot be read, or a path that names nothing.
    file_unreadable = 1,
    // A file larger than the converter reads (maximum_source_bytes).
    file_too_large = 2,
    // A project directory with no *.Designer.cs, or more forms than read.
    project_empty = 3,
    // A form named by the request that the project does not have.
    form_not_found = 4,

    // WF1xx: reading.
    // C# the lexer does not admit: an unterminated string or comment, an
    // interpolated string, a character the language does not have.
    csharp_lexical = 101,
    // The designer file has no `void InitializeComponent()` with a body.
    initialize_component_missing = 102,
    // A statement of InitializeComponent the reader does not understand:
    // it is skipped and quoted in the detail.
    statement_skipped = 103,
    // An expression nested deeper than maximum_expression_depth, or a
    // statement longer than the reader keeps.
    expression_too_deep = 104,
    // A .resx file that is not XML the bounded reader admits.
    resx_invalid = 105,
    // More statements, objects or diagnostics than the converter keeps.
    limit_reached = 106,

    // WF2xx: mapping.
    // A Windows Forms type with no GUI.Forms counterpart: the object is
    // written as a panel holding its children, with the class
    // `winforms-unmapped`.
    type_unmapped = 201,
    // A project's own control class, converted as the nearest Windows
    // Forms base the converter knows.
    type_converted_as_base = 202,
    // A property GUI.Forms has no member for (the member manifest says
    // unmatched): left out.
    property_unmapped = 203,
    // A property the member manifest marks deferred or excluded: left out.
    property_not_admitted = 204,
    // A property GUI.Forms has but a form document cannot set (read only,
    // not in the form schema, a window setting outside the form root's):
    // written into the C# binding instead.
    property_to_code = 205,
    // A colour or font: written into the theme sheet under the object's
    // stable-ID selector, since a form holds no presentation.
    presentation_to_theme = 206,
    // A value the converter cannot spell for the property's kind (an
    // expression it does not evaluate, a choice the enumeration lacks).
    value_unconverted = 207,
    // An event GUI.Forms has no interaction for: its handler is not
    // attached, and the binding says so.
    event_unmapped = 208,
    // A resource the .resx does not hold, or a form that reads resources
    // without a .resx beside it.
    resource_missing = 209,
    // An image, icon or other binary resource: not carried into the form.
    resource_binary = 210,
    // A data binding the converter cannot write as a `bind` element.
    binding_unconverted = 211,
    // A field name that is not a stable ID segment, spelled as one
    // (textBox1 becomes text-box1).
    identity_respelled = 212,
    // A member whose GUI.Forms behaviour is recorded as differing from
    // Windows Forms' (manifests/winforms-divergences.json): the detail
    // gives GUI.Forms' behaviour.
    known_divergence = 213,
    // A handler method the form class does not declare.
    handler_missing = 214,
    // A method call on an object that the converter does not map: written
    // into the C# binding as a comment.
    call_to_code = 215,

    // WF3xx: layout.
    // Windows Forms' automatic scaling (AutoScaleMode and its
    // dimensions): GUI.Forms lays out in logical units at every scale, so
    // it is left out.
    auto_scale_dropped = 301,
    // A layout setting carried over approximately (a docked control's
    // order, a split container's panels).
    layout_approximated = 302,
};

struct ConversionDiagnostic final {
    ConversionCode code{ConversionCode::file_unreadable};
    std::string file{};
    // From 1; zero when the record concerns the whole file.
    std::uint32_t line{};
    std::uint32_t column{};
    // The object (its field name) or member the record is about.
    std::string subject{};
    std::string detail{};

    friend bool operator==(const ConversionDiagnostic& left, const ConversionDiagnostic& right) noexcept {
        const bool equal = left.code == right.code && left.file == right.file && left.line == right.line &&
                           left.column == right.column && left.subject == right.subject &&
                           left.detail == right.detail;
        return equal;
    }
};

// The most records one list keeps; the rest are counted.
inline constexpr std::size_t maximum_conversion_diagnostics = 2000U;
// The most bytes of a detail kept; a longer one ends in "...".
inline constexpr std::size_t maximum_conversion_detail_bytes = 240U;

// A conversion's diagnostics, in the order found. Caller-owned and reused.
class ConversionDiagnosticList final {
public:
    // Keeps the record (its detail bounded) while fewer than
    // maximum_conversion_diagnostics are kept, and counts it either way.
    void add(ConversionDiagnostic diagnostic);
    // Ends every record's lifetime and the counts; the capacity remains.
    void clear() noexcept;

    [[nodiscard]] std::span<const ConversionDiagnostic> records() const& noexcept { return records_; }
    std::span<const ConversionDiagnostic> records() const&& = delete;
    [[nodiscard]] std::size_t error_count() const noexcept { return errors_; }
    [[nodiscard]] std::size_t warning_count() const noexcept { return warnings_; }
    [[nodiscard]] std::size_t note_count() const noexcept { return notes_; }
    [[nodiscard]] std::size_t dropped_count() const noexcept { return dropped_; }

private:
    std::vector<ConversionDiagnostic> records_{};
    std::size_t errors_{};
    std::size_t warnings_{};
    std::size_t notes_{};
    std::size_t dropped_{};
};

[[nodiscard]] ConversionSeverity conversion_severity(ConversionCode code) noexcept;
// "WF201".
[[nodiscard]] std::string conversion_code_label(ConversionCode code);
// "type_unmapped"; "unknown" for a value outside the enumeration.
[[nodiscard]] std::string_view conversion_code_name(ConversionCode code) noexcept;
[[nodiscard]] std::string_view conversion_severity_name(ConversionSeverity severity) noexcept;

// One line, as compilers write them:
// `Form1.Designer.cs:42:13: warning: WF201 type_unmapped: chart1: ...`.
[[nodiscard]] std::string conversion_diagnostic_line(const ConversionDiagnostic& diagnostic);

} // namespace gui_forms::winforms_converter
