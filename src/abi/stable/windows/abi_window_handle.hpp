#pragma once

// The object windows.create makes (stage C5): a window before and during its
// run, as a program holds a Window between constructing it and handing it
// to Application::run. Components made over it (objects.create_with) bind
// to the window as the C++ constructors that take Window& do: they observe
// it and do not keep it, and it does not keep them.

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {

class WindowHandle final : public Component {
public:
    // Takes window, whose root is root.
    WindowHandle(std::unique_ptr<Window> window, Control::Ptr root) noexcept;

    // The window from create until its run ends; null after, and after the
    // handle is disposed.
    [[nodiscard]] Window* window() const noexcept { return window_; }
    // Whether the handle still owns its window (it has not been run).
    [[nodiscard]] bool owns_window() const noexcept { return owned_ != nullptr; }
    // The root's stable id, which is the window's.
    [[nodiscard]] std::string_view stable_id() const noexcept { return (*root_).stable_id().value(); }

    // Hands the window to a run; window() still observes it until end_run.
    [[nodiscard]] std::unique_ptr<Window> take_for_run() noexcept;
    // The run has ended and destroyed the window: the handle is disposed.
    void end_run() noexcept;

    // The window's interactions (Window::append_interactions), which
    // events.subscribe takes by name on this handle; none once the window
    // is gone. Subscriptions end when the window is destroyed, with their
    // callbacks released once each.
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    // Destroys a window the handle still owns.
    void on_dispose() noexcept override;

    std::unique_ptr<Window> owned_;
    // Observes owned_'s window, or the running one Application::run owns.
    Window* window_ = nullptr;
    Control::Ptr root_;
};

} // namespace gui_forms::abi1
