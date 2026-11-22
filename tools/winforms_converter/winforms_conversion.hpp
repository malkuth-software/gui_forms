#pragma once

// A Windows Forms form converted to GUI.Forms (stage MCP G7; owner,
// 2026-11-22): one designer file, its .resx and the project's classes in;
// a form document, a theme sheet and a C# binding out, with a diagnostic
// for everything that did not carry over as it was.
//
// What goes where follows the form format decisions (notes
// planning/audits/FORM_FORMAT_AUDIT_2026-11-21.md, docs/user/forms.md):
//
// - **The form document** holds structure: which objects exist (an element
//   per object, named by its registry type in kebab case), their stable IDs
//   (`<form>.<field>`, the field respelled as a stable ID segment), their
//   nesting and order, their literal property values (attributes) and their
//   bindings (`<bind property to field? update?/>` elements, from
//   `DataBindings.Add`). A Form becomes the `form` root holding one `panel`
//   (`<form>.root`) for the form's own controls; a UserControl becomes a
//   `user-control` root. Child order is GUI.Forms' painter order, later on
//   top: the reverse of the order the designer adds controls in (Windows
//   Forms' Controls collection puts index 0 on top). Components (a timer,
//   a tool tip, a dialog) are objects directly inside the root.
// - **The theme sheet** holds description the form may not: a control's
//   colours and font become declarations under its stable-ID selector
//   (`#main-form\.ok-button { color: #FF0000; }`). A system colour is the
//   theme's own and is left out.
// - **The C# binding** is the code half: a partial class of the form class
//   that finds each object under the form's root by its stable ID, attaches
//   the form class's handlers to the mapped interactions, and lists, as
//   comments with their diagnostic codes, the settings and calls a form
//   document cannot hold.
//
// Layout geometry: Location and Size become `bounds="x y width height"`,
// Padding (left, top, right, bottom) becomes `padding` in CSS order,
// TableLayoutPanel cells become the children's `cell`, `column-span` and
// `row-span`. Windows Forms' automatic scaling is left out (WF301): GUI.Forms
// lays out in logical units.
//
// Nothing here touches the file system; winforms_project.hpp finds the
// files and the converter program writes the results.

#include "conversion_diagnostics.hpp"
#include "csharp_classes.hpp"
#include "designer_reader.hpp"
#include "resx_reader.hpp"

#include "../../src/core/form_language/form_model/form_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gui_forms::winforms_converter {

struct ConversionOptions final {
    // The form's stable ID; empty takes the class name respelled
    // (`MainForm`: main-form).
    std::string form_id{};
    // The namespace of the C# binding; empty takes the designer file's.
    std::string csharp_namespace{};
    // The file names the outputs cite one another by; empty takes the
    // form ID with `.form`, `.theme.css` and `.GuiForms.g.cs`.
    std::string form_file{};
    std::string theme_file{};
    std::string binding_file{};
};

// What a bound property's `to` names: a member the program's model must
// have for the form to compile (gui_forms_add_form MODEL) or load
// (FormSources).
enum class ModelSourceKind : std::uint8_t {
    // A BindingSource: `<bind ... field="...">`.
    binding_source,
    // A Value<bool>, Value<int> or Value<double>.
    value,
};

struct ModelSource final {
    std::string name{};
    ModelSourceKind kind{ModelSourceKind::binding_source};
    // The Windows Forms field it replaces.
    std::string winforms_field{};
};

struct ConversionCounts final {
    std::size_t objects{};
    std::size_t unmapped_objects{};
    std::size_t properties{};
    std::size_t bindings{};
    std::size_t events{};
    std::size_t theme_declarations{};
    std::size_t code_items{};
    std::size_t dropped{};
};

// One converted form. Caller-owned; replaced whole by a conversion that
// succeeds.
struct ConvertedForm final {
    // The form class's name, and the form's stable ID.
    std::string class_name{};
    std::string form_id{};
    std::string form_file{};
    std::string form_document{};
    // Empty when the form sets no colour or font.
    std::string theme_file{};
    std::string theme_sheet{};
    std::string binding_file{};
    std::string csharp_binding{};
    std::vector<ModelSource> sources{};
    ConversionCounts counts{};
};

enum class ConversionOutcome : std::uint8_t {
    converted,
    // An error (no object could be made of the root): `form` unchanged.
    refused,
};

// Converts `designer` against `schema` (the form compiler's
// registry_form_schema, plus any application types). `resx` is the form's
// resources, or null when it has none; `classes` the project's classes
// (the form class among them, for its base and its handlers). Everything
// is borrowed for the call.
[[nodiscard]] ConversionOutcome convert_winforms_form(const DesignerDocument& designer, const ResxDocument* resx,
                                                      const CSharpClassIndex& classes,
                                                      const form_language::FormSchema& schema,
                                                      const ConversionOptions& options, ConvertedForm& form,
                                                      ConversionDiagnosticList& diagnostics);

// `text` as a form document's attribute value: XML's five escapes, and
// character references for line breaks and tabs, which XML would otherwise
// read as spaces.
[[nodiscard]] std::string escape_form_attribute(std::string_view text);

} // namespace gui_forms::winforms_converter
