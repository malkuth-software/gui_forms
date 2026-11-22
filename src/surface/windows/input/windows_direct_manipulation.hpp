#pragma once
// Precision-touchpad gestures for a classic Win32 window (windows_host.cpp):
// DirectManipulation, the only way such a window receives a touchpad pinch
// as a pinch. Without it Windows synthesizes Ctrl+wheel from the pinch
// (and WM_GESTURE's GID_ZOOM comes from touch screens only). Windows 10
// (1607 or later, with a precision touchpad) sends the window
// DM_POINTERHITTEST for each touchpad contact; the window hands the
// contact to its viewport (SetContact), and DirectManipulation reports the
// fingers' combined transform and the viewport's status to the event
// handler, which forwards them to the window (Sink). The window turns them
// into events with windows_manipulation_rules.hpp and pumps the update
// manager on a timer while a gesture runs. Where DirectManipulation is not
// available (CoCreateInstance fails, an older Windows) attach returns false
// and the window keeps Windows' own wheel synthesis.
//
// Lifetime: one per window, attached after the window is created and
// detached at WM_DESTROY (or by the destructor). The event handler is a COM
// object DirectManipulation holds; detach removes it and clears its
// pointer back here, so a report after detach reaches nothing.

#include "../../../host/windows/input/windows_manipulation_rules.hpp"
#include "gui_forms/types.hpp"

#include <memory>

#include <windows.h>

namespace gui_forms::host::windows_detail {

class DirectManipulationHandler;
struct DirectManipulationObjects;

class DirectManipulationInput final {
public:
  // The window's side: the viewport's status and the content's transform
  // (scale, offset in device pixels), on the UI thread, from inside the
  // update manager's Update (pump) or DirectManipulation's own message
  // handling.
  class Sink {
  public:
    virtual void manipulation_status(ManipulationStatus status) = 0;
    virtual void manipulation_transform(double scale, Point device_offset) = 0;

  protected:
    ~Sink() = default;
  };

  DirectManipulationInput() noexcept;
  ~DirectManipulationInput();
  DirectManipulationInput(const DirectManipulationInput &) = delete;
  DirectManipulationInput &operator=(const DirectManipulationInput &) = delete;

  // Creates the manager and a viewport over `window`'s client area for
  // pan and pinch. Returns false (and holds nothing) where DirectManipulation
  // is unavailable. The thread must have COM initialized (the host's
  // apartment-threaded run).
  [[nodiscard]] bool attach(HWND window, Sink &sink) noexcept;
  // Releases the viewport and the manager; the handler forgets this object.
  void detach() noexcept;
  [[nodiscard]] bool attached() const noexcept;
  // DM_POINTERHITTEST: a new contact; a touchpad's goes to the viewport.
  void pointer_hit_test(WPARAM wparam) noexcept;
  // Lets DirectManipulation advance a running gesture or its inertia (the
  // window's manipulation timer while one runs).
  void pump() noexcept;
  // Returns the viewport's transform to identity after a gesture.
  void reset_content() noexcept;
  // The viewport's size follows the client area (WM_SIZE), device pixels.
  void resize(int width, int height) noexcept;

  // From the handler (DirectManipulation's callbacks).
  void report_status(ManipulationStatus status);
  void report_transform(double scale, Point device_offset);

private:
  HWND window_{};
  Sink *sink_{};
  std::unique_ptr<DirectManipulationObjects> objects_;
};

} // namespace gui_forms::host::windows_detail
