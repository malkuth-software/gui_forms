#pragma once

// The authority of a host session over its window (B10): the binding that
// fixes the non-opaque declaration, the host service seam and the host's
// report of the system's presentation settings.
//
// Holder: HostSession.
//
// Stale use: the session holds its window as an observing pointer that its
// own contract keeps valid until its shutdown, where it releases the seam
// (release_host_services) and stops; a session never calls through a window
// it no longer holds. The functions are static and keep nothing.

#include "gui_forms/surface_root/surface_root.hpp"

namespace gui_forms::detail {

class SurfaceHostAccess final {
public:
    SurfaceHostAccess() = delete;

    // The first session binds the root; the binding is never cleared and
    // ends declare_non_opaque.
    static void mark_session_bound(SurfaceRoot& root) noexcept { root.host_session_bound_ = true; }
    // Makes `services` the root's host service seam (read through the public
    // host_services()).
    static void set_host_services(SurfaceRoot& root, HostServices* services) noexcept {
        root.host_services_ = services;
    }
    // Releases the seam when it is still `services`; another session's seam
    // stays.
    static void release_host_services(SurfaceRoot& root, const HostServices* services) noexcept {
        if (root.host_services_ == services) root.host_services_ = nullptr;
    }
    // The host's report of the system's settings, as the window's own
    // (SurfaceRoot::set_host_presentation).
    static void set_host_presentation(SurfaceRoot& root, PresentationSettings settings) {
        root.set_host_presentation(settings);
    }
};

} // namespace gui_forms::detail
