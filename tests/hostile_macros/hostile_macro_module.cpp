// Hostile-macro conformance, the module path: `import gui_forms;` after every
// identifier of the module's umbrellas that a consumer may define has been
// defined as a macro that fails where it expands (hostile_macros_module.h,
// generated with the umbrellas as seeds). The module is compiled from its
// own unit (modules/gui_forms.cppm), so none of these macros can reach it.
// This unit spells nothing after the import: a consumer's own spelling of a
// GUI.Forms name is consumer space.
#include "hostile_macros_module.h"

import gui_forms;
