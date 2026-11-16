// ForeignWindowScope (x11_foreign_window.hpp): its own file, so the host
// library and the accessibility fixtures, which compile
// linux_accessibility.cpp without the rest of the services, share one
// definition.
#include "x11_foreign_window.hpp"
#include <X11/Xproto.h>

namespace gui_forms::host::linux_detail {
thread_local ForeignWindowScope *ForeignWindowScope::active_ = nullptr;
ForeignWindowScope::ForeignWindowScope(Display *display, ::Window window)
    : ForeignWindowScope(display, window, ForeignWindowErrors::bad_window) {}
ForeignWindowScope::ForeignWindowScope(Display *display, ::Window window, ForeignWindowErrors errors)
    : display_(display), window_(window), errors_(errors), previous_(nullptr), parent_(active_) {
  XSync(display_, False);
  active_ = this;
  previous_ = XSetErrorHandler(handle);
}
ForeignWindowScope::ForeignWindowScope(Display *display, ::Window window, std::span<const int> opcodes)
    : display_(display), window_(window), errors_(ForeignWindowErrors::extension_requests), previous_(nullptr),
      parent_(active_) {
  const std::size_t count = opcodes.size() < maximum_opcodes ? opcodes.size() : maximum_opcodes;
  for (std::size_t index = 0U; index < count; ++index) opcodes_[index] = opcodes[index];
  XSync(display_, False);
  active_ = this;
  previous_ = XSetErrorHandler(handle);
}
bool ForeignWindowScope::synchronize() noexcept {
  XSync(display_, False);
  return absorbed_;
}
ForeignWindowScope::~ForeignWindowScope() {
  XSync(display_, False);
  XSetErrorHandler(previous_);
  active_ = parent_;
}
int ForeignWindowScope::handle(Display *display, XErrorEvent *error) noexcept {
  ForeignWindowScope *scope = active_;
  if (scope != nullptr && (*scope).display_ == display) {
    const bool bad_window = (*error).error_code == BadWindow;
    const bool names_window = (*error).resourceid == (*scope).window_;
    bool absorbed = false;
    switch ((*scope).errors_) {
    case ForeignWindowErrors::bad_window:
      absorbed = bad_window && names_window;
      break;
    case ForeignWindowErrors::any_bad_window:
      absorbed = bad_window;
      break;
    case ForeignWindowErrors::input_focus:
      absorbed = (bad_window && names_window) ||
                 ((*error).error_code == BadMatch && (*error).request_code == X_SetInputFocus);
      break;
    case ForeignWindowErrors::selection_reply:
      absorbed = (bad_window && names_window) || (*error).error_code == BadAtom ||
                 ((*error).error_code == BadMatch && (*error).request_code == X_ChangeProperty);
      break;
    case ForeignWindowErrors::extension_requests:
      for (const int opcode : (*scope).opcodes_) {
        if (opcode >= 0 && (*error).request_code == static_cast<unsigned char>(opcode)) absorbed = true;
      }
      break;
    default:
      absorbed = false;
      break;
    }
    if (absorbed) {
      (*scope).absorbed_ = true;
      return 0;
    }
  }
  while (scope != nullptr && (*scope).previous_ == handle)
    scope = (*scope).parent_;
  return scope != nullptr && (*scope).previous_ != nullptr
             ? (*scope).previous_(display, error)
             : 0;
}
} // namespace gui_forms::host::linux_detail
