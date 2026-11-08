# Localizable literals

Text a program sets after a window is built, such as `label.set_text("Ready")`
in an event handler, never appears in the window's initial description, so a
translator could not see it. GUI.Forms finds that text while the program
compiles. A Clang plugin records every string literal passed to a localizable
property, and the program can read the list back at run time.

The plugin changes no code. It adds one table per translation unit to the
object file, in a section of its own. Each record holds the property, a
variant name derived from the text, the literal's file, line and column, the
function that passes it, and the literal's bytes.

## Localizable properties

A parameter or field is localizable when its declaration carries
`GUI_FORMS_LOCALIZABLE(property)` (from
`gui_forms/text/localizable/localizable_property.hpp`). The toolkit marks:

| Property | Where |
| --- | --- |
| `text` | `set_text` of Label, ButtonBase, GroupBox, TabPage, TextBlock, RibbonColorPicker, Command and NotifyIcon; the text of menu, tool strip, menu strip, ribbon, backstage, list view, tree view, choice, section and command bar items |
| `tool_tip` | the `Control::tool_tip_text` property cell (also a TabPage's header tooltip and a field's hint), `ToolTip::set_tool_tip`, a tool strip item's `tool_tip_text`, and `RibbonToolTipContent`'s `title`, `description` and `disabled_reason` (not its `shortcut`, which comes from the key binding) |
| `title` | `set_title` of ColorDialog, OpenFileDialog and SaveFileDialog |
| `placeholder` | `set_placeholder_text` of TextBox and ComboBox |
| `accessible_name`, `accessible_description` | the Control property cells, and the accessible names of choice and command bar items |
| `header` | list view and tree list view column text, list view group headers, `DataGridViewColumn::header_text` |
| `items` | the `items` collection cell of ComboBox, ListBox, CheckedListBox and DomainUpDown (assignment, `insert`, `append` and `replace`), and `CheckedListBox::add_item` |

The text of a TextBox, ComboBox or DomainUpDown is the user's, so its
`set_text` is not marked.

A function of your own that passes its parameter on to a marked one is found
when you mark its parameter too:

```cpp
#include "gui_forms/text/localizable/localizable_property.hpp"

void show_status(gui_forms::Label& status, GUI_FORMS_LOCALIZABLE(text) std::string_view message) {
    status.set_text(std::string(message));
}

show_status(status, "Saved");  // recorded
```

## Turning it on and off

The toolkit builds the plugin with the Clang it is compiled with, using that
Clang's CMake package (Homebrew's `llvm@22` has it; on Linux, LLVM 22's Clang
development package). The `GUI_FORMS_LOCALIZABLE_PLUGIN` option, on by
default, builds it, compiles the Gallery with it and installs it with the SDK.
The configure first proves the plugin usable: it configures and builds the
plugin's project and compiles a probe unit with it. When the Clang package is
missing or unusable (present but naming libraries that are not installed, for
example), or the compiler cannot load the plugin, the build goes on without
the plugin and says so in one status line; the reason's details are in
`localizable_plugin/probe.log` in the build tree.

A program consuming the installed SDK with Clang 22 is compiled with the
plugin through `GUIForms::Core`. To compile without it, turn the package's
option off before `find_package`:

```cmake
set(GUI_FORMS_LOCALIZABLE_LITERALS OFF)
find_package(GUIForms 0.2 CONFIG REQUIRED COMPONENTS Application)
```

## Reading the table

`gui_forms::localizable_literals()` returns every record in the program and in
each shared library it has loaded. A translation unit compiled without the
plugin adds nothing, and a program can mix the two.

From the command line, `tools/dump_localizable_literals.py` prints the records
of a linked program or library (it reads the section with `llvm-objcopy`):

```sh
python3 tools/dump_localizable_literals.py build/my_forms
```

## What is found

A literal is found when it reaches a marked parameter or field without being
computed:

- in every branch of an `if`, a `switch` or a conditional expression, whether
  or not the branch runs;
- at every site that passes it, each with its own record and the same variant
  name;
- after adjacent literals are joined (`"Save " "all"` is one record, `Save all`);
- as an ordinary or raw literal, with its escapes resolved and its UTF-8 bytes
  as written; a `u8` literal reaches a marked `char8_t` parameter directly, or
  a `char` one through a cast;
- through `std::string`, `std::string_view`, `"…"s` and `"…"sv`;
- through a `const` or `constexpr` variable or static member initialised by a
  literal (`constexpr const char* title = "Settings";`), recorded at the
  constant's definition and under each function that uses it;
- in templates, once however many instantiations repeat it, and in lambdas;
- in a designated or positional aggregate initialiser, a constructor argument
  or an assignment to a marked field (`spec.text = "Open";`);
- in a static or a shared library compiled with the plugin.

## What is not found

- Text built at run time, such as `"Page " + std::to_string(page)`, or read
  from a file. Such text is never translated.
- A literal that reaches a marked parameter only through an unmarked
  parameter of a function of your own.
- A control's initial text passed to its constructor; initial text is
  translated with the window's description.
- A template's calls that are never instantiated.
- Wide, UTF-16 and UTF-32 literals.
- A constructor that keeps only part of its argument (a substring) is recorded
  with the whole literal.

## Requirements and limits

- The plugin loads only into the Clang 22 it was built against. The package
  passes it to Clang 22 compilers only; with any other compiler the mark does
  nothing.
- Records are read back on macOS (each loaded image), Linux (each loaded ELF
  object's notes) and Windows (the program's own image only).
- The plugin itself is never built with sanitizers, and its tables are left
  out of AddressSanitizer's instrumentation, so a program compiled with
  `-fsanitize=address` reads them as usual.
