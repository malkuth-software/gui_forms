// Composite controls of the C ABI. See abi_composite.hpp.

#include "abi_composite.hpp"

#include "../events/abi_event_fields.hpp"
#include "../schema/abi_schema.hpp"

#include "../../../core/control_language/composite_definition/composite_definition.hpp"

#include "gui_forms/controls/button_base.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms::abi1 {

CompositeDefinitionHandle::CompositeDefinitionHandle(std::shared_ptr<const CompositeDefinition> definition) noexcept
    : definition_(std::move(definition)) {}

void CompositeDefinitionHandle::on_dispose() noexcept {
    definition_.reset();
}

namespace {

// The stable ID of each prototype a child type is checked on.
constexpr std::string_view prototype_stable_id = "gf-abi.composite.prototype";

// A button child's command binding (a document's `command="bind(...)"`):
// every stock type a command reaches is a ButtonBase.
void bind_button_command(Component& owner, Command& command) {
    ButtonBase* const button = dynamic_cast<ButtonBase*>(&owner);
    if (button != nullptr) (*button).bind(command);
}

// Forwards one declared event of a composite to a subscription's callback,
// its payload as the field "value". The event's copy of the forwarder owns
// the binding and the reused fields.
struct CompositeEventForwarder final {
    std::shared_ptr<SubscriptionBinding> binding;
    // Reused by every emission of this subscription (N12).
    std::shared_ptr<EventFields> reusable;
    // The event as written, which CompositeEvent::name holds.
    std::string event_name;

    void operator()(const CompositeEvent& raised) const {
        if (raised.name != event_name) return;
        // A nested emission builds in its own fields; nested is declared
        // first so the claim ends before it is destroyed.
        std::optional<EventFields> nested;
        EventFields* claimed = reusable.get();
        if (!(*claimed).claim()) {
            nested.emplace();
            claimed = &nested.value();
            static_cast<void>((*claimed).claim());
        }
        const EventFieldsClaim claim(*claimed);
        EventFields& fields = *claimed;
        const bool carries = !std::holds_alternative<std::monostate>(raised.payload);
        gf_event event{};
        try {
            fields.set_writable(false);
            if (carries) append_field(fields, "value", raised.payload);
            fields.publish(event, carries);
        } catch (...) {
            // Out of memory building the fields: the callback is not called
            // for this emission.
            return;
        }
        (*binding).emit(event);
        fields.end_publication();
    }
};

} // namespace

void read_composite_child_types(const std::vector<std::string_view>& names, CompositeChildTypesRead& read) {
    read.types.clear();
    read.refused = false;
    read.types.reserve(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const std::size_t found = find_catalog_type(names[index]);
        if (found == catalog_type_count()) {
            read.refused = true;
            read.refused_index = index;
            read.refusal = "a child type is not a schema type";
            return;
        }
        const CatalogCreator create = catalog_type_creator(found);
        const Component::Ptr prototype =
            create == nullptr ? nullptr : (*create)(StableId(std::string(prototype_stable_id)));
        const Control::Ptr control = std::dynamic_pointer_cast<Control>(prototype);
        if (!control) {
            read.refused = true;
            read.refused_index = index;
            read.refusal = "a child type is not a control made from its stable id alone";
            return;
        }
        CompositeChildType type{};
        type.name = catalog_type_id(found);
        type.create = create;
        if (dynamic_cast<const ButtonBase*>(control.get()) != nullptr) type.bind_command = &bind_button_command;
        read.types.push_back(type);
    }
}

SubscriptionToken connect_composite_event(Component& source, const std::string_view name,
                                          const std::shared_ptr<SubscriptionBinding>& binding) {
    CompositeControl* const composite = dynamic_cast<CompositeControl*>(&source);
    if (composite == nullptr) return SubscriptionToken{};
    const CompositeDefinition& definition = (*composite).definition();
    for (std::size_t index = 0U; index < definition.interaction_names.size(); ++index) {
        if (definition.interaction_names[index] != name || index >= definition.events.size()) continue;
        CompositeEventForwarder forwarder{binding, std::make_shared<EventFields>(), definition.events[index].name};
        SubscriptionToken token = (*composite).raised().subscribe(std::move(forwarder));
        return token;
    }
    return SubscriptionToken{};
}

} // namespace gui_forms::abi1
