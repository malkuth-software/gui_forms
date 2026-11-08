#pragma once
#include "gui_forms/window.hpp"
#include <X11/Xlib.h>
#include <memory>
namespace gui_forms::host::linux_detail {
// ATK/AT-SPI is a native accessibility service only; it owns no widgets,
// rendering, event routing, or document state.
//
// Execution context: every ATK entry point is called by the AT-SPI bridge
// from the default GLib main context, which the host iterates on the UI
// thread (accessibility_dispatch and accessibility_wait). The semantic tree
// has one owner, that thread; nothing here is called from another thread.
//
// Identity: each published object is keyed by the runtime identity of the
// control that publishes it and, for a virtual child, the child's stable
// identity. Runtime identities are never reused, so an object a client
// still holds after its control is disposed or replaced is marked defunct,
// released, and refused by Window::perform_semantic_action (interview GF017).
class LinuxAccessibility final {
  public:
    LinuxAccessibility(Display* display, ::Window native, Window& model, const std::string& title);
    ~LinuxAccessibility();
    LinuxAccessibility(const LinuxAccessibility&) = delete;
    LinuxAccessibility& operator=(const LinuxAccessibility&) = delete;
    // Publishes the model's semantic tree when its generation changed, once
    // a client has asked for it (a frame's children, or an action): before
    // that no snapshot is built (APP_WINDOW_RULES_DESIGN K.1).
    void update();
    void set_title(const std::string& title);
    // Marks every published object defunct and releases it; `model` is no
    // longer read. Called by the destructor; repeated calls do nothing.
    void detach();
    struct Impl;
  private:
    std::unique_ptr<Impl> impl_;
};
void accessibility_dispatch();
void accessibility_wait(int display_fd, int wake_fd, int milliseconds);
}
