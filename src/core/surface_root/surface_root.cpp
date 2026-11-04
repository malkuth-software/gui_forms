#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window.hpp"

#include "../dispatcher/state/dispatcher_state.hpp"
// The constructor destroys its built members if a later one throws, so every
// member held through a forward-declared type must be complete here.
#include "../window/tool_tips/tool_tip_presenter.hpp"

#include <stdexcept>
#include <string>
#include <thread>

namespace gui_forms {

SurfaceRoot::SurfaceRoot(Control::Ptr root, Size client_size, std::shared_ptr<ApplicationResources> resources)
    : root_thread_(std::this_thread::get_id()),
      dispatcher_state_(std::make_shared<detail::DispatcherState>(root_thread_)),
      root_(std::move(root)), client_size_(client_size),
      theme_(resources ? (*resources).theme_ptr() : default_theme()),
      resources_(std::move(resources)),
      lifetime_(std::make_shared<detail::WindowLifetime>()) {}

bool SurfaceRoot::check_access() const noexcept {
    return std::this_thread::get_id() == root_thread_;
}

void SurfaceRoot::verify_access(std::string_view operation) {
    require_root_thread(operation);
}

void SurfaceRoot::require_root_thread(std::string_view operation) {
    if (std::this_thread::get_id() == root_thread_) {
        return;
    }
    metrics_.record_wrong_thread_rejection();
    throw std::logic_error("GUI.Forms rejected wrong-thread " + std::string(operation));
}

} // namespace gui_forms
