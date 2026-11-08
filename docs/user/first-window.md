# Your first window

This program opens one native window containing a label and a button, and
updates the label each time the button is clicked. It uses only the public
headers and links `GUIForms::Application`; the CMake project in
[Consume the SDK](consume-the-sdk) builds it unchanged.

```cpp
#include <gui_forms/application.hpp>
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/component.hpp>
#include <gui_forms/event.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace gf = gui_forms;

// A Component owns the subscriptions it registers. The retained parent owns
// the controls; this object adds behaviour without adding visual ownership.
class Greeter final : public gf::Component {
public:
    Greeter() {
        root = gf::make_control<gf::Panel>(gf::StableId("root"));
        status = gf::make_control<gf::Label>(gf::StableId("status"), "Ready.");
        run = gf::make_control<gf::Button>(gf::StableId("run"), "Run");
        (*status).set_bounds({24.0, 24.0, 432.0, 32.0});
        (*run).set_bounds({24.0, 72.0, 120.0, 36.0});
        (*root).add_child(status);
        (*root).add_child(run);
        gf::on((*run).clicked(), *this, &Greeter::on_run);
    }

    std::shared_ptr<gf::Panel> root{};
    std::shared_ptr<gf::Label> status{};
    std::shared_ptr<gf::Button> run{};

private:
    void on_run() {
        ++runs_;
        const std::string text = "Clicked " + std::to_string(runs_) + " time(s).";
        (*status).set_text(text);
    }

    unsigned runs_{};
};

int main() {
    try {
        // The application first: the windows made after it join it.
        gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
        if (!created) return 1;
        gf::Application& application = *created.application;
        Greeter greeter{};
        std::unique_ptr<gf::Window> window =
            std::make_unique<gf::Window>(greeter.root, gf::Size{480.0, 140.0});
        const gf::ApplicationResult result = application.run(
            std::move(window),
            {.title = "GUI.Forms — first window",
             .initial_size = {480.0, 140.0},
             .minimum_size = {320.0, 140.0}});
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

## What each part does

Controls are created once with `make_control<T>(StableId, ...)`
: Every control carries an immutable, nonempty `StableId` that is unique
  within its window. Stable IDs name controls in logs, instrumentation
  snapshots, semantic actions and automation; the Gallery's automation
  channel addresses controls by exactly these strings.

The tree owns the controls
: `add_child` gives the panel strong ownership of the label and the button.
  Children refer to their parent weakly. Keeping your own `shared_ptr` is
  allowed and is how `Greeter` reaches `status` from a handler.

`on(event, owner, &Owner::method)` registers an owned handler
: The connection token is kept by the `Component`. When `Greeter` is
  disposed or destroyed, the subscription is revoked before the button can
  call back into a dead object. Handlers may take a prefix of the event's
  arguments; `clicked()` supplies a `ButtonBase&` sender, which `on_run()`
  omits because this handler does not need it.

Mutation is imperative and synchronous
: `Label::set_text` stores the new text and marks paint, semantics and
  accessibility dirty. It also requests measure and arrange when the label
  is auto-sized or has an unspecified dimension; this example's fixed-size
  label does not need a layout pass for each click.
  The host paints on a later frame using the accumulated damage
  bounds, which may include gaps between changed areas. See
  [Update scopes and dirtiness](update-scopes-and-dirtiness).

The application comes first
: `Application::create` makes the program's application: its theme is
  compiled once, and every window made after it on the same thread joins it
  and shares its resources. A refusal (another application, a refused theme)
  makes nothing. The application must outlive its windows.

`Window` is the retained model; `Application::run` is the host
: `Window` binds to the thread that constructs it and owns the root, focus,
  capture, damage and scheduling. It works without any native window at all, which is how the
  tests run. `Application::run` hands the model to the native host for your
  platform, presents it, pumps events until the window closes, and returns an
  `ApplicationResult` whose `accepted()` is true for an ordinary close.
  The window's model must be created on the thread that calls `run`.

Window options are a designated-initializer struct
: `ApplicationWindowOptions` also offers `ready`, `closing` and `closed`
  callbacks and `print_metrics_on_close`, which prints the instrumentation
  snapshot to standard output as one JSON line when the window closes. Set it
  to see the counters described in [Painting and damage](painting-and-damage).
  `initially_visible` and `hide_on_close` apply only to secondary windows
  run together with this one.

## A window from a form document

The same window can be described in a `.form` document and generated at build
time. With `MODEL`, `gui_forms_add_form` generates a class that *is* the
window: a `gui_forms::Window` subclass whose controls are typed members, made
in document order and bound to the model's members when it is constructed.

```cmake
gui_forms_add_form(my_app forms/settings.form CLASS SettingsWindow
                   MODEL my_app::Settings MODEL_HEADER my_app/settings.hpp)
```

```cpp
gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
my_app::Settings settings{};             // the model, declared before the window
std::unique_ptr<SettingsWindow> window = std::make_unique<SettingsWindow>(settings);
(*(*window).ok).set_text("Apply");       // a member per control: a wrong name does not compile
```

- **The model** is named once, in the CMake call. The constructor takes it only
  when the form binds; a form without bindings is constructed with no
  arguments, and `DERIVED` without `MODEL` generates that class.
- **The application.** Each constructor has an overload that takes the
  `Application&` first; without it the window joins the current application.
- **A control root** (`user-control`, `panel`, ...) generates a subclass of
  that type instead, made with `X::make(model)`.
- **What a form does not describe**, such as a collection's items or a click's
  behaviour, the program attaches to the members afterwards.

Without `MODEL` or `DERIVED`, the generated class is the earlier document
class template over an owner, which the Gallery still uses.

## Where to go next

- [Working with controls](controls) covers the conventions every control shares.
- [Retained tree](retained-tree) explains ownership, stable
  identity and disposal in detail.
- `examples/reference/collection_gallery.cpp` and `collection_gallery.form` in
  the repository build a window from a form document, with a command bar,
  shared `Value<T>` state, a choice group and expandable sections.
