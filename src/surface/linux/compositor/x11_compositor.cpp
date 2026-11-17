#include "x11_compositor.hpp"

#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>

#include <cstddef>
#include <string>

namespace gui_forms::host::linux_detail {

X11ArgbVisual find_x11_argb_visual(Display *display, int screen) {
  X11ArgbVisual found{};
  int render_event_base = 0;
  int render_error_base = 0;
  if (XRenderQueryExtension(display, &render_event_base, &render_error_base) == False)
    return found;
  XVisualInfo match{};
  if (XMatchVisualInfo(display, screen, 32, TrueColor, &match) == 0)
    return found;
  XRenderPictFormat *const format = XRenderFindVisualFormat(display, match.visual);
  if (format == nullptr || (*format).type != PictTypeDirect || (*format).direct.alphaMask == 0)
    return found;
  const unsigned long alpha_bits = static_cast<unsigned long>((*format).direct.alphaMask);
  const unsigned long alpha_shift = static_cast<unsigned long>((*format).direct.alpha);
  found.visual = match.visual;
  found.depth = match.depth;
  found.alpha_mask = alpha_bits << alpha_shift;
  found.found = true;
  return found;
}

X11CompositorWatch::X11CompositorWatch(Display *display, int screen)
    : display_(display), root_(RootWindow(display, screen)) {
  const std::string name = "_NET_WM_CM_S" + std::to_string(screen);
  selection_ = XInternAtom(display_, name.c_str(), False);
  int error_base = 0;
  if (XFixesQueryExtension(display_, &event_base_, &error_base) == False)
    event_base_ = 0;
  if (event_base_ != 0)
    XFixesSelectSelectionInput(display_, root_, selection_,
                               XFixesSetSelectionOwnerNotifyMask |
                                   XFixesSelectionWindowDestroyNotifyMask |
                                   XFixesSelectionClientCloseNotifyMask);
  argb_ = find_x11_argb_visual(display_, screen);
}

X11CompositorWatch::~X11CompositorWatch() {
  // Stops the notifications before the runtime that reads them goes.
  if (event_base_ != 0)
    XFixesSelectSelectionInput(display_, root_, selection_, 0UL);
}

bool X11CompositorWatch::selection_event(const XEvent &event) const noexcept {
  if (event_base_ == 0 || event.type != event_base_ + XFixesSelectionNotify)
    return false;
  const XFixesSelectionNotifyEvent &notify =
      reinterpret_cast<const XFixesSelectionNotifyEvent &>(event);
  const bool ours = notify.selection == selection_;
  return ours;
}

bool X11CompositorWatch::compositor_present() const {
  const ::Window owner = XGetSelectionOwner(display_, selection_);
  const bool present = owner != None;
  return present;
}

CompositorState x11_compositor_state(bool compositor_present, bool argb_window,
                                     CompositorGeneration generation) noexcept {
  CompositorState state{};
  state.generation = generation;
  state.compositing = compositor_present ? CompositorPresence::present : CompositorPresence::absent;
  WindowBackdropSupport transparent = WindowBackdropSupport::host_unsupported;
  if (argb_window)
    transparent = compositor_present ? WindowBackdropSupport::supported : WindowBackdropSupport::no_compositor;
  state.support[static_cast<std::size_t>(WindowBackdrop::transparent)] = transparent;
  state.support[static_cast<std::size_t>(WindowBackdrop::window_material)] = WindowBackdropSupport::host_unsupported;
  state.support[static_cast<std::size_t>(WindowBackdrop::transient_material)] =
      WindowBackdropSupport::host_unsupported;
  state.transparency_enabled = true;
  state.reduced_transparency = false;
  state.high_contrast = false;
  return state;
}

} // namespace gui_forms::host::linux_detail
