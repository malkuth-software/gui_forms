#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component/revocable/revocable.hpp"

#include <memory>
#include <utility>

namespace gui_forms {

class Component;

class GUI_FORMS_API SubscriptionToken final {
public:
    SubscriptionToken() = default;
    ~SubscriptionToken() { disconnect(); }
    SubscriptionToken(SubscriptionToken&& other) noexcept
        : revocable_(std::move(other.revocable_)) {}
    SubscriptionToken& operator=(SubscriptionToken&& other) noexcept {
        if (this != &other) {
            disconnect();
            revocable_ = std::move(other.revocable_);
        }
        return *this;
    }
    SubscriptionToken(const SubscriptionToken&) = delete;
    SubscriptionToken& operator=(const SubscriptionToken&) = delete;

    void disconnect() noexcept {
        if (revocable_) {
            (*revocable_).disconnect();
            revocable_.reset();
        }
    }
    [[nodiscard]] bool connected() const noexcept {
        const bool result = revocable_ != nullptr && (*revocable_).connected();
        return result;
    }

private:
    friend class Component;
    template <typename... Arguments>
    friend class Event;
    explicit SubscriptionToken(std::shared_ptr<detail::Revocable> revocable)
        : revocable_(std::move(revocable)) {}

    std::shared_ptr<detail::Revocable> revocable_{};
};

} // namespace gui_forms
