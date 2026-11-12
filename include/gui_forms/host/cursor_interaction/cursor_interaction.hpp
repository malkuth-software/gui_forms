#pragma once
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include <memory>

namespace gui_forms {
class HostServices;
class Window;
struct CursorLeaseState;
enum class CursorError {
    none, unsupported, denied, wrong_thread, invalid_coordinate, stale_window,
    stale_metrics, busy, revoked, closing, native_failure
};
struct GUI_FORMS_API CursorStatus final {
    CursorError error{CursorError::none};
    [[nodiscard]] bool accepted() const noexcept {
        const bool result = error == CursorError::none;
        return result;
    }
    // Compatibility projection loses detail; retain CursorError for decisions.
    [[nodiscard]] HostServiceStatus host_status() const noexcept;
};
struct GUI_FORMS_API CursorCapabilities final { bool hide{}; bool warp{}; };
struct GUI_FORMS_API CursorMetrics final {
    std::uint64_t window_id{};
    std::uint64_t generation{};
    Size client_dip{};
    double scale{};
};
enum class CursorLeasePhase { empty, active, released, revoked };
struct GUI_FORMS_API CursorLeaseSnapshot final {
    CursorLeasePhase phase{CursorLeasePhase::empty};
    CursorStatus cause{};
    CursorStatus restoration{};
};
// Unique UI-executor lease, not capture or confinement. Destruction must occur
// on its creating executor. Wrong-thread explicit release changes no authority.
// A retained lease can inspect terminal cause/restoration after host teardown.
class GUI_FORMS_API CursorHiddenLease final {
public:
    CursorHiddenLease() = default;
    ~CursorHiddenLease();
    CursorHiddenLease(const CursorHiddenLease&) = delete;
    CursorHiddenLease& operator=(const CursorHiddenLease&) = delete;
    CursorHiddenLease(CursorHiddenLease&&) noexcept;
    CursorHiddenLease& operator=(CursorHiddenLease&&) noexcept;
    [[nodiscard]] CursorStatus release() noexcept;
    [[nodiscard]] CursorLeaseSnapshot snapshot() const noexcept;
private:
    friend class HostServices;
    explicit CursorHiddenLease(std::shared_ptr<CursorLeaseState> state);
    std::shared_ptr<CursorLeaseState> state_{};
};
struct GUI_FORMS_API CursorLeaseResult final {
    CursorHiddenLease lease{};
    CursorStatus status{};
};
}
