#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"
#include "support/routed_input_probe.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace gui_forms;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
HostCapabilities capabilities() {
    HostCapabilities result{};
    result.platform = "cursor-fixture";
    result.available = HostCapability::cursor | HostCapability::pointer_capture | HostCapability::dialogs;
    return result;
}
class Root final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    Root() : gui_forms::tests::RoutedInputProbe<Control>(StableId("cursor-root")) {}
    unsigned motions{};
    void routed_pointer(PointerEvent&) override { ++motions; }
};
class Services final : public HostServices {
public:
    Services() : HostServices(capabilities()) {}
    ~Services() override { shutdown(); }
    bool supported{true}, allowed{true}, fail_restore{}, fail_warp{};
    unsigned hides{}, restores{}, warps{}, shutdowns{}, restores_at_shutdown{};
    bool modal_saw_revoked{};
    int x{}, y{};
    CursorKind desired{CursorKind::arrow}, restored{CursorKind::arrow};
protected:
    CursorCapabilities cursor_capabilities_impl() const noexcept override { return {supported, supported}; }
    CursorStatus cursor_authority_impl(const Window&, bool) const noexcept override {
        const CursorStatus result{allowed ? CursorError::none : CursorError::denied};
        return result;
    }
    CursorStatus hide_cursor_impl() noexcept override { ++hides; return {}; }
    CursorStatus restore_cursor_impl() noexcept override {
        ++restores;
        restored = desired;
        const CursorStatus result{fail_restore ? CursorError::native_failure : CursorError::none};
        return result;
    }
    CursorStatus warp_cursor_impl(int next_x, int next_y) noexcept override {
        ++warps; x = next_x; y = next_y;
        const CursorStatus result{fail_warp ? CursorError::native_failure : CursorError::none};
        return result;
    }
    HostMonitorResult query_monitors_impl() override { return {}; }
    HostServiceStatus set_cursor_impl(CursorKind kind) override { desired = kind; return {}; }
    HostServiceStatus set_pointer_capture_impl(bool, std::uint64_t) override { return {}; }
    HostClipboardTextResult read_clipboard_text_impl() override { return {}; }
    HostServiceStatus write_clipboard_text_impl(std::string_view) override { return {}; }
    HostDialogResult show_dialog_impl(const HostDialogRequest& request) override {
        const CursorLeaseSnapshot cursor = cursor_interaction_snapshot();
        modal_saw_revoked = cursor.phase == CursorLeasePhase::revoked &&
            cursor.cause.error == CursorError::denied && restores == 1;
        const HostDialogResult result{{}, request.request_id, HostMessageDialogResult{}};
        return result;
    }
    void shutdown_impl() noexcept override { ++shutdowns; restores_at_shutdown = restores; }
    HostServiceStatus play_sound_cue_impl(const HostSoundCueRequest&) override { return {}; }
};
struct ForeignRelease final {
    CursorHiddenLease* lease{};
    CursorStatus result{};
    void run() { result = (*lease).release(); }
};
void lease_lifetime() {
    const std::shared_ptr<Root> root = std::make_shared<Root>();
    Window window(root, {120, 100});
    Services services{};
    HostSession session(window, capabilities(), &services);
    CursorLeaseResult first = window.begin_cursor_hidden();
    require(first.status.accepted() && first.lease.snapshot().phase == CursorLeasePhase::active, "lease acquired");
    CursorLeaseResult duplicate = window.begin_cursor_hidden();
    require(duplicate.status.error == CursorError::busy && services.hides == 1, "duplicate does not nest native hide");
    ForeignRelease foreign{&first.lease, {}};
    std::thread worker(&ForeignRelease::run, &foreign);
    worker.join();
    require(foreign.result.error == CursorError::wrong_thread && services.restores == 0, "wrong-thread release preserves authority");
    const HostServiceStatus changed = services.set_cursor(CursorKind::hand);
    require(changed.accepted(), "cursor policy changed during lease");
    const CursorStatus released = first.lease.release();
    const CursorStatus repeated = first.lease.release();
    require(released.accepted() && repeated.accepted() && services.restores == 1, "release is idempotent");
    require(services.restored == CursorKind::hand, "restore uses current policy");
    require(first.lease.snapshot().phase == CursorLeasePhase::released, "successful release is explicitly inactive");
    CursorLeaseResult focused = window.begin_cursor_hidden();
    require(focused.status.accepted(), "reacquire");
    window.set_active(false);
    window.set_active(true);
    require(focused.lease.snapshot().phase == CursorLeasePhase::revoked && services.restores == 2, "focus return does not reactivate revoked lease");
    window.capture_pointer(root);
    CursorLeaseResult captured = window.begin_cursor_hidden();
    require(captured.status.accepted(), "captured interaction");
    window.release_pointer();
    require(captured.lease.snapshot().phase == CursorLeasePhase::revoked && services.restores == 3, "capture loss restores");
    CursorLeaseResult detached = window.begin_cursor_hidden();
    require(detached.status.accepted(), "interaction without capture");
    session.shutdown();
    const CursorStatus after_detach = detached.lease.release();
    require(after_detach.error == CursorError::closing && services.restores == 4, "detach without capture revokes before observer removal");
    require(window.host_services() == nullptr, "host observer cleared");
    const CursorStatus old_release = first.lease.release();
    require(old_release.accepted() && services.restores == 4, "old released handle cannot affect later interaction");
}
void metrics_and_placement() {
    const std::shared_ptr<Root> root = std::make_shared<Root>();
    Window window(root, {120, 100});
    Services services{};
    HostSession session(window, capabilities(), &services);
    window.set_scale(1.25);
    const CursorMetrics current = window.cursor_metrics();
    const CursorStatus placed = window.warp_cursor(current, {12.5, 8.7});
    require(placed.accepted() && services.x == 15 && services.y == 10, "fractional DPI floors client pixels");
    require((*root).motions == 0 && !window.captured_control(), "placement creates no fabricated input or capture");
    const std::shared_ptr<Root> other_root = std::make_shared<Root>();
    Window other(other_root, {120, 100});
    Services other_services{};
    HostSession other_session(other, capabilities(), &other_services);
    other.set_scale(1.25);
    const CursorStatus crossed = other.warp_cursor(current, {10, 10});
    require(crossed.error == CursorError::stale_window && other_services.warps == 0, "same size and generation cannot cross windows");
    CursorMetrics forged = current;
    forged.scale = 2;
    const CursorStatus altered_scale = window.warp_cursor(forged, {10, 10});
    require(altered_scale.error == CursorError::stale_metrics, "caller cannot replace scale under a valid generation");
    forged = current;
    forged.client_dip.width = 500;
    const CursorStatus altered_extent = window.warp_cursor(forged, {10, 10});
    require(altered_extent.error == CursorError::stale_metrics, "caller cannot enlarge admitted bounds");
    const Point invalid[] = {{-1, 0}, {120, 0}, {0, 100}, {std::numeric_limits<double>::infinity(), 0},
                             {0, std::numeric_limits<double>::quiet_NaN()}};
    for (Point point : invalid) {
        const CursorStatus result = window.warp_cursor(current, point);
        require(result.error == CursorError::invalid_coordinate, "invalid coordinate refused before native call");
    }
    require(services.warps == 1, "all invalid requests bypass native placement");
    window.resize({121, 100});
    const CursorStatus resized = window.warp_cursor(current, {10, 10});
    require(resized.error == CursorError::stale_metrics, "resize invalidates placement snapshot");
    const CursorMetrics before_scale = window.cursor_metrics();
    window.set_scale(2);
    const CursorStatus scaled = window.warp_cursor(before_scale, {10, 10});
    require(scaled.error == CursorError::stale_metrics, "DPI invalidates placement snapshot");
    services.allowed = false;
    const CursorStatus denied = window.warp_cursor(window.cursor_metrics(), {10, 10});
    require(denied.error == CursorError::denied && services.warps == 1, "lost native authority refuses placement");
    services.allowed = true;
    services.fail_warp = true;
    CursorLeaseResult hidden = window.begin_cursor_hidden();
    require(hidden.status.accepted(), "lease before refused placement");
    const CursorStatus failed = window.warp_cursor(window.cursor_metrics(), {10, 10});
    require(failed.error == CursorError::native_failure, "native failure is inspectable");
    const CursorStatus restored = hidden.lease.release();
    require(restored.accepted(), "failed placement does not strand an unowned hide");
    services.supported = false;
    const CursorStatus unsupported = window.warp_cursor(window.cursor_metrics(), {10, 10});
    CursorLeaseResult unavailable = window.begin_cursor_hidden();
    require(unsupported.error == CursorError::unsupported && unavailable.status.error == CursorError::unsupported, "backend capability refused honestly");
}
void terminal_failure() {
    CursorHiddenLease retained{};
    {
        const std::shared_ptr<Root> root = std::make_shared<Root>();
        Window window(root, {120, 100});
        Services services{};
        HostSession session(window, capabilities(), &services);
        CursorLeaseResult result = window.begin_cursor_hidden();
        require(result.status.accepted(), "failure fixture acquired");
        retained = std::move(result.lease);
        services.fail_restore = true;
        const CursorStatus first = retained.release();
        const CursorStatus second = retained.release();
        require(first.error == CursorError::native_failure && second.error == CursorError::native_failure, "repeated release retains restoration failure");
        require(services.restores == 1 && retained.snapshot().phase == CursorLeasePhase::released, "failed cleanup remains explicitly terminal");
        CursorLeaseResult refused = window.begin_cursor_hidden();
        require(refused.status.error == CursorError::native_failure, "unresolved restoration prevents another hide");
    }
    const CursorLeaseSnapshot terminal = retained.snapshot();
    const CursorStatus released = retained.release();
    require(terminal.restoration.error == CursorError::native_failure && released.error == CursorError::native_failure,
            "retained terminal handle has no dangling native owner after teardown");
}
void shutdown_and_modal() {
    const std::shared_ptr<Root> root = std::make_shared<Root>();
    Window window(root, {120, 100});
    Services services{};
    HostSession session(window, capabilities(), &services);
    CursorLeaseResult hidden = window.begin_cursor_hidden();
    require(hidden.status.accepted(), "shutdown fixture acquired");
    services.fail_restore = true;
    services.shutdown();
    services.shutdown();
    const CursorLeaseSnapshot ended = hidden.lease.snapshot();
    require(services.restores == 2 && services.restores_at_shutdown == 2 && services.shutdowns == 1,
            "active shutdown retries restoration once before native teardown; repeated shutdown is inert");
    require(ended.phase == CursorLeasePhase::revoked && ended.cause.error == CursorError::closing &&
            ended.restoration.error == CursorError::native_failure, "shutdown retains first failure and closing cause");
    const CursorStatus released = hidden.lease.release();
    require(released.error == CursorError::native_failure && services.restores == 2,
            "terminal release cannot retry against a closed native owner");

    const std::shared_ptr<Root> modal_root = std::make_shared<Root>();
    Window modal_window(modal_root, {120, 100});
    Services modal_services{};
    HostSession modal_session(modal_window, capabilities(), &modal_services);
    CursorLeaseResult modal_lease = modal_window.begin_cursor_hidden();
    require(modal_lease.status.accepted(), "modal fixture acquired");
    const HostDialogRequest request{1, "cursor-root", HostMessageDialogRequest{"Title", "Message"}};
    const HostDialogResult dialog = modal_services.show_dialog(request);
    require(dialog.status.accepted() && modal_services.modal_saw_revoked,
            "modal entry restores and revokes before native dialog opens");
    require(modal_lease.lease.snapshot().phase == CursorLeasePhase::revoked,
            "closing a modal does not reactivate the hidden lease");
}
}
int main() {
    try {
        lease_lifetime();
        metrics_and_placement();
        terminal_failure();
        shutdown_and_modal();
        std::cout << "Cursor lease policy, metrics, revocation and terminal outcomes pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
