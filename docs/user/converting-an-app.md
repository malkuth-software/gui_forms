# Converting an existing application

This guide moves a GUI.Forms program written entirely in C++ onto the shape
GUI.Forms recommends:

- a **form document** for what exists and where it sits;
- a **model** for the state the form binds to;
- a **theme package** for the look;
- **program code** for the behaviour, with handlers and window rules.

Each step leaves a working program, so you can stop after any of them. The
example is the template harness's `example_app`, a window with a label, a
text box, a button and a list. Its finished form is GUI.Forms' reference app.

## Before

The original program builds its controls in a constructor, places them, and
subscribes to the button:

```cpp
Greeter::Greeter()
    : root(gf::make_control<gf::Panel>(gf::StableId("example.root"))),
      greeting(gf::make_control<gf::Label>(gf::StableId("example.greeting"), "Type a name, then press Greet.")),
      name(gf::make_control<gf::TextBox>(gf::StableId("example.name"))),
      greet(gf::make_control<gf::Button>(gf::StableId("example.greet"), "Greet")),
      greeted(gf::make_control<gf::ListBox>(gf::StableId("example.greeted"))) {
    (*greeting).set_bounds({20.0, 20.0, 380.0, 28.0});
    // ... the other bounds, then add_child for each control ...
    gf::on((*greet).clicked(), *this, &Greeter::on_greet);
}
```

`main` makes a `Window` over `greeter.root` and runs it.

## 1. Move the controls into a form document

Write one object for each control you made. Use the same stable IDs, and
nest each object as it is nested in the tree. The property values you set
become declarations:

```text
form example.window {
  text: "GUI.Forms example";
  client-size: 420 360;
  accept-button: example.greet;

  panel example.root {
    label example.greeting { text: "Type a name, then press Greet."; bounds: 20 20 380 28; }
    text-box example.name { bounds: 20 60 260 30; tab-index: 0; }
    button example.greet { text: "Greet"; bounds: 292 60 108 30; tab-index: 1; }
    list-box example.greeted { bounds: 20 136 380 164; tab-index: 2; }
  }
}
```

The window's own settings move into the document as well. The title, the
client size and the default button were window options or calls in `main`.

Generate the window class in CMake:

```cmake
gui_forms_add_form(example_app forms/greeter.form CLASS GreeterWindow NAMESPACE example_app DERIVED)
```

`DERIVED` without `MODEL` generates a window class that takes no model. In
`main`, replace the hand-built window with
`std::make_unique<example_app::GreeterWindow>()`. The controls are now its
members, `window.example_greet` and so on, so the constructor that made them
goes away. The handler subscription stays for now and moves to a behaviour
component in step 3.

**Check:** the window looks and behaves as before. The document refuses a
typo with its line and column. A wrong member name in C++ does not compile.

## 2. Give the state a model and bind it

State the controls share, or state your logic reads, moves into a model, and
the form binds to it:

```cpp
struct GreeterModel final {
    gf::Value<bool> shout{false};
    gf::Command greet{"example.greet", "Greet"};
    gf::Command clear{"example.clear", "Clear"};
};
```

```css
button example.greet { text: "Greet"; bounds: 292 60 108 30; command: bind(greet); }
check-box example.shout { text: "Shout the greeting"; bounds: 20 100 380 28; checked: bind(shout); }
```

Switch the CMake call to `MODEL example_app::GreeterModel MODEL_HEADER
greeter_model.hpp` and construct the window with the model. Declare the model
before the window, so the window and its bindings end first.

What changes:

- **A button runs a command** instead of a click handler. The command's
  enabled state disables the button, so an "is it allowed" rule lives in one
  place: `model.clear.set_enabled(has_names)`.
- **A bound check box** keeps the model's `Value<bool>` current both ways.
  The logic reads `model.shout.get()` instead of asking the control.
- **Calls to `bind(...)` are removed.** A control that had one
  (`check.bind(value)`, `button.bind(command)`) is bound in the document
  instead, when the control is created ([Forms](forms.md#bindings)).

## 3. Attach the behaviour from program code

Handlers subscribe to the model's commands, or to the generated members, from
a component that owns the subscriptions:

```cpp
Greeter::Greeter(GreeterWindow& window, GreeterModel& model) : window_(window), model_(model) {
    gf::on(model_.greet.invoked(), *this, &Greeter::on_greet);
    gf::on(model_.clear.invoked(), *this, &Greeter::on_clear);
}
```

Write properties that are cells by assigning to them, and read them with
`.read()` ([Properties](properties)):

```cpp
(*window_.example_greeted).visible = has_names;
```

A keyboard shortcut that used to be a key handler on a control, or a check in
the window's key preview, becomes a window rule. A rule runs before the
control route, and a consuming rule ends the key:

```cpp
void clear_name_action(Greeter& greeter, const gf::RuleFiring&) { greeter.clear_name(); }

gf::Rule escape{};
escape.trigger = gf::trigger::key_pressed(gf::PhysicalKey::escape);
escape.conditions.push_back(gf::condition::focused(*window_.example_name));
escape.actions.push_back(gf::action(*this, &clear_name_action));
escape.consume = true;
window_.rules().add(escape);
```

The behaviour is declared after the window, so it ends first. A rule whose
action context is a component is disarmed when that component is disposed.

## 4. Move colours and fonts into a theme

A form document cannot hold colour or font properties (E813). Every inline
`back_color`, `fore_color` and font you set moves into a theme package, and
the object names its role instead:

```css
label example.greeting { text: "Type a name, then press Greet."; bounds: 20 20 380 28; classes: "greeting"; }
```

```css
/* themes/example/example.theme.css */
:root { ink-on-paper: #1d3557 on #ffffff; }
.greeting { color: role(ink-on-paper); font-family: content; font-size: 16px; font-weight: 600; }
button.default-action::part(face) { border-radius: 6px; }
```

Embed the package with `gui_forms_add_theme(... FORMS forms/greeter.form)`
and compile it once, when the application is created
(`ApplicationConfig::theme.packages`). Write text colours as colour roles,
never as literals, so that forced high contrast can still replace them. If
you want a familiar look rather than your own, a window can show a built-in
familiar theme instead ([Themes](themes)).

## 5. Test without a window

The generated window is an ordinary `Window` model, so a test links
`GUIForms::Controls`, constructs it with a model and drives it:

```cpp
example_app::GreeterModel model{};
example_app::GreeterWindow window(model);
example_app::Greeter greeter(window, model);
(*window.example_name).set_text("Ada");
(*window.example_greet).perform_click();
```

The test can also compile the theme package against the window's resources
and require that it produces no findings. A rule is tested by sending a key
with `window.dispatch_key`.

## What stays in C++

These parts stay in C++:

- **A collection's items.** List rows and command-bar buttons are data, and
  the program sets them.
- **Logic.** Sequential behaviour belongs inside a handler or a named rule
  action, not in the document.
- **Your own control types.** Name them in documents with
  `gui_forms_add_form(... TYPES ...)`.
- **A window built at run time from data.** Only build-time generation is
  public in 0.5.
