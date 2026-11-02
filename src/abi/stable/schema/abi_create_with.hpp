#pragma once

// The objects objects.create_with makes (stage C5): the controls that need
// another object, the command extensions and the components bound to a
// window. See gf_abi.h, gf_create_arguments.

#include "gui_forms/c/gf_abi.h"

#include "../failure/abi_failure.hpp"

#include "gui_forms/binding.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gui_forms::abi1 {

// A caller's arguments, read and validated: the owner (null when none) and
// the enumeration a flags editor edits (absent when none).
struct CreateRequest final {
    std::size_t type = 0U;
    Component::Ptr owner;
    std::optional<PropertyEnumDescriptor> enumeration;
    std::int64_t value = 0;
};

// Makes the catalog type request.type with stable_id into made. A type that
// needs no arguments is made as objects.create makes it, when the request
// asks nothing of it. Fails, making nothing, with
// GF_STATUS_WRONG_HANDLE_KIND for an owner of the wrong type,
// GF_STATUS_INVALID_ARGUMENT for arguments the type does not read or lacks,
// and GF_STATUS_UNSUPPORTED_CAPABILITY for a type neither slot makes (binding,
// live_surface). UI thread. Throws what the constructors throw.
[[nodiscard]] gf_status create_from_request(const CreateRequest& request, StableId stable_id, SlotIdentity slot,
                                            Component::Ptr& made);

// Reads a caller's enumeration (its name and choices, as create_with and a
// custom property declare one) into enumeration, validating it: nothing for
// no name and no choices, else 1 to GF_CREATE_MAXIMUM_CHOICES distinct
// non-empty names under a non-empty name. Fails with
// GF_STATUS_INVALID_ARGUMENT, leaving enumeration unchanged.
[[nodiscard]] gf_status read_enumeration(gf_utf8_view enumeration_name, const gf_create_choice* choices,
                                         std::uint64_t choice_count, bool combinable, SlotIdentity slot,
                                         std::optional<PropertyEnumDescriptor>& enumeration);

} // namespace gui_forms::abi1
