// The installed form compiler's extension types: none. A document names the
// registry's types and its own application types (--type) only.

#include "form_catalog.hpp"

namespace gui_forms::form_compiler {

void add_extension_types(form_language::FormSchema& schema) {
    static_cast<void>(schema);
}

} // namespace gui_forms::form_compiler
