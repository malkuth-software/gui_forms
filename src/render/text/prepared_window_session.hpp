#pragma once

#include "prepared_window_shape.hpp"

#include <stxlib/threads.hpp>

namespace gui_forms::render::text {

enum class PreparedWindowWakeResult { none, signalled, closed, failed };

// Source-private host seam. signal() is worker-only, bounded and nonblocking;
// it carries no payload and must not invoke UI or reenter session operations.
// Ownership lasts through join, including a signal racing begin_close. A native
// adapter must provide its own reliable pending/drain protocol; this seam alone
// does not establish native delivery. Neither target nor hook may own session.
class PreparedWindowWakeConnection {
public:
    virtual ~PreparedWindowWakeConnection() = default;
    [[nodiscard]] virtual PreparedWindowWakeResult signal() noexcept = 0;
};

// Private deterministic test seam. No mutable batch/input is exposed. Hooks
// run outside session/authority/ledger locks and may inject an exception.
class PreparedWindowSessionTestHook {
public:
    virtual ~PreparedWindowSessionTestHook() = default;
    virtual void before_thread_start() {}
    virtual void before_initialize() {}
    virtual void before_shape() {}
};

struct PreparedWindowSessionSnapshot final {
    PreparedTextSlot slot{PreparedTextSlot::empty};
    PreparedTextStatus initialization{PreparedTextStatus::pending};
    PreparedTextStatus completion{PreparedTextStatus::pending};
    PreparedWindowWakeResult notification{PreparedWindowWakeResult::none};
    std::optional<detail::PreparedWindowKey> desired{};
    std::uint64_t session_instance{};
    std::uint64_t controller_instance{};
    bool started{};
    bool closing{};
    bool exited{};
    bool joined{};
};

// Development-only: a ledger-wide claim excludes A2 and other private sessions
// until confirmed join. The application must explicitly close/join this private
// owner: the public service's old weak-session registry does not notify it.
// Construct/start/control/join/destroy on one owner executor. begin_close only
// revokes and wakes; join_and_release can block on native work and notification.
// The shared notification target survives begin_close and is released at join.
// One projection-controller lifetime per session lifetime in this private
// stage. start requires a fresh authority and assigns nonreused IDs before
// launching; callers derive keys from inspect. Failed starts burn assigned IDs
// and do not make that bound authority fresh again. A surviving controller
// across replacement sessions requires a separately negotiated factory.
class PreparedWindowSession final {
public:
    PreparedWindowSession(const std::uint64_t provider_instance, std::shared_ptr<const detail::PreparedFontBank> fonts,
        std::shared_ptr<detail::PreparedWindowBatchAuthority> authority,
        std::shared_ptr<PreparedWindowWakeConnection> connection,
        std::shared_ptr<PreparedWindowSessionTestHook> hook = {});
    ~PreparedWindowSession();
    PreparedWindowSession(const PreparedWindowSession&) = delete;
    PreparedWindowSession& operator=(const PreparedWindowSession&) = delete;
    [[nodiscard]] PreparedTextStatus start();
    [[nodiscard]] PreparedTextStatus desire(const detail::PreparedWindowKey& key);
    // Only already-admitted unshaped input is transferred; no extra reservation.
    // All refusals preserve caller ownership. No queue beyond this one slot.
    [[nodiscard]] PreparedTextStatus submit(std::unique_ptr<detail::PreparedWindowBatchStorage>& batch);
    [[nodiscard]] PreparedTextStatus inspect(PreparedWindowSessionSnapshot& output) const;
    // Empty output required. Validation failures preserve ready and output.
    [[nodiscard]] PreparedTextStatus adopt(const detail::PreparedWindowKey& expected,
        std::shared_ptr<const detail::PreparedWindowBatchStorage>& output);
    [[nodiscard]] PreparedTextStatus discard();
    [[nodiscard]] PreparedTextStatus cancel();
    [[nodiscard]] PreparedTextStatus begin_close();
    [[nodiscard]] PreparedTextStatus join_and_release();
private:
    // The worker's STXlib entry (`context` is the session) and its body.
    static void thread_main(const stxlib::StopSignal& stop, void* context) noexcept;
    void run() noexcept;
    // work's wait predicate, under the mutex: a queued batch or closing.
    [[nodiscard]] static bool job_or_closing(const void* context) noexcept;
    void work();
    void notify_owner() noexcept;
    void revoke_locked();
    [[nodiscard]] PreparedTextStatus start_failed(const PreparedTextStatus failure);
    [[nodiscard]] PreparedTextStatus admission_locked() const noexcept;
    const std::uint64_t provider_instance_{};
    std::shared_ptr<const detail::PreparedFontBank> fonts_{};
    detail::PreparedSessionClaim claim_{};
    std::shared_ptr<detail::PreparedWindowBatchAuthority> authority_{};
    std::shared_ptr<PreparedWindowWakeConnection> connection_{};
    std::shared_ptr<PreparedWindowSessionTestHook> hook_{};
    const stxlib::ThreadIdentity owner_{stxlib::current_thread()};
    mutable stxlib::Mutex mutex_{};
    stxlib::ConditionVariable condition_{};
    PreparedWindowSessionSnapshot snapshot_{};
    std::unique_ptr<detail::PreparedWindowBatchStorage> batch_{};
    stxlib::Thread worker_{};
};

} // namespace gui_forms::render::text
