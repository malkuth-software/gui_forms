#pragma once

// GUI_FORMS_LOCALIZABLE(property) marks a parameter or field whose string is
// text a user reads: a control's text, a tool tip, a title, a placeholder, an
// accessible name or description, a column header, or an item's text. The
// GUI.Forms Clang plugin (tools/localizable_plugin) records every string
// literal that reaches a marked parameter or field, with the property named
// here and its call site, in a table the runtime reads back
// (gui_forms::localizable_literals). A helper function of your own that
// passes its parameter on to a marked one is found when you mark that
// parameter too. With another compiler, or without the plugin, the mark
// changes nothing.
#if defined(__clang__)
#define GUI_FORMS_LOCALIZABLE(property) [[clang::annotate("gui_forms.localizable:" #property)]]
#else
#define GUI_FORMS_LOCALIZABLE(property)
#endif
