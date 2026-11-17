#pragma once

// A minimal test game for relative pointer mode (cp34;
// gui_forms/events/relative_pointer): an orbit camera around a target. A
// primary press on the view asks for the pointer confined to it
// (confinement before a hard lock), hidden, with raw deltas; each motion
// sample turns the camera, the wheel moves it in or out, and the primary
// button fires while the mode holds. The shell's capsule is a button at the
// window's top left, declared as the window's release region: the pointer
// reaches it whatever the mode, and Escape always gives the pointer back.
//
// The PlaySuite games keep the pointer visible (owner, 2026-11-15); this
// toy exists to exercise the capability, and `visible` asks for the
// visible-cursor variant instead (free mode: raw deltas, every sample, the
// pointer shown and unlocked).
//
// Holder: OrbitToy owns its controls and window and the subscriptions; the
// view's state is plain values read by the test and the native run.

#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/panel.hpp>
#include <gui_forms/event/subscription_token/subscription_token.hpp>
#include <gui_forms/events/relative_pointer/relative_pointer.hpp>
#include <gui_forms/window/window.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace gui_forms {
class Application;
} // namespace gui_forms

namespace relative_pointer_example {

// The camera and what the mode did. yaw and pitch: degrees; distance:
// units from the target, within [2, 40]. samples: motion samples applied;
// shots: primary presses while the mode held; ends: the modes that ended,
// last_end the latest's reason.
struct OrbitState final {
    double yaw{};
    double pitch{};
    double distance{10.0};
    std::uint64_t samples{};
    std::uint64_t shots{};
    std::uint64_t ends{};
    gui_forms::RelativePointerEnd last_end{gui_forms::RelativePointerEnd::released};
    gui_forms::RelativePointerGrant last_grant{};
};

// The view: asks for the mode on a primary press and turns the camera by
// the samples.
class OrbitView final : public gui_forms::Control {
public:
    explicit OrbitView(gui_forms::StableId id);

    OrbitState state{};
    // Asks for the visible-cursor variant (free mode) instead of confined.
    bool visible{};

protected:
    void on_mouse_down(gui_forms::MouseEventArgs& arguments) override;

private:
    void sample(gui_forms::RelativePointerEventArgs& arguments);
    void ended(const gui_forms::RelativePointerEnded& ended);
    std::vector<gui_forms::SubscriptionToken> tokens_{};
};

// The window: the capsule {0, 0, 72, 32} and the view under it, filling a
// 480 by 320 client area, with the capsule as the release region. With an
// application the window joins it (for a native run); without, it stands
// alone (headless).
class OrbitToy final {
public:
    explicit OrbitToy(gui_forms::Application* application = nullptr);

    std::shared_ptr<gui_forms::Panel> root;
    std::shared_ptr<gui_forms::Button> capsule;
    std::shared_ptr<OrbitView> view;
    std::unique_ptr<gui_forms::Window> window;
    std::uint64_t capsule_clicks{};

private:
    void capsule_clicked(gui_forms::ButtonBase& button);
    gui_forms::SubscriptionToken capsule_token_{};
};

} // namespace relative_pointer_example
