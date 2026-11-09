# Form documents

A `.form` document describes an interface: the controls, their stable IDs,
their places, their property values and their bindings. It holds nothing
else. The build generates a C++ class from it.

```text
/* forms/settings.form */
form settings.window {
  text: "Settings";
  client-size: 480 320;
  accept-button: settings.ok;

  panel settings.root {
    check-box settings.autosave { text: "Save automatically"; bounds: 16 16 300 28; checked: bind(autosave); }
    button settings.ok { text: "OK"; bounds: 376 272 88 32; command: bind(accept); classes: "default-action"; }
  }
}
```

```cmake
gui_forms_add_form(my_app forms/settings.form CLASS SettingsWindow NAMESPACE my_app
                   MODEL my_app::Settings MODEL_HEADER my_app/settings.hpp)
```

- **The window is the form.** A `form` root generates a `gui_forms::Window`
  subclass; a control root (`user-control`, `panel`, ...) a subclass of that
  control, or, for a final layout panel (`flex-panel`, `grid-panel`, ...),
  a class holding the panel as a member (`root_control()` shows it). Its
  controls are typed public members named after their stable
  IDs (`settings.ok` becomes `settings_ok`).
- **Bindings** are made when each control is created, from the model named
  in CMake: `bind(autosave)` reads `settings.autosave`. A property the user
  edits binds two way, anything else one way. `bind(people, "name")` binds a
  field of a `BindingSource`.
- **No look, no code.** Colours and fonts belong to the theme (E813); the
  form names roles with `classes`. Handlers and rules belong to program code
  (E816 for an `on-...:` handler).
- **Grids** declare spans and place children by area or cell:
  `span footer { anchor: 2 0; columns: 3; }`, then `area: "footer";` or
  `cell: 0 2;` on a child.
- **Round trip.** An untouched document round-trips byte for byte, and an
  edit of one property changes only its bytes.

Every defect is reported at build time with its code, file, line and column
(E801 to E818, E999). Full page, with the refusal table:
[Forms](https://gui-forms.readthedocs.io/user/forms.html).
