// GF_TABLE_EVENTS: subscriptions to interactions and property changes. See
// gf_abi.h.
//
// A subscription is a forwarder subscribed to the source's C++ event. The
// forwarder owns the subscription's binding; the event drops the forwarder
// when the subscription is disconnected or the source ends, and the
// binding's destruction then frees the identity and releases the callback.

#include "abi_tables.hpp"

#include "../events/abi_event_catalog.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity subscribe_slot{GF_TABLE_EVENTS, 0U};
constexpr SlotIdentity subscribe_property_slot{GF_TABLE_EVENTS, 1U};
constexpr SlotIdentity disconnect_slot{GF_TABLE_EVENTS, 2U};
constexpr SlotIdentity write_field_slot{GF_TABLE_EVENTS, 3U};
constexpr SlotIdentity field_element_slot{GF_TABLE_EVENTS, 4U};

// A property subscription's forwarder: the event has no payload. The
// event's copy of the forwarder owns the binding.
struct PlainForwarder {
    std::shared_ptr<SubscriptionBinding> binding;
    void operator()() const {
        (*binding).emit(nullptr);
    }
};

// Orders connectors by interaction name (the catalog's order).
struct ConnectorBefore final {
    bool operator()(const EventConnector& connector, std::string_view name) const noexcept {
        return connector.interaction < name;
    }
    bool operator()(std::string_view name, const EventConnector& connector) const noexcept {
        return name < connector.interaction;
    }
};

// Whether source's type raises the interaction name.
bool raises(const Component& source, std::string_view name) {
    const std::vector<InteractionDeclaration> declared = source.interactions();
    for (const InteractionDeclaration& declaration : declared) {
        if (declaration.name == name) {
            return true;
        }
    }
    return false;
}

// Subscribes binding to the interaction name of source through the most
// derived declaring type's connector. An empty token when no connector
// takes source (a catalog that does not match the schema).
SubscriptionToken connect_interaction(Component& source, std::string_view name,
                                      const std::shared_ptr<SubscriptionBinding>& binding) {
    using ConnectorIterator = std::array<EventConnector, event_connectors.size()>::const_iterator;
    const std::pair<ConnectorIterator, ConnectorIterator> range =
        std::equal_range(event_connectors.begin(), event_connectors.end(), name, ConnectorBefore{});
    const EventSink sink{binding};
    ConnectorIterator connector = range.first;
    for (connector = range.first; connector != range.second; ++connector) {
        SubscriptionToken token = (*connector).connect(source, sink);
        if (token.connected()) {
            return token;
        }
    }
    return SubscriptionToken{};
}

// What a subscription connects to: an interaction or a property's changes.
// The shared body of subscribe and subscribe_property.
gf_status subscribe_to(gf_object source, gf_utf8_view name, const gf_callback* callback,
                       gf_subscription* subscription, bool property, SlotIdentity slot) {
    if (subscription == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "subscribing needs a subscription output");
    }
    gf_callback copy{};
    const gf_status read = read_callback(callback, slot, copy);
    if (read != GF_STATUS_OK) {
        return read;
    }
    std::string_view name_text;
    const gf_status named = read_text(name, slot, name_text);
    if (named != GF_STATUS_OK) {
        return named;
    }
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    // A component raises interactions; only a control has properties.
    Component::Ptr component;
    Control::Ptr control;
    const gf_status found = property ? registry().find_object(source, slot, control)
                                     : registry().find_component(source, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (property) {
        component = control;
    }
    if (!(*component).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the source has been disposed");
    }
    if (property) {
        if (!(*control).property_descriptor(name_text)) {
            return fail(GF_STATUS_NOT_FOUND, slot, "the source has no property of that name");
        }
    } else if (!raises(*component, name_text)) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the source's type does not raise an interaction of that name");
    }
    const gf_subscription reserved = registry().reserve_subscription(component);
    std::shared_ptr<SubscriptionBinding> binding;
    SubscriptionToken token;
    try {
        binding = std::make_shared<SubscriptionBinding>(copy, reserved, component.get(), std::string(name_text));
        if (property) {
            token = (*control).subscribe_property_changed(name_text, *control, PlainForwarder{binding});
        } else {
            token = connect_interaction(*component, name_text, binding);
        }
    } catch (...) {
        if (binding) {
            (*binding).disarm();
        }
        binding.reset();
        registry().abandon_subscription(reserved);
        throw;
    }
    if (!token.connected()) {
        (*binding).disarm();
        binding.reset();
        registry().abandon_subscription(reserved);
        if (property) {
            return fail(GF_STATUS_NOT_FOUND, slot, "the property does not report its changes");
        }
        return fail(GF_STATUS_INTERNAL, slot, "the event catalog has no connector for the source's type");
    }
    // The event's forwarder now owns the binding.
    binding.reset();
    registry().complete_subscription(reserved, std::move(token));
    *subscription = reserved;
    return GF_STATUS_OK;
}

gf_status GF_CALL events_subscribe(gf_object source, gf_utf8_view interaction, const gf_callback* callback,
                                   gf_subscription* subscription) noexcept {
    try {
        const ShadowFrameScope shadow_frame(subscribe_slot.table_id, subscribe_slot.slot_index);
        const gf_status subscribed = subscribe_to(source, interaction, callback, subscription, false, subscribe_slot);
        return subscribed;
    } catch (...) {
        return fail_with_current_exception(subscribe_slot);
    }
}

gf_status GF_CALL events_subscribe_property(gf_object source, gf_utf8_view property, const gf_callback* callback,
                                            gf_subscription* subscription) noexcept {
    try {
        const ShadowFrameScope shadow_frame(subscribe_property_slot.table_id, subscribe_property_slot.slot_index);
        const gf_status subscribed =
            subscribe_to(source, property, callback, subscription, true, subscribe_property_slot);
        return subscribed;
    } catch (...) {
        return fail_with_current_exception(subscribe_property_slot);
    }
}

gf_status GF_CALL events_disconnect(gf_subscription subscription) noexcept {
    try {
        const ShadowFrameScope shadow_frame(disconnect_slot.table_id, disconnect_slot.slot_index);
        const gf_status entered = registry().enter_ui(disconnect_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status disconnected = registry().disconnect(subscription, disconnect_slot);
        return disconnected;
    } catch (...) {
        return fail_with_current_exception(disconnect_slot);
    }
}

gf_status GF_CALL events_write_field(gf_event* event, std::uint64_t field, const gf_value* value) noexcept {
    try {
        const ShadowFrameScope shadow_frame(write_field_slot.table_id, write_field_slot.slot_index);
        const gf_status entered = registry().enter_ui(write_field_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EventFields* frame = EventFields::frame_of(event);
        if (frame == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, write_field_slot, "no callback is running for that event");
        }
        const gf_status written = (*frame).write(field, value, write_field_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(write_field_slot);
    }
}

gf_status GF_CALL events_field_element(const gf_event* event, std::uint64_t field, std::uint64_t index,
                                       gf_value* element) noexcept {
    try {
        const ShadowFrameScope shadow_frame(field_element_slot.table_id, field_element_slot.slot_index);
        const gf_status entered = registry().enter_ui(field_element_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        EventFields* frame = EventFields::frame_of(event);
        if (frame == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, field_element_slot, "no callback is running for that event");
        }
        const gf_status read = (*frame).element(field, index, element, field_element_slot);
        return read;
    } catch (...) {
        return fail_with_current_exception(field_element_slot);
    }
}

constexpr gf_events_table table{
    .struct_size = sizeof(gf_events_table),
    .abi_minor = GF_ABI_MINOR,
    .subscribe = &events_subscribe,
    .subscribe_property = &events_subscribe_property,
    .disconnect = &events_disconnect,
    .write_field = &events_write_field,
    .field_element = &events_field_element,
};

} // namespace

const gf_events_table& events_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
