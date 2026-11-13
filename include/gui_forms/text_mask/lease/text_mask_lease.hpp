#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text_mask/types/text_mask_types.hpp"
#include <memory>
#include <span>

namespace gui_forms {
namespace detail { struct TextMaskStorage; struct TextMaskAccess; }

// Copying retains immutable storage; all views require a surviving owner.
class GUI_FORMS_API TextMaskLease final {
public:
    [[nodiscard]] bool has_value() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> coverage() const noexcept;
    [[nodiscard]] std::span<const TextMaskLine> lines() const noexcept;
    [[nodiscard]] std::string_view source_utf8() const noexcept;
    [[nodiscard]] TextMaskMetrics metrics() const noexcept;
    void reset() noexcept;
private:
    friend struct detail::TextMaskAccess;
    std::shared_ptr<const detail::TextMaskStorage> storage_{};
};
} // namespace gui_forms
