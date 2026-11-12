#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text_mask/lease/text_mask_lease.hpp"
#include "gui_forms/prepared_text/service/prepared_text_service.hpp"

namespace gui_forms {
namespace detail { struct TextMaskServiceState; struct TextMaskSessionState; }

class GUI_FORMS_API TextMaskSession final {
public:
    ~TextMaskSession();
    TextMaskSession(const TextMaskSession&) = delete;
    TextMaskSession& operator=(const TextMaskSession&) = delete;
    // Opening executor only. Failure preserves outputs. A failed completion
    // consumed by take retires its slot while preserving the previous lease.
    [[nodiscard]] TextMaskResult lookup(const EncodedFontLease& fonts,
        const TextMaskRequest& request, TextMaskLease& output);
    [[nodiscard]] TextMaskResult submit(const EncodedFontLease& fonts,
        const TextMaskRequest& request, TextMaskRequestId& output);
    [[nodiscard]] TextMaskSessionSnapshot snapshot() const;
    [[nodiscard]] TextMaskResult take(const TextMaskRequestId id, TextMaskLease& output);
    [[nodiscard]] TextMaskResult discard(const TextMaskRequestId id);
    [[nodiscard]] TextMaskCancellation cancel(const TextMaskRequestId id);
    // Void/snapshot controls throw logic_error for a wrong executor.
    void clear_cache();
    void begin_close();
    void join_and_release();
private:
    friend struct detail::TextMaskAccess;
    explicit TextMaskSession(std::shared_ptr<detail::TextMaskSessionState> state);
    std::shared_ptr<detail::TextMaskSessionState> state_{};
};

class GUI_FORMS_API TextMaskService final {
public:
    TextMaskService();
    ~TextMaskService();
    TextMaskService(const TextMaskService&) = delete;
    TextMaskService& operator=(const TextMaskService&) = delete;
    [[nodiscard]] TextMaskResult create_font_bank(
        const std::span<const PreparedFontSource> sources, EncodedFontLease& output);
    // Wake only posts a payload-free notification. It must survive through
    // session join_and_release, including after nonblocking begin_close.
    // Output must be empty or already joined; replacement never implicitly
    // joins an unrelated live worker on the opening executor.
    [[nodiscard]] TextMaskResult open_session(PreparedTextWakeTarget* const wake,
        std::unique_ptr<TextMaskSession>& output);
    [[nodiscard]] TextMaskBudgetSnapshot budget_snapshot() const;
    // Nonblocking revocation. The session's explicit join or service destruction
    // establishes worker/wake quiescence; wake target must survive that join.
    void begin_close();
private:
    friend struct detail::TextMaskAccess;
    std::shared_ptr<detail::TextMaskServiceState> state_{};
};
} // namespace gui_forms
