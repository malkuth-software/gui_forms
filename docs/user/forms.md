# Forms

A **form document** (a `.form` file) describes an interface: which controls
exist, their stable IDs, where they sit, their property values and their
bindings. It holds nothing else. The build turns each document into a C++
class, and your program attaches the behaviour.

A form has no theme and no handlers (a [mockup](mockups) may declare
handlers that only record themselves):

- Colours and fonts belong to the program's theme. The form names each
  object's role with `classes`, and the theme gives that role its look
  ([Themes](themes)).
- Behaviour belongs to program code. Event handlers attach to the generated
  members, and a window's [rules](rules) live on the window.

## A document

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- settings.form -->
<form xmlns="urn:gui-forms:form:1" id="settings.window"
    text="Settings"
    client-size="480 320"
    accept-button="settings.ok"
    cancel-button="settings.cancel">
  <panel id="settings.root">
    <check-box id="settings.autosave"
        text="Save automatically"
        bounds="16 16 300 28">
      <bind property="checked" to="autosave"/>
    </check-box>
    <numeric-up-down id="settings.interval" bounds="16 52 120 28" classes="field">
      <bind property="value" to="interval_minutes"/>
    </numeric-up-down>
    <button id="settings.ok" text="OK" bounds="280 272 88 32" classes="default-action">
      <bind property="command" to="accept"/>
    </button>
    <button id="settings.cancel" text="Cancel" bounds="376 272 88 32">
      <bind property="command" to="dismiss"/>
    </button>
  </panel>
</form>
```

A form document is XML, read by GUI.Forms' own bounded XML reader: UTF-8
(a byte order mark is admitted), elements, attributes, comments, the five
predefined entities and character references. There is no DTD, no CDATA and
no processing instruction other than the leading XML declaration. The file
keeps the `.form` extension.

- **The namespace.** The root element declares
  `xmlns="urn:gui-forms:form:1"`, and no other element declares one. The
  version is in the name: a later, incompatible vocabulary is `:2`. An XML
  editor that knows the namespace's schema offers completion and checks
  the document as you type.
- **Objects.** Each object is an element named by its registered type in
  kebab case (`check-box`, `numeric-up-down`), and `form` is a window. The
  `id` attribute holds the object's stable ID and is required. Child
  elements are the object's children, in z-order and tab-traversal order
  as written. An object holds no text: only whitespace and comments
  between its children. Two element names are reserved and are no
  objects: `bind` (a binding, below) and `span` (a grid's region).
- **Properties.** Each other attribute is a property: its schema name in
  kebab case, its value in the attribute (`tab-index="2"`,
  `anchor="top right"`, `padding="4 8"`). The property's type decides how
  the value is read. A value the type cannot take is refused with its line
  and column. Attributes may be written in any order, on one line or one
  per line; the generated code sets them in the order written. An
  attribute is always a value: a property is bound with a `bind` element
  instead ([Bindings](#bindings)).
- **Text.** A text property's value is the attribute's text, after XML
  decodes it: `&amp;`, `&lt;`, `&quot;` and so on, and `&#10;` for a line
  break (XML reads a literal line break in an attribute as a space). No
  text needs an escape of its own: `text="bind(x) is a call"` is that text.
- **Lists.** A text collection, such as `items` or `classes`, is one
  attribute of items separated by whitespace. An item that holds whitespace
  or begins with a quote is quoted, with single quotes inside a
  double-quoted attribute: `items="AM FM 'Wide FM'"`. A quoted item takes
  the escapes `\n`, `\r`, `\t`, `\"`, `\'`, `\\` and `\u{H}`, and no
  others.
- **Classes.** `classes="field wide"` gives an object the classes a theme
  selects (`.field`, `.wide`).
- **Comments** are `<!-- ... -->`, anywhere XML admits them (not inside a
  tag). An untouched document round-trips byte for byte, and an edit of one
  property changes only that property's bytes. The same property written in
  program code and in a document is the same property
  ([Properties](properties)).
- **Layout declarations.** The CSS layout panels (`flex-panel`,
  `grid-panel`, `block-panel`) take their container declarations as their
  `style` property, and each child its `item-style`:
  `style="grid-template-columns: 1fr 2fr; column-gap: 6px"`. These are
  geometry only, never colour or type. A grid's named spans are `span`
  elements directly inside it, before or between its children:
  `<span name="head" anchor="0 0" columns="2"/>`, placed with
  `area="head"` on a child.

## Refusals

A document is refused before anything is built when it is wrong, and every
defect found is reported together. Each diagnostic carries its code, the
file, the line and the column: of the element's `<` for an object, of the
attribute's name for a property, and of the offending word inside a value.
A refused document generates nothing.

| Code | Refused |
|---|---|
| E801 `form_root_invalid` | a root that is not an object, or an object without an `id` |
| E802 `form_type_unknown` | a type the form schema does not list |
| E803 `form_identity_repeated` | one stable ID on two objects |
| E804 `form_property_unknown` | a property the type does not list, set or named by a `bind` |
| E805 `form_property_read_only` | a property a document may not set |
| E806 `form_member_repeated` | one property written twice on one object (under two spellings, as an attribute and by a `bind`, or by two `bind` elements), or one span name on two spans (XML itself refuses an attribute written twice, as E821) |
| E807 `form_value_invalid` | a value the property's kind does not admit, or that the property refuses |
| E809 `form_children_not_admitted` | a child element inside a type that holds no children, or anything inside a `bind` |
| E810 `form_style_block` | an element named `style`: the look belongs to the theme, reached by `classes` |
| E812 `form_generation_refused` | something the C++ generator cannot spell: a type without a C++ name, a stable ID that is no member name or clashes with another, a reserved source spelling, or a binding without `MODEL` |
| E813 `form_presentation_property` | a colour or font property: it belongs in the theme |
| E814 `form_span_conflict` | grid spans that overlap, a span past the grid's limit or over a placed child, an `area` naming no span, a `cell` inside a span |
| E815 `form_escape_invalid` | an escape the language does not admit, in a quoted list item |
| E816 `form_handler_removed` | an `on-<interaction>` handler attribute: attach behaviour from program code |
| E817 `form_binding_invalid` | a `bind` without `property` or `to`, a `to` that is not one C++ identifier, an empty `field`, a `field` or `mode` on a Command, or a binding on a property that is not bindable |
| E818 `form_binding_option_invalid` | an unknown `bind` attribute, or a misplaced one, such as `mode="two-way"` on a one-way property |
| E820 `form_property_renamed` | a property the language spells another way: `layout-direction` is written `right-to-left` (left-to-right is `no`, right-to-left is `yes`, inherit is `inherit`) |
| E821 `form_xml_invalid` | a document that is not well-formed XML in the reader's subset (the detail names what the reader found), text inside an object, or a missing, wrong or extra `xmlns` |
| E999 `not_yet_built` | a form reserved for a later phase, such as a binding's `converter` |

E808 is retired. Loading a document at run time adds two:

| Code | Refused |
|---|---|
| E811 `form_load_refused` | a type the run-time registry cannot make, or an object, child, value or span a made object refuses |
| E819 `form_binding_source_refused` | a binding whose source is missing, of another kind, or refused; in a mockup, a fixture value of the wrong kind or a `shown-in` that names nothing that can show it |

## Loading a document at run time

A program can load a document while it runs, with no build step, through
`gui_forms/forms/form_documents/form_documents.hpp` and
`gui_forms/forms/form_mockup/form_mockup.hpp`:

- `check_form_document` checks a document as the form compiler would and
  returns every diagnostic with its file, line, column, code and detail.
- `load_form_document_window` loads a document that binds nothing into a
  window to look at.
- `FormMockup` loads a document, its fixture data and a theme sheet into a
  control of your window, and replaces what it shows each time you load
  again. Its bindings read the fixture instead of a model, and its
  `on-<interaction>` attributes record themselves instead of being
  refused. [Mockups](mockups) describes it and the fixture format.

Each refusal is reported with the codes above, at the same lines and
columns as the form compiler reports it.

## The window is the form

A document whose root is `form` describes a window. Its root properties are
the window's own attributes: `text` (the title), `client-size` or `size`,
and the `accept-button` and `cancel-button`, each naming a button's stable
ID. The form's single child element is the window's root control.

`gui_forms_add_form` with `MODEL` generates a class that *is* the window. It
is a `gui_forms::Window` subclass whose controls are typed members, made in
document order and bound to the model when the class is constructed:

```cmake
find_package(GUIForms CONFIG REQUIRED COMPONENTS Controls)
gui_forms_add_form(my_app forms/settings.form CLASS SettingsWindow NAMESPACE my_app
                   MODEL my_app::Settings MODEL_HEADER my_app/settings.hpp)
```

```cpp
#include "settings_form.hpp"   // generated: <stem>_form.hpp in the build tree

my_app::Settings settings{};   // the model, declared before the window
std::unique_ptr<my_app::SettingsWindow> window = std::make_unique<my_app::SettingsWindow>(settings);
(*window).settings_ok;         // a std::shared_ptr<gui_forms::Button> member
my_app::SettingsWindow::window_title;   // "Settings"
gui_forms::ApplicationResult result = application.run(std::move(window));   // the run owns the window
```

`run` takes the window by `std::unique_ptr`: the run owns it until the
window closes. A test that only drives the model constructs the window on
the stack instead (`my_app::SettingsWindow window(settings);`) and never
runs it.

- **Members.** Each control and component is a public member named after its
  stable ID, with dots and hyphens turned into underscores
  (`settings.ok` becomes `settings_ok`). A wrong name does not compile.
- **The model** is named once, in the CMake call. Its members are the
  sources the document binds, found by name: `to="autosave"` reads
  `settings.autosave`. A form that binds nothing is constructed without a
  model.
- **The application.** Each constructor has an overload that takes an
  `Application&` first. Without it, the window joins the current
  application, or makes its own resources when there is none, as a test
  does.
- **A control root.** A document whose root is a control (`user-control`,
  `panel` and so on) generates a subclass of that control's type instead,
  made with `X::make(model)`. The layout panels (`flex-panel`,
  `grid-panel`, `block-panel`, `flow-layout-panel-compat`,
  `table-layout-panel-compat`) are final: their measure, arrange and the
  solver state they keep for inspection are one contract a subclass could
  break. A document rooted in one generates a class that holds the panel
  as its first member instead; `X::make(model)` returns that class by
  value, and `root_control()` is the control to show or add to a parent.
- **Application types.** `TYPES "note_card,my_app::NoteCard,user_control,my_app/note_card.hpp"`
  lets a document name your own control types.
- **Composite controls.** `COMPOSITES "controls/card-with-action.control,my_app::CardWithAction,card_with_action_control.hpp"`
  lets a document name a composite control that `gui_forms_add_control`
  generates. The document names it by its `.control` name
  (`<card-with-action title="Storage">`) and sets or binds its declared
  properties like any control's. The element's children go into the
  composite's slot, in document order. A composite without a slot refuses
  children (E809). The composite's own children stay private: a form
  cannot name or reach them.

A designer reads the same types from the form schema. `gui_forms_add_form_schema(my_app
OUTPUT form_schema.json COMPOSITES ...)` runs the form compiler's `--schema` mode at build
time. It writes every type a document may name, as JSON: the type's properties and
interactions, and whether its children go into it, into a slot or nowhere. For a
composite, it also lists the slot, the events and the commands. A designer lists and
places exactly what the build compiles.

The form schema and the control lock file describe what authors write, so they keep
the document's kind words: an event declared `payload="boolean"` is listed as
`boolean`. The control schema (`manifests/control-schema.json`) and the C ABI are the
run-time surface and use C++'s words for the same payload (`bool`; `double`,
`std::int64_t`, `Point` and so on for the other kinds), the words every C++ event's
payload is written in.

Without `MODEL` or `DERIVED`, `gui_forms_add_form` generates the earlier
document class template over an owner, with `initialize_component(owner)`.

## Bindings

A property is bound with a `bind` element inside its object, which names
the property and the source:

```xml
<check-box id="settings.auto" text="Save automatically">
  <bind property="checked" to="autosave"/>
</check-box>
<progress-bar id="job.progress">
  <bind property="value" to="progress" mode="one-way"/>
</progress-bar>
```

A binding is structure, like the object that holds it, so it is an element
and never an attribute's value: every attribute stays a literal value. A
`bind` goes directly inside the object, before or between its children
(first, by convention), even in a type that holds no children. It holds
nothing itself and has no `id`.

The binding is made when the control is created. The property takes the
source's value at the `bind` element's place in document order, after the
object's attributes and inside the control's `begin_init`/`end_init`
bracket, and the subscription is made in the same step. No control is ever
shown with a default value that a binding then overwrites.

| Written | Source | Notes |
|---|---|---|
| `<bind property="checked" to="autosave"/>` | a `Value<bool>`, `Value<int>` or `Value<double>` member | The `Value` type must carry the property's kind, or the generated code does not compile. |
| `<bind property="command" to="accept"/>` | a `Command` member | The button runs the command, follows its enabled state and shows its checked state. |
| `<bind property="text" to="people" field="name"/>` | a `BindingSource` member and a field of its current record | Goes through the control's `data_bindings()`. |

- **`property`** names the bound property in kebab case, as its attribute
  would be written. The object's own type must list it (E804), and it is
  either set or bound, once (E806).
- **`to`** names the source: one C++ identifier, the model's member.
- **`field`** names a field of a `BindingSource`'s records.
- **`mode`**: the property's table entry decides the default. A property
  the user edits binds two way, and anything else binds one way. Write
  `mode="one-way"` to narrow a two-way default; `mode="two-way"` on a
  one-way property is refused (E818).
- **`update`**: a two-way text bound to a field writes back when the
  control validates, which is Forms' `OnValidation` and the default
  (`update="on-validate"`). Write `update="on-change"` to write back on
  every edit.
- **`converter`** is reserved and refused for now (E999).
- **Lifetime.** The control owns what the binding made and ends it when the
  control is destroyed. The model must outlive the window, so declare it
  first. A disposed `Value` is no longer written.
- **Refusals.** An unbindable property or a malformed binding is E817, an
  unknown or misplaced attribute E818. A derived class that binds without
  `MODEL` is E812.

A composite control's `.control` document binds its children to its own
properties the same way, with `to="self.<property>"`:
`<bind property="text" to="self.title" mode="one-way"/>`.

## Behaviour

A form names no program code. Attach behaviour to the generated members
after the window is made, with an owning component:

```cpp
class SettingsBehaviour final : public gf::Component {
public:
    SettingsBehaviour(my_app::SettingsWindow& window, my_app::Settings& settings) : settings_(settings) {
        gf::on((*window.settings_interval).value_changed(), *this, &SettingsBehaviour::interval_changed);
        gf::on(settings.accept.invoked(), *this, &SettingsBehaviour::save);
    }
    // ...
};
```

A collection's items, such as a list's rows or a command bar's buttons, are
data. The program sets them on the members too.

## Rules

A window keeps its [rules](rules) in `window.rules()`. Form documents hold no
rules, so the program adds them after it makes the window, beside the
handlers:

```cpp
gf::Rule clear{};
clear.trigger = gf::trigger::key_pressed(gf::PhysicalKey::escape);
clear.conditions.push_back(gf::condition::focused(*window.settings_interval));
clear.actions.push_back(gf::builtin::invoke(settings.reset));
clear.consume = true;
const gf::RuleAddResult added = window.rules().add(clear);
```

A rule that names a control is disarmed when that control is disposed. A
window's rules end with the window.

## The pieces together

[Converting an existing application](converting-an-app) moves a program
written in C++ onto a form, a model, a theme sheet and rules, one step at a
time. `examples/reference/forms/collection_gallery.form` and its program are a
complete window in this shape.
