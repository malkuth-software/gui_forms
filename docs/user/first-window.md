# Your first window

This program opens one native window containing a label and a button, and
updates the label each time the button is clicked. It is the shape GUI.Forms
recommends: a form document says what exists, a model holds the state it
binds, a theme package gives it its look, and program code attaches the
behaviour. It uses only the installed
package; the CMake project in [Consume the SDK](consume-the-sdk) builds it.

```css
/* forms/greeter.form */
form greeter.window {
  text: "GUI.Forms - first window";
  client-size: 480 140;

  panel greeter.root {
    label greeter.status { text: "Ready."; bounds: 24 24 432 32; classes: "status"; }
    button greeter.run { text: "Run"; bounds: 24 72 120 36; command: bind(run); }
  }
}
```

```cpp
// greeter_model.hpp: the state the form binds, found by name.
#pragma once
#include <gui_forms/binding.hpp>

namespace greeter {

struct GreeterModel final {
    gui_forms::Command run{"greeter.run", "Run"};
};

} // namespace greeter
```

```css
/* themes/greeter/greeter.theme.css, named with its hash by themes/greeter/theme.json */
:root { ink-on-paper: #1d3557 on #ffffff; }
.status { color: role(ink-on-paper); font-family: content; font-size: 15px; font-weight: 600; }
```

```cmake
gui_forms_add_form(my_forms forms/greeter.form CLASS GreeterWindow NAMESPACE greeter
                   MODEL greeter::GreeterModel MODEL_HEADER greeter_model.hpp)
gui_forms_add_theme(my_forms themes/greeter FUNCTION greeter_theme NAMESPACE greeter
                    FORMS forms/greeter.form)
```

```cpp
// main.cpp
#include "greeter_form.hpp"   // generated: <stem>_form.hpp in the build tree
#include "greeter_theme.hpp"  // generated: <function>.hpp

#include <gui_forms/application.hpp>
#include <gui_forms/component.hpp>
#include <gui_forms/event.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace gf = gui_forms;

// A Component owns the subscriptions it registers. The window owns the
// controls; this object adds behaviour without adding visual ownership.
class Greeter final : public gf::Component {
public:
    Greeter(greeter::GreeterWindow& window, greeter::GreeterModel& model) : window_(window) {
        gf::on(model.run.invoked(), *this, &Greeter::on_run);
    }

private:
    void on_run() {
        ++runs_;
        const std::string text = "Clicked " + std::to_string(runs_) + " time(s).";
        (*window_.greeter_status).text = text;
    }

    greeter::GreeterWindow& window_;
    unsigned runs_{};
};

int main() {
    try {
        gf::ApplicationConfig config{};
        config.theme.packages.push_back(greeter::greeter_theme());
        gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
        if (!created) return 1;   // created.findings say why the theme was refused
        gf::Application& application = *created.application;
        greeter::GreeterModel model{};   // declared before the window it binds
        std::unique_ptr<greeter::GreeterWindow> window =
            std::make_unique<greeter::GreeterWindow>(application, model);
        Greeter behaviour(*window, model);
        gf::ApplicationWindowOptions options{};
        options.title = std::string(greeter::GreeterWindow::window_title);
        options.initial_size = {480.0, 140.0};
        options.minimum_size = {320.0, 140.0};
        const gf::ApplicationResult result = application.run(std::move(window), options);
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

## What each part does

The form document declares the controls
: Each object is a type, a stable ID and a block of properties
  ([Forms](forms)). Every control carries an immutable, nonempty stable ID
  that is unique within its window. Stable IDs name controls in logs,
  instrumentation snapshots, semantic actions and automation, and
  `window.control("greeter.status")` finds a control by its ID.

The generated class is the window
: `gui_forms_add_form` turns the document into `greeter::GreeterWindow`, a
  `gui_forms::Window` subclass. Its controls are typed public members named
  after their stable IDs (`greeter.status` becomes `greeter_status`), made in
  document order and owned by the window's tree. A wrong member name does
  not compile.

The theme belongs to the program
: The form names the label's role (`classes: "status"`); the theme package
  gives the role its colour and font. A form holds no colours or fonts. The
  application compiles the package once, when it is created, and every window
  it makes uses the result ([Themes](themes)).

The model is bound when the window is made
: `command: bind(run)` connects the button to `model.run`: a click invokes
  the command, and the command's enabled state enables the button. The
  binding is made as the button is created, so no control shows a default
  that a binding then overwrites. Declare the model before the window, so the
  window and its bindings end first.

`on(event, owner, &Owner::method)` registers an owned handler
: The connection token is kept by the `Component`. When `Greeter` is
  disposed or destroyed, the subscription is revoked before the command can
  call back into a dead object. `Greeter` is declared after the window, so
  it ends first.

Properties are cells, and a write is synchronous
: `(*window_.greeter_status).text = text;` stores the new text and marks
  paint, semantics and accessibility dirty; `.read()` reads it
  ([Properties](properties)). The label also requests measure and arrange
  when it is auto-sized; this example's fixed-size label needs no layout pass
  for each click. The host paints on a later frame using the accumulated
  damage bounds. See [Update scopes and dirtiness](update-scopes-and-dirtiness).

`Application::create` makes the application; `run` is the host
: The application owns what every window shares: the resources, the theme
  compiled once at creation ([Themes](themes)) and the presentation
  defaults. A window made with it joins it. `run` hands the window to the
  native host for your platform, presents it, pumps events until the window
  closes, and returns an `ApplicationResult` whose `accepted()` is true for
  an ordinary close. The window must be made on the thread that calls `run`.
  A window also works without any native window at all, which is how the
  tests run: construct `GreeterWindow(model)` and drive its members.

Window options are a plain struct
: `ApplicationWindowOptions` also offers `ready`, `closing` and `closed`
  callbacks and `print_metrics_on_close`, which prints the instrumentation
  snapshot to standard output as one JSON line when the window closes. Set it
  to see the counters described in [Painting and damage](painting-and-damage).
  `initially_visible` and `hide_on_close` apply only to secondary windows
  run together with this one.

## Where to go next

- [Working with controls](controls) covers the conventions every control shares.
- [Retained tree](retained-tree) explains ownership, stable
  identity and disposal in detail.
- [Forms](forms) and [Converting an existing application](converting-an-app)
  cover documents, bindings and rules in full.
- `examples/reference/forms/collection_gallery.form` and its program extend this
  shape with a command bar, shared `Value<T>` state, a choice group and
  expandable sections.
