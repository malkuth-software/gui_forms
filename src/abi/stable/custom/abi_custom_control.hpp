// A custom control of the C ABI (GF_TABLE_CUSTOM_CONTROLS): a stock base
// extended as a C++ subclass extends it. The caller's properties are
// defined through define_bindable_property, and on_paint presents the
// pixels the caller's paint callback fills. See gf_abi.h.

#pragma once

#include "gui_forms/c/gf_abi.h"

#include "gui_forms/binding.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/user_control/user_control.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/live_surface/wake_connection/live_surface_wake_connection.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::abi1 {

// One property as create read it from the caller's spec: its descriptor,
// complete but for what define_bindable_property derives (resettable,
// change notifications).
struct CustomPropertyDefinition final {
    PropertyDescriptor descriptor;
    bool change_notifications{};
};

// The wake of a bound surface (CustomControlState::bind_surface), shared
// with the producer's wake callback: connected until the control lets the
// surface go, queued while a repaint it asked for is pending, so a fast
// producer posts at most one.
struct BoundSurfaceWake final {
    std::atomic<bool> connected{true};
    std::atomic<bool> queued{};
};

// The state a custom control keeps whatever its base: the caller's
// callbacks, one changed event per notifying property, the surface its
// paint fills and the surface a Windows paint endpoint fills instead. The control owns it as a member; the getters, setters and
// change connectors it registers point back at it and live in the same
// control, so they never outlive it.
class CustomControlState final {
public:
    CustomControlState(const gf_custom_control_callbacks& callbacks, const gf_custom_layout_callbacks& layout,
                       std::size_t property_count);
    ~CustomControlState();
    CustomControlState(const CustomControlState&) = delete;
    CustomControlState& operator=(const CustomControlState&) = delete;

    // The registration of every property, the caller's getters and setters
    // behind them, for control to define (define_bindable_property is the
    // subclass's). Throws std::invalid_argument for a name the base already
    // defines; create then discards the control.
    [[nodiscard]] std::vector<PropertyRegistration> registrations(
        const Control& control, const std::vector<CustomPropertyDefinition>& definitions);
    // The value the caller's get_property gives, converted to the
    // property's descriptor. Throws std::logic_error when the callback
    // fails, gives another kind or the control has been disposed.
    [[nodiscard]] BindingValue get(const Control& control, std::size_t index) const;
    // Hands value to the caller's set_property. Throws
    // std::invalid_argument when the callback refuses it, std::logic_error
    // after disposal.
    void set(const Control& control, std::size_t index, const BindingValue& value) const;
    // Fills the surface through the paint callback and draws it over the
    // client area; with a bound surface, draws that instead. Nothing after
    // disposal.
    void paint(Control& control, Painter& painter, Rect local_damage);
    // The caller's measure, hit_test and semantics answers (K11): empty, or
    // descriptor unchanged, when the callback is absent, fails or answers
    // out of range, so the base's answer stands. None after disposal.
    [[nodiscard]] std::optional<Size> measure(const Control& control, Size available) const;
    [[nodiscard]] std::optional<bool> hit_test(const Control& control, Point local_point) const;
    void describe(const Control& control, SemanticDescriptor& descriptor) const;
    // The changed event of property index, null when it does not notify.
    [[nodiscard]] Event<>* changed_event(std::size_t index) noexcept;
    // Shows surface, which another producer fills (a Windows paint
    // endpoint), in place of the paint callback, repainting when it
    // publishes. UI thread.
    void bind_surface(Control& control, std::shared_ptr<LiveSurface> surface);
    // Lets surface go when it is the bound one; the paint callback paints
    // again. UI thread.
    void unbind_surface(Control& control, const std::shared_ptr<LiveSurface>& surface);
    // Starts and stops the bound surface's wake with the control's window:
    // the window presents it directly when its host can, else a publication
    // posts one repaint to the UI thread.
    void connect_wake(Control& control);
    void disconnect_wake() noexcept;
    // Arms release: from here a revoke, or the state's destruction, calls
    // it. A control whose construction fails never arms it, so a refused
    // create calls no callback.
    void arm() noexcept { armed_ = true; }
    // Calls release once when armed; no callback runs afterwards.
    void revoke() noexcept;

private:
    gf_custom_control_callbacks callbacks_{};
    // The layout callbacks (since 1.2), called with callbacks_.context;
    // each null keeps the base's answer.
    gf_custom_layout_callbacks layout_{};
    std::vector<CustomPropertyDefinition> definitions_;
    // One per property, null for a property without change notifications;
    // the vector never grows after registrations, and each event lives
    // at its own address for the connectors that point at it.
    std::vector<std::unique_ptr<Event<>>> changed_events_;
    std::shared_ptr<LiveSurface> surface_;
    // The surface's contents do not match the control (first paint, a new
    // size or scale): the next paint repaints it whole.
    bool stale_surface_{true};
    // The surface a Windows paint endpoint fills, null when none; its wake
    // while the control is in a window, and whether the window presents it
    // directly (then no wake is connected).
    std::shared_ptr<LiveSurface> bound_surface_;
    LiveSurfaceWakeConnection wake_;
    std::shared_ptr<BoundSurfaceWake> wake_state_;
    bool direct_presentation_{};
    bool armed_{};
    bool revoked_{};
};

// The custom control over Base (ScrollableControl or UserControl). Its
// anatomy, and so its schema type, is Base's.
template <typename Base>
class CustomControl final : public Base {
public:
    CustomControl(StableId stable_id, const gf_custom_control_callbacks& callbacks,
                  const gf_custom_layout_callbacks& layout, const std::vector<CustomPropertyDefinition>& definitions,
                  bool focusable);

    // Raises property index's changed event (publish_change, so an
    // initialization scope defers it). False when the property does not
    // notify or index is past the properties.
    [[nodiscard]] bool notify_changed(std::size_t index);

    // See CustomControlState::bind_surface and unbind_surface.
    void bind_surface(std::shared_ptr<LiveSurface> surface);
    void unbind_surface(const std::shared_ptr<LiveSurface>& surface);

    // The caller's answers where it gives them, else Base's (K11).
    [[nodiscard]] Size measure(Size available) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

protected:
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void on_dispose() noexcept override;

private:
    CustomControlState state_;
};

extern template class CustomControl<ScrollableControl>;
extern template class CustomControl<UserControl>;

enum class CustomNotifyResult : std::uint32_t {
    notified = 0U,
    // control is not a custom control.
    not_custom = 1U,
    // The property does not notify, or index is past the properties.
    not_notifying = 2U,
};

// Raises property index's changed event when control is a custom control.
[[nodiscard]] CustomNotifyResult notify_custom_changed(Control& control, std::size_t index);
// Binds surface to control, or lets it go; false when control is not a
// custom control. UI thread.
[[nodiscard]] bool bind_custom_surface(Control& control, std::shared_ptr<LiveSurface> surface);
[[nodiscard]] bool unbind_custom_surface(Control& control, const std::shared_ptr<LiveSurface>& surface);

} // namespace gui_forms::abi1
