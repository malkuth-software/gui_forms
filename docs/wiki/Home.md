# GUI.Forms

GUI.Forms is a retained, CPU-painted C++20 user-interface toolkit with the
vocabulary of Windows Forms, for macOS, Windows and Linux. Controls are
owned objects in a tree, a change marks only what it touches, and a window
that has settled draws nothing.

A GUI.Forms 0.5 program has four parts:

- a **form document** (`.form`) that says which controls exist, generated at
  build time into a window class;
- a **model** whose values and commands the form binds;
- a **theme package** in a subset of CSS that gives the program its look;
- **program code**: `Application::create`, the window, handlers and rules.

```cpp
gf::ApplicationConfig config{};
config.theme.packages.push_back(notes::notes_theme());
gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
if (!created) return 1;                       // created.findings say why
notes::NotesModel model{};                    // what the form binds
std::unique_ptr<notes::NotesWindow> window = std::make_unique<notes::NotesWindow>(*created.application, model);
(*(*window).notes_status).text = "Ready.";    // a typed member and a property
const gf::ApplicationResult result = (*created.application).run(std::move(window));
```

## Start here

- [Setting up a GUI.Forms application](Setting-up-a-GUI-Forms-application):
  the end-to-end tutorial, from CMake to a running, tested window.

## Topics

- [Application and windows](Application-and-windows)
- [Form documents](Form-documents)
- [Properties](Properties)
- [Rules](Rules)
- [Themes](Themes)
- [Overlays, Popups and Splash](Overlays-Popups-and-Splash)
- [Roadmap](Roadmap)

The full documentation, with the API reference, is at
[gui-forms.readthedocs.io](https://gui-forms.readthedocs.io/). These wiki
pages are written in the repository under `docs/wiki/` and published from
there; edit them there.
