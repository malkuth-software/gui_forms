# Forms

A **form document** (a `.form` file) describes an interface: which controls
exist, their stable IDs, where they sit, their property values and their
bindings. It holds nothing else. The build turns each document into a C++
class, and your program attaches the behaviour.

A form has no theme and no handlers:

- Colours and fonts belong to the program's theme. The form names each
  object's role with `classes`, and the theme gives that role its look
  ([Themes](themes)).
- Behaviour belongs to program code. Event handlers attach to the generated
  members, and a window's [rules](rules) live on the window.

## A document

```css
/* settings.form */
form settings.window {
  text: "Settings";
  client-size: 480 320;
  accept-button: settings.ok;
  cancel-button: settings.cancel;

  panel settings.root {
    check-box settings.autosave {
      text: "Save automatically";
      bounds: 16 16 300 28;
      checked: bind(autosave);
    }
    numeric-up-down settings.interval {
      bounds: 16 52 120 28;
      value: bind(interval_minutes);
      classes: "field";
    }
    button settings.ok { text: "OK"; bounds: 280 272 88 32; command: bind(accept); classes: "default-action"; }
    button settings.cancel { text: "Cancel"; bounds: 376 272 88 32; command: bind(dismiss); }
  }
}
```

The syntax belongs to the CSS family and is read by the same parser as theme
sheets:

- **Objects.** Each object is a type name, then its stable ID, then a block.
  The type is the control's registered name in kebab case (`check-box`,
  `numeric-up-down`). Child objects are nested in their parent's block, in
  z-order and tab-traversal order as written.
- **Properties.** Each property is its schema name in kebab case, then a
  value: `tab-index: 2;`, `anchor: top right;`, `padding: 4 8;`,
  `items: "AM" "FM";`. The property's type decides how the value is read. A
  value the type cannot take is refused with its line and column.
- **Comments** are `/* ... */`. An untouched document round-trips byte for
  byte, and an edit of one property changes only that property's bytes. The
  same property written in program code and in a document is the same
  property ([Properties](properties)).
- **Classes.** `classes: "field wide";` gives an object the classes a theme
  selects (`.field`).

A document is refused before anything is built when it is wrong. That covers
an unknown type or property, a duplicate stable ID, a value of the wrong
kind, a colour or font property (E813, which belongs in the theme), and an
`on-` handler reference (E816: attach behaviour from program code). Each
diagnostic carries its code, the file, the line and the column.

## The window is the form

A document whose root is `form` describes a window. Its root properties are
the window's own: `text` (the title), `client-size` or `size`, and the
`accept-button` and `cancel-button` it names. The form's single child object
is the window's root control.

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
  sources the document binds, found by name: `bind(autosave)` reads
  `settings.autosave`. A form that binds nothing is constructed without a
  model.
- **The application.** Each constructor has an overload that takes an
  `Application&` first. Without it, the window joins the current
  application, or makes its own resources when there is none, as a test
  does.
- **A control root.** A document whose root is a control (`user-control`,
  `panel` and so on) generates a subclass of that control's type instead,
  made with `X::make(model)`.
- **Application types.** `TYPES "note_card,my_app::NoteCard,user_control,my_app/note_card.hpp"`
  lets a document name your own control types.

Without `MODEL` or `DERIVED`, `gui_forms_add_form` generates the earlier
document class template over an owner, with `initialize_component(owner)`.

## Bindings

A property binds in its own slot: `value: bind(interval_minutes);`. The
binding is made when the control is created. The property takes the source's
value at its place in document order, inside the control's
`begin_init`/`end_init` bracket, and the subscription is made in the same
step. No control is ever shown with a default value that a binding then
overwrites.

| Written | Source | Notes |
|---|---|---|
| `checked: bind(autosave);` | a `Value<bool>`, `Value<int>` or `Value<double>` member | The `Value` type must carry the property's kind, or the generated code does not compile. |
| `command: bind(accept);` | a `Command` member | The button runs the command, follows its enabled state and shows its checked state. |
| `text: bind(people, "name");` | a `BindingSource` member and a field of its current record | Goes through the control's `data_bindings()`. |

- **Direction.** The property's table entry decides the default. A property
  the user edits binds two way, and anything else binds one way. Write
  `bind(level, one_way)` to narrow a two-way default. Asking for two way on a
  one-way property is refused (E818).
- **Update trigger.** A two-way text bound to a field writes back when the
  control validates, which is Forms' `OnValidation` and the default. Write
  `bind(people, "name", update on-change)` to write back on every edit.
- **Converters.** `convert <name>` is reserved and refused for now (E999).
- **Lifetime.** The control owns what the binding made and ends it when the
  control is destroyed. The model must outlive the window, so declare it
  first. A disposed `Value` is no longer written.
- **Refusals.** An unbindable property or a malformed binding is E817. A
  derived class that binds without `MODEL` is E812.

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
rules in 0.5, so the program adds them after it makes the window, beside the
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
