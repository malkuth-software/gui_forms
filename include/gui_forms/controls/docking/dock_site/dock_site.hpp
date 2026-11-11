#pragma once

#include "gui_forms/application/window_frame_drag/window_frame_drag.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/docking/dock_navigator/dock_navigator.hpp"
#include "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp"
#include "gui_forms/controls/pages/page_view/page_view.hpp"
#include "gui_forms/docking.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/controls/docking/dock_site/dock_site_capabilities/dock_site_capabilities.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace gui_forms {

// The rows of a panel's position menu (owner decision DK-30; docking study
// 5.7). Float floats the panel into a new floating window (phase D4; enabled
// while the workspace has windows attached and the panel may float). Auto
// Hide pins a root-level tool stack out to the nearest window edge; for an
// auto-hidden panel Dock returns its stack to where it stood, for a floating
// panel it docks the panel back where it was docked last
// (DockWorkspace::dock_back). Move starts the keyboard move mode (phase D3,
// G3-1). A document's menu also offers Close All Documents and Close Other
// Documents (phase D6, owner 2026-10-31), which ask as
// DockWorkspace::close_all_documents and close_other_documents do.
enum class DockPositionCommand : std::uint8_t {
    float_panel,
    dock,
    dock_as_document,
    auto_hide,
    hide,
    close,
    move,
    close_all_documents,
    close_other_documents,
};

// The phases of a drag (docking study 5.1, phase D3): `armed` from a press
// on a caption until the pointer has moved the drag threshold; `dragging`
// while the stamp follows the pointer, or the keyboard steps through the
// targets; `settling` while the stamp grows into its new place, or back into
// its old one. Input other than the drag's own is not taken while settling.
enum class DockDragPhase : std::uint8_t { idle, armed, dragging, settling };

// How a drag ended: dropped on a target (one layout transaction, in this
// site's window or another's); Escape; the pointer capture lost; the window
// deactivated; the layout changed under it (by the program, so the drag
// never applies to a stale tree); released where nothing accepts it, which
// floats the subject into a new floating window where it may float
// (`floated`, D4-1) and snaps it back otherwise (`rejected`).
enum class DockDragEnd : std::uint8_t {
    dropped,
    escaped,
    capture_lost,
    deactivated,
    layout_changed,
    rejected,
    floated,
};

// How a drag ended, with the subject dragged. Only a drop has a target and
// only a float a frame; the subject is common to every end. One alternative
// per state (house form for results).
class DockDragOutcome final {
public:
    // `across_windows`: whether the target was in another site's window (a
    // drag across windows, phase D4).
    [[nodiscard]] static DockDragOutcome dropped(DockSubject subject, DockDropTarget target,
            DockTransactionResult result, bool across_windows) noexcept {
        DockDragOutcome built{std::move(subject), Dropped{std::move(target), std::move(result), across_windows}};
        return built;
    }
    // `frame`: the new floating window's client area, in desktop logical
    // units.
    [[nodiscard]] static DockDragOutcome floated(DockSubject subject, DockTransactionResult result,
            Rect frame) noexcept {
        DockDragOutcome built{std::move(subject), Floated{std::move(result), frame}};
        return built;
    }
    // Every other end. A drop or a float without its parts is read as
    // `rejected`.
    [[nodiscard]] static DockDragOutcome ended(DockDragEnd end, DockSubject subject) noexcept {
        const bool partless = end == DockDragEnd::dropped || end == DockDragEnd::floated;
        const DockDragEnd reason = partless ? DockDragEnd::rejected : end;
        DockDragOutcome built{std::move(subject), Ended{reason}};
        return built;
    }

    [[nodiscard]] DockDragEnd end() const noexcept {
        const Ended* const other = std::get_if<Ended>(&state_);
        DockDragEnd end = DockDragEnd::dropped;
        if (std::holds_alternative<Floated>(state_)) end = DockDragEnd::floated;
        if (other != nullptr) end = (*other).end;
        return end;
    }
    [[nodiscard]] const DockSubject& subject() const noexcept {
        return subject_;
    }
    // The target dropped on, for `dropped`, in the coordinates of the site
    // that showed it; null otherwise.
    [[nodiscard]] const DockDropTarget* target() const noexcept {
        const Dropped* const drop = std::get_if<Dropped>(&state_);
        const DockDropTarget* const found = drop != nullptr ? &(*drop).target : nullptr;
        return found;
    }
    // The layout transaction's result, for `dropped` and `floated`; null
    // otherwise.
    [[nodiscard]] const DockTransactionResult* result() const noexcept {
        const Dropped* const drop = std::get_if<Dropped>(&state_);
        const Floated* const floating = std::get_if<Floated>(&state_);
        const DockTransactionResult* found = nullptr;
        if (drop != nullptr) found = &(*drop).result;
        if (floating != nullptr) found = &(*floating).result;
        return found;
    }
    // For `dropped`: whether the target was in another site's window (a drag
    // across windows, phase D4); false otherwise.
    [[nodiscard]] bool across_windows() const noexcept {
        const Dropped* const drop = std::get_if<Dropped>(&state_);
        const bool across = drop != nullptr && (*drop).across_windows;
        return across;
    }
    // For `floated`: the new floating window's client area, in desktop
    // logical units; null otherwise.
    [[nodiscard]] const Rect* frame() const noexcept {
        const Floated* const floating = std::get_if<Floated>(&state_);
        const Rect* const found = floating != nullptr ? &(*floating).frame : nullptr;
        return found;
    }

private:
    struct Dropped final {
        DockDropTarget target{};
        DockTransactionResult result{};
        bool across_windows{};
    };
    struct Floated final {
        DockTransactionResult result{};
        Rect frame{};
    };
    struct Ended final {
        DockDragEnd end{DockDragEnd::escaped};
    };
    using State = std::variant<Dropped, Floated, Ended>;
    // A throwing move would leave the variant valueless, a state no query
    // describes.
    static_assert(std::is_nothrow_move_constructible_v<Dropped>);
    static_assert(std::is_nothrow_move_constructible_v<Floated>);
    static_assert(std::is_nothrow_move_constructible_v<DockSubject>);

    DockDragOutcome(DockSubject subject, State state) noexcept
        : subject_(std::move(subject)), state_(std::move(state)) {}

    DockSubject subject_;
    State state_;
};

// Why a flyout opened or closed (DockFlyoutChange; owner decision DK-27,
// docking study 5.5).
enum class DockFlyoutCause : std::uint8_t {
    // Opening: the pointer rested on a side tab for the hover delay.
    hover,
    // Opening: a side tab was clicked. Closing: the caption's close or pin.
    pointer,
    // Opening: Enter or Space on a side tab, or the tab's accessibility
    // action.
    keyboard,
    // open_flyout and close_flyout, or activating the panel.
    programmatic,
    // Closing: the pointer left a flyout that does not have focus, for the
    // leave delay.
    pointer_left,
    // Closing: a press outside the flyout and the side strips (it goes on to
    // what was pressed).
    outside_press,
    // Closing: focus left a flyout that had it.
    focus_lost,
    // Closing: Escape inside the flyout; focus returns to where it was.
    escape,
    // Closing: another side tab's flyout replaced it.
    swapped,
    // Closing: the layout changed so that the panel is no longer behind a
    // side tab (docked back, hidden, closed, or its collapsed stack has room
    // again).
    layout,
};

// Raised after a flyout opened (open true) or closed.
struct DockFlyoutChange final {
    DockPanelId panel{};
    bool open{};
    DockFlyoutCause cause{DockFlyoutCause::programmatic};
};

// One site of a docking layout in one window (stage DK phase D2; owner
// decisions DK-18 to DK-23, DK-28 to DK-30, DK-41): binary splits separated
// by seams, tool stacks with captions and, in the main site, the one
// document area with its document stacks, each stack a PageView over the
// stack's panels. A site realizes either the main site (the first
// constructor) or one floating site (the second; phase D4, normally made by
// the workspace as the root of that site's floating window).
//
// The site borrows its workspace: the workspace must outlive it, or be
// destroyed first, which unbinds the site (it then shows nothing). The site
// realizes its tree after every layout change (the workspace tells it): one
// PageView per stack (`<site id>.stack.<node id>`), reused while the stack
// keeps its node identity, its pages the stack's panels in order with the
// stack's selection; tool stacks show their tabs at the bottom only when
// they hold two or more panels, document stacks at the top. A panel's content is its
// workspace control, created when the panel is first shown selected (lazy
// creation).
//
// A floating site (phase D4; owner decisions DK-1, DK-6 to DK-12, DK-41):
// no document area, side strips, collapsing, maximizing or window-edge
// targets; it shows nothing once its site leaves the layout. Its tool
// stacks keep their drawn captions (the one-stack floating window's caption
// of T6-3; the host's title bar stays too, since no host offers a frameless
// owned window yet). A double-click on a floating caption docks the stack
// back where its panel was docked last. While the site holds exactly one
// tool stack, dragging its caption moves the window itself with the
// framework's capture drag (WindowFrameDrag, decision 12) where the host
// supports it, the targets of the window under the pointer showing as for
// any drag; elsewhere the caption drags the stack as in the main site. Keys
// a floating site's window does not take go to the main window's
// accelerators.
//
// Drags across windows (phase D4; decision 41, T8-3): while the workspace
// has windows attached, a drag follows the desktop pointer: the topmost
// site under it (ApplicationWindows::window_at) shows the targets, tiles and
// tint, only over its hovered pane (its window edges too when it is the
// main site), and the stamp; the other sites show none. A release on a target
// in another window applies that target's edit, as one user transaction
// across the windows; a release where nothing accepts floats the subject
// into a new floating window at the pointer, the panel's size, when it may
// float (DockWorkspace::can_float), and snaps it back otherwise (the rejected
// cue shows only then).
//
// Auto-hide (stage DK phase D5; owner decisions DK-27, DK-28, DK-41, DK-42):
// each window edge holding auto-hidden panels shows a side strip, a PageStrip
// (`<site id>.side.<edge>`) with one tab per panel, the auto-hidden stacks in
// layout order and their panels in stack order; left and right strips turn
// their labels (bottom to top on the left, top to bottom on the right). The
// strips take their band from the site's edges, left and right the full
// height and top and bottom between them; the tree is laid out in what
// remains. A side tab opens its panel's flyout: an in-window overlay that
// slides out from the strip over the tree in `flyout_slide_duration`
// (linear, on the window's motion clock; at once under reduced motion or in
// an occluded window),
// casting the overlay shadow, holding the stack's caption (its pin unchecked
// for an auto-hidden stack), the panel and a grip on its inner edge. The
// flyout opens when the pointer rests on a tab for `flyout_hover_delay`
// (unless the workspace's flyout_opening is click), at once on a click, or
// on Enter or Space on a focused tab (each strip is a focus region; its
// arrow keys move without opening). Opened by click, keyboard or program,
// focus moves into the panel; opened by hover, focus stays where it was
// until the user clicks inside. A flyout without focus closes
// `flyout_leave_delay` after the pointer leaves it and its tab; one with
// focus stays until focus leaves it. A press outside the flyout and the
// strips closes it and goes on to what was pressed. Escape inside closes it
// and returns focus to the control that had it before (not a side strip),
// else to the active document. The grip resizes the flyout within the tree's
// extent; the new extent lasts for the session (the layout is not edited).
// Opening, resizing and closing a flyout never edit the layout: a group's
// side tab shows that panel without changing the stack's selection.
//
// The pin of a root-level tool stack's caption auto-hides the whole stack
// (E4-1) as one user transaction, onto the window edge nearest the stack,
// with the stack's extent across that edge; the pin of a flyout of an
// auto-hidden stack returns the stack to where it stood (DockEdit::unpin),
// so a pin and an unpin with nothing between leave the layout exactly as it
// was.
//
// Parts: seams (item system `seam`), 3 units, with a grip of dots, between
// the two children of each split; a seam is dragged with the pointer or,
// while the site has focus, moved with the arrow keys (Tab and Shift+Tab
// step between seams), within both sides' minimums (DockLayout's solver).
// Each tool stack has a caption (item system `stack`): the identity key,
// the selected panel's title, the position menu, the pin (root-level items
// only, owner decision DK-28: checked while docked; pressing it auto-hides
// the stack) and close (which hides the panel). The caption of the stack holding the
// active panel is `current`: the look (a pale tint with a 2-unit accent line
// on top, decision DK-41) is the default theme's (T4); until then it paints
// through paint_part with today's recipes.
//
// Shrinking: when the site is smaller than the tree's minimum, tool stacks
// collapse in increasing collapse priority (the largest priority of their
// panels; ties collapse the later stack in tree order first) until the rest
// fits (owner decision DK-23). A collapsed stack goes behind side tabs on
// the window edge nearest the place it would have in the tree, as if
// auto-hidden, and opens in a flyout like one; the layout model is unchanged
// (the stack stays docked, its flyout's pin checked: pressing it auto-hides
// the stack for good), and the stack returns to the tree when room returns.
// The side strips' bands are taken before the collapse is planned, so a
// collapse that adds a strip is planned again with that band. A stack whose
// collapse would not reduce the shortfall is kept. The maximized document
// area hides the tool stacks in place, without side tabs.
//
// Keyboard (owner decision DK-30), while focus is in the site: F6 and
// Shift+F6 move between the stacks (each a focus region of the window, in
// tree order) and then the side strips (left, top, right, bottom); Ctrl+Tab
// and Ctrl+Shift+Tab open the navigator on the documents, Alt+F7 and Alt+Shift+F7 on the tool panels, and releasing the
// modifier activates the current row; Ctrl+F6 and Ctrl+Shift+F6 activate the
// next and previous document in most-recently-used order (DK-46); Escape
// in a tool panel (unless its content used it) returns to the active
// document, Shift+Escape hides
// the active tool panel; Ctrl+Shift+F12 toggles the maximized document area
// (every tool stack hidden, as by collapsing, and restored); Alt+Hyphen,
// Shift+F10 and the context-menu key open the active panel's position menu.
// A focused TabControl keeps Ctrl+Tab (PageView's Ctrl+Tab policy is `pass`
// inside the site). Every key but F6 is a named keyboard command
// (KeyCommand, G6-1), so the window's key map, as the host reports it and
// the application rebinds it, decides the chords; the navigator commits on
// the release of the chord's Ctrl, Alt or Meta.
//
// Activation: focus entering a stack (a click, Tab, F6, a tab chosen)
// makes its selected panel the workspace's active panel; a press on a
// caption activates it too.
//
// Semantics: each stack is a `pane` named by its selected panel (the
// document stacks "Documents"), holding the PageView's tab list and tab
// panel and its caption buttons; each seam is a `split_pane` with its
// position as the value and increment, decrement and set_value. A side strip
// is a tab list (vertical on the left and right) named "Auto-hidden panels";
// an open flyout's stack is a `pane` named by its panel, with the caption's
// buttons (the pin a check box, checked while the stack is docked).
//
// Dragging (phase D3; owner decisions DK-24 to DK-26, DK-30, DK-41): a press
// on a caption that moves the drag threshold (4 units, D5-1) drags the whole
// stack; a tab dragged off its strip drags one panel (D7-1). A tool stack's
// strip does not reorder, so its tab drag is claimed at once
// (page_drag_started); a document strip reorders by its live slide until the
// pointer leaves the strip's band, and then the panel tears off. At the
// start the subject leaves its place and the place closes up in one layout
// pass of the site; the layout model is unchanged until the drop (docking
// study 5.1, invariant 4), so Escape leaves it as it was. A miniature of the
// panel (its caption strip by role over a picture of its view, taken with
// Window::snapshot_control as the drag starts; the content well where the
// window cannot snapshot) is interpolated from the panel's rectangle down to a stamp about 100 units
// long over 220 ms (smoothstep, on the window's motion clock) and follows
// the pointer; at the drop it grows into the panel's new place, on Escape
// back into its old one; reduced motion and an occluded window skip the
// motion. Targets (DockDropTarget, planned when the drag starts and at
// every layout pass): over the hovered pane only, a cross of five quiet
// tiles (top, left, tab into, right, bottom) appears, fading in over 100 ms;
// the pointer's position within the pane (or the tile under it) selects
// one, which is lit; the main window's edges are global targets with a tile
// at each edge's middle; the area the subject would take is tinted. Targets
// the restrictions refuse are not planned and their tiles are disabled.
// Ctrl held suppresses docking. A release on a target applies one user
// transaction and activates the moved panel; a release elsewhere snaps
// back. Escape, losing the pointer capture, the window deactivating and a
// layout change by the program cancel.
//
// The keyboard move mode (G3-1, docking study 5.7.1): the position menu's
// Move or the move_panel key command (Ctrl+Alt+Shift+M) on the active
// panel. The arrows step through the current pane's tiles (Left and Right
// along left, centre, right; Up and Down along top, centre, bottom), Tab and
// Shift+Tab to the next and previous pane and then the window edges, over
// exactly the targets the pointer reaches; each step updates the preview and
// the announcement (drag_announcement, also the name of the site's live
// region, "<site id>.announcements"). Enter commits through the same
// transaction as a drop; Escape cancels.
class DockSite : public Control, public DockSiteCapabilities<DockSite> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // Borrows `workspace` (see the lifetime rule above). Realizes the main
    // site.
    DockSite(StableId stable_id, DockWorkspace& workspace);
    // Realizes the floating site `floating_site` (phase D4). A second site
    // made for the same floating site replaces the first in the workspace.
    DockSite(StableId stable_id, DockWorkspace& workspace, DockSiteId floating_site);
    ~DockSite() override;
    // Realizes the workspace's layout. make_control calls it; do not call it
    // yourself.
    void initialize_control_tree();

    // The bound workspace, or null after release_workspace.
    [[nodiscard]] DockWorkspace* workspace() const noexcept { return workspace_; }
    // Whether the site realizes a floating site, and which (none for the
    // main site).
    [[nodiscard]] bool floating() const noexcept { return !(floating_site_ == DockSiteId{}); }
    [[nodiscard]] DockSiteId floating_site() const noexcept { return floating_site_; }
    // Unbinds the workspace: the site removes its stacks and shows nothing.
    void release_workspace() noexcept;

    // The maximized document area (Ctrl+Shift+F12): every tool stack hidden
    // in place, as by collapsing, until it is restored. Focus in a tool
    // panel moves to the active document.
    [[nodiscard]] bool document_area_maximized() const noexcept { return maximized_; }
    void set_document_area_maximized(bool maximized);

    // The PageView realizing a stack of this site, or null.
    [[nodiscard]] std::shared_ptr<PageView> stack_view(DockNodeId stack) const;
    // The realized stacks in tree order, collapsed ones included, then the
    // auto-hidden stacks in layout order.
    [[nodiscard]] std::vector<DockNodeId> realized_stacks() const;
    // The tool stacks hidden by shrinking or by the maximized document area,
    // after the last layout pass, in the order they collapsed.
    [[nodiscard]] std::vector<DockNodeId> collapsed_stacks() const;
    // The tree's minimum size with nothing collapsed, from the panels'
    // minimums, the captions, the tab strips and the seams.
    [[nodiscard]] Size minimum_size() const;

    // Focuses the site with the keyboard on a seam. False when there is no
    // such seam or the site cannot take focus.
    bool focus_seam(std::size_t seam);
    // The seam the keyboard is on while the site has focus, or none.
    [[nodiscard]] std::optional<std::size_t> focused_seam() const noexcept;
    // Moves a seam by `delta` logical units along its split's axis, within
    // both sides' minimums, as a user edit. False when it cannot move.
    bool move_seam(std::size_t seam, double delta);

    // The position menu of a panel docked in the main site, anchored at its
    // caption's menu button (a tool stack) or below its tab strip (a
    // document stack). False, opening nothing, for any other panel or a
    // detached site. Throws std::invalid_argument for an unknown mode.
    bool show_position_menu(const DockPanelId& panel, MenuOpenMode mode = MenuOpenMode::keyboard);
    void close_position_menu() noexcept;
    [[nodiscard]] bool position_menu_open() const noexcept;
    // Whether a position command is available for a panel now, and runs
    // it, as the menu does. Running an unavailable command is refused
    // (false).
    [[nodiscard]] bool position_command_enabled(const DockPanelId& panel, DockPositionCommand command) const;
    bool run_position_command(const DockPanelId& panel, DockPositionCommand command);

    // The navigator overlay, centred over the site, on `list` with the row
    // after the active one current (`direction` +1) or the last (-1);
    // releasing `release` (control or alt; none for Enter only) chooses.
    // False when both lists are empty or the site is detached.
    bool open_navigator(DockNavigatorList list, int direction, Modifier release);
    void close_navigator() noexcept;
    [[nodiscard]] bool navigator_open() const noexcept { return navigator_open_; }
    // The open navigator, or null.
    [[nodiscard]] std::shared_ptr<DockNavigator> navigator() const noexcept;

    // Activates a document by most-recently-used order (owner decision
    // DK-46): +1 the document used before the active one, -1 the one used
    // longest ago (documents never active come last, in tab order),
    // wrapping. False when there is no document. Ctrl+F6 and Ctrl+Shift+F6
    // step on along one order while their modifier is held.
    bool activate_relative_document(int direction);

    // -- dragging (phase D3) --
    [[nodiscard]] DockDragPhase drag_phase() const noexcept { return drag_.phase; }
    // The subject of the drag under way (armed or dragging), or none.
    [[nodiscard]] std::optional<DockSubject> drag_subject() const;
    // While dragging, or while this site shows another site's drag (the
    // pointer over its window): the planned targets in keyboard order, the
    // panes they were planned over (the layout with the subject's place
    // closed up), and the selected target, or none (between targets, over a
    // refused zone, outside the site, or with Ctrl held).
    [[nodiscard]] std::span<const DockDropTarget> drag_targets() const noexcept;
    [[nodiscard]] std::span<const DockDropPane> drag_panes() const noexcept;
    [[nodiscard]] std::optional<DockDropTarget> drag_target() const;
    // Whether this site shows a drag that runs in another site (phase D4).
    [[nodiscard]] bool showing_remote_drag() const noexcept { return drag_.guest; }
    // While a floating window is moved by its caption (phase D4).
    [[nodiscard]] bool moving_window() const noexcept { return window_move_.active; }
    // Whether Ctrl suppresses docking now (D6-1).
    [[nodiscard]] bool drag_suppressed() const noexcept { return drag_.suppressed; }
    // The stamp's rectangle in the site's coordinates while dragging or
    // settling, else empty.
    [[nodiscard]] Rect drag_stamp() const noexcept;
    // What the drag announces now ("Output: dock right of Properties"), or
    // empty while idle.
    [[nodiscard]] const std::string& drag_announcement() const noexcept { return drag_.announcement; }
    // Starts the keyboard move mode for a panel docked in this site, on the
    // first planned target. False, starting nothing, for any other panel,
    // while a drag runs, or when there is no target.
    bool begin_keyboard_move(const DockPanelId& panel);
    // Ends a drag as Escape does. False when none runs.
    bool cancel_drag();
    // Raised once when a drag that left the armed phase ends, after the
    // layout transaction of a drop.
    [[nodiscard]] Event<const DockDragOutcome&>& drag_ended() noexcept { return drag_ended_; }
    // -- auto-hide (phase D5) --
    // The pointer's rest on a side tab before its flyout opens, the leave
    // delay before an unfocused flyout closes, and the slide's duration
    // (owner decisions DK-27 and DK-41; docking study 5.5).
    static constexpr std::chrono::milliseconds flyout_hover_delay{400};
    static constexpr std::chrono::milliseconds flyout_leave_delay{500};
    static constexpr std::chrono::milliseconds flyout_slide_duration{180};
    // The side strip on an edge, or null while that edge has no side tabs.
    [[nodiscard]] std::shared_ptr<PageStrip> side_strip(DockSide edge) const;
    // The panels behind side tabs on an edge, in tab order: auto-hidden
    // stacks', then collapsed stacks'.
    [[nodiscard]] std::vector<DockPanelId> side_panels(DockSide edge) const;
    // Opens the flyout of a panel behind a side tab (closing another
    // flyout first, cause swapped). Focus moves into the panel unless the
    // cause is hover. True when the flyout is open for the panel afterwards;
    // false, changing nothing, for a panel behind no side tab or a detached
    // site.
    bool open_flyout(const DockPanelId& panel, DockFlyoutCause cause = DockFlyoutCause::programmatic);
    void close_flyout(DockFlyoutCause cause = DockFlyoutCause::programmatic) noexcept;
    [[nodiscard]] bool flyout_open() const noexcept { return flyout_panel_.has_value(); }
    // The panel the flyout shows, or none.
    [[nodiscard]] const std::optional<DockPanelId>& flyout_panel() const noexcept { return flyout_panel_; }
    // How far the flyout has slid out, from 0 to 1 (1 while closed).
    [[nodiscard]] double flyout_reveal() const noexcept { return flyout_slide_.current(); }
    // The open flyout's extent across its edge, in logical units, or 0.
    [[nodiscard]] double flyout_extent() const;
    // Sets the open flyout's extent, as its grip does, within the tree's
    // extent; it lasts for the session. False when no flyout is open or the
    // extent is not finite.
    bool resize_flyout(double extent);
    // The flyout's root control while it is open (an overlay of the window),
    // or null.
    [[nodiscard]] Control::Ptr flyout_control() const noexcept;
    [[nodiscard]] Event<const DockFlyoutChange&>& flyout_changed() noexcept { return flyout_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer_preview(PointerEvent& event) override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_context_menu_requested(ContextMenuRequest& request) override;
    void on_key_bubble(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        // The document area's rectangle, its well showing while it is empty.
        PartGeometry document_area;
        PartGeometry focus;
        // The drag layer (present only while dragging or settling): the
        // stamp, its label, the tinted area the subject would take
        // (drop_accepted) and the cue at the pointer where nothing accepts
        // a drop (drop_rejected).
        PartGeometry drag_stamp;
        PartGeometry drag_stamp_label;
        // The panel's picture in the stamp, below its label, while the
        // window can snapshot it (Window::snapshot_control); absent where it
        // cannot, the content well standing in.
        PartGeometry drag_stamp_image;
        PartGeometry drop_area;
        PartGeometry drop_cue;
        // The open flyout, in the site's coordinates as it stands in its
        // slide: its overlay shadow, its face and the grip on its inner edge
        // (hot and captured as the pointer is on it or drags it). Absent
        // while no flyout is open.
        PartGeometry flyout_shadow;
        PartGeometry flyout_face;
        PartGeometry flyout_grip;
    };
    [[nodiscard]] Parts parts() const;
    // Item system seam: the bar between a split's two children and its grip.
    struct SeamParts final {
        PartGeometry seam_bar;
        PartGeometry seam_grip;
    };
    struct SeamStates final {
        bool hot{};
        bool captured{};
        bool focus_visible{};
        bool disabled{};
    };
    struct Seam final {
        DockNodeId split{};
        DockAxis axis{DockAxis::horizontal};
        std::string stable_id;
        SeamParts parts;
        SeamStates states;
    };
    // Item system stack: a stack's area and, for a tool stack, its caption
    // and the caption's parts (absent for a document stack). The actuators
    // carry their own hot, pressed, open, checked and disabled states.
    struct StackParts final {
        PartGeometry stack_area;
        PartGeometry caption;
        PartGeometry caption_accent;
        PartGeometry caption_key;
        PartGeometry caption_label;
        // The dotted grip between the title and the buttons (cp29), painted
        // in every theme; dock-site::part(caption_grip) { muted-color:
        // transparent; } hides it.
        PartGeometry caption_grip;
        PartGeometry caption_menu;
        PartGeometry caption_menu_glyph;
        PartGeometry caption_pin;
        PartGeometry caption_pin_glyph;
        PartGeometry caption_close;
        PartGeometry caption_close_glyph;
    };
    struct StackStates final {
        // The stack holds the active panel.
        bool current{};
        bool disabled{};
    };
    struct Stack final {
        DockNodeId stack{};
        DockNodeKind kind{DockNodeKind::tool_stack};
        // The stack's selected panel.
        DockPanelId panel{};
        std::string stable_id;
        StackParts parts;
        StackStates states;
    };
    // Item system drop_tile: a target tile and its dock-shape glyph, over
    // the hovered pane (five) and at the window's edges (four), while
    // dragging. `selected` is the lit tile; `disabled` a choice the
    // restrictions refuse; `expanding` while the cross fades in.
    struct TileParts final {
        PartGeometry tile;
        PartGeometry tile_glyph;
    };
    struct TileStates final {
        bool selected{};
        bool disabled{};
        bool expanding{};
    };
    struct Tile final {
        DockDropTargetKind kind{DockDropTargetKind::pane};
        DockNodeId pane{};
        DockDropZone zone{DockDropZone::center};
        std::string stable_id;
        TileParts parts;
        TileStates states;
    };
    // The seams, then the stacks that are not collapsed, in tree order, then
    // the open flyout's stack (its caption in the site's coordinates); then,
    // while dragging, the tiles (the hovered pane's cross, then the edges).
    [[nodiscard]] std::vector<Seam> realized_seams() const;
    [[nodiscard]] std::vector<Stack> realized_stack_items() const;
    [[nodiscard]] std::vector<Tile> realized_tiles() const;

    // The semantic identities: `<site id>.seam.<split id>` for a seam,
    // `<site id>.stack.<node id>` for a stack's view and
    // `<view id>.menu`, `.pin` and `.close` for its caption buttons.
    [[nodiscard]] std::string seam_identity(DockNodeId split) const;
    [[nodiscard]] std::string stack_identity(DockNodeId stack) const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attachment_committed() noexcept override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    friend class DockWorkspace;
    class StackView;
    class DragLayer;
    class Flyout;
    struct HoverElapsed;
    struct LeaveElapsed;
    struct StaleFlyout;

    // What the pointer or a press is on; grip only in the flyout.
    enum class Zone : std::uint8_t { none, seam, caption, menu, pin, close, grip };
    struct Hit final {
        Zone zone{Zone::none};
        // The seam's index for Zone::seam; the stack's index in stacks_ for
        // the caption zones.
        std::size_t index{};

        friend bool operator==(const Hit& left, const Hit& right) noexcept {
            const bool equal = left.zone == right.zone && left.index == right.index;
            return equal;
        }
        friend bool operator!=(const Hit& left, const Hit& right) noexcept {
            const bool different = !(left == right);
            return different;
        }
    };
    // One realized stack and its view. `collapsed` is the last layout pass's
    // decision (always set for an auto-hidden stack, which has no place in
    // the tree); `bounds` and `caption` are its rectangles there, or, while
    // the stack is in the flyout, the flyout's in the site's coordinates.
    // `side_tabbed`: collapsed by shrinking, so behind side tabs on `edge`;
    // an auto-hidden stack is always behind side tabs on its entry's edge.
    // `in_flyout`: the view is the flyout's child, not the site's.
    struct StackSlot final {
        DockNodeId stack{};
        DockNodeKind kind{DockNodeKind::tool_stack};
        std::shared_ptr<StackView> view{};
        // The selected panel's title, kept for painting the caption.
        std::string title{};
        bool collapsed{};
        bool auto_hidden{};
        bool side_tabbed{};
        bool in_flyout{};
        DockSide edge{DockSide::left};
        Rect bounds{};
        Rect caption{};
    };
    // One window edge's side tabs: the strip's own page set (owned here and
    // released from the strip before it goes), the strip, its band in the
    // site's coordinates (empty while the edge has no tabs) and the panels
    // it shows, in order.
    struct SideStrip final {
        std::unique_ptr<PageSet> pages{};
        std::shared_ptr<PageStrip> strip{};
        Rect band{};
        std::vector<DockPanelId> panels{};
    };
    // A flyout extent set by the grip, for the rest of the session.
    struct FlyoutExtent final {
        DockNodeId stack{};
        double extent{};
    };
    // The open flyout's rectangles in the site's coordinates: `full` is the
    // whole flyout where it stands in its slide (it starts under the strip
    // and moves out), `revealed` the part past the strip's band, which is the
    // overlay's bounds; caption, content and grip lie inside `full`.
    struct FlyoutGeometry final {
        Rect full{};
        Rect revealed{};
        Rect caption{};
        Rect content{};
        Rect grip{};
    };
    // One seam from the last layout pass: the split, its axis, the bar and
    // its wider hit band, and what a move needs (the span shared by the two
    // children after the seam, where it starts, and both minimums).
    struct SeamSlot final {
        DockNodeId split{};
        DockAxis axis{DockAxis::horizontal};
        Rect bar{};
        Rect hit{};
        double span_start{};
        double span{};
        double first{};
        double first_minimum{};
        double second_minimum{};
    };
    // A subtree's minimum size; `present` is false when every stack in it is
    // collapsed (it then takes no space).
    struct Extent final {
        bool present{};
        Size minimum{};
    };
    struct CaptionGeometry final {
        Rect caption{};
        Rect accent{};
        Rect key{};
        Rect label{};
        Rect menu{};
        Rect menu_glyph{};
        Rect pin{};
        Rect pin_glyph{};
        Rect close{};
        Rect close_glyph{};
    };

    // -- realization --
    void realize();
    void remove_slot(StackSlot& slot) noexcept;
    void sync_pages(StackSlot& slot, const DockNode& node);
    void supply_content(StackSlot& slot);
    void update_focus_regions();
    void on_layout_changed(const DockLayoutChange& change);
    // The views' events, bound to the stack's node identity.
    void on_view_selection(DockNodeId stack, const PageSelectionChange& change);
    void on_view_content(DockNodeId stack, const PageContentRequest& request);
    void on_view_close(DockNodeId stack, PageCloseRequest& request);
    void on_activation_changed(const DockActivationChange& change);
    void on_focus_moved(const FocusedControlChange& change);
    [[nodiscard]] StackSlot* slot_for(DockNodeId stack) noexcept;
    [[nodiscard]] const StackSlot* slot_for(DockNodeId stack) const noexcept;
    // The index in stacks_ of the stack whose view is or contains `control`,
    // or stacks_.size().
    [[nodiscard]] std::size_t slot_index_of(const Control& control) const;
    [[nodiscard]] std::optional<DockPanelId> selected_panel(const StackSlot& slot) const;

    // -- layout --
    // The stages of arrange (one layout pass), in order.
    struct PreviousPass final {
        Rect document_area{};
        Rect tree_area{};
    };
    [[nodiscard]] PreviousPass begin_layout_pass();
    void hold_out_dragged_page();
    [[nodiscard]] Rect plan_bands_and_collapse(Size surface, std::array<bool, 4>& edges, std::array<Rect, 4>& bands);
    void arrange_stack_views();
    void arrange_side_strips(const std::array<bool, 4>& edges, const std::array<Rect, 4>& bands);
    void follow_flyout_after_pass();
    void finish_layout_pass(const PreviousPass& previous, Size surface, bool focus_was_inside);
    [[nodiscard]] Extent extent_of(DockNodeId node, std::span<const DockNodeId> collapsed) const;
    [[nodiscard]] Size stack_minimum(const DockNode& node) const;
    [[nodiscard]] std::vector<DockNodeId> plan_collapse(Size available) const;
    void place(DockNodeId node, Rect bounds, std::span<const DockNodeId> collapsed);
    [[nodiscard]] CaptionGeometry caption_geometry(Rect caption, bool pin, bool close) const;
    // The caption grip's box in `placed`: the label's free run after the
    // title's measured width, a gap in, the grip's height centred on the
    // caption; empty where the run is shorter than one dot pitch.
    [[nodiscard]] Rect caption_grip(const StackSlot& slot, const CaptionGeometry& placed) const;
    [[nodiscard]] bool seams_moved() const noexcept;
    [[nodiscard]] double caption_height() const;

    // -- input and commands --
    [[nodiscard]] Hit hit_at(Point local) const;
    void set_hot(Hit hit);
    void begin_seam_drag(std::size_t seam, Point local);
    void continue_seam_drag(Point local);
    bool set_seam_first(std::size_t seam, double first);
    bool handle_docking_key(KeyEvent& event);
    void pass_key_to_main_window(KeyEvent& event);
    void collect_document_steps(std::vector<DockPanelId>& out) const;
    bool step_document(int direction, Modifier release);
    void end_document_steps(const KeyEvent& event) noexcept;
    bool handle_seam_key(KeyEvent& event);
    [[nodiscard]] std::optional<DockPanelId> focused_panel() const;
    [[nodiscard]] bool focus_in_tool_stack() const;
    void focus_panel(const DockPanelId& panel);
    void refocus_if_lost(bool focus_was_inside);
    void on_menu_item_invoked(const MenuItemInvocation& invocation);
    void on_menu_open_changed(bool open);
    void on_navigator_chosen(const DockNavigatorChoice& choice);
    void on_navigator_cancelled();
    void on_navigator_revoked();
    void on_window_pointer(const PointerEvent& event);

    // -- paint and states --
    [[nodiscard]] AnatomyStateSet seam_states(std::size_t seam) const;
    [[nodiscard]] AnatomyStateSet stack_states(const StackSlot& slot) const;
    [[nodiscard]] AnatomyStateSet button_states(std::size_t stack, Zone zone) const;
    [[nodiscard]] bool stack_current(const StackSlot& slot) const;
    [[nodiscard]] bool pin_shown(const StackSlot& slot) const;
    // Whether the stack's selected panel shows a close button
    // (DockPanelDescriptor::close_button_visible).
    [[nodiscard]] bool close_shown(const StackSlot& slot) const;
    // Whether a document panel's tab shows its close button.
    [[nodiscard]] bool tab_closable(const DockPanelId& panel) const;
    [[nodiscard]] SeamParts seam_parts(std::size_t seam) const;
    [[nodiscard]] StackParts stack_parts(const StackSlot& slot, std::size_t index) const;
    [[nodiscard]] std::size_t visible_stack_count() const;
    [[nodiscard]] std::size_t visible_stack_at(std::size_t ordinal) const;
    [[nodiscard]] FontSpec caption_font() const;
    [[nodiscard]] bool focus_cue() const noexcept;

    [[nodiscard]] std::vector<SemanticNode> caption_nodes(const StackSlot& slot, std::size_t index) const;
    bool caption_action(std::size_t index, std::string_view stable_id, SemanticAction action);
    void paint_caption(Painter& painter, const Control& surface, const StackSlot& slot, std::size_t index,
                       Point offset) const;
    // Runs a caption button of the stack at `index` (pin or close).
    void press_caption_button(std::size_t index, Zone zone);

    // -- auto-hide --
    [[nodiscard]] SideStrip& side(DockSide edge) noexcept;
    [[nodiscard]] const SideStrip& side(DockSide edge) const noexcept;
    // Fills `out` (cleared first) with the panels behind side tabs on an
    // edge, in tab order (see side_panels).
    void collect_side_panels(DockSide edge, std::vector<DockPanelId>& out) const;
    void ensure_side_strip(DockSide edge);
    void release_side_strips() noexcept;
    // Brings each edge's strip in line with the slots behind side tabs.
    void sync_side_strips();
    [[nodiscard]] double side_band_thickness() const;
    // The tree's rectangle once the bands of the edges in `edges` (indexed
    // by DockSide) are taken from `surface`, and each band.
    [[nodiscard]] Rect tree_area(Size surface, const std::array<bool, 4>& edges,
                                 std::array<Rect, 4>& bands) const;
    // The edge nearest a rectangle inside the tree's rectangle.
    [[nodiscard]] static DockSide nearest_edge(Rect bounds, Rect area) noexcept;
    // Each collapsed tool stack's edge: where it would stand in the tree.
    void assign_collapsed_edges(Rect area, std::span<const DockNodeId> collapsed);
    [[nodiscard]] StackSlot* side_slot_of(const DockPanelId& panel);
    [[nodiscard]] std::size_t flyout_slot_index() const noexcept;
    [[nodiscard]] double flyout_default_extent(const StackSlot& slot) const;
    [[nodiscard]] FlyoutGeometry flyout_geometry() const;
    void place_flyout();
    void focus_into_flyout();
    [[nodiscard]] bool focus_in_flyout() const;
    void restore_focus_from_flyout();
    void on_side_tab_invoked(DockSide edge, const PageInvocation& invocation);
    void on_root_pointer(const PointerEvent& event);
    [[nodiscard]] bool side_tab_at(Point point, std::optional<DockPanelId>& tab) const;
    void close_on_outside_press(const PointerEvent& event);
    void on_flyout_pointer_preview(const PointerEvent& event);
    void on_flyout_pointer(PointerEvent& event);
    void on_flyout_key(KeyEvent& event);
    void on_flyout_revoked();
    void paint_flyout(Painter& painter, const Control& surface) const;
    [[nodiscard]] Hit flyout_hit(Point local) const;
    void start_hover(const DockPanelId& panel);
    void stop_hover() noexcept;
    void start_leave();
    // The slide's steps on the window's motion clock: the flyout moves.
    void on_slide_motion(const MotionStep& step);
    void hover_elapsed(const DockPanelId& panel);
    void leave_elapsed();
    void stale_flyout_elapsed();
    // Closes, once the layout pass is over, a flyout whose stack is no
    // longer behind side tabs.
    void schedule_stale_check();
    // Takes the slot's view into the flyout or gives it back.
    void move_view_to_flyout(StackSlot& slot);
    // The nearest theme override on this site or its ancestors (null when
    // none): the flyout, an overlay root outside the site's tree, takes it so
    // it is drawn with the theme the site is.
    [[nodiscard]] std::shared_ptr<const Theme> inherited_theme_override() const;
    void move_view_to_site(StackSlot& slot);
    // Puts a view into the flyout (Flyout is complete only in
    // dock_site_auto_hide.cpp).
    void flyout_adopt(const Control::Ptr& view);
    // Lets go of the flyout control, which stops reaching back.
    void release_flyout_control() noexcept;

    // -- dragging (dock_site_drag.cpp) --
    // One tile as painted and published: where it is, which target it
    // stands for, and whether it is lit or refused.
    struct TileSlot final {
        DockDropTargetKind kind{DockDropTargetKind::pane};
        DockNodeId pane{};
        DockDropZone zone{DockDropZone::center};
        Rect bounds{};
        bool lit{};
        bool refused{};
    };
    // The drag session. `panel_only`: a tab drag of one panel out of a stack
    // that keeps others (its page leaves the view); otherwise the whole
    // source stack leaves the layout pass. `grab` is where the pointer holds
    // the source, as fractions of `source`, the subject's rectangle when the
    // drag started. `panes` and `targets` are planned at the start and at
    // every layout pass; `target` and `hovered_pane` index them.
    struct DragSession final {
        DockDragPhase phase{DockDragPhase::idle};
        DockSubject subject{};
        DockNodeId source_stack{};
        bool panel_only{};
        bool keyboard{};
        bool suppressed{};
        bool over_site{};
        std::uint64_t pointer_id{1U};
        Point press{};
        Point pointer{};
        Point grab{};
        Rect source{};
        std::string title{};
        std::uint64_t generation{};
        std::vector<DockDropPane> panes{};
        std::vector<DockDropTarget> targets{};
        std::optional<std::size_t> target{};
        std::optional<std::size_t> hovered_pane{};
        // A panel_only drag's page has left its view (done at the first
        // layout pass, never inside the strip's own drag event).
        bool page_held_out{};
        Rect stamp{};
        // Settling: from the stamp where the drag ended to the panel's
        // rectangle, which the next layout pass finds (`settle_panel`).
        DockPanelId settle_panel{};
        bool settle_found{};
        Rect settle_from{};
        Rect settle_to{};
        std::string announcement{};
        // The panel's picture, taken as the drag starts (the stamp's image;
        // none where the window cannot snapshot), and released when the
        // session ends (reset_drag) by the site that took it. Images are
        // application-scope, so a guest shows the same image.
        std::optional<ControlSnapshot> picture{};
        // Phase D4. `guest`: this site shows another site's drag (no capture,
        // no picture of its own, never ends it). `stamp_hidden`: no stamp (a
        // window moved by its caption is its own picture). `may_float`: a
        // release where nothing accepts floats the subject. `remote`: the
        // site whose window is under the pointer, while that is another
        // site, observed.
        bool guest{};
        bool stamp_hidden{};
        bool may_float{};
        std::weak_ptr<Control> remote{};
    };
    // Ends the session: its picture's image is released (unless a guest's),
    // then the session is cleared.
    void reset_drag() noexcept;
    // -- across windows (phase D4) --
    // The desktop point of a point in this site, or none without frames.
    [[nodiscard]] std::optional<Point> to_desktop(Point local) const;
    // The site under the desktop pointer for a drag of this site (`excluded`
    // skipped), or null; this site itself when the pointer is over it.
    [[nodiscard]] DockSite* remote_site(Point desktop, const DockSite* excluded) const;
    [[nodiscard]] DockSite* remote_guest() const noexcept;
    // Shows, follows and stops showing another site's drag. The guest's
    // stamp moves as `source_motion` does (the source site's stamp), or
    // starts its own shrink when that is null.
    void show_guest_drag(const DragSession& source, const MotionValue* source_motion, Point local,
                         Modifier modifiers);
    void leave_guest_drag() noexcept;
    // Moves the drag's targets to the site under `desktop` (another site,
    // this one, or none).
    void route_drag(Point desktop, Modifier modifiers, const DockSite* excluded);
    // The floating frame for a subject released at `local`: the source's
    // size, held where the pointer grabbed it.
    [[nodiscard]] std::optional<Rect> float_frame_at(Point local) const;
    // A floating site's caption drag that moves its window (WindowFrameDrag).
    struct WindowMove final {
        bool active{};
        WindowFrameDrag drag{};
        DockSubject subject{};
        std::uint64_t pointer_id{1U};
        Point grab{};
        std::weak_ptr<Control> remote{};
    };
    [[nodiscard]] bool moves_window_by_caption() const;
    bool begin_window_move(std::uint64_t pointer_id);
    void continue_window_move(Modifier modifiers);
    void end_window_move(bool cancelled);
    void leave_window_move_guest() noexcept;
    [[nodiscard]] static std::uint64_t now_nanoseconds() noexcept;
    // The tree this site lays out: the main root or the floating site's root
    // (none once that site left the layout).
    [[nodiscard]] DockNodeId site_root() const;

    void create_drag_layer();
    // Whether a tab drag holds `panel` out of `stack`'s view.
    [[nodiscard]] bool dragged_out(DockNodeId stack, const DockPanelId& panel) const noexcept;
    void arm_caption_drag(std::size_t stack, Point local, std::uint64_t pointer_id);
    void on_tab_drag(DockNodeId stack, PageDragStart& start);
    bool tear_off_document_tab(const PointerEvent& event);
    // `press` is where the subject was grabbed and `pointer` where the
    // pointer is now, both in the site's coordinates.
    bool begin_drag(const DockSubject& subject, DockNodeId source_stack, bool panel_only, Point press,
                    Point pointer, bool keyboard, std::uint64_t pointer_id);
    // Posted when the capture is released during a drag: a primary release
    // releases it just before the drop, so only a drag still running after
    // that dispatch lost its capture.
    struct CaptureCheck;
    void check_capture_after_release();
    [[nodiscard]] Rect drag_paint_extent() const;
    void plan_drag_targets();
    void update_drag_pointer(Point local, Modifier modifiers);
    void select_drag_target(std::optional<std::size_t> target, std::optional<std::size_t> hovered_pane);
    void end_drag(DockDragEnd end);
    void finish_settling() noexcept;
    // The stamp's and the tiles' steps on the window's motion clock.
    void on_drag_motion(const MotionStep& step);
    void place_settle_target();
    [[nodiscard]] bool hidden_by_drag(DockNodeId stack) const noexcept;
    [[nodiscard]] bool motion_reduced() const noexcept;
    // The hovered pane's cross is fading in (T2-4, about 100 ms).
    [[nodiscard]] bool tiles_fading() const noexcept { return !tile_fade_.settled(); }
    // The stamp where the shrink stands now (stamp_motion_).
    [[nodiscard]] Rect stamp_at() const;
    [[nodiscard]] Rect drag_label_bounds() const noexcept;
    [[nodiscard]] Rect drag_image_bounds() const noexcept;
    [[nodiscard]] Rect drop_cue_bounds() const noexcept;
    [[nodiscard]] std::size_t tile_count() const noexcept;
    [[nodiscard]] TileSlot tile_at(std::size_t tile) const;
    // A tile's glyph: the tile less a fifth of its side all round.
    [[nodiscard]] static Rect tile_glyph_bounds(Rect tile) noexcept;
    [[nodiscard]] AnatomyStateSet tile_states(const TileSlot& tile) const;
    [[nodiscard]] std::string tile_identity(const TileSlot& tile) const;
    [[nodiscard]] std::string describe_target(const DockDropTarget& target) const;
    void announce_drag();
    bool handle_drag_key(KeyEvent& event);
    void step_keyboard_target(int direction);
    void step_keyboard_zone(std::uint32_t key);
    void paint_drag_layer(Painter& painter);
    void on_capture_changed(const PointerCaptureChange& change);
    void on_window_active(bool active);

    DockWorkspace* workspace_{};
    // The floating site realized (none: the main site).
    DockSiteId floating_site_{};
    WindowMove window_move_{};
    SubscriptionToken activation_token_{};
    SubscriptionToken focus_token_{};
    // The realized stacks in tree order.
    std::vector<StackSlot> stacks_{};
    // From the last layout pass.
    std::vector<SeamSlot> seams_{};
    std::vector<DockNodeId> collapsed_{};
    // The pass before, kept for its storage and to tell what moved.
    std::vector<SeamSlot> previous_seams_{};
    std::vector<DockNodeId> previous_collapsed_{};
    Rect document_area_{};
    bool maximized_{};
    // Set while the site edits its views' page sets, whose selection events
    // it then ignores.
    bool syncing_{};
    bool focused_{};
    Hit hot_{};
    Hit pressed_{};
    std::size_t keyboard_seam_{};
    // The seam drag: the seam and the pointer's offset from the first
    // child's end along the axis.
    bool seam_captured_{};
    std::size_t drag_seam_{};
    double drag_offset_{};
    // The window whose focus the site follows and whose regions it set.
    Window* followed_window_{};
    std::vector<Control::WeakPtr> registered_regions_{};
    // The control that last had focus in each panel, for activation.
    struct RememberedFocus final {
        DockPanelId panel{};
        Control::WeakPtr control{};
    };
    std::vector<RememberedFocus> remembered_focus_{};
    std::unique_ptr<ContextMenu> menu_{};
    SubscriptionToken menu_invoked_{};
    SubscriptionToken menu_changed_{};
    // One command per position row, in DockPositionCommand order.
    std::array<std::shared_ptr<Command>, 9> menu_commands_{};
    DockPanelId menu_panel_{};
    std::size_t menu_stack_{};
    bool menu_open_{};
    std::shared_ptr<DockNavigator> navigator_{};
    OverlayToken navigator_token_{};
    FocusScopeId navigator_scope_{};
    bool navigator_open_{};
    bool navigator_closing_{};
    SubscriptionToken navigator_chosen_{};
    SubscriptionToken navigator_cancelled_{};
    SubscriptionToken navigator_revoked_{};
    SubscriptionToken window_pointer_{};
    // The drag: its session, the layer that paints it above the stacks (a
    // child kept last), its motion and the window events that cancel it.
    // stamp_motion_ is the stamp's progress, 0 to 1 over 220 ms
    // (smoothstep): from the source's rectangle to the miniature while
    // dragging, and from where the drag ended to the panel's place while
    // settling. tile_fade_ runs 0 to 1 over 100 ms (linear) after the
    // hovered pane changes. Both are outside the session, which is replaced
    // whole, and both end with it (reset_drag).
    DragSession drag_{};
    std::shared_ptr<DragLayer> drag_layer_{};
    MotionValue stamp_motion_;
    MotionValue tile_fade_;
    DispatchOperation capture_check_{};
    SubscriptionToken capture_token_{};
    SubscriptionToken active_token_{};
    // drag_gesture::default_threshold, kept here so the public header needs
    // no private one.
    // A run of Ctrl+F6 steps: the order taken at its first press, the
    // position reached and the modifier whose release ends it.
    std::vector<DockPanelId> document_steps_{};
    std::size_t document_step_{};
    Modifier document_step_release_{Modifier::none};
    bool document_stepping_{};
    Event<const DockDragOutcome&> drag_ended_{};

    // Auto-hide: the side strips by DockSide, the tree's rectangle from the
    // last layout pass, and the flyout. The flyout control is the window's
    // overlay while open (flyout_token_) and observes the site, which
    // releases it before letting it go.
    std::array<SideStrip, 4> sides_{};
    Rect tree_area_{};
    std::shared_ptr<Flyout> flyout_{};
    // The same control as a Control, for the sources that see Flyout only
    // declared.
    Control::Ptr flyout_root_{};
    OverlayToken flyout_token_{};
    SubscriptionToken flyout_revoked_{};
    std::optional<DockPanelId> flyout_panel_{};
    DockNodeId flyout_stack_{};
    // How far the flyout has slid out: 0 to 1 over flyout_slide_duration
    // (linear), and 1 while closed.
    MotionValue flyout_slide_;
    FrameRequestToken hover_timer_{};
    DockPanelId hover_panel_{};
    FrameRequestToken leave_timer_{};
    FrameRequestToken stale_timer_{};
    bool pointer_in_flyout_{};
    bool flyout_focused_{};
    bool flyout_closing_{};
    // A side tab's press opened the flyout: the strip's focus that follows
    // is sent on into it, until the release.
    bool flyout_focus_pending_{};
    // The position menu was opened from the flyout's caption: focus in it
    // does not count as leaving the flyout.
    bool menu_from_flyout_{};
    // The grip drag: the extent and the pointer where it started.
    bool grip_captured_{};
    double grip_start_extent_{};
    Point grip_origin_{};
    // Kept for its storage: the tree arranged without collapsing, to find
    // the edge of each collapsed stack.
    std::vector<DockArrangedNode> arranged_scratch_{};
    // The last control focused outside the flyout and the side strips, and
    // the one focus returns to when the flyout closes with focus inside.
    Control::WeakPtr focus_outside_{};
    Control::WeakPtr return_focus_{};
    std::vector<FlyoutExtent> flyout_extents_{};
    SubscriptionToken root_pointer_{};
    Window* observed_root_window_{};
    Event<const DockFlyoutChange&> flyout_changed_{};
    // property_codemod begin DockSite (tools/property_families/dock_site.json; generated, do not edit)
private:
    friend struct DockSiteKeyboardIncrementCapability<DockSite>;
    friend struct DockSiteDragThresholdCapability<DockSite>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(keyboard_increment)& cell, double& incoming);
    void written(const decltype(keyboard_increment)& cell);
    void prepare_write(const decltype(drag_threshold)& cell, double& incoming);
    void written(const decltype(drag_threshold)& cell);
    // property_codemod end DockSite
};

} // namespace gui_forms
