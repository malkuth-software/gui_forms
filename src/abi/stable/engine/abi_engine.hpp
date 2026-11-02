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
#include "gui_forms/control.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {

// The most names the library publishes values for (gf_abi.h).
inline constexpr std::size_t maximum_published_values = GF_THEME_MAXIMUM_PUBLISHED_VALUES;

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
    [[nodiscard]] ThemeLibrary& library() noexcept { return library_; }
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

    // The paths the last dialog chose.
    [[nodiscard]] std::vector<std::string>& dialog_paths() noexcept { return dialog_paths_; }

private:
    std::shared_ptr<ApplicationResources> resources_{};
    ThemeLibrary library_{};
    // Copies of the schema's anatomies, which name static storage.
    std::vector<ControlAnatomy> controls_{};
    bool controls_built_ = false;
    std::vector<ThemeCompileDiagnostic> diagnostics_{};
    // At the same positions: the inputs and their names. Bounded by
    // maximum_published_values.
    std::vector<PublishedInput> published_{};
    std::vector<std::string> published_names_{};
    Window* running_ = nullptr;
    std::vector<std::string> dialog_paths_{};
};

// The library's one engine state. UI thread.
[[nodiscard]] EngineState& engine();

// The window a run shows: root in a Window that shares the library's
// resources (so its theme) and carries the published values. UI thread.
// Throws what the Window constructor throws.
[[nodiscard]] std::unique_ptr<Window> make_run_window(const Control::Ptr& root, Size client_size);

} // namespace gui_forms::abi1
