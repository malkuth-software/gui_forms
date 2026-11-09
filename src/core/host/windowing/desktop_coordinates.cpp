#include "gui_forms/host/windowing/desktop_coordinates.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {
namespace {
// Squared distance from `point` to the closed rectangle `frame`; zero inside.
double distance_squared(Rect frame, Point point) noexcept {
    const double nearest_x = std::clamp(point.x, frame.x, frame.x + frame.width);
    const double nearest_y = std::clamp(point.y, frame.y, frame.y + frame.height);
    const double dx = point.x - nearest_x;
    const double dy = point.y - nearest_y;
    const double result = dx * dx + dy * dy;
    return result;
}

double usable_scale(double scale) noexcept {
    const bool valid = std::isfinite(scale) && scale > 0.0;
    const double result = valid ? scale : 1.0;
    return result;
}
} // namespace

HostMonitor logical_monitor(const HostMonitor& device) noexcept {
    HostMonitor result = device;
    make_logical_monitor(result);
    return result;
}

void make_logical_monitor(HostMonitor& monitor) noexcept {
    const double scale = usable_scale(monitor.scale);
    const Rect frame = monitor.frame;
    const Rect work_area = monitor.work_area;
    monitor.frame.width = frame.width / scale;
    monitor.frame.height = frame.height / scale;
    monitor.work_area.x = frame.x + (work_area.x - frame.x) / scale;
    monitor.work_area.y = frame.y + (work_area.y - frame.y) / scale;
    monitor.work_area.width = work_area.width / scale;
    monitor.work_area.height = work_area.height / scale;
}

namespace {
// nearest_device_monitor and nearest_logical_monitor: the monitors and the
// point are in one space, which this search does not need to know.
std::size_t nearest_monitor_in_space(std::span<const HostMonitor> monitors, Point point) noexcept {
    std::size_t best = 0U;
    double best_distance = 0.0;
    for (std::size_t index = 0U; index < monitors.size(); ++index) {
        const double distance = distance_squared(monitors[index].frame, point);
        if (index == 0U || distance < best_distance) {
            best = index;
            best_distance = distance;
        }
        if (distance == 0.0 && monitors[index].frame.contains(point)) {
            best = index;
            break;
        }
    }
    return best;
}
} // namespace

std::size_t nearest_device_monitor(std::span<const HostMonitor> device_monitors,
                                   DesktopDevicePoint point) noexcept {
    const std::size_t index = nearest_monitor_in_space(device_monitors, Point{point.x, point.y});
    return index;
}

std::size_t nearest_logical_monitor(std::span<const HostMonitor> logical_monitors,
                                    DesktopLogicalPoint point) noexcept {
    const std::size_t index = nearest_monitor_in_space(logical_monitors, Point{point.x, point.y});
    return index;
}

DesktopLogicalPoint desktop_logical_point(std::span<const HostMonitor> device_monitors,
                                          DesktopDevicePoint device_point) noexcept {
    if (device_monitors.empty()) {
        // No monitors: the spaces coincide.
        const DesktopLogicalPoint unchanged{device_point.x, device_point.y};
        return unchanged;
    }
    const std::size_t index = nearest_device_monitor(device_monitors, device_point);
    const HostMonitor& monitor = device_monitors[index];
    const double scale = usable_scale(monitor.scale);
    const DesktopLogicalPoint result{monitor.frame.x + (device_point.x - monitor.frame.x) / scale,
                                     monitor.frame.y + (device_point.y - monitor.frame.y) / scale};
    return result;
}

DesktopDevicePoint desktop_device_point(std::span<const HostMonitor> device_monitors,
                                        DesktopLogicalPoint logical_point, double& scale) noexcept {
    scale = 1.0;
    if (device_monitors.empty()) {
        const DesktopDevicePoint unchanged{logical_point.x, logical_point.y};
        return unchanged;
    }
    const Point point_in_logical{logical_point.x, logical_point.y};
    std::size_t best = 0U;
    double best_distance = 0.0;
    for (std::size_t index = 0U; index < device_monitors.size(); ++index) {
        const HostMonitor logical = logical_monitor(device_monitors[index]);
        const double distance = distance_squared(logical.frame, point_in_logical);
        if (index == 0U || distance < best_distance) {
            best = index;
            best_distance = distance;
        }
        if (logical.frame.contains(point_in_logical)) {
            best = index;
            break;
        }
    }
    const HostMonitor& monitor = device_monitors[best];
    scale = usable_scale(monitor.scale);
    const DesktopDevicePoint result{monitor.frame.x + (logical_point.x - monitor.frame.x) * scale,
                                    monitor.frame.y + (logical_point.y - monitor.frame.y) * scale};
    return result;
}

} // namespace gui_forms
