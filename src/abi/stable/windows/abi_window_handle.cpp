// The object windows.create makes. See abi_window_handle.hpp.

#include "abi_window_handle.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {

WindowHandle::WindowHandle(std::unique_ptr<Window> window, Control::Ptr root) noexcept
    : owned_(std::move(window)), root_(std::move(root)) {
    window_ = owned_.get();
}

std::unique_ptr<Window> WindowHandle::take_for_run() noexcept {
    std::unique_ptr<Window> taken = std::move(owned_);
    return taken;
}

void WindowHandle::end_run() noexcept {
    window_ = nullptr;
    if (is_alive()) {
        dispose();
    }
}

void WindowHandle::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    if (window_ != nullptr) {
        Window::append_interactions(interactions);
    }
}

void WindowHandle::on_dispose() noexcept {
    // During a run the window is Application::run's; the run ends it.
    if (owned_ != nullptr) {
        window_ = nullptr;
        owned_.reset();
    }
    Component::on_dispose();
}

} // namespace gui_forms::abi1
