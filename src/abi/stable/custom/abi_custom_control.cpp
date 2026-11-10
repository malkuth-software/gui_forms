// A custom control of the C ABI. See abi_custom_control.hpp and gf_abi.h.

#include "abi_custom_control.hpp"

#include "../registry/abi_registry.hpp"
#include "../schema/abi_schema.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace gui_forms::abi1 {
namespace {

// Where a failure inside a callback's conversion is recorded; the calling
// slot's own failure replaces it when the exception reaches that slot.
constexpr SlotIdentity callback_slot{GF_TABLE_CUSTOM_CONTROLS, 0U};

// The widest and tallest surface a custom control paints, in device pixels;
// a larger control paints only its base.
constexpr double maximum_surface_side = 16384.0;

constexpr std::string_view paint_frame_name = "paint";

class CustomPropertyGetter final {
public:
    CustomPropertyGetter(const Control& control, const CustomControlState& state, std::size_t index) noexcept
        : control_(&control), state_(&state), index_(index) {}

    BindingValue operator()() const {
        return (*state_).get(*control_, index_);
    }

private:
    const Control* control_;
    const CustomControlState* state_;
    std::size_t index_;
};

class CustomPropertySetter final {
public:
    CustomPropertySetter(const Control& control, const CustomControlState& state, std::size_t index) noexcept
        : control_(&control), state_(&state), index_(index) {}

    void operator()(const BindingValue& value) const {
        (*state_).set(*control_, index_, value);
    }

private:
    const Control* control_;
    const CustomControlState* state_;
    std::size_t index_;
};

// Posted by a bound surface's wake: repaints the control on the UI thread.
// The paint that samples the surface rearms the wake.
class BoundSurfaceRepaint final {
public:
    BoundSurfaceRepaint(std::weak_ptr<Control> target, std::weak_ptr<BoundSurfaceWake> wake) noexcept
        : target_(std::move(target)), wake_(std::move(wake)) {}

    void operator()() const {
        const std::shared_ptr<BoundSurfaceWake> wake = wake_.lock();
        if (!wake || !(*wake).connected.load(std::memory_order_acquire)) {
            return;
        }
        const std::shared_ptr<Control> target = target_.lock();
        if (!target) {
            return;
        }
        (*target).invalidate(invalidation::paint_only);
    }

private:
    std::weak_ptr<Control> target_;
    std::weak_ptr<BoundSurfaceWake> wake_;
};

// Posted when a paint found no free write lease (N17): the paint consumed
// the control's paint dirt and showed the last frame, so the control asks
// again on the next turn, until a reader gives a buffer back.
class RefusedLeaseRepaint final {
public:
    explicit RefusedLeaseRepaint(std::weak_ptr<Control> target) noexcept : target_(std::move(target)) {}

    void operator()() const {
        const std::shared_ptr<Control> target = target_.lock();
        if (!target || !(*target).is_alive()) {
            return;
        }
        (*target).invalidate(invalidation::paint_only);
    }

private:
    std::weak_ptr<Control> target_;
};

// A bound surface's wake, called on the producer's thread when it
// publishes: posts one repaint unless one is pending.
class BoundSurfaceWakeCallback final {
public:
    BoundSurfaceWakeCallback(std::weak_ptr<Control> target, std::weak_ptr<BoundSurfaceWake> wake) noexcept
        : target_(std::move(target)), wake_(std::move(wake)) {}

    void operator()() const noexcept {
        const std::shared_ptr<BoundSurfaceWake> wake = wake_.lock();
        if (!wake || !(*wake).connected.load(std::memory_order_acquire) ||
            (*wake).queued.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const std::shared_ptr<Control> target = target_.lock();
        if (!target) {
            (*wake).queued.store(false, std::memory_order_release);
            return;
        }
        try {
            static_cast<void>((*target).begin_invoke(BoundSurfaceRepaint(target_, wake_)));
        } catch (...) {
            (*wake).queued.store(false, std::memory_order_release);
        }
    }

private:
    std::weak_ptr<Control> target_;
    std::weak_ptr<BoundSurfaceWake> wake_;
};

} // namespace

CustomControlState::CustomControlState(const gf_custom_control_callbacks& callbacks, std::size_t property_count)
    : callbacks_(callbacks) {
    definitions_.reserve(property_count);
    changed_events_.reserve(property_count);
}

CustomControlState::~CustomControlState() {
    revoke();
}

std::vector<PropertyRegistration> CustomControlState::registrations(
    const Control& control, const std::vector<CustomPropertyDefinition>& definitions) {
    // Every name is checked against the base before any is defined, so a
    // clash leaves the base's properties as they were.
    for (const CustomPropertyDefinition& definition : definitions) {
        if (control.property_descriptor(definition.descriptor.name).has_value()) {
            throw std::invalid_argument("a custom property's name is one its base already defines");
        }
    }
    definitions_ = definitions;
    changed_events_.clear();
    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        std::unique_ptr<Event<>> changed;
        if (definitions_[index].change_notifications) {
            changed = std::make_unique<Event<>>();
        }
        changed_events_.push_back(std::move(changed));
    }
    std::vector<PropertyRegistration> made;
    made.reserve(definitions_.size());
    for (std::size_t index = 0U; index < definitions_.size(); ++index) {
        const PropertyDescriptor& descriptor = definitions_[index].descriptor;
        PropertyRegistration registration{};
        registration.descriptor = descriptor;
        registration.get = CustomPropertyGetter(control, *this, index);
        if (descriptor.writable) {
            registration.set = CustomPropertySetter(control, *this, index);
        }
        if (changed_events_[index]) {
            registration.connect_changed = detail::EventChangeConnector<>(*changed_events_[index]);
        }
        made.push_back(std::move(registration));
    }
    return made;
}

BindingValue CustomControlState::get(const Control& control, std::size_t index) const {
    if (revoked_) {
        throw std::logic_error("the custom control has been disposed");
    }
    const PropertyDescriptor& descriptor = definitions_[index].descriptor;
    gf_value record{};
    record.struct_size = value_size_1_0;
    gf_status status = GF_STATUS_INTERNAL;
    {
        const ShadowFrameScope shadow_frame(ShadowFrameKind::event_callback, control.stable_id().value(),
                                            descriptor.name, 0U);
        status = callbacks_.get_property(callbacks_.context, registry().identity_of(&control),
                                         static_cast<std::uint64_t>(index), &record);
    }
    if (status != GF_STATUS_OK) {
        throw std::logic_error("the custom control's get_property failed");
    }
    if (record.kind != value_kind_of(descriptor.kind)) {
        throw std::logic_error("the custom control's get_property gave a value of another kind");
    }
    // Restores the size the library set, in case the callback wrote over it.
    record.struct_size = value_size_1_0;
    BindingValue read;
    if (read_value(&record, callback_slot, read) != GF_STATUS_OK) {
        throw std::logic_error("the custom control's get_property gave a malformed value");
    }
    const std::optional<BindingValue> converted = convert_property_value(read, descriptor);
    if (!converted) {
        throw std::logic_error("the custom control's get_property gave a value its property does not hold");
    }
    return *converted;
}

void CustomControlState::set(const Control& control, std::size_t index, const BindingValue& value) const {
    if (revoked_) {
        throw std::logic_error("the custom control has been disposed");
    }
    const PropertyDescriptor& descriptor = definitions_[index].descriptor;
    gf_value record{};
    record.struct_size = value_size_1_0;
    std::string_view text;
    if (!record_binding_value(value, record, text)) {
        throw std::invalid_argument("the value has no gf_value form");
    }
    record.text.data = text.data();
    record.text.size = text.size();
    gf_status status = GF_STATUS_INTERNAL;
    {
        const ShadowFrameScope shadow_frame(ShadowFrameKind::event_callback, control.stable_id().value(),
                                            descriptor.name, 0U);
        status = callbacks_.set_property(callbacks_.context, registry().identity_of(&control),
                                         static_cast<std::uint64_t>(index), &record);
    }
    if (status != GF_STATUS_OK) {
        throw std::invalid_argument("the custom control's set_property refused the value");
    }
}

void CustomControlState::paint(Control& control, Painter& painter, Rect local_damage) {
    if (revoked_) {
        return;
    }
    const Rect client = control.client_rectangle();
    if (bound_surface_) {
        const std::uint64_t sampled = (*bound_surface_).snapshot().published_generation;
        painter.draw_live_surface(bound_surface_, client);
        // This paint consumed the newest frame: the wake may post again. A
        // frame published while it ran asks for exactly one more repaint.
        if (wake_state_) {
            (*wake_state_).queued.store(false, std::memory_order_release);
            if ((*bound_surface_).snapshot().published_generation != sampled) {
                const BoundSurfaceWakeCallback wake(control.weak_from_this(), wake_state_);
                wake();
            }
        }
        return;
    }
    const Window* window = control.attached_window();
    const double scale = window != nullptr ? (*window).scale() : 1.0;
    const double pixel_width = std::ceil(client.width * scale);
    const double pixel_height = std::ceil(client.height * scale);
    if (!std::isfinite(scale) || !(scale > 0.0) || !(pixel_width >= 1.0) || !(pixel_height >= 1.0) ||
        pixel_width > maximum_surface_side || pixel_height > maximum_surface_side) {
        return;
    }
    LiveSurfaceDescription description{};
    description.width = static_cast<std::uint32_t>(pixel_width);
    description.height = static_cast<std::uint32_t>(pixel_height);
    // One logical unit is scale pixels: the surface is drawn device-aligned,
    // one pixel per device pixel, over a destination at least the client.
    const Rect destination{0.0, 0.0, pixel_width / scale, pixel_height / scale};
    if (!surface_) {
        surface_ = LiveSurface::create(description);
        stale_surface_ = true;
    } else {
        const LiveSurfaceSnapshot snapshot = (*surface_).snapshot();
        if (snapshot.description.width != description.width || snapshot.description.height != description.height) {
            if (!(*surface_).reconfigure(description)) {
                painter.draw_live_surface(surface_, destination);
                return;
            }
            stale_surface_ = true;
        }
    }
    LiveSurfaceWriteLease lease = (*surface_).try_acquire_write(!stale_surface_);
    if (!lease) {
        // Every buffer is held by a reader: this paint shows the last frame,
        // and asks for another (N17). The surface is repainted whole then.
        painter.draw_live_surface(surface_, destination);
        stale_surface_ = true;
        try {
            static_cast<void>(control.begin_invoke(RefusedLeaseRepaint(control.weak_from_this())));
        } catch (...) {
        }
        return;
    }
    const Rect damage = stale_surface_ ? client : Rect::intersection(local_damage, client);
    std::span<std::byte> pixels = lease.pixels();
    gf_paint_target target{};
    target.struct_size = sizeof(gf_paint_target);
    target.width = lease.width();
    target.height = lease.height();
    target.row_bytes = lease.row_bytes();
    target.pixels = reinterpret_cast<std::uint8_t*>(pixels.data());
    target.scale = scale;
    target.damage = gf_logical_rect{damage.x, damage.y, damage.width, damage.height};
    {
        const ShadowFrameScope shadow_frame(ShadowFrameKind::event_callback, control.stable_id().value(),
                                            paint_frame_name, 0U);
        callbacks_.paint(callbacks_.context, registry().identity_of(&control), &target);
    }
    if (revoked_) {
        // The callback disposed the control.
        lease.abandon();
        return;
    }
    Rect pixel_damage{};
    if (!stale_surface_) {
        const double left = std::floor(damage.x * scale);
        const double top = std::floor(damage.y * scale);
        const double right = std::ceil((damage.x + damage.width) * scale);
        const double bottom = std::ceil((damage.y + damage.height) * scale);
        pixel_damage = Rect{left, top, right - left, bottom - top};
    }
    static_cast<void>(lease.publish(pixel_damage));
    stale_surface_ = false;
    painter.draw_live_surface(surface_, destination);
}

Event<>* CustomControlState::changed_event(std::size_t index) noexcept {
    if (index >= changed_events_.size()) {
        return nullptr;
    }
    return changed_events_[index].get();
}

void CustomControlState::bind_surface(Control& control, std::shared_ptr<LiveSurface> surface) {
    disconnect_wake();
    bound_surface_ = std::move(surface);
    connect_wake(control);
    control.invalidate(invalidation::paint_only);
}

void CustomControlState::unbind_surface(Control& control, const std::shared_ptr<LiveSurface>& surface) {
    if (!bound_surface_ || bound_surface_ != surface) {
        return;
    }
    disconnect_wake();
    bound_surface_.reset();
    // The callback's own surface no longer matches what was shown.
    stale_surface_ = true;
    control.invalidate(invalidation::paint_only);
}

void CustomControlState::connect_wake(Control& control) {
    disconnect_wake();
    Window* window = control.attached_window();
    if (!bound_surface_ || window == nullptr || revoked_) {
        return;
    }
    const Control::Ptr self = control.shared_from_this();
    if ((*window).queue_live_surface_presentation(self, bound_surface_)) {
        // The window's host samples the newest frames itself; a publication
        // posts nothing.
        direct_presentation_ = true;
        return;
    }
    wake_state_ = std::make_shared<BoundSurfaceWake>();
    wake_ = (*bound_surface_).connect_presentation_wake(
        BoundSurfaceWakeCallback(std::weak_ptr<Control>(self), std::weak_ptr<BoundSurfaceWake>(wake_state_)));
}

void CustomControlState::disconnect_wake() noexcept {
    direct_presentation_ = false;
    if (wake_state_) {
        (*wake_state_).connected.store(false, std::memory_order_release);
    }
    wake_.disconnect();
    wake_state_.reset();
}

void CustomControlState::revoke() noexcept {
    if (revoked_) {
        return;
    }
    revoked_ = true;
    disconnect_wake();
    if (armed_ && callbacks_.release != nullptr) {
        callbacks_.release(callbacks_.context);
    }
}

template <typename Base>
CustomControl<Base>::CustomControl(StableId stable_id, const gf_custom_control_callbacks& callbacks,
                                   const std::vector<CustomPropertyDefinition>& definitions, bool focusable)
    : Base(std::move(stable_id)), state_(callbacks, definitions.size()) {
    std::vector<PropertyRegistration> registrations = state_.registrations(*this, definitions);
    for (PropertyRegistration& registration : registrations) {
        (*this).define_bindable_property(std::move(registration));
    }
    if (focusable) {
        (*this).focusable = true;
    }
    state_.arm();
}

template <typename Base>
bool CustomControl<Base>::notify_changed(std::size_t index) {
    Event<>* changed = state_.changed_event(index);
    if (changed == nullptr) {
        return false;
    }
    (*this).publish_change(*changed);
    return true;
}

template <typename Base>
void CustomControl<Base>::on_paint(Painter& painter, Rect local_damage) {
    Base::on_paint(painter, local_damage);
    state_.paint(*this, painter, local_damage);
}

template <typename Base>
void CustomControl<Base>::bind_surface(std::shared_ptr<LiveSurface> surface) {
    state_.bind_surface(*this, std::move(surface));
}

template <typename Base>
void CustomControl<Base>::unbind_surface(const std::shared_ptr<LiveSurface>& surface) {
    state_.unbind_surface(*this, surface);
}

template <typename Base>
void CustomControl<Base>::on_attached_to_window() {
    Base::on_attached_to_window();
    state_.connect_wake(*this);
}

template <typename Base>
void CustomControl<Base>::on_detached_from_window() noexcept {
    state_.disconnect_wake();
    Base::on_detached_from_window();
}

template <typename Base>
void CustomControl<Base>::on_dispose() noexcept {
    Base::on_dispose();
    state_.revoke();
}

template class CustomControl<ScrollableControl>;
template class CustomControl<UserControl>;

CustomNotifyResult notify_custom_changed(Control& control, std::size_t index) {
    CustomControl<ScrollableControl>* scrollable = dynamic_cast<CustomControl<ScrollableControl>*>(&control);
    if (scrollable != nullptr) {
        return (*scrollable).notify_changed(index) ? CustomNotifyResult::notified : CustomNotifyResult::not_notifying;
    }
    CustomControl<UserControl>* user = dynamic_cast<CustomControl<UserControl>*>(&control);
    if (user != nullptr) {
        return (*user).notify_changed(index) ? CustomNotifyResult::notified : CustomNotifyResult::not_notifying;
    }
    return CustomNotifyResult::not_custom;
}

bool bind_custom_surface(Control& control, std::shared_ptr<LiveSurface> surface) {
    CustomControl<ScrollableControl>* scrollable = dynamic_cast<CustomControl<ScrollableControl>*>(&control);
    if (scrollable != nullptr) {
        (*scrollable).bind_surface(std::move(surface));
        return true;
    }
    CustomControl<UserControl>* user = dynamic_cast<CustomControl<UserControl>*>(&control);
    if (user != nullptr) {
        (*user).bind_surface(std::move(surface));
        return true;
    }
    return false;
}

bool unbind_custom_surface(Control& control, const std::shared_ptr<LiveSurface>& surface) {
    CustomControl<ScrollableControl>* scrollable = dynamic_cast<CustomControl<ScrollableControl>*>(&control);
    if (scrollable != nullptr) {
        (*scrollable).unbind_surface(surface);
        return true;
    }
    CustomControl<UserControl>* user = dynamic_cast<CustomControl<UserControl>*>(&control);
    if (user != nullptr) {
        (*user).unbind_surface(surface);
        return true;
    }
    return false;
}

} // namespace gui_forms::abi1
