#pragma once

#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component/revocable/revocable.hpp"
#include "gui_forms/component/types/component_types.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/schema/types/schema_types.hpp"

#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

struct PropertyEntry;

namespace detail {
// The bit-flip regression tests' hook (bit-flip run 7): a friend of the
// classes whose stored state those tests corrupt, defined only in the test
// that uses it. The library never defines or calls it.
struct StateFlipAccess;
} // namespace detail

// LibraryAllocated (allocation/library_memory): every component and control, and every
// class derived from it, takes its storage from GUI.Forms' allocator
// whoever creates it, and its delete releases it there.
// ic: gui_forms::Component: generated from include/gui_forms/component/component/component.ic.toml by
// ic: tools/ic_declarations.py; edit the declaration, not these lines. States: alive (initial), disposing, disposed.
// ic: Observed by component_state(): alive ComponentState::alive, disposing ComponentState::disposing, disposed
// ic: ComponentState::disposed. Thread: owner, unless a call says otherwise.
class GUI_FORMS_API Component : public LibraryAllocated {
public:
    using Ptr = std::shared_ptr<Component>;

    Component() = default;
    virtual ~Component();
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    // ic: Admitted in any state; then disposed. Fails logic_error (verify_dispose_thread refuses the calling thread (a
    // ic: control's or window's own rule)), unchanged.
    void dispose();
    [[nodiscard]] ComponentState component_state() const noexcept { return checked_state(); }
    // The alive test is one comparison while alive; any other code is
    // checked (checked_state), so a code that is no state fails fast here.
    [[nodiscard]] bool is_alive() const noexcept {
        if (state_ == ComponentState::alive) return true;
        static_cast<void>(checked_state());
        return false;
    }
    [[nodiscard]] bool is_disposed() const noexcept {
        const bool disposed = checked_state() == ComponentState::disposed;
        return disposed;
    }

    // Observes revocation authority; the caller retains its subscription token.
    // ic: Admitted in alive. In disposing, disposed: returns null, unchanged. Transfers an observation of the revocable
    // ic: work (weak) from caller to component.
    void own_revocable(const std::weak_ptr<detail::Revocable>& revocable);

    // Transfers the token into this component. Disposal/destruction disconnects
    // it; no ownership of the event publisher or callback target is introduced.
    // A dead component disconnects immediately. Failure also disconnects the
    // transferred token. Like event dispatch, this is execution-thread confined.
    // ic: Admitted in alive. In disposing, disposed: returns null, unchanged. Transfers the subscription token from
    // ic: caller to component.
    void own_subscription(SubscriptionToken subscription);
    [[nodiscard]] std::size_t owned_subscription_count() const noexcept;

    // The interactions the dynamic type raises (stage T1): one record per
    // public event, its base types' first.
    [[nodiscard]] std::vector<InteractionDeclaration> interactions() const;

    // The type's constant property table (property-capabilities study,
    // decision 5 and section 14, G7): its capabilities' entries, base class
    // first, for controls and components alike. Empty by default, which
    // costs no storage; a type with capabilities overrides it to return its
    // static table. Control's name-based property API reads its instance
    // registry, then this table (a registration replaces a table entry of
    // the same name); find_property_entry and the entry operations in
    // property_table.hpp read the table of any owner.
    [[nodiscard]] virtual std::span<const PropertyEntry> property_table() const noexcept;

protected:
    // Appends the type's own interactions to `interactions`. A type with
    // events of its own overrides this and calls its base type's first.
    virtual void append_interactions(std::vector<InteractionDeclaration>& interactions) const;
    virtual void verify_dispose_thread();
    virtual void on_dispose() noexcept;
    void revoke_owned_work() noexcept;

private:
    friend class detail::Revocable;
    friend struct detail::StateFlipAccess;
    void release_subscription(detail::Revocable& subscription) noexcept;

    struct ObservedRevocable final {
        std::weak_ptr<detail::Revocable> work{};
        std::uint64_t order{};
    };
    [[nodiscard]] std::uint64_t acquire_work_order();
    // The stored code, checked: one that is none of the three states is a
    // defect (corrupted storage; no call writes it) and fails fast through
    // fail_lifecycle_code with the component's address and the code.
    [[nodiscard]] ComponentState checked_state() const noexcept {
        const ComponentState state = state_;
        const bool known = state == ComponentState::alive || state == ComponentState::disposing ||
                           state == ComponentState::disposed;
        if (!known) fail_lifecycle_code(state);
        return state;
    }
    [[noreturn]] void fail_lifecycle_code(ComponentState code) const noexcept;
    ComponentState state_{ComponentState::alive};
    std::uint64_t next_work_order_{};
    std::vector<ObservedRevocable> owned_revocables_{};
    // Slots carry their links. No separate token store or retained capacity.
    std::shared_ptr<detail::Revocable> owned_subscriptions_{};
};

} // namespace gui_forms
