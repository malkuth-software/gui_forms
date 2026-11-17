#pragma once

// A composite control's C++ generator (cp34, phase 2): the header the form
// compiler's control mode writes for a `.control` document, which
// gui_forms_add_control runs at build time. Internal; the form compiler
// (tools/form_compiler/) is its program.
//
// The header holds one class over CompositeControl, constructed from a
// stable ID like any control:
//
//   class TogglePill : public gui_forms::CompositeControl {
//   public:
//       static constexpr std::string_view document_name = "toggle-pill.control";
//       static constexpr std::string_view document = R"gfcontrol(...)gfcontrol";
//       static constexpr std::array<gui_forms::CompositeChildType, N> child_types{...};
//       explicit TogglePill(gui_forms::StableId stable_id);
//       static std::shared_ptr<const gui_forms::CompositeDefinition> shared_definition();
//       bool read_checked() const;
//       void write_checked(bool value);
//       ...
//   };
//
// The document is embedded and loaded once, on first use, through
// load_composite_definition with the child types it names. The build has
// already checked and compiled it, so only a library that differs from the
// compiler's could refuse it then (std::logic_error). Typed read_ and
// write_ functions are generated for the boolean, number and text
// properties a binding reaches (not presentational, not commands); they go
// through the registered property, so a write is converted and refused as
// set_property_value refuses it.

#include "../composite_definition/composite_definition.hpp"

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace gui_forms::control_language {

struct ControlGeneratorOptions final {
    // The class's name: one identifier.
    std::string class_name{};
    // The namespace it is declared in, `::`-separated identifiers; empty
    // for the global namespace.
    std::string namespace_name{};
    // The document's file name, as the header cites it and as the
    // definition's diagnostics name it at run time.
    std::string source_name{};
};

enum class ControlGenerateOutcome : std::uint8_t {
    generated,
    // A refusal (E812), every one reported: `generated` is unchanged.
    refused,
};

// The largest document embedded, kept under the string-literal limits of
// the compilers a consumer may use (65,535 bytes is the smallest).
inline constexpr std::size_t maximum_embedded_control_bytes = 60000U;

// Generates the header for `definition`, compiled from `source`. Refused
// when the class or namespace name is not an identifier path, when a child
// type has no C++ class or header, when the document is larger than
// maximum_embedded_control_bytes, or when it holds the raw literal's
// delimiter. All borrowed for the call.
[[nodiscard]] ControlGenerateOutcome generate_control_cpp(const theme_language::ThemeSource& source,
                                                          const CompositeDefinition& definition,
                                                          const ControlGeneratorOptions& options,
                                                          std::string& generated,
                                                          theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::control_language
