#pragma once

// A form schema as a designer reads it (cp34): every type a document may
// name, with what a designer needs to list and place one. The form
// compiler writes it (--schema, with the application's --type and
// --composite types), so a designer lists exactly what the build compiles.
// Internal; the JSON is the contract.
//
//   {"schema": 1, "types": [
//     {"name": "card_with_action", "element": "card-with-action",
//      "kind": "composite",          // control, component, composite or form
//      "cpp_type": "my_app::CardWithAction", "header": "card_with_action_control.hpp",
//      "children": "slot",           // self, slot, refused or none
//      "properties": [{"name": "title", "kind": "text", "category": "Appearance",
//                      "writable": true, "bindable": true, "choices": []}, ...],
//      "child_properties": [...],
//      "interactions": ["action_invoked", ...],
//      "composite": {"name": "card-with-action", "version": "1.0", "slot": "content",
//                    "events": [{"name": "action-invoked", "payload": null}],
//                    "commands": ["..."]}},
//     ...]}
//
// "composite" is present only for a composite type whose definition the
// schema holds (FormSchemaType::composite): the run-time loader's, and the
// form compiler's --composite types. Names are snake_case except the
// composite's own (as its document writes them) and "element", the
// kebab-case element a document writes. Types are in schema order.

#include "../form_model/form_model.hpp"

#include <string>

namespace gui_forms::form_language {

// The JSON of `schema`, ending in a line break. Borrowed for the call.
[[nodiscard]] std::string form_schema_json(const FormSchema& schema);

} // namespace gui_forms::form_language
