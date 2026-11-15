// Composite controls of the C ABI (GF_TABLE_CUSTOM_CONTROLS, since 1.5): the
// definition object load_composite gives, the child types a caller names by
// schema type id, and the subscriptions to a composite's declared events.
// See gf_abi.h.

#pragma once

#include "gui_forms/c/gf_abi.h"
#include "../registry/abi_registry.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/composites/composite_control/composite_control.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {

// A definition as load_composite gives it: shared by the controls made
// from it, which keep their own reference, so releasing or disposing the
// object ends only the caller's hold.
class CompositeDefinitionHandle final : public Component {
public:
    explicit CompositeDefinitionHandle(std::shared_ptr<const CompositeDefinition> definition) noexcept;

    // The definition; null once the object is disposed.
    [[nodiscard]] const std::shared_ptr<const CompositeDefinition>& definition() const& noexcept {
        return definition_;
    }
    const std::shared_ptr<const CompositeDefinition>& definition() const&& = delete;

private:
    void on_dispose() noexcept override;

    std::shared_ptr<const CompositeDefinition> definition_;
};

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
