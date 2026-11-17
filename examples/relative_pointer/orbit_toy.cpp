// The orbit toy (orbit_toy.hpp): the view's request, its samples and ends,
// and the window with the shell's capsule as its release region.

#include "orbit_toy.hpp"

#include <gui_forms/application/application.hpp>
#include <gui_forms/event/delegate/delegate.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace relative_pointer_example {

namespace {
// Degrees of turn per logical unit of motion, and units of distance per
// wheel pixel.
constexpr double degrees_per_unit = 0.25;
constexpr double distance_per_pixel = 0.05;
constexpr double minimum_distance = 2.0;
constexpr double maximum_distance = 40.0;
constexpr double pitch_limit = 89.0;
} // namespace

OrbitView::OrbitView(gui_forms::StableId id) : gui_forms::Control(std::move(id)) {
    focusable = true;
    tokens_.push_back(relative_pointer().subscribe(
        gui_forms::Delegate<gui_forms::RelativePointerEventArgs&>::bind<OrbitView, &OrbitView::sample>(*this)));
    tokens_.push_back(relative_pointer_ended().subscribe(
        gui_forms::Delegate<const gui_forms::RelativePointerEnded&>::bind<OrbitView, &OrbitView::ended>(*this)));
}

void OrbitView::on_mouse_down(gui_forms::MouseEventArgs& arguments) {
    gui_forms::Control::on_mouse_down(arguments);
    if (arguments.button != gui_forms::PointerButton::primary || has_relative_pointer()) return;
    gui_forms::RelativePointerRequest request{};
    request.mode = visible ? gui_forms::RelativePointerMode::free : gui_forms::RelativePointerMode::confined;
    request.hide_cursor = !visible;
    request.raw_motion = true;
    state.last_grant = request_relative_pointer(request);
}

void OrbitView::sample(gui_forms::RelativePointerEventArgs& arguments) {
    switch (arguments.kind) {
    case gui_forms::RelativePointerSampleKind::motion:
        state.yaw = std::fmod(state.yaw + arguments.delta.x * degrees_per_unit, 360.0);
        state.pitch = std::clamp(state.pitch - arguments.delta.y * degrees_per_unit, -pitch_limit, pitch_limit);
        ++state.samples;
        break;
    case gui_forms::RelativePointerSampleKind::wheel: {
        const double step = arguments.wheel_unit == gui_forms::WheelDeltaUnit::pixels
                                ? arguments.wheel_delta.y * distance_per_pixel
                                : arguments.wheel_delta.y;
        state.distance = std::clamp(state.distance - step, minimum_distance, maximum_distance);
        break;
    }
    case gui_forms::RelativePointerSampleKind::button_down:
        if (arguments.button == gui_forms::PointerButton::primary) ++state.shots;
        break;
    case gui_forms::RelativePointerSampleKind::button_up:
    default:
        break;
    }
    arguments.handled = true;
}

void OrbitView::ended(const gui_forms::RelativePointerEnded& ended) {
    ++state.ends;
    state.last_end = ended.reason;
}

OrbitToy::OrbitToy(gui_forms::Application* application) {
    root = gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("orbit.root"));
    capsule = gui_forms::make_control<gui_forms::Button>(gui_forms::StableId("orbit.capsule"), "Menu");
    (*capsule).set_requested_bounds({0.0, 0.0, 72.0, 32.0});
    view = gui_forms::make_control<OrbitView>(gui_forms::StableId("orbit.view"));
    (*view).set_requested_bounds({0.0, 32.0, 480.0, 288.0});
    (*root).add_child(capsule);
    (*root).add_child(view);
    const gui_forms::Size client{480.0, 320.0};
    window = application != nullptr ? std::make_unique<gui_forms::Window>(*application, root, client)
                                    : std::make_unique<gui_forms::Window>(root, client);
    (*window).perform_layout();
    // The shell's capsule stays reachable in every mode.
    const std::array<gui_forms::Rect, 1> regions{{{0.0, 0.0, 72.0, 32.0}}};
    static_cast<void>((*window).set_pointer_release_regions(regions));
    capsule_token_ = (*capsule).clicked().subscribe(
        gui_forms::Delegate<gui_forms::ButtonBase&>::bind<OrbitToy, &OrbitToy::capsule_clicked>(*this));
}

void OrbitToy::capsule_clicked(gui_forms::ButtonBase&) {
    ++capsule_clicks;
}

} // namespace relative_pointer_example
