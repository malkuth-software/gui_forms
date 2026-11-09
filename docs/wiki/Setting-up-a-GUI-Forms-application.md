# Setting up a GUI.Forms application

This tutorial builds a small program from nothing: a window where you type a
note, press **Add** (or Ctrl+Enter), and see it appear in a list. On the way
it uses every part of the GUI.Forms 0.5 approach:

1. a **CMake project** that consumes the installed SDK;
2. a **form document** (`.form`) that says which controls exist;
3. a **model** that holds the state the form binds;
4. a **theme package** that gives the program its look;
5. `Application::create`, the **window class** the form generates, and
   **rules** and handlers that attach the behaviour;
6. building, running, and testing without a window.

The finished project has this layout:

```text
notes/
  CMakeLists.txt
  main.cpp
  notes_model.hpp
  forms/notes.form
  themes/notes/theme.json
  themes/notes/notes.theme.css
```

## 1. Get the SDK

You need an installed GUI.Forms SDK and the toolchain it was built with
(LLVM 22, through the SDK's `cmake/llvm22.cmake`). Build and install it from
source as the [Install](https://gui-forms.readthedocs.io/user/install.html)
page describes; the result is a prefix such as `/path/to/sdk` holding
`include/gui_forms/`, the libraries, `lib/cmake/GUIForms/` and
`share/GUIForms/fonts/`. Keep the path of `cmake/llvm22.cmake`: every
consumer project is configured with the same toolchain file.

## 2. The CMake project

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.25)
project(Notes LANGUAGES CXX)

find_package(GUIForms 0.2 CONFIG REQUIRED COMPONENTS Application)

add_executable(notes main.cpp)
target_compile_features(notes PRIVATE cxx_std_20)
target_link_libraries(notes PRIVATE GUIForms::Application)
target_include_directories(notes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Keep the selected SDK ahead of any older GUI.Forms installation.
set_target_properties(notes PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)

# The window class the form document generates, bound to the model.
gui_forms_add_form(notes forms/notes.form CLASS NotesWindow NAMESPACE notes
                   MODEL notes::NotesModel MODEL_HEADER notes_model.hpp)
# The program's theme, embedded and checked against the form.
gui_forms_add_theme(notes themes/notes FUNCTION notes_theme NAMESPACE notes
                    FORMS forms/notes.form)

# Windows: the GUI.Forms DLLs beside the executable.
if(WIN32)
    add_custom_command(TARGET notes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:notes> $<TARGET_FILE_DIR:notes>
        COMMAND_EXPAND_LISTS)
endif()
# Windows and Linux: the bundled fonts in fonts/ beside the executable.
if(NOT APPLE)
    add_custom_command(TARGET notes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:notes>/fonts")
endif()
```

- `GUIForms::Application` is the native host for your platform, and it
  already contains the core, the controls and the drawing library. Link
  nothing else from GUI.Forms beside it: CMake stops with an error if you
  add `GUIForms::Core` too.
- `gui_forms_add_form` runs the form compiler at build time and generates
  `notes_form.hpp` in the build tree.
- `gui_forms_add_theme` embeds the theme package and generates
  `notes_theme.hpp`. The build fails when the package's hash does not match
  its sheet, or when a stable-ID selector names nothing in the listed forms.
- GUI.Forms draws only its bundled fonts, never the system's. On macOS,
  ship them in the app bundle's `Contents/Resources/fonts`
  ([Consume the SDK](https://gui-forms.readthedocs.io/user/consume-the-sdk.html#fonts)).

## 3. The form document

A form says what exists: controls, their stable IDs, their places, their
property values and their bindings. It holds no colours, no fonts and no
program code.

```text
/* forms/notes.form */
form notes.window {
  text: "Notes";
  client-size: 420 320;
  accept-button: notes.add;

  panel notes.root {
    label notes.heading { text: "Notes"; bounds: 16 12 388 28; classes: "heading"; }
    text-box notes.entry { bounds: 16 48 300 28; placeholder-text: "Type a note"; }
    button notes.add { text: "Add"; bounds: 324 46 80 32; command: bind(add); }
    list-box notes.list { bounds: 16 88 388 192; }
    label notes.status { text: "No notes yet."; bounds: 16 288 388 24; classes: "status"; }
  }
}
```

- **Objects** are a type, a stable ID and a block. The root `form` is the
  window, and its single child is the window's root control.
- **Stable IDs** are required and unique in the window. They name controls in
  logs, automation and themes, and become the generated members' names:
  `notes.entry` becomes `notes_entry`.
- **`classes`** names a role; the theme gives it a look.
- **`command: bind(add)`** binds the button to the model's `add` command
  when the button is created: a click runs the command, and the command's
  enabled state enables the button.
- **`accept-button`** makes Enter press **Add** when the focused control does
  not use the key.

A document that is wrong is refused at build time with a code, the file, the
line and the column: an unknown type or property, a repeated stable ID, a
colour or font (E813: those belong in the theme), or an `on-...:` handler
(E816: behaviour belongs in program code). See
[Forms](https://gui-forms.readthedocs.io/user/forms.html).

## 4. The model

The model holds the state the form binds, found by name: `bind(add)` reads
the member `add`. Declare it before the window, so the window and its
bindings end first.

```cpp
// notes_model.hpp
#pragma once

#include <gui_forms/binding.hpp>
#include <gui_forms/commands.hpp>

namespace notes {

struct NotesModel final {
    gui_forms::Command add{"notes.add", "Add"};
};

} // namespace notes
```

## 5. The theme package

A theme package is a manifest and a sheet in the theme language, a subset of
CSS addressed to controls and their named parts.

```css
/* themes/notes/notes.theme.css */
:root {
  ink-on-paper: #1d3557 on #ffffff;
  muted-ink-on-paper: #5b6b79 on #ffffff;
}
.heading { color: role(ink-on-paper); font-family: content; font-size: 18px; font-weight: 700; }
.status { color: role(muted-ink-on-paper); }
button::part(face):hover { scale: 1.02; transition: scale 90ms ease; }
```

```json
{
  "format": "gui-forms-theme/1",
  "name": "notes",
  "version": "1.0.0",
  "engine": ">=0.3",
  "sheet": {"path": "notes.theme.css", "sha256": "<the sheet's SHA-256>"}
}
```

Put the sheet's hash in the manifest; `cmake -E sha256sum
themes/notes/notes.theme.css` prints it. Change the sheet, change the hash.

- **Colour roles** (`ink-on-paper: #1d3557 on #ffffff`) name a text colour
  on its background, so that forced high contrast can replace each by its
  meaning. Write roles, not literal text colours.
- **Selectors** name a control type (`button`), a stable ID
  (`#notes\.entry`, with the dot escaped), a class (`.status`), a part
  (`::part(face)`), a state (`:hover`), a control family
  (`:family(accent-button)`) and up to eight ancestors.
- The package compiles onto a **base**: GUI.Forms' own theme in this
  tutorial. Its dark variant is derived from your light colours.

See [Themes](https://gui-forms.readthedocs.io/user/themes.html) and
[The theme language](https://gui-forms.readthedocs.io/user/theme-language.html).

## 6. The program

```cpp
// main.cpp
#include "notes_form.hpp"    // generated by gui_forms_add_form
#include "notes_model.hpp"
#include "notes_theme.hpp"   // generated by gui_forms_add_theme

#include <gui_forms/application.hpp>
#include <gui_forms/component.hpp>
#include <gui_forms/event.hpp>
#include <gui_forms/rules.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gf = gui_forms;

namespace {

// The behaviour. A Component owns the subscriptions it makes, so they end
// when it does; the window owns the controls.
class Notes final : public gf::Component {
public:
    Notes(notes::NotesWindow& window, notes::NotesModel& model) : window_(window), model_(model) {
        gf::on(model_.add.invoked(), *this, &Notes::on_add);
        gf::on((*window_.notes_entry).text_changed(), *this, &Notes::on_entry_changed);
        model_.add.set_enabled(false);   // nothing to add yet
    }

private:
    void on_entry_changed(const std::string& text) {
        const bool has_text = !text.empty();
        model_.add.set_enabled(has_text);
    }

    void on_add() {
        const std::string note = (*window_.notes_entry).text.read();
        if (note.empty()) return;
        (*window_.notes_list).items.append(note);
        (*window_.notes_entry).text = std::string{};
        const std::size_t count = (*window_.notes_list).items.read().size();
        (*window_.notes_status).text = std::to_string(count) + " note(s).";
    }

    notes::NotesWindow& window_;
    notes::NotesModel& model_;
};

// Ctrl+Enter in the entry runs Add: a rule with one trigger, two conditions
// and a built-in action. Rules run before the focused control sees the key,
// and `consume` keeps the text box from receiving it.
bool arm_rules(notes::NotesWindow& window, notes::NotesModel& model) {
    gf::Rule add_note{};
    add_note.trigger = gf::trigger::key_pressed(gf::PhysicalKey::enter);
    add_note.conditions = {gf::condition::modifiers(gf::Modifier::control),
                           gf::condition::focused(*window.notes_entry)};
    add_note.actions = {gf::builtin::invoke(model.add)};
    add_note.consume = true;
    const gf::RuleAddResult added = window.rules().add(add_note);
    const bool armed = added.refusal == gf::RuleRefusal::none;
    return armed;
}

void report_findings(const std::vector<gf::ThemeCompileDiagnostic>& findings) {
    for (const gf::ThemeCompileDiagnostic& finding : findings) {
        std::cerr << finding.code << ' ' << finding.file << ':' << finding.line << ':' << finding.column << ' '
                  << finding.detail << '\n';
    }
}

} // namespace

int main() {
    try {
        // 1. The application: the theme compiles once, here.
        gf::ApplicationConfig config{};
        config.theme.base = gf::ThemeBase::gui_forms;
        config.theme.packages.push_back(notes::notes_theme());
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        report_findings(created.findings);   // warnings, or why the theme was refused
        if (!created) return 1;
        gf::Application& application = *created.application;
        {
            // 2. The model, then the window: its controls are made and bound now.
            notes::NotesModel model{};
            std::unique_ptr<notes::NotesWindow> window = std::make_unique<notes::NotesWindow>(application, model);
            // 3. The behaviour, declared after the window so that it ends first.
            Notes behaviour(*window, model);
            if (!arm_rules(*window, model)) return 1;
            // 4. The run: the host presents the window until it closes.
            gf::ApplicationWindowOptions options{};
            options.title = std::string(notes::NotesWindow::window_title);
            options.initial_size = {420.0, 320.0};
            options.minimum_size = {420.0, 320.0};
            const gf::ApplicationResult result = application.run(std::move(window), options);
            if (!result.accepted()) return 1;
        }
        // Once, after the last run: frees the rendering library's caches.
        const gf::ApplicationResult ended = gf::Application::shutdown();
        return ended.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

What each part does:

- **`Application::create`** makes the one current application. It compiles
  the theme (the base, then your packages) and refuses, making nothing, when
  the theme has an error; `created.findings` say why, with file, line and
  column.
- **`NotesWindow`** is the class the form generated: a `gui_forms::Window`
  subclass whose controls are public `std::shared_ptr` members. A wrong
  member name does not compile. Passing `application` is optional: without
  it the window joins the current application.
- **Properties** are written by assignment and read with `.read()`:
  `(*window_.notes_status).text = "...";`, `(*window_.notes_entry).text.read()`.
  Writing an equal value does nothing; a change marks the control dirty, and
  the host paints on its next frame. A list's `items` is a collection:
  `append`, `insert`, `erase`, `replace`, `clear`, or assign the whole list.
- **`gf::on(event, owner, &Owner::method)`** subscribes a named member
  function. The connection belongs to the owner, so destroying `Notes`
  disconnects it. There are no lambdas.
- **Rules** are data: a trigger, conditions that must all hold, and ordered
  actions, either built-in (`invoke`, `set`, `toggle`, `focus`) or a named
  function `void f(Context&, const gf::RuleFiring&)` with its context. The
  window owns its rules.
- **`run`** takes the window by `std::unique_ptr` and owns it until it
  closes. Make the window on the thread that calls `run`.

## 7. Build and run

Configure with the SDK's prefix and the same toolchain file, then build:

```sh
cmake -S notes -B notes/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/path/to/sdk \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/gui_forms/cmake/llvm22.cmake
cmake --build notes/build
```

On Windows, run these in the MSYS2 CLANG64 shell the SDK was built in. Then
start `notes/build/notes` (`notes.exe` on Windows). Type a note and press
**Add**, Enter or Ctrl+Enter.

To see the instrumentation, set `options.print_metrics_on_close = true;`:
the window prints its counters (frames, damage, layout passes) as one JSON
line when it closes.

## 8. Test without a window

A generated window is a complete model of the window and works without any
native window, which is how GUI.Forms' own tests run. A test links
`GUIForms::Controls` instead of `GUIForms::Application`, constructs the window
with a model and drives it. Move `Notes` and `arm_rules` into a header of
their own (`notes.hpp`) so that the program and the test share them:

```cpp
notes::NotesModel model{};
notes::NotesWindow window(model);        // no application: the built-in theme
Notes behaviour(window, model);
(*window.notes_entry).text = "Buy milk";
(*window.notes_add).perform_click();
const bool added = (*window.notes_list).items.read().size() == 1U;
```

Keys reach the window's rules through `window.dispatch_key`, and
`window.interaction_snapshot()` shows the input state.

## Where to go next

- [The application and its windows](https://gui-forms.readthedocs.io/user/application-and-windows.html):
  several windows, settings changed during a run, presentation overrides.
- [Properties](https://gui-forms.readthedocs.io/user/properties.html):
  views, collections, child properties such as a grid's `(*table).area[*c]`,
  and `as_type` conversions.
- [Rules](https://gui-forms.readthedocs.io/user/rules.html): the whole
  vocabulary, consume and lifetime.
- [Overlays, Popups and Splash](https://gui-forms.readthedocs.io/user/overlays-popups-and-splash.html):
  menus, dialogs, tooltips and the splashscreen.
- The repository's `examples/reference/` builds the same shape against the
  installed SDK in CI; `collection_gallery` is the complete reference
  program.
