#include "gui_forms/component/component/component.hpp"

#include "gui_forms/binding/property_table/property_table.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
std::vector<InteractionDeclaration> Component::interactions() const {
    std::vector<InteractionDeclaration> declared;
    append_interactions(declared);
    return declared;
}

// Component itself raises no events.
void Component::append_interactions(std::vector<InteractionDeclaration>&) const {}

std::span<const PropertyEntry> Component::property_table() const noexcept {
    return {};
}

Component::~Component() {
    // Natural C++ destruction ends subscriptions just as explicit disposal
    // does. Do not call virtual disposal hooks after derived members are gone.
    state_ = ComponentState::disposing;
    revoke_owned_work();
    state_ = ComponentState::disposed;
}

void Component::dispose() {
    if (state_ != ComponentState::alive) {
        return;
    }
    verify_dispose_thread();
    state_ = ComponentState::disposing;
    revoke_owned_work();
    on_dispose();
    state_ = ComponentState::disposed;
}

void Component::own_revocable(
    const std::weak_ptr<detail::Revocable>& revocable) {
    if (!is_alive()) {
        if (std::shared_ptr<gui_forms::detail::Revocable> work = revocable.lock()) {
            (*work).disconnect();
        }
        return;
    }
    struct Expired final {
        [[nodiscard]] bool operator()(const ObservedRevocable& entry) const noexcept {
            const bool expired = entry.work.expired();
            return expired;
        }
    };
    // Expired entries are purged only when the vector is full, before it
    // would grow: each purge then pays for the adds since the last one, so an
    // add costs amortised constant time instead of a pass over every entry.
    if (owned_revocables_.size() == owned_revocables_.capacity()) {
        const std::vector<ObservedRevocable>::iterator retained_end = std::remove_if(
            owned_revocables_.begin(), owned_revocables_.end(), Expired{});
        owned_revocables_.erase(retained_end, owned_revocables_.end());
    }
    const std::uint64_t order = acquire_work_order();
    owned_revocables_.push_back(ObservedRevocable{revocable, order});
}

void Component::verify_dispose_thread() {}

void Component::own_subscription(SubscriptionToken subscription) {
    if (!is_alive() || !subscription.connected()) {
        return;
    }
    const std::uint64_t order = acquire_work_order();
    const std::shared_ptr<detail::Revocable> work =
        std::move(subscription.revocable_);
    detail::Revocable& entry = *work;
    entry.subscription_owner_ = this;
    entry.subscription_order_ = order;
    entry.next_subscription_ = std::move(owned_subscriptions_);
    if (entry.next_subscription_) {
        (*entry.next_subscription_).previous_subscription_ = &entry;
    }
    owned_subscriptions_ = work;
}

std::size_t Component::owned_subscription_count() const noexcept {
    std::size_t count = 0U;
    const detail::Revocable* entry = owned_subscriptions_.get();
    while (entry != nullptr) {
        ++count;
        entry = (*entry).next_subscription_.get();
    }
    return count;
}

void Component::release_subscription(detail::Revocable& entry) noexcept {
    std::shared_ptr<detail::Revocable> retained{};
    if (entry.previous_subscription_ != nullptr) {
        retained = std::move((*entry.previous_subscription_).next_subscription_);
        (*entry.previous_subscription_).next_subscription_ =
            std::move(entry.next_subscription_);
    } else {
        retained = std::move(owned_subscriptions_);
        owned_subscriptions_ = std::move(entry.next_subscription_);
    }
    const std::shared_ptr<detail::Revocable>& next =
        entry.previous_subscription_ != nullptr
            ? (*entry.previous_subscription_).next_subscription_
            : owned_subscriptions_;
    if (next) {
        (*next).previous_subscription_ = entry.previous_subscription_;
    }
    entry.previous_subscription_ = nullptr;
    entry.subscription_owner_ = nullptr;
}

void detail::Revocable::release_subscription_owner() noexcept {
    if (subscription_owner_ != nullptr) {
        (*subscription_owner_).release_subscription(*this);
    }
}

void Component::on_dispose() noexcept {}

std::uint64_t Component::acquire_work_order() {
    if (next_work_order_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Component revocation order exhausted");
    }
    ++next_work_order_;
    return next_work_order_;
}

void Component::revoke_owned_work() noexcept {
    std::vector<ObservedRevocable> observed = std::exchange(owned_revocables_, {});
    // Merge the two acquisition stacks, retaining strict reverse order across
    // caller-held revocation authority and transferred subscription tokens.
    while (!observed.empty() || owned_subscriptions_) {
        if (owned_subscriptions_ && (observed.empty() ||
            (*owned_subscriptions_).subscription_order_ > observed.back().order)) {
            const std::shared_ptr<detail::Revocable> work = owned_subscriptions_;
            release_subscription(*work);
            (*work).disconnect();
        } else {
            const std::shared_ptr<detail::Revocable> work = observed.back().work.lock();
            observed.pop_back();
            if (work) (*work).disconnect();
        }
    }
}

} // namespace gui_forms
