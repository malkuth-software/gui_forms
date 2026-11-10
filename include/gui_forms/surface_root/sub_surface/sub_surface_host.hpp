#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Counters of a host's side of its sub-surface's requests (design note
// section 15). UI thread.
struct SubSurfaceRequestStatistics final {
    std::uint64_t received{};
    // Answered shown by the window's tooltip presenter.
    std::uint64_t shown{};
    // Refused by the window's policy (its tooltips off for that origin).
    std::uint64_t declined{};
    // Dropped as out of date: measured in an older content epoch, or older
    // than the pointer or focus leaving the host.
    std::uint64_t stale{};
    // Revoked by the host: the pointer or keyboard focus left it.
    std::uint64_t revoked{};
    // Revoked by the sub-surface.
    std::uint64_t withdrawn{};
    // Alerts raised on the host for the sub-surface (SurfaceRoot::raise_alert).
    std::uint64_t alerts{};
};

// How a host's sub-surface thread ended (SubSurfaceHost::stopped): stopped
// when the host stopped it (new content, leaving the window), failed when
// the content threw or built no root, with what it threw.
struct SubSurfaceStopped final {
    SubSurfaceState state{SubSurfaceState::stopped};
    std::string failure{};
};

// The parent's control for one threaded sub-surface (stage S2, design note
// section 2): it occupies the region, owns the sub-surface's thread, sizes
// and reconfigures it, and composes its LiveSurface into the window. On the
// window's dirty structure it is one control; the sub-surface's frames never
// mark the window's tree. UI thread, like every control of its window.
//
// Life: the content starts on its own thread the first time the host is
// attached to a Window with a non-empty size (nothing is produced before
// the first agreed size, resize rule 5), and stops when the host leaves
// the window: the thread releases the content and is joined. A host that
// is attached again shows nothing until it is given content again.
//
// Input (design note section 4): the host is a tab stop. Pointer events that
// reach it (the window's capture holds a pressed pointer on it), and key and
// text events while it has keyboard focus, are forwarded in order to the
// sub-surface's thread, which dispatches them to its own tree; the window's
// accelerator pre-filter has already run. Keys the sub-surface leaves
// unhandled come back asynchronously and continue in the window as if the
// host had not handled them: bubble from the host's parent, accelerators,
// dialog keys, and Tab, which moves focus past the host because the
// sub-surface's own traversal does not wrap. Keyboard focus inside the
// sub-surface follows the host's focus and the window's activation.
//
// Requests (design note section 15): the sub-surface shows nothing beyond
// its region, so it asks for its tooltips. The host receives each request on
// the window's thread, drops it as stale when it is out of date, declines it
// when the window's tooltips are off for its origin, and otherwise offers it
// to the window's tooltip presenter as one of its tooltip items
// ("sub-surface-request.<id>", anchored at the host's origin plus the
// request's anchor), which applies every policy and placement; a program
// request is shown at once. Answers go back asynchronously. The host revokes
// the pointer's requests when the pointer leaves it, the keyboard's when its
// keyboard focus leaves, and drops every request when its thread stops.
//
// Notices (decision 6 and section 9): `stopped` is raised once for each
// thread that ends, whether the host stopped it or its content failed, and
// `pairing_missed` once for each content epoch whose required frame was not
// published in time, so the window presented an older frame unpaired (the
// epoch is the payload). Both are raised by tasks posted to the window's
// thread, never inside the present path or the stop that found them: a
// handler may set new content or call redraw_all. A host destroyed before
// its task runs raises nothing; one detached raises `stopped` only while
// its former window's dispatcher still runs.
//
// Accessibility (design note section 5, decision 5): the sub-surface
// publishes an immutable semantic snapshot whenever its tree's semantic
// generation moves, and the host publishes that snapshot's nodes as its own
// virtual children, so every host adapter reads one tree from the window's
// snapshot and names those nodes as it names any virtual child. Each node
// is translated into the window: its stable identity is prefixed with the
// host's ("<host>.<node>"), and so is a controlled panel's; its bounds move
// by the host's position; its runtime identity is
// virtual_semantic_runtime_id of "<host>#<node's runtime identity>", so a
// node of a control the sub-surface replaced, or of a restarted
// sub-surface, is a different node; and it is focused only while the
// sub-surface has keyboard focus. Actions on those nodes are posted to the
// sub-surface's thread with the node's own runtime identity, which refuses
// them there if the control has gone since; a focus action also focuses
// the host. Editable text on those nodes publishes its value and selection
// but no text layout (Window::semantic_text_layout answers only for the
// window's own controls), so hosts offer no text ranges inside a
// sub-surface.
class SubSurfaceHost final : public Control {
public:
    explicit SubSurfaceHost(StableId stable_id);
    ~SubSurfaceHost() override;

    // Whether the host publishes the sub-surface in the window's semantic
    // tree: its own pane and the sub-surface's nodes as its virtual
    // children (the class comment). On by default. A control that publishes
    // what the sub-surface draws itself (a DataGridView's cell region: the
    // grid publishes its cells) turns it off; the host then publishes no
    // node, takes no snapshot and performs no semantic action, and the
    // sub-surface's own controls are reached only through its owner.
    void set_publishes_semantics(bool publishes);
    [[nodiscard]] bool publishes_semantics() const noexcept { return publishes_semantics_; }
    // The content to show and whether it covers its whole region with
    // opaque pixels (the window then copies its frames instead of blending,
    // resize rule 2). A running sub-surface is stopped and joined first.
    // Null content stops it and leaves the region empty.
    void set_content(std::unique_ptr<SubSurfaceContent> content, bool opaque);
    // As set_content, moving from `content` only once nothing before the
    // take can fail: a throw before it leaves `content` holding the
    // content, so a caller that promised not to take it on failure (the C
    // ABI) can give it back. After the take the host holds it.
    void adopt_content(std::unique_ptr<SubSurfaceContent>& content, bool opaque);
    [[nodiscard]] bool opaque() const noexcept { return opaque_; }
    // The parent's redraw-all (design note section 2): sends a configuration
    // under a new content epoch carrying `update` (null for none), so that
    // the sub-surface's first frame at that epoch shows it and the window
    // composes no older frame once it pairs (section 6). Call it in the same
    // turn as the chrome change it must agree with (a scroll, a column
    // width). Returns the epoch the update rides: the new content_epoch(),
    // or, while there is no content, no window or no device size yet, 0 and
    // the update waits for the first configuration (a newer one replaces
    // it). New content (set_content) discards a waiting update.
    std::uint64_t redraw_all(std::unique_ptr<SubSurfaceContentUpdate> update);
    // Whether the sub-surface's thread exists (started and not stopped).
    [[nodiscard]] bool started() const noexcept { return static_cast<bool>(thread_); }
    // The thread's state; stopped while there is none.
    [[nodiscard]] SubSurfaceState state() const;
    // What the content threw or why it built no root, for state failed;
    // empty otherwise (SubSurfaceThread::failure).
    [[nodiscard]] std::string failure() const;
    [[nodiscard]] SubSurfaceStatistics statistics() const;
    // Posts to the sub-surface's thread (SubSurfaceThread::dispatcher);
    // empty while it is not running.
    [[nodiscard]] WindowDispatcher dispatcher() const;
    // The content epoch of the newest configuration sent (design note
    // section 6); zero before the first.
    [[nodiscard]] std::uint64_t content_epoch() const noexcept { return content_epoch_; }
    // The surface the sub-surface publishes into; null before it started.
    [[nodiscard]] std::shared_ptr<LiveSurface> live_surface() const noexcept { return surface_; }
    // Keys the sub-surface returned after the host had lost keyboard focus
    // or left its window; they are not replayed in the window.
    [[nodiscard]] std::uint64_t returned_keys_discarded() const noexcept { return returned_keys_discarded_; }
    [[nodiscard]] const SubSurfaceRequestStatistics& request_statistics() const noexcept {
        return request_statistics_;
    }
    // The sub-surface's open requests as tooltip parts, in window
    // coordinates; a program request has no bounds (it is shown at once,
    // never found under the pointer or focus).
    [[nodiscard]] std::vector<SemanticNode> tool_tip_items() const override;
    // A thread ended (see the class comment). Raised on the window's thread
    // by a posted task.
    [[nodiscard]] Event<const SubSurfaceStopped&>& stopped() noexcept { return stopped_; }
    // The window presented a frame older than the epoch it required (the
    // payload) once the one-frame wait ran out. Raised on the window's
    // thread by a posted task, once per epoch.
    [[nodiscard]] Event<std::uint64_t>& pairing_missed() noexcept { return pairing_missed_; }
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

    // Arranges as any control, then reconfigures the sub-surface when its
    // device size or scale changed. The window's theme and presentation
    // settings travel with every configuration, and a change of either
    // sends one.
    void arrange(Rect final_bounds) override;
    // Composes the newest frame into the region (Painter::draw_live_surface).
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // A pane of its own, named by the accessible name ("Sub-surface"
    // without one), whose default action focuses it: the host is the tab
    // stop through which focus enters the sub-surface. The sub-surface's
    // tree publishes its nodes in its own root, not under this one.
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    // The sub-surface's newest semantic snapshot taken by the host,
    // translated into the window (see the class comment); empty before the
    // first and while no sub-surface runs.
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    // Posts the action to the sub-surface when `stable_id` names one of the
    // nodes semantic_virtual_children publishes and that node offers it
    // (focus: a focusable node). True when posted; the sub-surface performs
    // or refuses it on its own thread (SubSurfaceStatistics).
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `content` is
    // the composed sub-surface frame, present once the sub-surface has a
    // LiveSurface; the sub-surface's own controls are parts of its tree,
    // not of the host.
    struct Parts final {
        PartGeometry region;
        PartGeometry content;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    // Forward to the sub-surface while it runs and mark the event handled.
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    // The sub-surface's published caret: its coordinates are the host's.
    [[nodiscard]] std::optional<Rect> text_input_caret() const override;

private:
    // The sub-surface's key return target for one thread. The host owns it
    // and revokes it when that thread stops; a key posted before then finds
    // it revoked or released and is dropped. Parent thread only.
    class KeyReturn;
    // The sub-surface's request target for one thread, revoked the same way.
    class RequestTarget;
    // Where the answers to the alerts raised for one thread go, revoked the
    // same way.
    class AlertRelay;
    // An alert raised on the host's root for the sub-surface: the root's
    // alert ID and the sub-surface's request ID.
    struct RaisedAlert final {
        std::uint64_t raised{};
        std::uint64_t request{};
    };

    // A request the sub-surface posted (RequestTarget).
    void receive_request(const SubSurfaceRequest& request);
    void receive_revoke(std::uint64_t id);
    // Revokes every open request of `origin` and tells the sub-surface.
    void revoke_requests(SubSurfaceToolTipOrigin origin);
    void answer(std::uint64_t id, SurfaceRequestResult result, HostDialogChoice choice = HostDialogChoice::none);
    // An alert the sub-surface raised: raised again on the host's root,
    // whose one answer goes back (relay_alert).
    void receive_alert(const SubSurfaceRequest& request);
    void relay_alert(const SurfaceRequestOutcome& outcome);
    // The window's presenter showed or closed one of this host's items.
    void on_window_tool_tip(const ToolTipEvent& change);
    // The sub-surface's semantics listener for one thread, revoked like
    // KeyReturn. Parent thread only.
    class SemanticsNotice;
    // The host's own notices, alive as long as the host and revoked by its
    // destructor: the window's unpaired-presentation notices, the thread's
    // end notice, and the stop notices the host posts for itself. Parent
    // thread only.
    class Notices;
    // The task a stop posts to raise `stopped` through Notices.
    class PostedStop;
    // The task the host posts when its thread failed before the end
    // listener was set (Notices::sub_surface_ended).
    class PostedEnd;
    // A posted notice ran (Notices): raises the event.
    void notice_thread_ended();
    void notice_stopped(const SubSurfaceStopped& stopped);
    void notice_pairing_missed(std::uint64_t epoch);

    // Tells the sub-surface whether it has keyboard focus: the host is the
    // window's focused control in an active window. `entry` is where focus
    // goes when it enters.
    void update_keyboard_focus(SubSurfaceFocusEntry entry);
    void on_window_active_changed(bool active);
    // Takes or releases the window's pointer capture for a forwarded press
    // or release (pressed_buttons_).
    void hold_capture_for(const PointerEvent& event);
    // A key the sub-surface returned (KeyReturn): continues in the window.
    void continue_returned_key(const KeyEvent& event);
    // A semantics notice (SemanticsNotice): takes the newest snapshot and
    // marks the host's semantics.
    void take_sub_surface_semantics();
    // Starts the thread when it can, and sends a configuration when the
    // region's device size or the window scale changed: a new content epoch,
    // a framebuffer at the new size, the pairing requirement, and a paint of
    // the region so the change is presented without waiting for an expose
    // (resize rule 3).
    void configure_region();
    void stop_thread() noexcept;
    // The window's theme or presentation settings changed: the next
    // configuration carries them under a new content epoch.
    void on_window_theme_changed(const Theme& theme);
    void on_window_presentation_changed(const PresentationSettings& settings);

    std::unique_ptr<SubSurfaceContent> content_{};
    std::shared_ptr<LiveSurface> surface_{};
    std::unique_ptr<SubSurfaceThread> thread_{};
    std::uint64_t content_epoch_{};
    // The device size and scale of the last configuration sent.
    std::uint32_t pixel_width_{};
    std::uint32_t pixel_height_{};
    double configured_scale_{};
    bool opaque_{};
    // An environment change not yet sent.
    bool environment_changed_{};
    // A redraw-all not yet sent, and its update (redraw_all).
    bool redraw_requested_{};
    std::unique_ptr<SubSurfaceContentUpdate> pending_update_{};
    SubscriptionToken theme_subscription_{};
    SubscriptionToken presentation_subscription_{};
    SubscriptionToken active_subscription_{};
    std::shared_ptr<KeyReturn> key_return_{};
    std::shared_ptr<RequestTarget> request_target_{};
    SubscriptionToken tool_tip_subscription_{};
    // The sub-surface's open requests, oldest first.
    std::vector<SubSurfaceRequest> requests_{};
    std::shared_ptr<AlertRelay> alert_relay_{};
    // The alerts raised for the sub-surface and not yet answered.
    std::vector<RaisedAlert> alerts_{};
    // The input sequences of the newest pointer leave and focus leave
    // forwarded: an older request of that origin is stale.
    std::uint64_t pointer_left_sequence_{};
    std::uint64_t focus_left_sequence_{};
    SubSurfaceRequestStatistics request_statistics_{};
    std::shared_ptr<SemanticsNotice> semantics_notice_{};
    // The newest snapshot taken from the running sub-surface; null before
    // the first and after the thread stopped.
    std::shared_ptr<const SemanticSnapshot> semantics_{};
    // set_publishes_semantics.
    bool publishes_semantics_{true};
    // The sub-surface was last told it has keyboard focus.
    bool keyboard_inside_{};
    // The non-primary pointer buttons forwarded down and not yet up, one bit
    // per PointerButton. While any is down the host holds the window's
    // pointer capture, so a drag that leaves the region still reaches the
    // sub-surface (design note section 4); the window itself captures only
    // for the primary button.
    std::uint8_t pressed_buttons_{};
    // The host, not the window, took the capture pressed_buttons_ holds.
    bool took_capture_{};
    std::uint64_t returned_keys_discarded_{};
    Event<const SubSurfaceStopped&> stopped_;
    Event<std::uint64_t> pairing_missed_;
    std::shared_ptr<Notices> notices_{};
    // The window dispatcher the running thread's notices go through, kept
    // so that a stop while detaching still posts to the former window.
    WindowDispatcher notice_dispatcher_{};
    // `stopped` is already raised (or posted) for the running thread.
    bool end_noticed_{};
};

} // namespace gui_forms
