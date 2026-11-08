#import <AppKit/AppKit.h>

#include "macos_host.hpp"

#include "gui_forms/gui_forms.hpp"
#include "support/stock_control_fixture.hpp"

#if defined(GUI_FORMS_AX_GALLERY)
#include "gallery.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>

// Stage A: the application the out-of-process AX client
// (macos_accessibility_client_tests) inspects. It shows the stock-control fixture, or,
// built as the Gallery variant (GUI_FORMS_AX_GALLERY), the GUI.Forms
// Gallery. While it runs it writes a dump the client compares against:
//
//   {"pid":N,"generation":G,"effects":E,"request":R,"settled":S,
//    "frame_pending":F,"occluded":O,"motion_armed":M,"freeze_pending":Z,
//    "paint_passes":P,"lease":L,"content_revision":C,"rendered_revision":D,
//    "presented_revision":V,
//    "types":{stable id: manifest id},"snapshot":<SemanticSnapshot::to_json()>}
//
// to the path after --semantic-dump, whenever its content changes (checked
// every 50 ms), by writing a temporary file and renaming it over the
// dump.
//
// "settled" is true when the window has painted at least once, no
// invalidation waits for a paint (Window::needs_frame), the motion clock is
// not armed and (Gallery) no page freeze is pending: nothing the app knows
// of will change the tree or a value (such as a paint counter a diagnostics
// control publishes) before the next input. Its inputs, the window's
// occlusion and its paint lease (state and revisions) are written beside
// it, so that a client that waits in vain can say what held it.
// "request" is the number the client last wrote to the file at the dump's
// path plus ".request", as read by the writer's latest tick. A client that
// writes a new number after its last action and then waits for a dump with
// that number and "settled" true reads a tree that reflects the action and
// is quiet; it never infers quiet from timing. The writer's ticks run on the
// main thread, after any AX action the client already completed there.
//
// The fixture variant adds two controls: "ax.disposable" and the
// button "ax.dispose", whose click removes and destroys "ax.disposable", so
// the client can hold an element of a disposed control (interview GF017).
//
// The Gallery variant freezes each page it shows, as the UIA Gallery client
// does: when the selected page changes (and for the first page), it cancels
// the window's frame requests before it writes that page's dump, so the
// timers and animations a page starts (the timing page's tick and progress,
// the easing preview, the motion target) cannot change the tree between the
// dump and the client's AX reads. The writer's own timer is started again
// after each freeze. The freeze runs as main-queue work after the writer's
// tick, outside the window's frame dispatch.

namespace {

using gui_forms::Control;
using gui_forms::SemanticNode;
using gui_forms::SemanticSnapshot;
using gui_forms::test_support::StockControlFixture;
using gui_forms::test_support::StockEffectCounter;

constexpr const char* fixture_title = "GUI.Forms AX fixture";

void append_json_string(std::string& out, std::string_view text) {
    out += '"';
    for (const char character : text) {
        if (character == '"' || character == '\\') out += '\\';
        out += character;
    }
    out += '"';
}

// Each node's control type, by stable id: the control's manifest id
// (anatomy().control_id) for a node that is a control.
void append_types(const gui_forms::Window& window, const SemanticNode& node, std::string& out,
                  bool& first) {
    const Control::Ptr control = window.find(node.stable_id);
    if (control) {
        if (!first) out += ',';
        first = false;
        append_json_string(out, node.stable_id);
        out += ':';
        append_json_string(out, (*control).anatomy().control_id);
    }
    for (const SemanticNode& child : node.children) append_types(window, child, out, first);
}

class SemanticDumpWriter;
void freeze_page_on_main(void* context);

#if defined(GUI_FORMS_AX_GALLERY)
// Whether every started sub-surface host under `control` has published its
// semantics (its first frame's snapshot). Until then a page is not frozen:
// the sub-surface's first frame repaints the window after the freeze, which
// restarted a page's ambient effects (the ranges page's animated progress
// bars) and left the motion clock armed, so the page never settled (the
// intermittent S5 failure; the UIA Gallery client waits the same way).
bool sub_surfaces_published(const Control::Ptr& control) {
    const std::shared_ptr<gui_forms::SubSurfaceHost> host =
        std::dynamic_pointer_cast<gui_forms::SubSurfaceHost>(control);
    // A host that publishes no semantics (a grid's cell region) is not
    // waited for.
    if (host != nullptr && (*host).publishes_semantics() && (*host).started() &&
        (*host).semantic_virtual_children().empty()) {
        return false;
    }
    const std::span<const Control::Ptr> children = (*control).children();
    for (const Control::Ptr& child : children) {
        if (!sub_surfaces_published(child)) return false;
    }
    return true;
}

// The writer ticks (50 ms each) a page's sub-surfaces may take to publish
// before it is frozen anyway; a page that still has not published then is
// reported by the client as unsettled, as before.
constexpr std::uint32_t sub_surface_wait_ticks = 100U;
#endif

class SemanticDumpWriter final {
public:
    SemanticDumpWriter(gui_forms::Window& window, const StockEffectCounter* effects, std::string path)
        : window_(window), effects_(effects), path_(std::move(path)), request_path_(path_ + ".request") {}

    // The timer whose ticks call tick(); observed, owned by AppState, which
    // keeps it no longer than this writer. Started again after a freeze.
    void set_timer(gui_forms::Timer* timer) noexcept { timer_ = timer; }

    // Rewrites the dump when its content changed: a new generation, a new
    // effect count, or a value that changed without a new generation (a
    // diagnostics control's live counters). In the Gallery variant, a newly
    // selected page is frozen first (freeze_page), and its dump is written
    // only then.
    void tick() {
#if defined(GUI_FORMS_AX_GALLERY)
        const std::string_view page = gui_forms::gallery::selected_gallery_page(window_);
        if (page != frozen_page_) {
            if (!freeze_pending_) {
                const bool published = sub_surfaces_published(window_.root());
                if (!published && sub_surface_waits_ < sub_surface_wait_ticks) {
                    ++sub_surface_waits_;
                    return;
                }
                sub_surface_waits_ = 0U;
                freeze_pending_ = true;
                dispatch_async_f(dispatch_get_main_queue(), this, &freeze_page_on_main);
            }
            return;
        }
#endif
        write_if_changed();
    }

    // Cancels the window's frame requests (every motion jumps to its end),
    // records the page as frozen, starts this writer's timer again and
    // writes the frozen page's dump.
    void freeze_page() {
        freeze_pending_ = false;
#if defined(GUI_FORMS_AX_GALLERY)
        window_.cancel_frame_requests();
        frozen_page_ = std::string(gui_forms::gallery::selected_gallery_page(window_));
#endif
        if (timer_ != nullptr) (*timer_).start();
        write_if_changed();
    }

private:
    void write_if_changed() {
        const std::uint64_t effects = effects_ == nullptr ? 0U : (*effects_).total();
        const std::uint64_t request = read_request();
        const SettleState settle = settle_state();
        const std::string text = dump_text(effects, request, settle);
        if (text == written_text_) return;
        write(text);
        written_text_ = text;
    }

    // What decides "settled" (see the file comment), read on the main
    // thread, which also paints, so the inputs cannot change while it runs.
    struct SettleState final {
        std::uint64_t paint_passes{};
        bool frame_pending{};
        bool occluded{};
        bool motion_armed{};
        bool freeze_pending{};
        gui_forms::PaintLeaseSnapshot lease{};
        bool settled{};
    };

    SettleState settle_state() const {
        SettleState state{};
        state.paint_passes = window_.metrics_snapshot().paint_passes;
        state.frame_pending = window_.needs_frame();
        state.occluded = window_.occluded();
        state.motion_armed = window_.motion_clock().armed();
        state.freeze_pending = freeze_pending_;
        state.lease = window_.paint_lease_snapshot();
        state.settled = state.paint_passes != 0U && !state.frame_pending && !state.motion_armed &&
                        !state.freeze_pending;
        return state;
    }

    static const char* lease_state_name(gui_forms::PaintLeaseState state) noexcept {
        switch (state) {
        case gui_forms::PaintLeaseState::clean: return "clean";
        case gui_forms::PaintLeaseState::dirty_queued: return "dirty_queued";
        case gui_forms::PaintLeaseState::rendering: return "rendering";
        case gui_forms::PaintLeaseState::ready: return "ready";
        case gui_forms::PaintLeaseState::occluded_dirty: return "occluded_dirty";
        case gui_forms::PaintLeaseState::retired: return "retired";
        case gui_forms::PaintLeaseState::rendering_dirty: return "rendering_dirty";
        }
        return "unknown";
    }

    // The client's latest request number, or 0 when it wrote none or the
    // file does not hold a number. The client replaces the file by rename,
    // so a read sees a whole number or the previous one.
    std::uint64_t read_request() const {
        std::FILE* file = std::fopen(request_path_.c_str(), "r");
        if (file == nullptr) return 0U;
        unsigned long long value = 0U;
        const int fields = std::fscanf(file, "%llu", &value);
        static_cast<void>(std::fclose(file));
        if (fields != 1) return 0U;
        const std::uint64_t request = static_cast<std::uint64_t>(value);
        return request;
    }

    static const char* json_bool(bool value) noexcept { return value ? "true" : "false"; }

    std::string dump_text(std::uint64_t effects, std::uint64_t request, const SettleState& settle) const {
        const SemanticSnapshot snapshot = window_.semantic_snapshot();
        std::string text = "{\"pid\":" + std::to_string(static_cast<long long>(getpid())) +
                           ",\"generation\":" + std::to_string(snapshot.generation) +
                           ",\"effects\":" + std::to_string(effects) +
                           ",\"request\":" + std::to_string(request) +
                           ",\"settled\":" + json_bool(settle.settled) +
                           ",\"frame_pending\":" + json_bool(settle.frame_pending) +
                           ",\"occluded\":" + json_bool(settle.occluded) +
                           ",\"motion_armed\":" + json_bool(settle.motion_armed) +
                           ",\"freeze_pending\":" + json_bool(settle.freeze_pending) +
                           ",\"paint_passes\":" + std::to_string(settle.paint_passes) +
                           ",\"lease\":\"" + lease_state_name(settle.lease.state) + "\"" +
                           ",\"content_revision\":" + std::to_string(settle.lease.content_revision) +
                           ",\"rendered_revision\":" + std::to_string(settle.lease.rendered_revision) +
                           ",\"presented_revision\":" + std::to_string(settle.lease.presented_revision) +
                           ",\"types\":{";
        bool first = true;
        for (const SemanticNode& root : snapshot.roots) append_types(window_, root, text, first);
        text += "},\"snapshot\":";
        text += snapshot.to_json();
        text += '}';
        return text;
    }

    void write(const std::string& text) const {
        const std::string temporary = path_ + ".tmp";
        std::FILE* file = std::fopen(temporary.c_str(), "w");
        if (file == nullptr) return;
        const std::size_t written = std::fwrite(text.data(), 1U, text.size(), file);
        const int closed = std::fclose(file);
        if (written == text.size() && closed == 0) std::rename(temporary.c_str(), path_.c_str());
    }

    gui_forms::Window& window_;
    const StockEffectCounter* effects_;
    std::string path_;
    // The file the client writes its request number to: path_ + ".request".
    std::string request_path_;
    std::string written_text_;
    gui_forms::Timer* timer_{};
    // The page whose frame requests were last cancelled; empty before the
    // first freeze.
    std::string frozen_page_;
    bool freeze_pending_{};
    // Ticks spent waiting for the selected page's sub-surfaces to publish.
    std::uint32_t sub_surface_waits_{};
};

// Main-queue work posted by SemanticDumpWriter::tick; the context is the
// writer, which AppState keeps for the whole run.
void freeze_page_on_main(void* context) {
    SemanticDumpWriter& writer = *static_cast<SemanticDumpWriter*>(context);
    writer.freeze_page();
}

// Removes and destroys the disposable sample when "ax.dispose" is clicked.
class DisposeSample final {
public:
    DisposeSample(std::weak_ptr<Control> parent, std::weak_ptr<Control> sample)
        : parent_(std::move(parent)), sample_(std::move(sample)) {}

    void clicked(gui_forms::ButtonBase&) {
        const Control::Ptr parent = parent_.lock();
        const Control::Ptr sample = sample_.lock();
        if (!parent || !sample) return;
        static_cast<void>((*parent).remove_child((*sample).runtime_id()));
    }

private:
    std::weak_ptr<Control> parent_;
    std::weak_ptr<Control> sample_;
};

// What the app owns for its run. The writer and its timer are made on the UI
// thread when the host is ready, and the timer is declared after the writer
// it reports into.
struct AppState final {
    gui_forms::Window* window{};
    const StockEffectCounter* effects{};
    std::string dump_path;
    std::unique_ptr<SemanticDumpWriter> writer;
    std::unique_ptr<gui_forms::Timer> timer;
    gui_forms::SubscriptionToken tick_subscription;
};

using VoidCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>;
using ReadClipboardCallback = std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback = std::function<gui_forms::HostServiceStatus(std::string_view)>;

class StartDumping final {
public:
    explicit StartDumping(AppState& state) noexcept : state_(state) {}

    void operator()(VoidCallback, VoidCallback, ShowDialogCallback,
                    ReadClipboardCallback, WriteClipboardCallback) const {
        if (state_.dump_path.empty()) return;
        gui_forms::Window& window = *state_.window;
        state_.writer = std::make_unique<SemanticDumpWriter>(window, state_.effects, state_.dump_path);
        state_.timer = std::make_unique<gui_forms::Timer>(window, std::chrono::milliseconds(50));
        state_.tick_subscription = (*state_.timer).tick().subscribe(
            gui_forms::Delegate<>::bind<SemanticDumpWriter, &SemanticDumpWriter::tick>(*state_.writer));
        (*state_.writer).set_timer(state_.timer.get());
        (*state_.timer).start();
        (*state_.writer).tick();
    }

private:
    AppState& state_;
};

std::string argument_after(int argc, char** argv, std::string_view name) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == name) return argv[index + 1];
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    AppState state;
    state.dump_path = argument_after(argc, argv, "--semantic-dump");
    gui_forms::host::MacHostOptions options;
    options.print_metrics_on_close = false;
    options.host_ready = StartDumping(state);
#if defined(GUI_FORMS_AX_GALLERY)
    std::unique_ptr<gui_forms::Window> window = gui_forms::gallery::make_gallery();
    options.title = std::string(gui_forms::gallery::gallery_program_name);
    options.initial_size = gui_forms::gallery::gallery_default_size;
    options.minimum_size = gui_forms::gallery::gallery_minimum_size;
    state.window = window.get();
    return gui_forms::host::run_macos(std::move(window), std::move(options));
#else
    StockControlFixture fixture = gui_forms::test_support::make_stock_control_fixture();
    std::shared_ptr<gui_forms::Button> disposable =
        gui_forms::make_control<gui_forms::Button>(gui_forms::StableId("ax.disposable"), "Disposable sample");
    (*disposable).set_requested_bounds({1500.0, 1100.0, 180.0, 28.0});
    (*fixture.root).add_child(disposable);
    std::shared_ptr<gui_forms::Button> dispose =
        gui_forms::make_control<gui_forms::Button>(gui_forms::StableId("ax.dispose"), "Dispose sample");
    (*dispose).set_requested_bounds({1700.0, 1100.0, 180.0, 28.0});
    (*fixture.root).add_child(dispose);
    DisposeSample dispose_sample(fixture.root, disposable);
    const gui_forms::SubscriptionToken dispose_subscription = (*dispose).clicked().subscribe(
        gui_forms::Delegate<gui_forms::ButtonBase&>::bind<DisposeSample, &DisposeSample::clicked>(dispose_sample));
    disposable.reset();
    std::unique_ptr<gui_forms::Window> window =
        std::make_unique<gui_forms::Window>(fixture.root, gui_forms::test_support::stock_control_fixture_size);
    options.title = fixture_title;
    options.initial_size = gui_forms::test_support::stock_control_fixture_size;
    state.window = window.get();
    state.effects = fixture.effects.get();
    return gui_forms::host::run_macos(std::move(window), std::move(options));
#endif
}
