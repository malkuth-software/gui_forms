#pragma once

// A composite type's lock file (cp34, the composite-control language;
// design note COMPOSITE_CONTROL_LANGUAGE.md, section 5.2). Internal.
//
// `version="<major>.<minor>"` is the type's contract: within one major
// version, part, state, property, event and command names are only added,
// never renamed or removed, and a property's kind never changes. The build
// writes `<name>.control.lock` from the model, the lock is committed with
// the document, and a later build checks the model against it, as the ABI
// manifest checks its append-only tables.
//
// The lock is text, one entry per line, in the model's document order:
//
//   control <name> <major>.<minor>
//   property <name> <kind>        a kind word (control_property_kind_name),
//                                 or `alias` for an alias
//   event <name> <payload>        a kind word, or `none`
//   command <name>
//   publish <state>               a published standard state
//   state <name>                  a custom state
//   part <name> <role>            the part's role (part_role_name, kebab)
//
// Blank lines and lines beginning with `#` are ignored on reading.

#include "../control_model/control_model.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::control_language {

// The lock text of `model`, ending in a line feed.
[[nodiscard]] std::string write_control_lock(const ControlModel& model);

enum class ControlLockOutcome : std::uint8_t {
    // Every locked entry is in the model with its kind, or the model's
    // major version differs from the lock's (a new major may rename).
    kept,
    // At least one E923: a malformed lock, another type's lock, a lower
    // version than the lock's, or a locked entry missing or changed.
    violated,
};

// Checks `model` against the lock text `lock`, read from `lock_file` (the
// name its diagnostics carry). Every violation is reported, each at its
// line of the lock. All arguments are borrowed for the call.
[[nodiscard]] ControlLockOutcome check_control_lock(const ControlModel& model, std::string_view lock,
                                                    std::string_view lock_file,
                                                    theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::control_language
