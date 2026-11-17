// GF_TABLE_DISPATCH: callbacks posted to the UI thread. See gf_abi.h.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"


#include <cstdint>
#include <memory>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity begin_invoke_slot{GF_TABLE_DISPATCH, 0U};
constexpr SlotIdentity run_pending_slot{GF_TABLE_DISPATCH, 1U};

gf_status dispatch_begin_invoke_body(gf_object target, const gf_callback* callback) {
    gf_callback copy{};
    const gf_status read = read_callback(callback, begin_invoke_slot, copy);
    if (read != GF_STATUS_OK) {
        return read;
    }
    std::unique_ptr<CallbackBinding> binding = std::make_unique<CallbackBinding>(copy);
    gf_status posted = GF_STATUS_INVALID_STATE;
    try {
        posted = registry().post(target, binding, begin_invoke_slot);
    } catch (...) {
        // Not queued (post moves the binding only once it is): the
        // caller keeps the callback, and no release follows (N34).
        if (binding) (*binding).disarm();
        throw;
    }
    if (posted != GF_STATUS_OK && binding) {
        // Not taken: the caller keeps the callback.
        (*binding).disarm();
    }
    return posted;
}

gf_status GF_CALL dispatch_begin_invoke(gf_object target, const gf_callback* callback) noexcept {
    return call_slot(begin_invoke_slot, dispatch_begin_invoke_body, target, callback);
}

gf_status dispatch_run_pending_body(std::uint64_t* invoked) {
    if (invoked == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, run_pending_slot, "run_pending needs an output");
    }
    const gf_status entered = registry().enter_ui(run_pending_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *invoked = registry().run_posted(maximum_posted_callbacks);
    return GF_STATUS_OK;
}

gf_status GF_CALL dispatch_run_pending(std::uint64_t* invoked) noexcept {
    return call_slot(run_pending_slot, dispatch_run_pending_body, invoked);
}

constexpr gf_dispatch_table table{
    .struct_size = sizeof(gf_dispatch_table),
    .abi_minor = GF_ABI_MINOR,
    .begin_invoke = &dispatch_begin_invoke,
    .run_pending = &dispatch_run_pending,
};

} // namespace

const gf_dispatch_table& dispatch_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
