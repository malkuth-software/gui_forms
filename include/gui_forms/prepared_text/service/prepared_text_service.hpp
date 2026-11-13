#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/prepared_text/layout/prepared_text_layout.hpp"

namespace gui_forms {
namespace detail { struct PreparedServiceState; struct PreparedSessionState; }

// Adapter for posting a coalesced UI-dispatch wake. Called on the worker; it
// must only enqueue a wake, never execute consumer/UI work or reenter session
// control. The target must survive until begin_close returns. The wake carries
// no payload: UI subsequently calls inspect_ready/adopt_ready.
class GUI_FORMS_API PreparedTextWakeTarget {
public:
    virtual ~PreparedTextWakeTarget() = default;
    virtual void post_prepared_text_wake() noexcept = 0;
};

class GUI_FORMS_API PreparedTextSession final {
public:
    ~PreparedTextSession();
    PreparedTextSession(const PreparedTextSession&) = delete;
    PreparedTextSession& operator=(const PreparedTextSession&) = delete;
    // All methods belong to the opening executor. Invalid desire preserves the
    // previous authority; valid desire revokes it even when keys compare equal.
    [[nodiscard]] PreparedTextStatus desire(const PreparedTextKey& key, LayoutAuthority& authority);
    // Busy/invalid/stale preserves input. Success moves it and empties caller.
    [[nodiscard]] PreparedTextStatus submit(LayoutAuthority authority, PrepareInput& input);
    [[nodiscard]] PreparedTextSessionSnapshot inspect_ready() const;
    [[nodiscard]] PreparedTextStatus adopt_ready(LayoutAuthority expected, PreparedTextLayout& output);
    [[nodiscard]] PreparedTextStatus discard_ready();
    void cancel();
    void begin_close();
    void join_and_release();
private:
    friend struct detail::PreparedTextAccess;
    explicit PreparedTextSession(std::shared_ptr<detail::PreparedSessionState> state);
    std::shared_ptr<detail::PreparedSessionState> state_{};
};

class GUI_FORMS_API PreparedTextService final {
public:
    PreparedTextService();
    ~PreparedTextService();
    PreparedTextService(const PreparedTextService&) = delete;
    PreparedTextService& operator=(const PreparedTextService&) = delete;
    [[nodiscard]] std::uint64_t instance() const noexcept;
    // Per-view lifetime budget survives through every retained result/lease.
    [[nodiscard]] PreparedTextBudgetSnapshot budget_snapshot() const;
    // Copies admitted encoded bytes into private immutable storage. Caller
    // spans need survive only this call. Failure preserves output.
    [[nodiscard]] PreparedTextStatus create_font_bank(std::span<const PreparedFontSource> sources,
        EncodedFontLease& output);
    [[nodiscard]] PreparedTextStatus create_input(const PreparedTextKey& key, std::string_view text,
        std::span<const DocumentMapSpan> mapping, std::span<const PreparedSourceEndpoint> endpoints,
        PreparedParagraphProof proof, PrepareInput& output);
    // One open session; retired layouts/fonts remain charged across replacement.
    [[nodiscard]] PreparedTextStatus open_session(const EncodedFontLease& fonts,
        PreparedTextWakeTarget* wake, std::unique_ptr<PreparedTextSession>& output);
    // Revokes admission/authority, then joins its session. Native shaping is
    // noninterruptible, so this service shutdown has no latency guarantee.
    void begin_close();
private:
    std::shared_ptr<detail::PreparedServiceState> state_{};
};
} // namespace gui_forms
