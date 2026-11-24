#include "../support/drawing_support.hpp"

#include <stxlib/threads.hpp>

DrawingObject::DrawingObject() : owner_thread_(stxlib::current_thread()) {}
DrawingObject::~DrawingObject() = default;

void DrawingObject::dispose() {
    verify_access();
    if (state_ == ObjectState::disposed) return;
    state_ = ObjectState::disposing;
    on_dispose();
    state_ = ObjectState::disposed;
}

ObjectState DrawingObject::state() const {
    verify_access();
    return state_;
}

bool DrawingObject::is_disposed() const {
    verify_access();
    return state_ == ObjectState::disposed;
}

void DrawingObject::verify_access() const {
    if (stxlib::current_thread() != owner_thread_) {
        throw std::logic_error("GUI.Drawing object accessed from a non-owner thread");
    }
}

void DrawingObject::require_alive() const {
    verify_access();
    if (state_ != ObjectState::alive) {
        throw std::logic_error("GUI.Drawing object is disposed");
    }
}

void DrawingObject::handoff_to_current_thread() {
    owner_thread_ = stxlib::current_thread();
}

void DrawingObject::on_dispose() noexcept {}


} // namespace gui_drawing

