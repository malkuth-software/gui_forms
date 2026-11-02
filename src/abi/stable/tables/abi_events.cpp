// GF_TABLE_EVENTS: subscriptions to interactions and property changes. See
// gf_abi.h.
//
// A subscription is a forwarder subscribed to the source's C++ event. The
// forwarder owns the subscription's binding; the event drops the forwarder
// when the subscription is disconnected or the source ends, and the
// binding's destruction then frees the identity and releases the callback.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"

#include "gui_forms/controls/button_base/button_base.hpp"
#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/controls/label/label.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity subscribe_slot{GF_TABLE_EVENTS, 0U};
constexpr SlotIdentity subscribe_property_slot{GF_TABLE_EVENTS, 1U};
constexpr SlotIdentity disconnect_slot{GF_TABLE_EVENTS, 2U};

// The interactions the table connects at ABI 1.0, by schema name.
enum class Interaction : std::uint8_t {
    name_changed,
    visible_changed,
    enabled_changed,
    clicked,
    text_changed,
    checked_changed,
};

struct NamedInteraction {
    std::string_view name;
    Interaction interaction = Interaction::name_changed;
};

constexpr std::array<NamedInteraction, 6> interactions{{
    {"name_changed", Interaction::name_changed},
    {"visible_changed", Interaction::visible_changed},
    {"enabled_changed", Interaction::enabled_changed},
    {"clicked", Interaction::clicked},
    {"text_changed", Interaction::text_changed},
    {"checked_changed", Interaction::checked_changed},
}};

// Forwarders: one per C++ event signature. Each holds the binding; the
// event's copy of the forwarder is the binding's owner.
struct PlainForwarder {
    std::shared_ptr<SubscriptionBinding> binding;
    void operator()() const {
        (*binding).emit(nullptr);
    }
};

struct ButtonForwarder {
    std::shared_ptr<SubscriptionBinding> binding;
    void operator()(ButtonBase&) const {
        (*binding).emit(nullptr);
    }
};

struct BooleanForwarder {
    std::shared_ptr<SubscriptionBinding> binding;
    void operator()(bool value) const {
        gf_value payload{};
        payload.struct_size = sizeof(gf_value);
        payload.kind = GF_VALUE_KIND_BOOLEAN;
        payload.boolean = value ? 1U : 0U;
        (*binding).emit(&payload);
    }
};

struct TextForwarder {
    std::shared_ptr<SubscriptionBinding> binding;
    void operator()(const std::string& text) const {
        gf_value payload{};
        payload.struct_size = sizeof(gf_value);
        payload.kind = GF_VALUE_KIND_TEXT;
        payload.text.data = text.data();
        payload.text.size = text.size();
        (*binding).emit(&payload);
    }
};

// Subscribes the forwarder for interaction to source. An empty token means
// the source's type does not offer the interaction.
SubscriptionToken connect_interaction(Control& source, Interaction interaction,
                                      const std::shared_ptr<SubscriptionBinding>& binding) {
    ButtonBase* button = dynamic_cast<ButtonBase*>(&source);
    switch (interaction) {
    case Interaction::name_changed:
        return source.name_changed().subscribe(TextForwarder{binding});
    case Interaction::visible_changed:
        return source.visible_changed().subscribe(BooleanForwarder{binding});
    case Interaction::enabled_changed:
        return source.enabled_changed().subscribe(BooleanForwarder{binding});
    case Interaction::clicked:
        if (button != nullptr) {
            return (*button).clicked().subscribe(ButtonForwarder{binding});
        }
        return SubscriptionToken{};
    case Interaction::text_changed: {
        if (button != nullptr) {
            return (*button).text_changed().subscribe(TextForwarder{binding});
        }
        Label* label = dynamic_cast<Label*>(&source);
        if (label != nullptr) {
            return (*label).text_changed().subscribe(TextForwarder{binding});
        }
        TextBox* text_box = dynamic_cast<TextBox*>(&source);
        if (text_box != nullptr) {
            return (*text_box).text_changed().subscribe(TextForwarder{binding});
        }
        return SubscriptionToken{};
    }
    case Interaction::checked_changed: {
        CheckBox* check_box = dynamic_cast<CheckBox*>(&source);
        if (check_box != nullptr) {
            return (*check_box).checked_changed().subscribe(BooleanForwarder{binding});
        }
        return SubscriptionToken{};
    }
    }
    return SubscriptionToken{};
}

// What a subscription connects to: an interaction or a property's changes.
struct SubscriptionTarget {
    bool property = false;
    Interaction interaction = Interaction::name_changed;
};

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
    Control::Ptr control;
    const gf_status found = registry().find_object(source, slot, control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the source has been disposed");
    }
    SubscriptionTarget target{};
    target.property = property;
    if (property) {
        if (!(*control).property_descriptor(name_text)) {
            return fail(GF_STATUS_NOT_FOUND, slot, "the source has no property of that name");
        }
    } else {
        const NamedInteraction* known = nullptr;
        std::size_t index = 0U;
        for (index = 0U; index < interactions.size(); ++index) {
            if (interactions[index].name == name_text) {
                known = &interactions[index];
            }
        }
        if (known == nullptr) {
            return fail(GF_STATUS_NOT_FOUND, slot, "this table does not connect an interaction of that name");
        }
        target.interaction = (*known).interaction;
    }
    const gf_subscription reserved = registry().reserve_subscription(control);
    std::shared_ptr<SubscriptionBinding> binding;
    SubscriptionToken token;
    try {
        binding = std::make_shared<SubscriptionBinding>(copy, reserved, control.get(), std::string(name_text));
        if (property) {
            token = (*control).subscribe_property_changed(name_text, *control, PlainForwarder{binding});
        } else {
            token = connect_interaction(*control, target.interaction, binding);
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
        return fail(GF_STATUS_NOT_FOUND, slot, property ? "the property does not report its changes"
                                                        : "the source's type does not offer that interaction");
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
        const gf_status subscribed = subscribe_to(source, interaction, callback, subscription, false, subscribe_slot);
        return subscribed;
    } catch (...) {
        return fail_with_current_exception(subscribe_slot);
    }
}

gf_status GF_CALL events_subscribe_property(gf_object source, gf_utf8_view property, const gf_callback* callback,
                                            gf_subscription* subscription) noexcept {
    try {
        const gf_status subscribed =
            subscribe_to(source, property, callback, subscription, true, subscribe_property_slot);
        return subscribed;
    } catch (...) {
        return fail_with_current_exception(subscribe_property_slot);
    }
}

gf_status GF_CALL events_disconnect(gf_subscription subscription) noexcept {
    try {
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

constexpr gf_events_table table{
    .struct_size = sizeof(gf_events_table),
    .abi_minor = GF_ABI_MINOR,
    .subscribe = &events_subscribe,
    .subscribe_property = &events_subscribe_property,
    .disconnect = &events_disconnect,
};

} // namespace

const gf_events_table& events_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
