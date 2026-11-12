#pragma once

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

class GUI_FORMS_API Component {
public:
    using Ptr = std::shared_ptr<Component>;

    Component() = default;
    virtual ~Component();
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    void dispose();
    [[nodiscard]] ComponentState component_state() const noexcept { return state_; }
    [[nodiscard]] bool is_alive() const noexcept {
        const bool alive = state_ == ComponentState::alive;
        return alive;
    }
    [[nodiscard]] bool is_disposed() const noexcept {
        const bool disposed = state_ == ComponentState::disposed;
        return disposed;
    }

    // Observes revocation authority; the caller retains its subscription token.
    void own_revocable(const std::weak_ptr<detail::Revocable>& revocable);

    // Transfers the token into this component. Disposal/destruction disconnects
    // it; no ownership of the event publisher or callback target is introduced.
    // A dead component disconnects immediately. Failure also disconnects the
    // transferred token. Like event dispatch, this is execution-thread confined.
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
    void release_subscription(detail::Revocable& subscription) noexcept;

    struct ObservedRevocable final {
        std::weak_ptr<detail::Revocable> work{};
        std::uint64_t order{};
    };
    [[nodiscard]] std::uint64_t acquire_work_order();
    ComponentState state_{ComponentState::alive};
    std::uint64_t next_work_order_{};
    std::vector<ObservedRevocable> owned_revocables_{};
    // Slots carry their links. No separate token store or retained capacity.
    std::shared_ptr<detail::Revocable> owned_subscriptions_{};
};

} // namespace gui_forms
