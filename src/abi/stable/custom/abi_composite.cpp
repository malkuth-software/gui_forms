// Composite controls of the C ABI. See abi_composite.hpp.

#include "abi_composite.hpp"

#include "../events/abi_event_fields.hpp"
#include "../schema/abi_schema.hpp"

#include "../../../core/control_language/composite_definition/composite_definition.hpp"
#include "../../../core/control_language/control_model/control_model.hpp"

#include "gui_forms/controls/button_base.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms::abi1 {

CompositeDefinitionHandle::CompositeDefinitionHandle(std::shared_ptr<const CompositeDefinition> definition,
                                                     const std::uint64_t listing) noexcept
    : definition_(std::move(definition)), listing_(listing) {}

CompositeDefinitionHandle::~CompositeDefinitionHandle() {
    forget_loaded_composite(listing_);
}

void CompositeDefinitionHandle::on_dispose() noexcept {
    forget_loaded_composite(listing_);
    listing_ = 0U;
    definition_.reset();
}

namespace {

// The listed loads (UI thread). Never destroyed, like the handle registry
// that holds the handles: a handle the registry still holds at exit is
// never destroyed, and none forgets its entry after this storage is gone.
struct LoadedComposites final {
    // In load order; each entry owned here, so a reference to one stays
    // valid while the vector reallocates.
    std::vector<std::unique_ptr<LoadedComposite>> listed{};
    // The last identity given; the next is one more.
    std::uint64_t last_identity = 0U;
};

LoadedComposites& loaded_composites() noexcept {
    static LoadedComposites* const instance = new LoadedComposites();
    return *instance;
}

// Whether a loaded entry has an identity (find_if's predicate).
struct HasIdentity final {
    std::uint64_t identity = 0U;

    bool operator()(const std::unique_ptr<LoadedComposite>& entry) const noexcept {
        return (*entry).identity == identity;
    }
};

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

std::uint64_t record_loaded_composite(const std::shared_ptr<const CompositeDefinition>& definition) {
    LoadedComposites& loads = loaded_composites();
    const CompositeDefinition& held = *definition;
    std::unique_ptr<LoadedComposite> entry = std::make_unique<LoadedComposite>();
    (*entry).definition = definition;
    (*entry).version = std::to_string(held.version_major);
    (*entry).version += '.';
    (*entry).version += std::to_string(held.version_minor);
    (*entry).properties.reserve(held.properties.size());
    for (const control_language::CompositePropertyDefinition& property : held.properties) {
        if (property.presentational || property.command) continue;
        (*entry).properties.push_back(schema_property_of(property.descriptor));
    }
    (*entry).event_payloads.reserve(held.events.size());
    for (const control_language::CompositeEventDefinition& event : held.events) {
        std::string payload;
        if (event.payload) {
            // The C++ word (control_payload_type_name), mapped to the ABI's
            // words as every interaction's payload is (K2).
            const std::string_view word = control_language::control_payload_type_name(*event.payload);
            payload = std::string(interaction_payload_fields(word));
        }
        (*entry).event_payloads.push_back(std::move(payload));
    }
    loads.listed.reserve(loads.listed.size() + 1U);
    // Nothing below throws: the identity is given only once the entry is in.
    const std::uint64_t identity = loads.last_identity + 1U;
    (*entry).identity = identity;
    loads.listed.push_back(std::move(entry));
    loads.last_identity = identity;
    return identity;
}

void forget_loaded_composite(const std::uint64_t identity) noexcept {
    if (identity == 0U) return;
    LoadedComposites& loads = loaded_composites();
    const HasIdentity matches{identity};
    const std::vector<std::unique_ptr<LoadedComposite>>::iterator found =
        std::find_if(loads.listed.begin(), loads.listed.end(), matches);
    if (found == loads.listed.end()) return;
    loads.listed.erase(found);
}

std::size_t loaded_composite_count() noexcept {
    const std::size_t count = loaded_composites().listed.size();
    return count;
}

const LoadedComposite& loaded_composite_at(const std::size_t index) noexcept {
    const LoadedComposite& entry = *loaded_composites().listed[index];
    return entry;
}

LoadedCompositeLookup find_loaded_composite(const std::uint64_t identity, const LoadedComposite*& found) noexcept {
    LoadedComposites& loads = loaded_composites();
    if (identity == 0U || identity > loads.last_identity) {
        return LoadedCompositeLookup::unknown;
    }
    const HasIdentity matches{identity};
    const std::vector<std::unique_ptr<LoadedComposite>>::const_iterator listed =
        std::find_if(loads.listed.cbegin(), loads.listed.cend(), matches);
    if (listed == loads.listed.cend()) {
        return LoadedCompositeLookup::released;
    }
    found = (*listed).get();
    return LoadedCompositeLookup::listed;
}

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
