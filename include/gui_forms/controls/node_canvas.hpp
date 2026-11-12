#pragma once

// NodeCanvas (roadmap 2.4, phase N1; node graph spec sections 3 to 6; the
// node engine study, desks/coordinator/NODE_ENGINE_AND_VALUE_BAR_STUDY.md):
// the control that shows and edits a NodeGraph. Nodes, ports and links are
// the graph's records, never child controls: the canvas paints and
// hit-tests them over a world-aligned grid, and turns pointer and key
// input into the graph's named operations.
//
// What it does:
// - pan and zoom (wheel about the pointer, Ctrl+wheel finer, middle-drag or
//   Space+drag, arrows when no node is focused, Home frames all, F frames
//   the selection; 1/64 to 8) with deep zoom: the grid shows one level of
//   its s * 4^k spacings at a time, cross-faded (select_grid_level), and
//   the level of detail (SceneDetailLevel) follows the zoom with
//   hysteresis; text under 6 view units is not drawn;
// - nodes, ports and lamps, painted through the theme's canvas look
//   (gui_forms/theme/canvas_look: node_canvas_anatomy's parts, resolved
//   once into a CanvasLook and again only when the theme, colour scheme,
//   contrast or classes change), never literal colours;
// - links as soft cubic curves (soft_link_cubic) or routed by LinkRouter
//   (NodeLinkStyle::routed), and magnetic link drags through DragTracker:
//   compatible free inputs near the press are the magnet's targets (snap
//   radius 16, release radius 24, view units); release on one connects,
//   release elsewhere raises palette_requested; pressing a connected input
//   detaches its link into a new drag, and detach and reconnect are one
//   undo step;
// - selection (click, Shift adds, Ctrl toggles, empty click clears),
//   marquee (full containment; Alt for touching), link selection within 4
//   view units, Ctrl+A;
// - moving the selection with grid snap (on by default, Ctrl inverts) and
//   auto-scroll, the knife (Ctrl + secondary drag) cutting crossed links;
// - double-click (or F2) to edit through the application's edit target
//   (NodeCanvasCallbacks::edit), whose editor control the canvas hosts over
//   the node as its own child (node graph gap N9);
// - the keyboard graph of spec section 6: Tab into the canvas, arrows to the
//   nearest node in a direction, Ctrl+arrows move, Enter into the ports,
//   L for a keyboard link with a target list sorted by distance, F2 edit,
//   Delete, Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z, Shift+F10 the palette.
//
// Gestures and undo (input order rules): every gesture is one DragTracker
// session and one undo group of the graph; it commits as one undo step,
// and Escape (which the window routes to the drag engine first) rolls the
// group back (NodeGraph::rollback_undo_group), leaving no step. The release
// that ends a planned drag sets suppress_click, so it never acts as a
// click; a press on a node already selected narrows the selection only on
// the click that follows a release without a drag.
//
// Performance: paint visits only the visible set. Nodes and links are kept
// in two public SpatialIndex instances (gui_forms/scene), rebuilt when the
// graph's revision changes and updated in place while a drag moves nodes;
// every per-frame buffer is sized at those rebuilds, so painting allocates
// nothing per node. Lamps have their own revision (lamp_revision): a lamp
// change repaints and invalidates no cache.
//
// Accessibility: the canvas is a group named by accessible_name; the
// visible nodes, and the focused one wherever it is, are its items, named
// by caption and kind, with their ports as children ("input Colour,
// connected to Noise"). Links cannot be published as relations: the
// semantic tree has no flows-to or flows-from fields yet (gap 13), so each
// input port names its source in its text instead. Announcements (drag
// progress, keyboard link targets, edits) go to an always-present live
// region child, "<id>.announcements".
//
// Ownership and lifetime: the canvas borrows its graph (set_graph), which
// must outlive it or be detached first with set_graph(nullptr). Its
// callbacks are named Delegates with explicit context; they run on the
// window's thread, may not set the graph, and a callback's mutation of the
// canvas is refused while it runs (calls return without effect). The
// DragTracker is declared last so that it ends first. An editor the
// application hands the canvas is its child until the edit ends.
//
// Subclassing: the class is not final. Each event is raised through a
// protected raiser (on_selection_changed, on_link_connected,
// on_graph_edited, on_view_changed, on_palette_requested, on_edit_ended)
// whose base raises the event; an override that calls the base keeps the
// event, and C and C# reach the raisers through the class tables. A raiser
// runs after its change, outside the canvas's callback scope, so it may use
// the canvas; it may also detach or destroy it, and the canvas checks that
// it is still alive before it continues.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/link_router.hpp"
#include "gui_forms/controls/node_graph.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/drag/drag_tracker/drag_tracker.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/scene/scene_view/scene_view.hpp"
#include "gui_forms/scene/spatial_index/spatial_index.hpp"
#include "gui_forms/theme/canvas_look/canvas_look.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Painter;

// -- values ------------------------------------------------------------------------

// How links are drawn: soft cubic curves, or the link router's
// obstacle-avoiding routes (re-routed when the graph changes, so live
// while nodes move).
enum class NodeLinkStyle : std::uint8_t { soft, routed };

// What lies under a point: a port (nearest within 8 view units), else the
// topmost node's caption or body, else a link within 4 view units.
enum class NodeCanvasHitKind : std::uint8_t { none, node_body, node_caption, port, link };

struct GUI_FORMS_API NodeCanvasHit final {
    NodeCanvasHitKind kind{NodeCanvasHitKind::none};
    NodeId node{};
    PortIndex port{};
    LinkId link{};
    friend bool operator==(const NodeCanvasHit& left, const NodeCanvasHit& right) noexcept {
        return left.kind == right.kind && left.node == right.node && left.port == right.port &&
               left.link == right.link;
    }
};

// The gesture in progress, one at a time.
enum class NodeCanvasGesture : std::uint8_t { none, move, link, marquee, knife, pan };

// The compatibility question for one prospective link (node graph gap N6:
// the graph has no conversion table yet, so the application answers). The
// canvas fills the ports and their socket types and `accepted` with its
// default, equal socket types; the check_link target may change
// `accepted`. Asked for every magnet candidate and before every connect.
struct GUI_FORMS_API NodeLinkCheck final {
    PortRef from{};
    PortRef to{};
    std::uint16_t from_type{};
    std::uint16_t to_type{};
    bool accepted{};
};

// Which part of a node an edit opens: the body (its values) or the caption
// (a rename).
enum class NodeEditPart : std::uint8_t { body, caption };

// The canvas's request to edit a node (double-click, F2, or begin_edit).
// `bounds` is the part's rectangle in the canvas's local units. The edit
// target may set `editor` to a control the canvas then hosts as its child
// over `bounds` (kept there through pans and zooms) until end_edit; the
// canvas opens an undo group first, so every graph change the editor makes
// before end_edit(commit) is one undo step, and end_edit(cancel) rolls
// them back. Leaving `editor` empty declines the edit.
struct GUI_FORMS_API NodeEditRequest final {
    NodeId node{};
    NodeEditPart part{NodeEditPart::body};
    Rect bounds{};
    Control::Ptr editor{};
};

enum class NodeEditEnd : std::uint8_t { commit, cancel };

// Raised after an edit ended, the editor already removed.
struct GUI_FORMS_API NodeEditEnded final {
    NodeId node{};
    NodeEditPart part{NodeEditPart::body};
    NodeEditEnd end{NodeEditEnd::commit};
};

// The add-node palette's request: after a double-click on empty canvas,
// Shift+F10 or the context menu key, or a link drag released off every
// port (then `from` is the dragged output, so the palette can offer kinds
// with a compatible input). `world` is where a new node would go and
// `local` the same point in the canvas's local units.
struct GUI_FORMS_API NodePaletteRequest final {
    WorldPoint world{};
    Point local{};
    std::optional<PortRef> from{};
};

// The application's drawing of a node's body at full detail: `body` is the
// area below the caption in local units, at `zoom` view units per world
// unit. The painter is borrowed for the call; the target may draw only
// inside `body` (the canvas clips to it).
struct GUI_FORMS_API NodeBodyPaint final {
    Painter* painter{};
    NodeId node{};
    Rect body{};
    double zoom{1.0};
    AnatomyStateSet states{};
};

// Named targets (Delegates: a function, or a member of an object the
// application keeps alive while the canvas holds the target).
struct GUI_FORMS_API NodeCanvasCallbacks final {
    Delegate<NodeLinkCheck&> check_link;
    Delegate<NodeEditRequest&> edit;
    Delegate<NodeBodyPaint&> paint_body;
};

struct GUI_FORMS_API NodeCanvasSelection final {
    std::size_t nodes{};
    std::optional<LinkId> link{};
};

struct GUI_FORMS_API NodeLinkConnected final {
    LinkId link{};
    PortRef from{};
    PortRef to{};
};

// What a canvas operation did to the graph (graph_edited), after it.
enum class NodeGraphEditKind : std::uint8_t { moved, connected, disconnected, removed, undone, redone, edited };

struct GUI_FORMS_API NodeGraphEdited final {
    NodeGraphEditKind kind{NodeGraphEditKind::moved};
    std::uint64_t revision{};
};

// Plain-data counters; they only grow, except the `last_` fields and
// `level`.
struct GUI_FORMS_API NodeCanvasStatistics final {
    std::uint64_t frames_painted{};
    std::uint64_t nodes_painted{};
    std::uint64_t links_painted{};
    std::uint64_t link_points_flattened{};
    std::uint64_t captions_drawn{};
    std::uint64_t text_skipped_small{};
    std::uint64_t ports_painted{};
    std::uint64_t lamps_painted{};
    std::uint64_t hit_tests{};
    std::uint64_t node_index_builds{};
    std::uint64_t link_index_builds{};
    std::uint64_t index_moves_in_place{};
    std::uint64_t index_moves_refused{};
    std::uint64_t router_runs{};
    std::uint64_t lamp_repaints{};
    std::uint64_t gestures_started{};
    std::uint64_t gestures_committed{};
    std::uint64_t gestures_cancelled{};
    std::uint64_t link_drags{};
    std::uint64_t magnet_candidates{};
    std::uint64_t magnet_captures{};
    std::uint64_t connections_made{};
    std::uint64_t connections_refused{};
    std::uint64_t links_detached{};
    std::uint64_t marquee_selections{};
    std::uint64_t knife_cuts{};
    std::uint64_t palette_requests{};
    std::uint64_t edit_requests{};
    std::uint64_t edits_committed{};
    std::uint64_t edits_cancelled{};
    std::uint64_t clicks_suppressed{};
    std::uint64_t key_presses_suppressed{};
    std::uint64_t level_transitions{};
    std::size_t last_frame_nodes{};
    std::size_t last_frame_links{};
    SceneDetailLevel level{SceneDetailLevel::full};
};

// -- the control -----------------------------------------------------------------------

class GUI_FORMS_API NodeCanvas : public Control {
public:
    // World units between grid lines at level 0, and the snap grid's cell,
    // until the theme's look (the region's grid-spacing) is resolved.
    static constexpr double default_grid_spacing = 16.0;
    // A node's caption band, world units (the body lies below it).
    static constexpr double caption_height = 24.0;
    // View units, zoom-independent (spec 4.2).
    static constexpr double magnet_snap_radius = 16.0;
    static constexpr double magnet_release_radius = 24.0;
    static constexpr double port_hit_radius = 8.0;
    static constexpr double link_hit_distance = 4.0;
    // The visible area plus this margin (view units) supplies link targets.
    static constexpr double candidate_margin = 200.0;
    // Text below this size in view units is not drawn (spec 3).
    static constexpr double smallest_text = 6.0;
    // The most points one link's drawn path holds (longer routes are cut).
    static constexpr std::size_t maximum_link_points = 1024U;

    explicit NodeCanvas(StableId stable_id);
    ~NodeCanvas() override;
    NodeCanvas(const NodeCanvas&) = delete;
    NodeCanvas& operator=(const NodeCanvas&) = delete;

    // -- graph and targets ---------------------------------------------------
    // Borrows `graph` (null detaches). Cancels a gesture or an edit in
    // progress (rolling back its group in the former graph), clears the
    // selection and the focus, and drops every cache. Refused (no change)
    // inside a callback.
    void set_graph(NodeGraph* graph);
    [[nodiscard]] NodeGraph* graph() const noexcept { return graph_; }
    void set_callbacks(NodeCanvasCallbacks callbacks);
    [[nodiscard]] const NodeCanvasCallbacks& callbacks() const noexcept { return callbacks_; }
    // Call after the application changed the graph (a load, an edit, an
    // evaluation's lamps): drops stale selection and focus identities and
    // repaints. Painting rechecks the revisions anyway; this also renews
    // the semantics.
    void refresh();

    // -- view ------------------------------------------------------------------
    [[nodiscard]] const SceneViewTransform& view() const noexcept { return view_; }
    // Sets the view, its zoom held to zoom_range(); false (no change) for a
    // view that is not valid_scene_view.
    bool set_view(const SceneViewTransform& view);
    [[nodiscard]] SceneZoomRange zoom_range() const noexcept { return zoom_range_; }
    void zoom_about(Point local, double factor);
    void pan_by(Point delta);
    void frame_all();
    void frame_selection();
    // Reveals the node (pans the least that shows it whole, if it fits).
    void reveal_node(NodeId node);
    [[nodiscard]] SceneDetailLevel detail_level() const noexcept { return level_; }
    // False (no change) for thresholds that are not valid.
    bool set_detail_thresholds(const SceneDetailThresholds& thresholds);
    [[nodiscard]] const SceneDetailThresholds& detail_thresholds() const noexcept { return thresholds_; }
    void set_link_style(NodeLinkStyle style);
    [[nodiscard]] NodeLinkStyle link_style() const noexcept { return link_style_; }
    void set_router_options(const LinkRouterOptions& options);
    [[nodiscard]] const LinkRouterOptions& router_options() const noexcept { return router_options_; }
    // World units; false (no change) unless finite and at least 1. The
    // spacing set here wins over the theme's grid-spacing for both the
    // drawn grid and the snap; clear_grid_spacing returns to the theme's.
    bool set_grid_spacing(double spacing);
    void clear_grid_spacing();
    // The grid's spacing: the one set, else the theme's (resolving the look
    // when the canvas is in a window), else default_grid_spacing.
    [[nodiscard]] double grid_spacing();
    // The look the last paint used (resolved on demand), for inspection.
    [[nodiscard]] const CanvasLook& canvas_look();
    void set_snap_to_grid(bool snap) noexcept { snap_ = snap; }
    [[nodiscard]] bool snap_to_grid() const noexcept { return snap_; }

    // -- selection and focus ---------------------------------------------------
    [[nodiscard]] std::span<const NodeId> selection() const& noexcept {
        return std::span<const NodeId>(selection_.data(), selection_.size());
    }
    std::span<const NodeId> selection() const&& = delete;
    [[nodiscard]] bool selected(NodeId node) const noexcept;
    [[nodiscard]] std::optional<LinkId> selected_link() const noexcept { return selected_link_; }
    void select_only(NodeId node);
    void add_to_selection(NodeId node);
    void toggle_selection(NodeId node);
    void select_link(LinkId link);
    void clear_selection();
    void select_all();
    // Selects the live nodes `mode` matches in `area` (replacing the
    // selection, or adding to it); returns how many it added.
    std::size_t select_in_rect(const WorldRect& area, SpatialQueryMode mode, bool add);
    // The keyboard's current node (and port, after Enter), or none.
    [[nodiscard]] std::optional<NodeId> focused_node() const noexcept { return focused_node_; }
    [[nodiscard]] std::optional<PortIndex> focused_port() const noexcept { return focused_port_; }
    // False for a stale node.
    bool set_focused_node(NodeId node);

    // -- operations (each one undo step) ---------------------------------------
    // Each refuses (returns false or nothing) without a graph, inside a
    // callback, during a gesture or an edit.
    bool delete_selection();
    bool undo();
    bool redo();
    bool move_selection(WorldPoint delta);
    // Disconnects every link whose drawn path crosses the local segment
    // from-to; returns how many it cut.
    std::size_t cut_links(Point from, Point to);
    // Connects after the compatibility check (check_link); the graph's own
    // refusals pass through. GraphStatus::invalid_argument when the check
    // refuses or the canvas refuses the operation.
    GraphResult connect_ports(PortRef from, PortRef to);
    // Connects `from` (an output) to the nearest compatible free input of
    // another node.
    bool link_to_nearest(PortRef from);

    // -- editing ---------------------------------------------------------------
    // Asks the edit target for an editor (NodeEditRequest); true when one
    // was given and is now hosted. Refused during a gesture or another edit.
    bool begin_edit(NodeId node, NodeEditPart part);
    // Ends the edit: commit closes its undo group, cancel rolls it back; the
    // editor is removed, edit_ended raised. Nothing when no edit is open.
    void end_edit(NodeEditEnd end);
    [[nodiscard]] bool editing() const noexcept { return editor_ != nullptr; }
    [[nodiscard]] std::optional<NodeId> edited_node() const noexcept;

    // -- queries ----------------------------------------------------------------
    // Not const: they may rebuild the indexes for the current revision.
    [[nodiscard]] NodeCanvasHit hit_test(Point local);
    // Writes into `out` the links whose drawn path crosses the local segment
    // from-to and returns how many cross (more than written when `out` is
    // short).
    [[nodiscard]] std::size_t links_crossing(Point from, Point to, std::span<LinkId> out);
    // A port's anchor in local units, or none for a stale port.
    [[nodiscard]] std::optional<Point> port_view_anchor(PortRef port) const;
    // The node slots the last paint drew, in paint order.
    [[nodiscard]] std::span<const std::uint32_t> visible_nodes() const& noexcept {
        return std::span<const std::uint32_t>(visible_nodes_.data(), visible_nodes_.size());
    }
    std::span<const std::uint32_t> visible_nodes() const&& = delete;
    [[nodiscard]] NodeCanvasGesture gesture() const noexcept { return gesture_; }
    // The live region's current text.
    [[nodiscard]] std::string_view announcement() const& noexcept { return announcement_; }
    std::string_view announcement() const&& = delete;
    [[nodiscard]] const NodeCanvasStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] DragTrackerStatistics drag_statistics() const;
    [[nodiscard]] const SpatialIndexStatistics& node_index_statistics() const noexcept {
        return node_index_.statistics();
    }
    [[nodiscard]] const SpatialIndexStatistics& link_index_statistics() const noexcept {
        return link_index_.statistics();
    }

    // -- events (raised after the change) ---------------------------------------
    [[nodiscard]] Event<const NodeCanvasSelection&>& selection_changed() noexcept { return selection_changed_; }
    [[nodiscard]] Event<const NodeLinkConnected&>& link_connected() noexcept { return link_connected_; }
    [[nodiscard]] Event<const NodeGraphEdited&>& graph_edited() noexcept { return graph_edited_; }
    [[nodiscard]] Event<const SceneViewTransform&>& view_changed() noexcept { return view_changed_; }
    [[nodiscard]] Event<const NodePaletteRequest&>& palette_requested() noexcept { return palette_requested_; }
    [[nodiscard]] Event<const NodeEditEnded&>& edit_ended() noexcept { return edit_ended_; }

    // -- Control ----------------------------------------------------------------
    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    // node_canvas_anatomy(): the theme's canvas anatomy (canvas_look.hpp).
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

protected:
    // The raisers (per event): each base raises its event with `arguments`.
    virtual void on_selection_changed(const NodeCanvasSelection& arguments);
    virtual void on_link_connected(const NodeLinkConnected& arguments);
    virtual void on_graph_edited(const NodeGraphEdited& arguments);
    virtual void on_view_changed(const SceneViewTransform& arguments);
    virtual void on_palette_requested(const NodePaletteRequest& arguments);
    virtual void on_edit_ended(const NodeEditEnded& arguments);

    // The anatomy's parts (region, the grid levels and the canvas-wide
    // marquee over the client area or the gesture; node, node_header,
    // node_selection, port and lamp for the focused node; link and
    // dashed_link for the selected link).
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_mouse_click(MouseEventArgs& arguments) override;
    void on_mouse_double_click(MouseEventArgs& arguments) override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_key_up(KeyEventArgs& arguments) override;
    void on_context_menu_requested(ContextMenuRequest& request) override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;

private:
    // One magnet or keyboard-link candidate: a compatible free input.
    struct LinkCandidate final {
        PortRef port{};
        Point view_anchor{};
        double distance{};
    };
    struct CandidateNearer final {
        bool operator()(const LinkCandidate& left, const LinkCandidate& right) const noexcept;
    };
    // Marks the canvas as running an application callback for the scope's
    // life (refusing reentrant mutation), restoring the previous mark.
    class CallbackScope;

    // Caches (node_canvas.cpp).
    void ensure_caches();
    void rebuild_node_cache();
    void rebuild_link_index();
    void update_moved_entries();
    [[nodiscard]] WorldRect link_world_bounds(std::uint32_t slot, const LinkRecord& link) const;
    [[nodiscard]] bool fresh_caches() const noexcept;
    void drop_caches() noexcept;

    // Model helpers (node_canvas.cpp).
    [[nodiscard]] bool can_mutate() const noexcept;
    [[nodiscard]] NodeId id_of_slot(std::uint32_t slot) const noexcept;
    [[nodiscard]] bool link_accepted(PortRef from, PortRef to) const;
    void refresh_selection_flags();
    void after_edit(NodeGraphEditKind kind);
    void emit_selection();
    void set_announcement(std::string_view text);
    void node_states_into(std::uint32_t slot, AnatomyStateSet& states) const;
    [[nodiscard]] std::size_t link_path(std::uint32_t slot, const LinkRecord& link, std::size_t segments,
                                        std::span<Point> out) const;
    [[nodiscard]] std::size_t curve_segments() const noexcept;
    void collect_link_candidates(Point around, bool within_margin);
    [[nodiscard]] std::optional<std::uint32_t> nearest_node_in_direction(double dx, double dy) const;
    void node_identity(std::uint32_t slot, std::string& identity) const;
    [[nodiscard]] std::optional<NodeId> node_of_child_id(std::string_view stable_id) const;
    [[nodiscard]] Rect edit_bounds() const;

    // Input (node_canvas_input.cpp).
    void start_gesture(NodeCanvasGesture gesture, const MouseEventArgs& arguments, Rect window_source,
                       double threshold);
    void settle_unplanned_gesture();
    void press_primary(MouseEventArgs& arguments);
    void plan_drag(DragPlan& plan);
    void preview_drag(const DragTrackerFrame& frame);
    void finish_drag(const DragTrackerFrame& frame);
    void announce_drag(DragAnnouncementRequest& request);
    void scroll_drag(DragAutoScrollStep& step);
    void plan_move(DragPlan& plan);
    void plan_link(DragPlan& plan);
    void free_placement(DragPlan& plan) const;
    void finish_move(const DragTrackerFrame& frame);
    void finish_link(const DragTrackerFrame& frame);
    void close_gesture_group(bool commit);
    [[nodiscard]] bool handle_navigation_key(std::uint32_t key, Modifier modifiers);
    [[nodiscard]] bool handle_keyboard_link_key(std::uint32_t key);
    bool start_keyboard_link();
    void announce_keyboard_target();
    void request_palette(Point local, std::optional<PortRef> from);

    // Painting (node_canvas_paint.cpp).
    void ensure_look();
    void paint_links(Painter& painter);
    void paint_nodes(Painter& painter);
    void paint_node(Painter& painter, std::uint32_t slot, const NodeRecord& node);
    void paint_ports(Painter& painter, std::uint32_t slot, const NodeRecord& node);
    void paint_overlay(Painter& painter);
    [[nodiscard]] CanvasPortState port_state(std::uint32_t slot, std::size_t port) const;

    NodeGraph* graph_{};
    NodeCanvasCallbacks callbacks_{};
    // Mutable: a const query (the link check) runs a callback too.
    mutable bool in_callback_{};
    bool focused_{};

    // The view.
    SceneViewTransform view_{};
    SceneZoomRange zoom_range_{};
    SceneDetailThresholds thresholds_{};
    SceneDetailLevel level_{SceneDetailLevel::full};
    NodeLinkStyle link_style_{NodeLinkStyle::soft};
    LinkRouterOptions router_options_{};
    std::optional<double> grid_spacing_set_{};
    bool snap_{true};

    // The theme's look and what it was resolved for: the theme and rule
    // sets (by address), the colour scheme and contrast, the classes and the
    // authored back_color (which replaces the region's colour). Compared at
    // each paint (no allocation unless they changed).
    CanvasLook look_{};
    bool look_resolved_{};
    const Theme* look_theme_{};
    const CompiledThemeRules* look_inline_rules_{};
    bool look_dark_{};
    bool look_high_contrast_{};
    std::optional<Color> look_back_color_{};
    std::vector<std::string> look_classes_{};

    // Caches keyed on the graph's revision (and the link style for links).
    LinkRouter router_{};
    SpatialIndex node_index_{};
    SpatialIndex link_index_{};
    std::uint64_t node_cache_revision_{};
    std::uint64_t link_cache_revision_{};
    NodeLinkStyle link_cache_style_{NodeLinkStyle::soft};
    std::uint64_t painted_lamp_revision_{};
    std::vector<SpatialEntry> entries_{};
    // Links touching each node slot (compressed rows, from the rebuild),
    // for the in-place index updates of a drag.
    std::vector<std::uint32_t> node_link_start_{};
    std::vector<std::uint32_t> node_links_{};

    // Selection: identities, and one flag per node slot.
    std::vector<NodeId> selection_{};
    std::vector<std::uint8_t> selected_flags_{};
    std::optional<LinkId> selected_link_{};
    std::optional<NodeId> focused_node_{};
    std::optional<PortIndex> focused_port_{};
    std::optional<std::uint32_t> hot_node_{};

    // The gesture in progress and its terms.
    NodeCanvasGesture gesture_{NodeCanvasGesture::none};
    NodeId move_anchor_{};
    Point press_view_{};
    Point current_view_{};
    Point pan_last_{};
    bool gesture_planned_{};
    bool gesture_group_open_{};
    PortRef link_from_{};
    std::optional<std::uint32_t> link_hit_{};
    std::optional<NodeId> pending_click_select_{};
    bool knife_finished_{};
    bool space_held_{};
    // The keyboard link: candidates_ sorted nearest first, the one shown.
    bool keyboard_link_{};
    std::size_t keyboard_target_{};
    std::vector<LinkCandidate> candidates_{};
    std::vector<DragTarget> targets_{};
    // One flag per node slot: whether a candidate lies on that node (the
    // paint's dimming test looks up candidates only on flagged nodes).
    std::vector<std::uint8_t> candidate_flags_{};

    // The edit host.
    Control::Ptr editor_{};
    NodeId edit_node_{};
    NodeEditPart edit_part_{NodeEditPart::body};
    bool edit_group_open_{};

    // Per-frame storage, sized at the cache rebuilds and reused.
    std::vector<std::uint32_t> visible_nodes_{};
    std::vector<std::uint32_t> visible_links_{};
    std::vector<std::uint32_t> query_{};
    std::vector<Point> link_points_{};
    std::vector<LinkId> crossing_{};
    std::vector<Rect> overview_rects_{};
    std::vector<Rect> overview_selected_{};
    std::string announcement_{};
    std::string text_{};

    NodeCanvasStatistics statistics_{};
    Event<const NodeCanvasSelection&> selection_changed_{};
    Event<const NodeLinkConnected&> link_connected_{};
    Event<const NodeGraphEdited&> graph_edited_{};
    Event<const SceneViewTransform&> view_changed_{};
    Event<const NodePaletteRequest&> palette_requested_{};
    Event<const NodeEditEnded&> edit_ended_{};
    // Declared last: it borrows this control and ends first.
    std::unique_ptr<DragTracker> drag_{};
};

} // namespace gui_forms
