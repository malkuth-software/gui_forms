# Mockups

A **mockup** is an interface shown from files alone: a form document
(`.form`), sample data for it (`.fixture`) and, if it wants one, a theme
sheet (`.theme.css`). Nothing is compiled. A program reads the files while
it runs, shows the interface at once and shows it again when a file
changes, so a mockup can be added or edited at any time.

The devkit's `gui-forms-mockup` program is such a program: it lists every
mockup in a folder, opens one by name, reloads it on save, switches its
theme and colour scheme, captures screenshots and serves MCP. This page
describes the library API it is built on, `FormMockup`, and the fixture
format, so that any program can do the same.

## Loading a mockup

```cpp
#include "gui_forms/forms.hpp"

// host: a panel the program keeps in its window. The mockup borrows it,
// so declare the mockup after it.
gui_forms::FormMockup mockup(*host);

gui_forms::FormMockupText form{};
form.file = stxlib::View<char>(form_name.data(), form_name.size());
form.text = stxlib::View<char>(form_text.data(), form_text.size());
gui_forms::FormMockupText data{};          // an empty text: no fixture
data.file = ...;
data.text = ...;

if (mockup.load(gui_forms::FormDocumentSchema::registry(), form, data) ==
    gui_forms::FormMockupLoadOutcome::refused) {
    const gui_forms::FormDocumentCheck& check = mockup.check();
    // each diagnostic: its file, line, column, code (E8xx) and detail
}
```

`load` reads the document as the form compiler does: the same checks, the
same diagnostics, at the same lines and columns ([Forms](forms)). Three
things make it a mockup:

- **Its bindings read the fixture.** Each `<bind to="x"/>` takes the
  fixture's value or record set named `x`. A source the fixture lacks
  gets a stand-in: the property's default, or a record set with no rows.
  A command binding (`<bind property="command" to="submit"/>`) gets a
  command that records each time it runs.
- **Its handlers record themselves.** An `on-<interaction>` attribute,
  such as `on-click="save_claim"`, names no code. The mockup accepts it,
  never refuses it, and records each time the interaction is raised. The
  form compiler still refuses the same attribute (E816), so a mockup's
  handlers never reach a built program by accident.
- **The fixture fills lists, grids and trees** by the stable IDs it
  names (below).

A document's root may be a `form`, whose control is shown and whose
accept and cancel buttons go to the host's window, or a control.

### Hot reload

Load again whenever a file changes. A load that succeeds replaces the
shown tree with the new one in one step: the old tree is removed and the
new one added inside one layout suspension, so no frame shows half of
each. A load that is refused leaves the shown tree as it was, and
`check()` says why. The program shows the refusal and keeps the last good
interface on screen; it never has to stop.

`generation()` counts the loads that succeeded, and `clear()` removes the
tree.

### Retained mode

A load builds the tree once. Nothing is rebuilt per frame or per event:
the controls stay, as in any GUI.Forms window, until the next load
replaces them. A program that watches the files compares their
modification times on a timer and loads only when one changed.

### Themes

```cpp
const gui_forms::ThemeCompileResult compiled =
    mockup.set_theme(gui_forms::ThemeBase::windows_professional, sheet);
```

`set_theme` compiles the sheet's text (empty for none) onto a built-in base
and sets it as the host's theme override, so that only the mockup's tree
shows it and the program's own controls keep their theme. A refused sheet
changes nothing, and the result holds its diagnostics. For a familiar
theme, set `familiar_theme(...)` on the host with
`Control::set_theme_override`. The colour scheme is the window's:
`Window::set_presentation_overrides` with `colour_scheme` forces light or
dark.

## Handlers and events

`handler_count()` and `handler(i)` list what the shown tree declares: its
commands first, in the order the document first binds them, then its
`on-...` handlers in document order. Each says whether it is
`observed`. The mockup observes:

- every control's `click`, `double_click`, `mouse_enter`, `mouse_hover`
  and `mouse_leave`;
- a button's `clicked`, and its `click`, which a button records on every
  activation (pointer, keyboard, access key or `perform_click`), once;
- `<property>_changed` for each property the control has
  (`on-checked-changed`, `on-selected-index-changed`, `on-text-changed`);
- each command's `invoked`.

Any other interaction is listed with `observed` false. It is accepted,
not refused, and records nothing.

Each recorded event has a sequence number, the handler's name, the
object's stable ID and the interaction. `event_count()` and `event(i)`
read the most recent `FormMockup::kept_events` of them, oldest first;
`clear_events()` drops them. `set_event_sink` calls a function after each
one, for a log or a status line. `gui-forms-mockup` prints each event as a
line of JSON on standard output and offers them to MCP as the
`mockup.events` tool. A command a mockup binds is also an MCP tool,
`command.<name>`, as every command with an identity is.

## The fixture format

A fixture is a small XML file beside the form, read by the same XML reader
as form documents and refused with the same diagnostics. It holds sample
data and nothing else:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<fixture xmlns="urn:gui-forms:fixture:1">
  <value name="holder_name" value="Ada Marsh"/>
  <value name="autosave" value="true"/>
  <records name="claims" columns="claim date amount:number paid:boolean"
      shown-in="claims.grid">
    <row values="C-1041 2026-11-02 120.50 false"/>
    <row id="late" values="C-1042 2026-11-03 '1 200' true"/>
  </records>
  <tree shown-in="files.tree">
    <item text="Documents" expanded="true">
      <item text="Claim.pdf"/>
    </item>
  </tree>
</fixture>
```

- **`value`** is one scalar source, as a model's `Value` member would be:
  `<bind property="checked" to="autosave"/>` reads it. Its text is read
  as the bound property's kind (text, boolean, integer or number) when the
  mockup binds it; a text the kind cannot take is refused (E819).
- **`records`** is a record set, as a `BindingSource` would be.
  `<bind property="text" to="claims" field="claim"/>` reads the field of
  the current record, which is the first row. `columns` lists the fields,
  each written `name` (text) or `name:boolean`, `name:integer`,
  `name:number`. Each `row` gives its cells in column order in `values`,
  written as a form's list is: whitespace between items, an item that
  holds whitespace quoted, with the escapes of [Forms](forms) inside the
  quotes. A row with fewer values than columns leaves the rest empty. A
  row's `id` is its stable ID, `row-1`, `row-2` and so on by default.
- **`shown-in`** names objects of the form by stable ID, separated by
  whitespace, that show the rows. A data grid shows them as its data
  source. A list view shows them as its items: the first column is each
  item's text, the others its sub-items. A list box, a combo box or any
  control with a text `items` collection shows the first column.
- **`tree`** holds the items of the tree views its `shown-in` names,
  nested as written. An item's `id` is its stable ID (`item-1` and so on
  by default), and `expanded="true"` opens it.

A name is one C++ identifier and is used once across the values and
record sets. Every element sits directly where the example shows it, and
no element holds text. Record sets and trees need the mockup's host to be
in a window.

| Code | Refused |
|---|---|
| E821 | XML outside the reader's subset, a missing or wrong `xmlns`, or text inside an element |
| E801 | a root other than `fixture` |
| E802 | an element the format does not have |
| E804 | an attribute the element does not have |
| E803 | a name, or a row's or item's `id`, used twice |
| E807 | a missing attribute, a name that is not an identifier, a column kind the format does not have, a value its column cannot take, or more values than columns |
| E815 | an escape the form strings do not admit |
| E819 | a value of the wrong kind for the property bound to it, or a `shown-in` that names nothing that can show it |

## A mockup next to a built form

A mockup's document is an ordinary form document. Once the interface is
settled, remove its `on-...` attributes, give the program a model with the
members its bindings name, and build it with `gui_forms_add_form`
([Forms](forms)). The fixture is no longer read; the model's values take
its place. `examples/reference/form_mockup.cpp` loads a mockup, clicks a
button, reads the recorded event and reloads a broken and then a repaired
document.
