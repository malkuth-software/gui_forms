#pragma once

// A form document loaded as a window at run time (stage B,
// APP_WINDOW_RULES_DESIGN B.3): the run-time equivalent of the generated
// Window subclass. Internal until stage D, which carries it to the C ABI;
// the result is ABI-shaped (plain record, explicit error, string IDs).
//
// Order: the document is read, its model built and its objects made and
// bound exactly as load_form does (the generated constructor's order);
// then the window is made over the form's control and given the accept
// and cancel buttons. A missing or mistyped source refuses the whole
// load (FD-6 R2) and nothing is built.

#include "form_loader.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::form_language {

enum class WindowLoadError : std::uint8_t {
    none,
    // The document does not parse, or its model is refused, or the load
    // is (a source among the reasons): the diagnostics say where.
    refused,
    // The document's root is a control, not a form: it describes no window.
    not_a_window,
};

// What load_window made. On a refusal nothing is made and every field
// but `error` is empty.
struct LoadedWindowResult final {
    WindowLoadError error{WindowLoadError::none};
    std::unique_ptr<Window> window{};
    // The document's title, for the caller's host (a Window holds none).
    std::string title{};
    // The components the document makes (commands among them), which only
    // the load owns: keep them with the window.
    std::vector<Component::Ptr> components{};

    [[nodiscard]] explicit operator bool() const noexcept {
        const bool loaded = error == WindowLoadError::none;
        return loaded;
    }
};

// Loads `document` (named `file` in the diagnostics) against `schema`,
// binding to `sources`, into a window of `application`. Every argument is
// borrowed for the call; the sources must outlive the controls bound to
// them. Refusals are reported to `diagnostics`. UI thread; the Window
// constructor's std::logic_error off the application's thread or once it
// is closed, with nothing made.
[[nodiscard]] LoadedWindowResult load_window(Application& application, std::string_view file,
                                             std::string_view document, const FormSchema& schema,
                                             const FormSources& sources,
                                             theme_language::ThemeDiagnosticList& diagnostics);
// The same, the window joining the current application (or none: a test
// window with its own resources).
[[nodiscard]] LoadedWindowResult load_window(std::string_view file, std::string_view document,
                                             const FormSchema& schema, const FormSources& sources,
                                             theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
