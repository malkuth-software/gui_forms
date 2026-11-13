#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"

#include <memory>
#include <string_view>

namespace gui_forms {
namespace detail {
struct PreparedFontBank;
struct PreparedInputStorage;
struct PreparedTextStorage;
struct PreparedMaskStorage;
struct PreparedTextAccess;
}

class GUI_FORMS_API EncodedFontLease final {
public:
    EncodedFontLease() = default;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
private:
    friend struct detail::PreparedTextAccess;
    std::shared_ptr<const detail::PreparedFontBank> storage_{};
};

class GUI_FORMS_API PrepareInput final {
public:
    PrepareInput();
    ~PrepareInput();
    PrepareInput(PrepareInput&& other) noexcept;
    PrepareInput& operator=(PrepareInput&& other) noexcept;
    PrepareInput(const PrepareInput&) = delete;
    PrepareInput& operator=(const PrepareInput&) = delete;
    [[nodiscard]] bool empty() const noexcept;
private:
    friend struct detail::PreparedTextAccess;
    std::unique_ptr<detail::PreparedInputStorage> storage_{};
};

class GUI_FORMS_API PreparedTextLayout final {
public:
    PreparedTextLayout();
    ~PreparedTextLayout();
    PreparedTextLayout(PreparedTextLayout&& other) noexcept;
    PreparedTextLayout& operator=(PreparedTextLayout&& other) noexcept;
    PreparedTextLayout(const PreparedTextLayout&) = delete;
    PreparedTextLayout& operator=(const PreparedTextLayout&) = delete;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] PreparedTextMetrics metrics() const noexcept;
    [[nodiscard]] LayoutAuthority authority() const noexcept;
    // Requested payload capacity and first-party shaping peak; excludes native
    // allocator memory and encoded fonts. Reservation remains a full 8 MiB.
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] std::size_t workspace_peak_bytes() const noexcept;
    // Borrows expire when this wrapper releases/replaces its owner. Immutable
    // recorded commands will retain typed storage independently of the wrapper.
    [[nodiscard]] const PreparedTextKey* key() const noexcept;
    [[nodiscard]] std::string_view display_utf8() const noexcept;
private:
    friend struct detail::PreparedTextAccess;
    std::shared_ptr<const detail::PreparedTextStorage> storage_{};
};

class GUI_FORMS_API GrayTextMask final {
public:
    GrayTextMask();
    ~GrayTextMask();
    GrayTextMask(GrayTextMask&& other) noexcept;
    GrayTextMask& operator=(GrayTextMask&& other) noexcept;
    GrayTextMask(const GrayTextMask&) = delete;
    GrayTextMask& operator=(const GrayTextMask&) = delete;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    // Device-pixel top-left bearing relative to the prepared baseline origin.
    [[nodiscard]] std::int32_t left() const noexcept;
    [[nodiscard]] std::int32_t top() const noexcept;
    [[nodiscard]] PreparedTextMetrics metrics() const noexcept;
    // Top-left gray8, tightly packed rows; borrow ends at release/replacement
    // (the sanitizer tree reports a later use).
    [[nodiscard]] BorrowedSpan<std::uint8_t> pixels() const noexcept;
private:
    friend struct detail::PreparedTextAccess;
    std::unique_ptr<detail::PreparedMaskStorage> storage_{};
};
} // namespace gui_forms
