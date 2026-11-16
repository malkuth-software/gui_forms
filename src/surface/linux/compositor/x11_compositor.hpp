#pragma once
// The X11 compositor adapter (stage T5, phase C4;
// notes:planning/stages/T5-compositor/PLAN.md §1.3 and MEASUREMENTS.md C4).
// Xlib, XFixes and XRender types stay here and in src/surface/linux/.
#include "gui_forms/host/compositor/window_backdrop.hpp"

#include <X11/Xlib.h>

namespace gui_forms::host::linux_detail {

// The screen's 32-bit TrueColor visual whose spare byte XRender names as
// alpha. Found once per run; `found` false when the server offers none.
struct X11ArgbVisual final {
  Visual *visual{};
  int depth{};
  // The pixel bits holding alpha, from the visual's XRender format.
  unsigned long alpha_mask{};
  bool found{};
};

// The visual, from XMatchVisualInfo (depth 32, TrueColor) confirmed with
// XRenderFindVisualFormat. Nothing found without XRender, or when the
// format has no alpha bits.
[[nodiscard]] X11ArgbVisual find_x11_argb_visual(Display *display, int screen);

// Watches the screen's compositing manager for one run: the owner of
// _NET_WM_CM_S<screen>, followed through the XFixes selection
// notifications (set-owner, owner window destroyed, owner client closed),
// which arrive in the event queue the UI thread reads. Made after the
// display opens, destroyed before it closes. Without XFixes the owner is
// read once per report and changes are not followed.
class X11CompositorWatch final {
public:
  X11CompositorWatch(Display *display, int screen);
  ~X11CompositorWatch();
  X11CompositorWatch(const X11CompositorWatch &) = delete;
  X11CompositorWatch &operator=(const X11CompositorWatch &) = delete;

  // True when `event` is an XFixes notification about the selection.
  [[nodiscard]] bool selection_event(const XEvent &event) const noexcept;
  // Whether a compositing manager owns the selection now (a round trip).
  [[nodiscard]] bool compositor_present() const;
  [[nodiscard]] const X11ArgbVisual &argb() const noexcept { return argb_; }

private:
  // Borrowed from the runtime, which outlives this watch.
  Display *display_{};
  ::Window root_{};
  Atom selection_{};
  // XFixes' first event code; 0 when the server lacks XFixes.
  int event_base_{};
  X11ArgbVisual argb_{};
};

// The state a window reports (PLAN.md §2.1): compositing present or
// absent; `transparent` supported for a window made with the ARGB visual
// while a compositor runs, no_compositor while none does, host_unsupported
// for any other window; no material (owner decision T5-Q10). X11 has no
// transparency, reduced-transparency or high-contrast setting to report.
[[nodiscard]] CompositorState x11_compositor_state(bool compositor_present, bool argb_window,
                                                   CompositorGeneration generation) noexcept;

} // namespace gui_forms::host::linux_detail
