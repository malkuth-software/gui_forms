#pragma once
// Interactions by schema name, shared by the events table (subscribe) and
// the rules table (an interaction trigger). Defined in abi_events.cpp,
// which holds the generated event catalog.

#include "../registry/abi_registry.hpp"

#include "gui_forms/component.hpp"

#include <memory>
#include <string_view>

namespace gui_forms::abi1 {

// Whether source's type raises the interaction name (its schema lists it).
[[nodiscard]] bool raises_interaction(const Component& source, std::string_view name);

// Subscribes binding to the interaction name of source through the most
// derived declaring type's connector: each emission calls the binding with
// the event's payload and fields. An empty token when no connector takes
// source (a catalog that does not match the schema).
[[nodiscard]] SubscriptionToken connect_interaction(Component& source, std::string_view name,
                                                    const std::shared_ptr<SubscriptionBinding>& binding);

} // namespace gui_forms::abi1
