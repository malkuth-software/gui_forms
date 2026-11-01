#pragma once

#include "gui_forms/application/application.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/docking.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/event/delegate/delegate.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class DockSite;
class WorkspaceTransaction;

// A factory's request: build the content of `panel` and store it in
// `content`. Left null, the panel shows an empty area and the factory is
// asked again the next time the panel is shown selected.
struct DockPanelCreation final {
    DockPanelId panel{};
    Control::Ptr content{};
};

// What the program registers for one panel: the layout's descriptor, the
// title its caption and tab show, and the factory that builds its content.
// The factory is a named delegate: its object must outlive the workspace's
// use of it (remove the panel or destroy the workspace first).
// A layout saved under a name (stage DK phase D7, owner decision DK-34,
// I6-1: Visual Studio's Window Layouts): the name the program shows and the
// layout's XML as DockLayout::write_xml wrote it. The program keeps and
// saves the list itself (decision DK-33: the framework never saves).
struct DockNamedLayout final {
    std::string name{};
    std::string xml{};
};

struct DockPanelRegistration final {
    DockPanelDescriptor descriptor{};
    std::string title{};
    Delegate<DockPanelCreation&> factory{};
};

// Why documents are asked to close (stage DK phase D6, owner decision
// DK-31): one document's own close (its tab, its caption or its position
// menu), Close All Documents, Close Other Documents, or the close button of
// the floating window holding them.
enum class DockCloseCause : std::uint8_t { document, all_documents, other_documents, floating_window };

// How the program is asked before documents close at the user's request
// (owner decision DK-31, H1): one closing event per document, in order, or
// one event for all of them (a combined prompt, such as a save-changes
// list).
enum class DockClosePrompt : std::uint8_t { each, combined };

// Documents about to close at the user's request (DockWorkspace::
// document_closing). `documents` is the one document asked about (each) or
// every document of the request (combined), in the request's order; it is
// valid only while the event is raised. A handler sets `cancel` to keep
// them, which ends the request with nothing closed, or `defer` to answer
// later with DockWorkspace::answer_close (an asynchronous prompt). With
// neither set, the request goes on.
struct DockDocumentClosing final {
    std::span<const DockPanelId> documents{};
    DockCloseCause cause{DockCloseCause::document};
    bool cancel{};
    bool defer{};
};

// What a close request did.
// - closed: every document was allowed, and they closed in one user
//   transaction (a document that left the layout meanwhile is skipped);
// - cancelled: a handler (or answer_close) kept a document; nothing closed
//   and the layout is unchanged;
// - pending: a handler deferred; answer_close goes on with the request;
// - busy: another request waits for its answer; nothing changed;
// - refused: nothing to close, or the layout refused the transaction.
enum class DockCloseOutcome : std::uint8_t { closed, cancelled, pending, busy, refused };

// Why a panel became active.
enum class DockActivationCause : std::uint8_t {
    programmatic,
    pointer,
    keyboard,
    // Focus entered the panel (a click in its content, Tab, F6).
    focus,
    navigator,
    // The active panel or document left the layout and another took its
    // place.
    fallback,
};

// The two activation tracks (owner decision DK-29; docking study 5.6): the
// active panel is the panel that has, or last had, keyboard focus, tool or
// document; the active document is the document panel last active. A tool
// panel's activation changes only the first.
enum class DockActivationTrack : std::uint8_t { panel, document };

struct DockActivationChange final {
    DockActivationTrack track{DockActivationTrack::panel};
    std::optional<DockPanelId> old_panel{};
    std::optional<DockPanelId> new_panel{};
    DockActivationCause cause{DockActivationCause::programmatic};
};

// What opens an auto-hidden panel's flyout from its side tab (owner decision
// DK-27, E1-1): resting the pointer on the tab for DockSite's hover delay or
// clicking it (the default), or only clicking it.
enum class DockFlyoutOpening : std::uint8_t { hover_or_click, click };

// The docking workspace (owner decisions DK-3, DK-29, DK-34): a component
// owning the DockLayout, the panel registrations with their factories, the
// content each created panel holds, and the activation tracks with their
// most-recently-active histories. A DockSite realizes the main site in a
// window, and one DockSite per floating site realizes it in its floating
// window; the workspace names, on every site's window's command routing
// chain, the command targets of the active panel and the active document
// (the targets their content controls carry, Control::set_command_target),
// so a document command such as Save reaches the active document from a
// focused tool panel, floating or not.
//
// Lazy creation: a panel's factory runs the first time the panel is shown
// selected in a realized stack (main or floating) or in a flyout, never for
// a panel that stays behind another tab, hidden, collapsed or behind a side
// tab without its flyout opening. The workspace keeps each created control
// until the panel is closed (close_panel, or a document leaving the layout);
// hiding keeps it.
//
// Edits are model transactions (apply, or the named operations below); the
// layout publishes one change per transaction, after which the workspace
// prunes its histories and moves an active panel or document that left the
// layout to the next in its history (cause fallback), and then the sites
// realize the change: the main site first, then the floating sites in the
// order they were bound. A change whose edits only set floating frames
// (DockLayoutChange::frames_only) moves the floating windows and realizes
// nothing. One main DockSite and one DockSite per floating site may be bound
// at a time.
//
// Floating windows (stage DK phase D4; owner decisions DK-1, DK-6 to DK-12,
// DK-41, DK-44): once attach_windows binds the run's main window, the
// workspace keeps one owned floating window per floating site of the layout.
// Each change is carried out as one WorkspaceTransaction across the windows
// (decision 9): the sites realize the change with every window's layout
// held, a floating site new to the layout gets a window opened hidden (its
// root a DockSite for the site, `<workspace id>.floating.<site id>`, sharing
// the main window's ApplicationResources and key bindings), and at the
// commit every window lays out once, the new windows are shown, and then the
// windows whose sites left the layout are destroyed, so a panel moving from
// a floating window into another window appears before its old window
// closes. A floating window holding a document is a floating document
// window (it stays visible while the application is inactive); any other is
// a floating tool window, which hides then (HostWindowFloating, W0b); the
// kind is chosen when the window opens. Its client area is the site's frame,
// moved if needed so its caption is reachable in a work area
// (place_on_monitor, decision 34); its title is the first stack's selected
// panel's. A move or resize of a floating window, by the user or the host,
// is written to the layout as set_floating_frame (a user edit, allowed
// under the docking lock) with the monitor its centre is on; a frame the
// program sets is requested from the window. The close button of a floating
// window hides its tool panels and closes its documents (one user
// transaction, after the close request returns), which closes the window;
// with no main site bound, the transaction runs inside the request and the
// host closes the window.
// Keys a floating window's own route and accelerators do not take are
// offered to the main window's accelerators (Window::offer_accelerators),
// so shortcuts such as Ctrl+S work from a floating tool window (F2-1).
//
// Execution: one UI thread, the run's; nothing is synchronized.
class DockWorkspace final : public Component {
public:
    explicit DockWorkspace(std::string stable_id);
    ~DockWorkspace() override;

    [[nodiscard]] const std::string& stable_id() const noexcept { return stable_id_; }
    // The layout. Restrictions, groups, saving and restoring go through it
    // directly; register panels through register_panel, so that their
    // titles and factories are known.
    [[nodiscard]] DockLayout& layout() noexcept { return layout_; }
    [[nodiscard]] const DockLayout& layout() const noexcept { return layout_; }

    // Registers a panel with the layout and keeps its title and factory.
    // Registering places nothing (show_panel does). False, changing
    // nothing, when the layout refuses the descriptor (an empty or repeated
    // identity).
    bool register_panel(DockPanelRegistration registration);
    [[nodiscard]] const DockPanelRegistration* find_registration(const DockPanelId& panel) const noexcept;
    // Retitles a registered panel's caption, tab and navigator row. False
    // for an unregistered panel.
    bool set_panel_title(const DockPanelId& panel, std::string title);
    // The registered title, or the identity for a panel without one.
    [[nodiscard]] std::string panel_title(const DockPanelId& panel) const;

    // Applies a transaction to the layout (DockLayout::apply).
    DockTransactionResult apply(const DockTransaction& transaction);
    // Shows a hidden or new panel where it was last (DockEdit::show) and
    // activates it. A placed panel is only activated.
    DockTransactionResult show_panel(const DockPanelId& panel, DockEditOrigin origin = DockEditOrigin::program);
    // Hides a panel, keeping its placeholder (DockEdit::hide).
    DockTransactionResult hide_panel(const DockPanelId& panel, DockEditOrigin origin = DockEditOrigin::user);
    // Closes a panel (DockEdit::close) and releases its content; a closed
    // tool panel keeps its placeholder, a closed document leaves.
    DockTransactionResult close_panel(const DockPanelId& panel, DockEditOrigin origin = DockEditOrigin::user);

    // -- closing documents at the user's request (phase D6) --
    // The user's close of one panel. A document is asked about first
    // (document_closing) and closes once allowed; a tool panel closes at
    // once (close_panel), with no event. Refused for a panel that is not
    // placed or may not close.
    DockCloseOutcome request_close(const DockPanelId& panel);
    // Close All Documents: every placed document that may close, the main
    // site's in tree and tab order, then each floating site's.
    DockCloseOutcome close_all_documents();
    // Close Other Documents: as close_all_documents, keeping `kept`.
    DockCloseOutcome close_other_documents(const DockPanelId& kept);
    // The placed documents that may close, main site first, in tree and
    // tab order: the documents Close All Documents asks about.
    [[nodiscard]] std::vector<DockPanelId> closable_documents() const;
    // Answers the deferred prompt of the waiting request: `close` goes on
    // with it (the next document is asked about, or the documents close),
    // false cancels it. Refused when no request waits for an answer.
    DockCloseOutcome answer_close(bool close);
    // Whether a request waits for answer_close, and the documents its
    // prompt asked about (empty when none). Valid until the next request,
    // answer or layout change.
    [[nodiscard]] bool close_pending() const noexcept;
    [[nodiscard]] std::span<const DockPanelId> pending_close() const noexcept;
    // One closing event per document (each, the default) or one for all.
    [[nodiscard]] DockClosePrompt close_prompt() const noexcept { return close_prompt_; }
    void set_close_prompt(DockClosePrompt prompt) noexcept { close_prompt_ = prompt; }
    // Raised before documents close at the user's request, on the UI
    // thread. A handler may change the layout or close panels itself; a
    // request made from inside it is busy.
    [[nodiscard]] Event<DockDocumentClosing&>& document_closing() noexcept { return document_closing_; }

    // -- saving and restoring layouts (phase D7) --
    // The layout as XML, on the program's request (decision DK-33); the
    // workspace never saves on its own.
    [[nodiscard]] DockLayoutWriteResult save_layout() const;
    // Replaces the layout with a saved one, on live windows: floating sites
    // the saved layout holds open their windows and those it drops close,
    // in one window transaction; only the panels then shown selected are
    // created (decision DK-34, I7-1); a saved panel not registered yet
    // stays a placeholder until it registers and is shown. With no monitors
    // in `options`, the attached run's monitors are used. A close request
    // waiting for answer_close ends, closing nothing. An unreadable or
    // rejected layout changes nothing (DockRestoreStatus::kept_current).
    DockRestoreResult load_layout(std::string_view xml, const DockRestoreOptions& options = {});
    // The named layouts, in the order they were first saved.
    [[nodiscard]] std::span<const DockNamedLayout> named_layouts() const noexcept { return named_layouts_; }
    // The named layout, or null.
    [[nodiscard]] const DockNamedLayout* find_named_layout(std::string_view name) const noexcept;
    // Saves the current layout under `name`, replacing a layout of that
    // name in its place. False, changing nothing, when the layout cannot be
    // written. Throws std::invalid_argument for an empty name or one that
    // is not valid UTF-8.
    bool save_named_layout(std::string name);
    // Loads the named layout as load_layout does; kept_current, with a
    // not_a_layout diagnostic, for a name not saved.
    DockRestoreResult apply_named_layout(std::string_view name, const DockRestoreOptions& options = {});
    // Removes the named layout; false when there is none.
    bool remove_named_layout(std::string_view name);
    // Replaces the list, as the program read it back. Throws
    // std::invalid_argument, changing nothing, for an empty, invalid or
    // repeated name; the XML is read only when a layout is applied.
    void set_named_layouts(std::vector<DockNamedLayout> layouts);

    // The panel's content, or null before its factory ran (or after it was
    // released).
    [[nodiscard]] Control::Ptr panel_content(const DockPanelId& panel) const;
    // Runs the panel's factory if the panel has no content yet, and returns
    // the content (null when there is no factory or it built nothing).
    Control::Ptr create_panel_content(const DockPanelId& panel);
    // The number of factory runs that produced content, for diagnostics
    // and tests.
    [[nodiscard]] std::size_t created_panel_count() const noexcept { return created_count_; }

    [[nodiscard]] const std::optional<DockPanelId>& active_panel() const noexcept { return active_panel_; }
    [[nodiscard]] const std::optional<DockPanelId>& active_document() const noexcept { return active_document_; }
    // Most recently active first; only placed panels. Valid until the next
    // activation or layout change.
    [[nodiscard]] std::span<const DockPanelId> tool_history() const noexcept {
        const std::span<const DockPanelId> history(tool_history_);
        return history;
    }
    [[nodiscard]] std::span<const DockPanelId> document_history() const noexcept {
        const std::span<const DockPanelId> history(document_history_);
        return history;
    }
    // Makes a placed panel active: selects it in its stack, creates its
    // content, moves focus into it (through the site realizing it, main or
    // floating) and updates the tracks. A panel behind a side tab
    // (auto-hidden, or in a stack collapsed by shrinking) opens in its
    // flyout with focus inside; an auto-hidden stack's selection is not
    // edited. False, changing nothing, for a panel that is not placed, not
    // registered, or floating in a site no DockSite is bound for.
    bool activate_panel(const DockPanelId& panel, DockActivationCause cause = DockActivationCause::programmatic);

    // -- floating windows (phase D4) --
    // Binds the run's main window (`main_window`, whose ApplicationWindow::
    // stable_id is `main_stable_id`, the owner the floating windows name)
    // and opens a floating window for every floating site of the layout. A
    // main DockSite must be bound and attached to that window's model for
    // floating windows to open (they share its resources and key bindings).
    // False, binding nothing, for an inactive handle, an empty identity, a
    // host without runtime windows, or windows already attached.
    bool attach_windows(ApplicationWindowHandle main_window, std::string main_stable_id);
    // Destroys the floating windows (their sites stay in the layout, so a
    // later attach opens them again) and unbinds the main window.
    void detach_windows() noexcept;
    [[nodiscard]] bool windows_attached() const noexcept { return main_window_.active(); }
    // The bound main window, or an inactive handle.
    [[nodiscard]] const ApplicationWindowHandle& main_window() const noexcept { return main_window_; }
    // The window realizing a floating site, or an inactive handle (no
    // window yet, or windows detached).
    [[nodiscard]] ApplicationWindowHandle floating_window(DockSiteId site) const;
    // The live floating windows, in opening order.
    [[nodiscard]] std::size_t floating_window_count() const noexcept;
    // The DockSite bound for a floating site, or null.
    [[nodiscard]] DockSite* floating_site(DockSiteId site) const noexcept;
    // Whether `subject` may float now: windows are attached, the run has
    // room for another window (Application::maximum_windows), and the layout
    // accepts float_subject for it as a user edit (the restrictions, the
    // docking lock and the limit of dock_maximum_floating_sites, DK-6).
    [[nodiscard]] bool can_float(const DockSubject& subject) const;
    // Floats `subject` into a new floating site whose client area is
    // `frame` (desktop logical units), recording the monitor its centre is
    // on; the window opens with the transaction. Refused, changing nothing,
    // as the layout refuses, or with placement_not_allowed while
    // can_float is false.
    DockTransactionResult float_subject(const DockSubject& subject, Rect frame,
                                        DockEditOrigin origin = DockEditOrigin::user);
    // The edit that docks `subject` back (a floating panel or stack, or any
    // panel): into the stack its panel was docked in last, else beside the
    // neighbour remembered there, else a document into the document area
    // and a tool panel along its default edge; the first the layout accepts
    // as an edit of `origin`. For a stack, its selected panel's memory
    // decides. None when no such edit is accepted.
    [[nodiscard]] std::optional<DockEdit> dock_back_edit(const DockSubject& subject,
                                                         DockEditOrigin origin = DockEditOrigin::user) const;
    // Applies dock_back_edit and activates the panel. Refused (not_placed)
    // when there is no edit.
    DockTransactionResult dock_back(const DockSubject& subject, DockEditOrigin origin = DockEditOrigin::user);
    // The generation of the last change that touched a site's tree (a
    // frames-only change does not); drags compare it to tell a stale plan.
    [[nodiscard]] std::uint64_t tree_generation() const noexcept { return tree_generation_; }

    // The workspace setting for opening flyouts; hover_or_click by default.
    [[nodiscard]] DockFlyoutOpening flyout_opening() const noexcept { return flyout_opening_; }
    void set_flyout_opening(DockFlyoutOpening opening) noexcept { flyout_opening_ = opening; }

    // Raised after a track changed: the panel track first, then the document
    // track when both change. Handlers may activate other panels.
    [[nodiscard]] Event<const DockActivationChange&>& activation_changed() noexcept { return activation_changed_; }

    // The bound main site, or null.
    [[nodiscard]] DockSite* site() const noexcept { return site_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    friend class DockSite;

    struct PanelEntry final {
        DockPanelRegistration registration{};
        Control::Ptr content{};
    };
    // One floating window the workspace opened. `model` observes the
    // window's Window from its ready callback until it closes; `frame` is
    // the client area last requested or reported; `destroying` once the
    // window is queued for destruction (its site left the layout).
    struct FloatingWindow final {
        DockSiteId site{};
        ApplicationWindowHandle handle{};
        Window* model{};
        HostWindowFloating kind{HostWindowFloating::tool};
        Rect frame{};
        std::string title{};
        bool destroying{};
    };
    // The floating windows' callbacks reach the workspace only through this
    // record, which the workspace clears when it detaches or goes, so a
    // window outliving the workspace calls nothing.
    struct WindowLiaison final {
        DockWorkspace* workspace{};
    };
    struct FloatingReady;
    struct FloatingClosing;
    struct FloatingClosed;
    struct FloatingFrame;
    struct CloseFloatingSite;
    // A user request to close documents, while its prompts run or one
    // waits for answer_close. `asked` is the number of documents, from
    // `next`, the waiting prompt asked about (zero while none waits).
    // `companions` are the edits that close with the documents (a floating
    // window's tool panels), and `closed_tools` the tool panels among them
    // whose content goes when they close.
    struct CloseRequest final {
        DockCloseCause cause{DockCloseCause::document};
        std::vector<DockPanelId> documents{};
        std::size_t next{};
        std::size_t asked{};
        std::vector<DockEdit> companions{};
        std::vector<DockPanelId> closed_tools{};
    };
    // A frame reported while a transaction was open, written after it.
    struct PendingFrame final {
        DockSiteId site{};
        Rect frame{};
    };

    [[nodiscard]] PanelEntry* find_entry(const DockPanelId& panel) noexcept;
    [[nodiscard]] const PanelEntry* find_entry(const DockPanelId& panel) const noexcept;
    [[nodiscard]] bool docked_in_main_site(const DockPanelId& panel) const;
    [[nodiscard]] bool placed(const DockPanelId& panel) const;
    [[nodiscard]] DockPanelKind panel_kind(const DockPanelId& panel) const;
    // The site binds itself on construction and unbinds on destruction.
    void attach_site(DockSite& site);
    void detach_site(DockSite& site) noexcept;
    // The bound sites, main first, as a list a handler may not invalidate.
    [[nodiscard]] std::vector<DockSite*> bound_sites() const;
    [[nodiscard]] bool bound(const DockSite* site) const noexcept;
    // The site realizing a panel's stack, or null.
    [[nodiscard]] DockSite* site_of(const DockPanelLocation& location) const noexcept;
    // -- floating windows --
    [[nodiscard]] FloatingWindow* find_window(DockSiteId site) noexcept;
    [[nodiscard]] const FloatingWindow* find_window(DockSiteId site) const noexcept;
    [[nodiscard]] Window* main_model() const noexcept;
    // Brings the floating windows in line with the layout's floating sites:
    // windows of sites that left are queued for destruction, sites without
    // a window get one, and every window's title follows its site.
    void reconcile_windows(WorkspaceTransaction& transaction);
    void open_window(const DockFloatingSite& site, WorkspaceTransaction& transaction);
    [[nodiscard]] std::string window_title(const DockFloatingSite& site) const;
    [[nodiscard]] HostWindowFloating window_kind(const DockFloatingSite& site) const;
    // Requests the frames the program set (a frames-only change).
    void follow_layout_frames();
    void write_pending_frames();
    [[nodiscard]] std::string monitor_at(Rect frame) const;
    [[nodiscard]] std::vector<DockMonitor> monitors() const;
    void on_window_ready(DockSiteId site, Window& window, const ApplicationWindowHandle& handle);
    void on_window_closing(DockSiteId site, HostCloseRequest& request);
    void on_window_closed(DockSiteId site) noexcept;
    void on_window_frame(DockSiteId site, const HostWindowFrameEvent& event);
    DockCloseOutcome close_floating_site(DockSiteId site);
    void forget_window(DockSiteId site) noexcept;
    void realize_on_sites(const DockLayoutChange& change);
    void release_sites() noexcept;
    // For DockSite's drags across windows: the window realizing a site, the
    // desktop position of the site's origin (none without window frames),
    // and the site topmost at a desktop point, `excluded` skipped (its own
    // window is being moved under the pointer: the next window below it,
    // floating windows before the main one).
    [[nodiscard]] ApplicationWindowHandle window_of(const DockSite& site) const;
    [[nodiscard]] std::optional<Point> desktop_origin(const DockSite& site) const;
    [[nodiscard]] DockSite* site_at(Point desktop, const DockSite* excluded) const;
    [[nodiscard]] bool site_contains(const DockSite& site, Point desktop) const;
    // Records `panel` as active without moving focus (the site calls it when
    // focus enters a panel).
    void note_active(const DockPanelId& panel, DockActivationCause cause);
    void set_track(DockActivationTrack track, const std::optional<DockPanelId>& panel, DockActivationCause cause);
    // Names the active panel's and document's command targets on every
    // bound site's window; floating windows also take the main window's
    // main_window target.
    void publish_command_targets();
    void on_layout_changed(const DockLayoutChange& change);
    // Starts a request (busy while another runs) and runs its prompts.
    DockCloseOutcome begin_close(CloseRequest request);
    // Asks about the documents from `next` until one is cancelled or
    // deferred, then closes them all.
    DockCloseOutcome run_close();
    void drop_unplaced_close_documents();
    DockCloseOutcome finish_close();

    std::string stable_id_;
    DockLayout layout_{};
    // One per registered panel, in registration order.
    std::vector<PanelEntry> panels_{};
    std::size_t created_count_{};
    std::optional<DockPanelId> active_panel_{};
    std::optional<DockPanelId> active_document_{};
    std::vector<DockPanelId> tool_history_{};
    std::vector<DockPanelId> document_history_{};
    // Observe the bound sites; each clears itself when it is destroyed.
    DockSite* site_{};
    std::vector<DockSite*> floating_sites_{};
    SubscriptionToken layout_token_{};
    std::uint64_t tree_generation_{};
    // The floating windows (attach_windows).
    ApplicationWindowHandle main_window_{};
    std::string main_window_id_{};
    std::shared_ptr<WindowLiaison> liaison_{};
    std::vector<FloatingWindow> windows_{};
    // The cross-window transaction of the change being carried out, while
    // one is: a nested change joins it.
    WorkspaceTransaction* transaction_{};
    std::vector<PendingFrame> pending_frames_{};
    DockFlyoutOpening flyout_opening_{DockFlyoutOpening::hover_or_click};
    DockClosePrompt close_prompt_{DockClosePrompt::each};
    std::optional<CloseRequest> close_request_{};
    std::vector<DockNamedLayout> named_layouts_{};
    Event<const DockActivationChange&> activation_changed_{};
    Event<DockDocumentClosing&> document_closing_{};
};

} // namespace gui_forms
