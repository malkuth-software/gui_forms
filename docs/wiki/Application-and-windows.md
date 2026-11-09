# Application and windows

A program makes its **application** first, then its **windows**, then runs
them. The application owns what every window shares: the resources, the
theme (compiled once, at creation), the presentation defaults, the
keyboard-command keys and the tooltip switch.

```cpp
gf::ApplicationConfig config{};
config.theme.base = gf::ThemeBase::gui_forms;
config.theme.packages.push_back(my_app::my_app_theme());
gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
if (!created) return 1;   // already_exists, or theme_refused with created.findings
gf::Application& application = *created.application;

my_app::Settings settings{};   // the model, before the window
std::unique_ptr<my_app::MainWindow> window = std::make_unique<my_app::MainWindow>(application, settings);
const gf::ApplicationResult result = application.run(std::move(window));
```

- **One current application.** While it lives it is the current
  application, and a second `create` is refused. Windows made on its thread
  join it; the `application` argument only makes that visible.
  `application.close()` is refused while a joined window lives.
- **No application** (tests, headless code): a window makes its own
  resources with the built-in theme.
- **During a run**, `set_theme`, `set_presentation_settings` and
  `set_tool_tips_shown` reach every joined window that has no override of
  its own.
- **`run`** owns the window (`std::unique_ptr`) until it closes;
  `run(std::vector<gf::ApplicationWindow>)` runs up to 64 windows.
  `Application::shutdown()` ends the rendering library after the last run.

## Window kinds

| Kind | It is | It asks the user |
|---|---|---|
| Window | a full top-level window, made by the program | |
| Popup | an undecorated window owned by a window: alerts, dialog panels, menus, drop-downs | a decision |
| Splash | a headless, lightweight window: tooltips, the splashscreen | nothing |

Popups and Splash windows open from their owner; see
[Overlays, Popups and Splash](Overlays-Popups-and-Splash).

Full page: [The application and its windows](https://gui-forms.readthedocs.io/user/application-and-windows.html).
