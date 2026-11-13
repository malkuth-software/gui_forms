#pragma once

#include "gui_forms/export/application_export.hpp"
#include <memory>
#include <cstdint>

namespace gui_forms {
class Component;

namespace detail {

class GUI_FORMS_API Revocable {
public:
    virtual ~Revocable() = default;
    virtual void disconnect() noexcept = 0;
    [[nodiscard]] virtual bool connected() const noexcept = 0;

protected:
    // Event disconnection releases owner-held storage immediately. The caller
    // retains this revocable while unlinking, including during dispatch.
    void release_subscription_owner() noexcept;

private:
    friend class gui_forms::Component;
    std::uint64_t subscription_order_{};
    Component* subscription_owner_{};
    Revocable* previous_subscription_{};
    std::shared_ptr<Revocable> next_subscription_{};
};

} // namespace detail
} // namespace gui_forms
