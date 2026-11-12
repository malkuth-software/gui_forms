// SurfaceRoot's named capabilities (B10, src/core/surface_root/access/):
// each is a set of static functions that keeps nothing, so no capability
// object can outlive its root; the lifetime capability's observer expires
// with the root, and the handle and host capabilities keep their stated
// invariants.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/update_scope/update_scope.hpp"
#include "gui_forms/window/window.hpp"
#include "../src/core/surface_root/access/surface_application_access.hpp"
#include "../src/core/surface_root/access/surface_engine_access.hpp"
#include "../src/core/surface_root/access/surface_forwarding_access.hpp"
#include "../src/core/surface_root/access/surface_handle_access.hpp"
#include "../src/core/surface_root/access/surface_help_access.hpp"
#include "../src/core/surface_root/access/surface_host_access.hpp"
#include "../src/core/surface_root/access/surface_lifetime_access.hpp"
#include "../src/core/surface_root/access/surface_request_access.hpp"
#include "../src/core/surface_root/access/surface_resource_access.hpp"
#include "../src/core/surface_root/access/surface_schedule_access.hpp"
#include "../src/core/surface_root/access/surface_tree_access.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// No capability can be made, copied or stored: only its static functions
// exist.
static_assert(!std::is_default_constructible_v<detail::SurfaceLifetimeAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceScheduleAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceRequestAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceForwardingAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceApplicationAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceHostAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceResourceAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceEngineAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceHandleAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceHelpAccess>);
static_assert(!std::is_default_constructible_v<detail::SurfaceTreeAccess>);

void test_lifetime_observer_expires_with_its_root() {
    const Control::Ptr root = make_control<Control>(StableId("capability.lifetime.root"));
    std::unique_ptr<Window> window = std::make_unique<Window>(root, Size{80.0, 40.0});
    const std::weak_ptr<detail::WindowLifetime> observer = detail::SurfaceLifetimeAccess::observe(*window);
    const std::shared_ptr<detail::WindowLifetime> held = observer.lock();
    require(held != nullptr && detail::SurfaceLifetimeAccess::is_lifetime_of(*window, held),
            "a live root's observer must name its lifetime record");
    require(!detail::SurfaceLifetimeAccess::is_lifetime_of(*window, nullptr),
            "a null record is no root's lifetime");
    require(detail::SurfaceLifetimeAccess::share_dispatcher(*window) != nullptr,
            "a live root must share its dispatcher state");
    Timer timer(*window, 10ms);
    window.reset();
    bool rejected = false;
    try {
        timer.start();
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected, "a component bound through the lifetime capability must reject use after its root");
}

void test_lifetime_observer_is_stale_after_root() {
    std::weak_ptr<detail::WindowLifetime> observer;
    {
        const Control::Ptr root = make_control<Control>(StableId("capability.stale.root"));
        Window window(root, Size{80.0, 40.0});
        observer = detail::SurfaceLifetimeAccess::observe(window);
        require(!observer.expired(), "the observer must be live while its root is");
    }
    require(observer.expired(), "the observer must expire with its root");
}

void test_update_scope_leaves_through_the_handle_capability() {
    const Control::Ptr root = make_control<Control>(StableId("capability.scope.root"));
    Window window(root, Size{80.0, 40.0});
    const std::uint64_t flushes_before = window.metrics_snapshot().flush_count;
    {
        UpdateScope scope = window.begin_update();
        (*root).set_requested_bounds({0.0, 0.0, 60.0, 30.0});
        require(window.metrics_snapshot().flush_count == flushes_before, "an open scope must defer its flush");
    }
    require(window.metrics_snapshot().flush_count > flushes_before,
            "closing the outermost scope must leave it, through SurfaceHandleAccess, and flush");
}

void test_forwarding_capability_reports_no_traversal_at_first() {
    const Control::Ptr root = make_control<Control>(StableId("capability.forwarding.root"));
    Window window(root, Size{80.0, 40.0});
    require(detail::SurfaceForwardingAccess::last_focus_traversal(window) ==
                detail::SurfaceForwardingAccess::FocusTraversal::none,
            "a root with no focus change reports no traversal");
}

void test_application_capability_is_empty_outside_a_run() {
    const Control::Ptr root = make_control<Control>(StableId("capability.application.root"));
    Window window(root, Size{80.0, 40.0});
    require(detail::SurfaceApplicationAccess::application_window(window).expired(),
            "a window outside a run observes no run state");
}

void test_tree_capability_follows_attachment() {
    const Control::Ptr root = make_control<Control>(StableId("capability.tree.root"));
    const Control::Ptr child = make_control<Control>(StableId("capability.tree.child"));
    Window window(root, Size{80.0, 40.0});
    require(!detail::SurfaceTreeAccess::in_lifecycle_notification(window),
            "a root outside an attach or detach notification must say so");
    (*root).add_child(child);
    require((*child).attached_surface_root().get() == &window,
            "a child added through the tree capability must be attached to the root");
    static_cast<void>((*root).remove_child((*child).runtime_id()));
    require(!(*child).attached(), "a child removed through the tree capability must be detached");
}

} // namespace

int main() {
    try {
        test_lifetime_observer_expires_with_its_root();
        test_lifetime_observer_is_stale_after_root();
        test_update_scope_leaves_through_the_handle_capability();
        test_forwarding_capability_reports_no_traversal_at_first();
        test_application_capability_is_empty_outside_a_run();
        test_tree_capability_follows_attachment();
        std::cout << "gui_forms_surface_capability_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_surface_capability_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
