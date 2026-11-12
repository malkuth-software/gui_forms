#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/types/host_types.hpp"

#include <cstddef>
#include <span>

namespace gui_forms {

// Desktop logical coordinates (stage DK decision 7).
//
// Window frames, the pointer's desktop position and the work areas the
// windowing seam publishes are in one desktop space: origin at the top left
// of the primary monitor, y growing downwards, in logical units. Some hosts
// address their desktop in device pixels (Windows with per-monitor DPI
// awareness, X11); there is no single logical desktop when monitors have
// different scales, so the mapping is per monitor:
//
//   - each monitor keeps its device origin;
//   - its extent is its device extent divided by its scale.
//
// With scales of at least 1, every logical monitor lies inside its device
// rectangle, so monitors that do not overlap on the device desktop do not
// overlap in logical units. A monitor scaled above 1 leaves a gap towards a
// neighbour on its right or below; a point in a gap, or outside every
// monitor, maps through the nearest monitor. With one scale everywhere and
// the primary monitor at the origin the mapping is a plain division. A host
// whose desktop is already logical (macOS points) uses its monitors as they
// are.
//
// Monitor records from HostServices::query_monitors stay in the host's
// device desktop units, as they always were; HostWindowing::query_monitors
// returns the same monitors in logical units.
//
// The two spaces are different types (house style section 3, owner decision
// V3-1), so a device point cannot be passed where a logical one is meant.
// Neither converts implicitly from or to Point; the functions below are the
// conversions, and `Point{p.x, p.y}` spells a deliberate change of view
// (a point tested against a Rect in the same space).

// A point on the host's device desktop: Win32 screen pixels of a
// per-monitor-aware process, X11 root-window pixels.
struct GUI_FORMS_API DesktopDevicePoint final {
    double x{};
    double y{};

    friend bool operator==(DesktopDevicePoint left, DesktopDevicePoint right) noexcept {
        const bool equal = left.x == right.x && left.y == right.y;
        return equal;
    }
    friend bool operator!=(DesktopDevicePoint left, DesktopDevicePoint right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// A point in desktop logical units: where window frames, the pointer's
// desktop position and logical work areas are published.
struct GUI_FORMS_API DesktopLogicalPoint final {
    double x{};
    double y{};

    friend bool operator==(DesktopLogicalPoint left, DesktopLogicalPoint right) noexcept {
        const bool equal = left.x == right.x && left.y == right.y;
        return equal;
    }
    friend bool operator!=(DesktopLogicalPoint left, DesktopLogicalPoint right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// The monitor `device` in logical units: same id, origin and scale; frame
// and work area extents divided by the scale. The work area keeps its
// offset from the monitor origin divided by the scale, so it stays inside
// the logical frame.
[[nodiscard]] GUI_FORMS_API HostMonitor logical_monitor(const HostMonitor& device) noexcept;
// As logical_monitor, converting `monitor` from device to logical units in
// place (its id untouched, so nothing is allocated).
GUI_FORMS_API void make_logical_monitor(HostMonitor& monitor) noexcept;

// Index of the monitor whose frame contains `point`, else of the monitor
// whose frame is nearest to it (squared distance to the rectangle; the
// first such monitor on a tie). `monitors` must not be empty. The device
// form reads device monitor records (HostServices::query_monitors), the
// logical form logical ones (HostWindowing::query_monitors).
[[nodiscard]] GUI_FORMS_API std::size_t nearest_device_monitor(std::span<const HostMonitor> device_monitors,
                                                 DesktopDevicePoint point) noexcept;
[[nodiscard]] GUI_FORMS_API std::size_t nearest_logical_monitor(std::span<const HostMonitor> logical_monitors,
                                                  DesktopLogicalPoint point) noexcept;

// Converts a device desktop point to desktop logical units through the
// monitor that contains it (or the nearest one). Returns the point
// unchanged when `device_monitors` is empty.
[[nodiscard]] GUI_FORMS_API DesktopLogicalPoint desktop_logical_point(std::span<const HostMonitor> device_monitors,
                                                        DesktopDevicePoint device_point) noexcept;

// The inverse: finds the monitor whose logical frame contains the point (or
// the nearest logical frame) and maps it back to device units. Returns the
// point unchanged when `device_monitors` is empty. `scale` receives that
// monitor's scale (1 when there is none), which a caller uses to convert an
// extent placed at that point.
[[nodiscard]] GUI_FORMS_API DesktopDevicePoint desktop_device_point(std::span<const HostMonitor> device_monitors,
                                                      DesktopLogicalPoint logical_point, double& scale) noexcept;

} // namespace gui_forms
