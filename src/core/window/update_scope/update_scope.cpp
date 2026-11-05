#include "gui_forms/window/update_scope/update_scope.hpp"

#include "gui_forms/window/window.hpp"

#include <utility>

namespace gui_forms {

UpdateScope::~UpdateScope() {
    close();
}

UpdateScope::UpdateScope(UpdateScope&& other) noexcept
    : window_(std::exchange(other.window_, nullptr)) {}

UpdateScope& UpdateScope::operator=(UpdateScope&& other) noexcept {
    if (this != &other) {
        close();
        window_ = std::exchange(other.window_, nullptr);
    }
    return *this;
}

void UpdateScope::perform_layout() {
    if (window_) {
        (*window_).perform_layout();
    }
}

void UpdateScope::abandon() noexcept {
    window_ = nullptr;
}

void UpdateScope::close() {
    if (window_) {
        SurfaceRoot* closing = std::exchange(window_, nullptr);
        (*closing).leave_update_scope();
    }
}

} // namespace gui_forms
