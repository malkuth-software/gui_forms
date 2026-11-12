#pragma once

// The engine state behind GF_TABLE_THEMES and GF_TABLE_HOST (stage C4): the
// library's ApplicationResources, which every window it runs shares, so one
// theme and one image registry serve them all; its theme library for
// `@import`; the findings of the last load or install; the published
// values; the window running now; and the paths the last dialog chose.
//
// UI thread only: every slot enters the registry before it reaches this
// state, and its first use holds the registry's binding for good. Created at
// first use and never destroyed, as the registry is, so no theme or image is
// released by a static destructor at exit.

#include "gui_forms/c/gf_abi.h"

#include "gui_forms/anatomy.hpp"
#include "gui_forms/application/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {

// The most names the library publishes values for (gf_abi.h).
inline constexpr std::size_t maximum_published_values = GF_THEME_MAXIMUM_PUBLISHED_VALUES;

// An object create_with made that borrows from its owner (a dock site its
// workspace, a page strip its owner's page set), and that owner, kept alive
// until the dependent has been destroyed.
struct KeptOwner final {
    std::weak_ptr<Component> dependent;
    Component::Ptr owner;
};

// An owned window open now (windows.show_modal or show_owned), from its
// ready to its closed, and the root it was opened over. Observed only: the
// application's window table owns the window.
struct OpenOwnedWindow final {
    gf_object root = 0U;
    Window* window = nullptr;
};

// One published name and, while present, its value.
struct PublishedInput final {
    std::string name;
    double value = 0.0;
    bool present = false;
};

class EngineState final {
public:
    EngineState() = default;
    EngineState(const EngineState&) = delete;
    EngineState& operator=(const EngineState&) = delete;

    // The resources every window of the library shares, made at the first
    // call on the UI thread.
    [[nodiscard]] ApplicationResources& resources();
    [[nodiscard]] const std::shared_ptr<ApplicationResources>& shared_resources();
    [[nodiscard]] ThemeLibrary& library() noexcept { return *library_; }
    // The library shared, for a theme request held until the first window
    // (user_themes.hpp: --theme, GUI_FORMS_THEME_SHEET).
    [[nodiscard]] std::shared_ptr<const ThemeLibrary> shared_library() const noexcept { return library_; }
    // Holds the process's theme request on `resources`, compiled against
    // the library's packages, control types and published names, onto
    // `base`: for resources the library makes, and for an application's
    // once its theme is set. UI thread.
    void hold_theme_request(ApplicationResources& resources, ThemeBase base);
    // The application made through GF_TABLE_APPLICATION, or null. While it
    // lives the library's resources are its resources.
    [[nodiscard]] Application* application() const noexcept { return application_.get(); }
    // Makes `made` the library's application, its resources the library's.
    void adopt_application(std::unique_ptr<Application> made);
    // Lets the application go (it has closed) and gives the library fresh
    // resources with the built-in theme.
    void release_application();
    // The stock control types a theme's selectors may name: every control
    // type of the schema. Built at the first call.
    [[nodiscard]] std::span<const ControlAnatomy> controls();

    // The findings of the last load or install.
    [[nodiscard]] std::span<const ThemeCompileDiagnostic> diagnostics() const noexcept { return diagnostics_; }
    void replace_diagnostics(std::span<const ThemeCompileDiagnostic> findings);
    void clear_diagnostics() noexcept { diagnostics_.clear(); }

    // Every name published so far, withdrawn or not, for a compilation's
    // ThemeCompileOptions::published_values.
    [[nodiscard]] std::span<const std::string> published_names() const noexcept { return published_names_; }
    // The input named name, or null. The pointer is valid until the next
    // publish of a new name.
    [[nodiscard]] PublishedInput* find_published(std::string_view name) noexcept;
    // Records name (valid, not yet published) with value. Throws
    // std::bad_alloc, leaving nothing recorded.
    PublishedInput& add_published(std::string_view name, double value);
    [[nodiscard]] std::size_t published_count() const noexcept { return published_.size(); }
    // Publishes every present value on window (a window being made).
    void publish_all(Window& window) const;

    // The window of the run, from the host's ready to its closed; null
    // outside a run. Observed only: Application::run owns it.
    void attach_window(Window& window) noexcept { running_ = &window; }
    void detach_window() noexcept { running_ = nullptr; }
    [[nodiscard]] Window* running_window() const noexcept { return running_; }

    // The owned windows open now, in opening order. attach_owned_window
    // throws std::bad_alloc, recording nothing; detach_owned_window forgets
    // the window opened over root, if any.
    void attach_owned_window(gf_object root, Window& window);
    void detach_owned_window(gf_object root) noexcept;
    [[nodiscard]] std::span<const OpenOwnedWindow> owned_windows() const noexcept { return owned_; }
    // The window open over root: the running window for zero or the run's
    // root (run_root), an owned window for its root; null for any other
    // object or when that window is not open (N32).
    [[nodiscard]] Window* window_over(gf_object root, gf_object run_root) const noexcept;

    // The paths the last dialog chose.
    [[nodiscard]] std::vector<std::string>& dialog_paths() noexcept { return dialog_paths_; }

    // A new identity for a dialog request: the host refuses identity zero
    // and an identity already on its modal stack. UI thread.
    [[nodiscard]] std::uint64_t next_dialog_request() noexcept { return next_dialog_request_++; }

    // Keeps owner alive while dependent lives. An owner whose dependent has
    // been destroyed is let go at the next call. Throws std::bad_alloc,
    // keeping nothing new.
    void keep_owner(const Component::Ptr& dependent, Component::Ptr owner);

private:
    std::shared_ptr<ApplicationResources> resources_{};
    // Declared after the resources: destroyed first (never, in practice; see
    // above), so the application never outlives what it shares.
    std::unique_ptr<Application> application_{};
    // Shared with a theme request held by the resources until their first
    // window.
    std::shared_ptr<ThemeLibrary> library_{std::make_shared<ThemeLibrary>()};
    // Copies of the schema's anatomies, which name static storage.
    std::vector<ControlAnatomy> controls_{};
    bool controls_built_ = false;
    std::vector<ThemeCompileDiagnostic> diagnostics_{};
    // At the same positions: the inputs and their names. Bounded by
    // maximum_published_values.
    std::vector<PublishedInput> published_{};
    std::vector<std::string> published_names_{};
    Window* running_ = nullptr;
    std::vector<OpenOwnedWindow> owned_{};
    std::vector<std::string> dialog_paths_{};
    std::uint64_t next_dialog_request_ = 1;
    std::vector<KeptOwner> kept_owners_{};
};

// The library's one engine state. UI thread.
[[nodiscard]] EngineState& engine();

// The window a run shows: root in a Window that shares the library's
// resources (so its theme) and carries the published values. UI thread.
// Throws what the Window constructor throws.
[[nodiscard]] std::unique_ptr<Window> make_run_window(const Control::Ptr& root, Size client_size);

} // namespace gui_forms::abi1
