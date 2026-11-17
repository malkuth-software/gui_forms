// Composite controls of the C ABI (GF_TABLE_CUSTOM_CONTROLS, since 1.5): the
// definition object load_composite gives, the child types a caller names by
// schema type id, and the subscriptions to a composite's declared events.
// See gf_abi.h.

#pragma once

#include "gui_forms/c/gf_abi.h"
#include "../registry/abi_registry.hpp"
#include "../schema/abi_schema.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/composites/composite_control/composite_control.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {

// A definition as load_composite gives it: shared by the controls made
// from it, which keep their own reference, so releasing or disposing the
// object ends only the caller's hold.
//
// The handle also keeps the definition listed in the schema table (since
// 1.5, provisional): `listing` is the identity record_loaded_composite gave
// it, and disposing or destroying the handle forgets it.
class CompositeDefinitionHandle final : public Component {
public:
    CompositeDefinitionHandle(std::shared_ptr<const CompositeDefinition> definition, std::uint64_t listing) noexcept;
    ~CompositeDefinitionHandle() override;

    // The definition; null once the object is disposed.
    [[nodiscard]] const std::shared_ptr<const CompositeDefinition>& definition() const& noexcept {
        return definition_;
    }
    const std::shared_ptr<const CompositeDefinition>& definition() const&& = delete;

private:
    void on_dispose() noexcept override;

    std::shared_ptr<const CompositeDefinition> definition_;
    std::uint64_t listing_ = 0U;
};

// One load of a composite definition as the schema table lists it
// (gf_schema_composite): the definition, shared with the handle, and the
// text the listing lends besides the definition's own, built once when the
// load is recorded. The lent views stay valid while the entry is listed.
struct LoadedComposite final {
    // From 1, in load order, never reused.
    std::uint64_t identity = 0U;
    std::shared_ptr<const CompositeDefinition> definition{};
    // "major.minor".
    std::string version{};
    // The declared properties (neither presentational nor command), in
    // the definition's order.
    std::vector<SchemaProperty> properties{};
    // Each event's payload in the interaction vocabulary, parallel to the
    // definition's events; empty for none.
    std::vector<std::string> event_payloads{};
};

// What finding a listing identity gives.
enum class LoadedCompositeLookup : std::uint8_t {
    listed,
    // Its load was released (the handle disposed or destroyed).
    released,
    // No load had it (zero, or past every identity given).
    unknown,
};

// Lists a loaded definition and gives its identity. UI thread. Throws
// std::bad_alloc, leaving nothing listed.
[[nodiscard]] std::uint64_t record_loaded_composite(const std::shared_ptr<const CompositeDefinition>& definition);
// Ends the listing of identity; nothing for one not listed (zero among
// them). UI thread (where a handle is disposed and destroyed).
void forget_loaded_composite(std::uint64_t identity) noexcept;
// The listed loads, in load order: how many, and one of them (index below
// the count). UI thread; the reference is valid until the next record or
// forget.
[[nodiscard]] std::size_t loaded_composite_count() noexcept;
[[nodiscard]] const LoadedComposite& loaded_composite_at(std::size_t index) noexcept;
// The listed load of identity in found, or why there is none (found is
// then left as it was). UI thread.
[[nodiscard]] LoadedCompositeLookup find_loaded_composite(std::uint64_t identity,
                                                          const LoadedComposite*& found) noexcept;

// What reading the caller's child types gives: each one, or the refusal
// (the index of the type and why) when one is not a control made from its
// stable id alone. The names are the schema's ids (static storage).
struct CompositeChildTypesRead final {
    std::vector<CompositeChildType> types{};
    bool refused{};
    std::size_t refused_index{};
    std::string_view refusal{};
};

// Resolves `names` (schema type ids in any spelling) to the child types a
// definition may name. UI thread: each type is checked on a prototype it
// makes and releases. Throws what a constructor throws.
void read_composite_child_types(const std::vector<std::string_view>& names, CompositeChildTypesRead& read);

// Subscribes `binding` to the declared event whose interaction name is
// `name` of `source`, when `source` is a composite control declaring one;
// an empty token otherwise. The payload, when the event has one, is the
// field "value".
[[nodiscard]] SubscriptionToken connect_composite_event(Component& source, std::string_view name,
                                                        const std::shared_ptr<SubscriptionBinding>& binding);

} // namespace gui_forms::abi1
