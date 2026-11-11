#pragma once

// The control schema as the C ABI serves it (stage C3): every registry type
// with its parts, states, item systems, properties and interactions, the
// vocabulary, and the engine's primitives. Built once, on the UI thread, by
// describing a fresh instance of every type; never changed or freed after,
// so the text it lends stays valid until the process ends.

#include "gui_forms/c/gf_abi.h"

#include "gui_forms/control.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/schema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {

struct SchemaChoice final {
    std::string name;
    std::int64_t value = 0;
};

struct SchemaProperty final {
    std::string name;
    gf_value_kind kind = GF_VALUE_KIND_NULL;
    std::string category;
    // Empty when the property is not an enumeration.
    std::string enumeration;
    std::vector<SchemaChoice> choices;
    gf_schema_property_flags flags = 0U;
    gf_schema_serialization serialization = GF_SCHEMA_SERIALIZATION_HIDDEN;
};

struct SchemaInteraction final {
    std::string name;
    std::string payload;
    bool mutable_payload = false;
};

struct SchemaType final {
    // Static storage: the generated catalog's literals.
    std::string_view id;
    std::string_view type_name;
    gf_schema_kind kind = GF_SCHEMA_KIND_SERVICE;
    // A control's anatomy (static storage, see ControlAnatomy); null for a
    // component or service.
    const ControlAnatomy* anatomy = nullptr;
    std::vector<SchemaProperty> properties;
    // The properties the type provides to its children (child_property_at).
    std::vector<SchemaProperty> child_properties;
    std::vector<SchemaInteraction> interactions;
};

// The number of gf_schema_vocabulary lists.
inline constexpr std::size_t vocabulary_list_count = 14U;

struct Schema final {
    std::vector<SchemaType> types;
    std::array<std::vector<std::string>, vocabulary_list_count> vocabularies;
    // The builtin registry's primitives, which live in static storage.
    std::span<const EnginePrimitive* const> primitives;
};

// The schema, built at the first call. UI thread: the caller holds the UI
// thread for the call (a schema read binds an unbound library for itself).
// Throws what building throws, leaving nothing built.
[[nodiscard]] const Schema& schema();

// The gf_value_kind of a property kind.
[[nodiscard]] gf_value_kind value_kind_of(BindingValueKind kind) noexcept;

// The registry's types, in the schema's order, without building the schema:
// how many there are, the index of the one named in any spelling of its id
// (the count when none is), its id (static storage), and a new object of
// the type at index for objects.create (null when the type is made neither
// from a stable id nor from nothing). find_catalog_type throws
// std::invalid_argument for an empty name.
[[nodiscard]] std::size_t catalog_type_count() noexcept;
[[nodiscard]] std::size_t find_catalog_type(std::string_view name);
[[nodiscard]] std::string_view catalog_type_id(std::size_t index);
[[nodiscard]] Component::Ptr create_catalog_type(std::size_t index, StableId stable_id);
// The window type (windows.create's object, since 1.1): listed after the
// registry's types, with Window's chrome properties and its interactions.
// objects.create does not make it.
inline constexpr std::string_view window_type_id = "window";
// Whether name, in any spelling, names the window type. Throws what
// find_catalog_type throws.
[[nodiscard]] bool names_window_type(std::string_view name);
// The schema index of the type named in any spelling of its id: a registry
// type's, the window type's (catalog_type_count()), or a value past every
// type when none is.
[[nodiscard]] std::size_t find_schema_type(std::string_view name);
// The schema type id of object: a control's nearest schema type (its
// anatomy's), a component's exact type; empty for a component of a type
// the schema does not list.
[[nodiscard]] std::string_view catalog_type_id_of(const Component& object);

// An interaction's payload in the ABI's own words (K2): the event fields its
// callback receives, from the table abi_event_catalog.py generates for every
// declaration (static storage). A payload the table lacks (a declaration the
// generator did not read) is empty rather than its C++ spelling.
[[nodiscard]] std::string_view interaction_payload_fields(std::string_view payload) noexcept;

} // namespace gui_forms::abi1
