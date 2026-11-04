#include "gui_forms/surface_root/surface_root.hpp"

#include "../dispatcher/state/dispatcher_state.hpp"

#include <stdexcept>
#include <string>
#include <thread>

namespace gui_forms {

SurfaceRoot::SurfaceRoot()
    : root_thread_(std::this_thread::get_id()),
      dispatcher_state_(std::make_shared<detail::DispatcherState>(root_thread_)) {}

SurfaceRoot::~SurfaceRoot() = default;

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
