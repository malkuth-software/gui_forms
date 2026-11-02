// The entry of the family tables (GF_TABLE_CONTROLS and after): the UI
// thread, then the live control behind an identity, of the type a slot
// works on.

#pragma once

#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/control.hpp"

#include <memory>

namespace gui_forms::abi1 {

// Enters the UI thread without binding it and finds the live control behind
// object: the find's status, or GF_STATUS_DISPOSED for a disposed control.
[[nodiscard]] gf_status enter_live_control(gf_object object, SlotIdentity slot, Control::Ptr& control);

// As enter_live_control, then the control as Typed;
// GF_STATUS_WRONG_HANDLE_KIND, with refusal as the detail, for another type.
template <typename Typed>
[[nodiscard]] gf_status enter_typed_control(gf_object object, SlotIdentity slot, const char* refusal,
                                            std::shared_ptr<Typed>& typed) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    std::shared_ptr<Typed> cast = std::dynamic_pointer_cast<Typed>(control);
    if (!cast) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, refusal);
    }
    typed = std::move(cast);
    return GF_STATUS_OK;
}

} // namespace gui_forms::abi1
