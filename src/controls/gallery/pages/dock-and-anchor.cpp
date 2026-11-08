#include "pages.hpp"

#include <cmath>
#include <string>

namespace gui_forms::gallery::pages {

// Owner of the Dock and Anchor page: a padded dock canvas whose edges are
// hidden and re-ordered at runtime, and an anchor canvas whose parent slot is
// rebased live, and the Windows Forms Splitter docked after a pane it
// resizes. The two toggles are page state kept here, not on the
// controls; the shell status line is reached through the shared context.
class DockAndAnchorPage final : public gui_forms::Component {
public:
    DockAndAnchorPage(const std::shared_ptr<Surface>& page,
                      const std::shared_ptr<PageContext>& context)
        : context_(context) {
        (*page).add_at(label("gallery.dock.heading", "DOCK AND ANCHOR", 22.0, 700,
                             shell_blue_dark),
                       {24.0, 18.0, 620.0, 34.0});
        (*page).add_at(label("gallery.dock.lead",
                             "Base Control layout owns z-ordered docking, padded client consumption, compound edge anchors, and runtime rebasing.",
                             12.0, 400, muted),
                       {26.0, 52.0, 920.0, 24.0});
        build_dock(page);
        build_anchor(page);
        build_splitter(page);
        on((*toggle_left_).clicked(), *this, &DockAndAnchorPage::on_toggle_left);
        on((*swap_top_).clicked(), *this, &DockAndAnchorPage::on_swap_top);
        on((*resize_).clicked(), *this, &DockAndAnchorPage::on_resize);
    }

private:
    [[nodiscard]] static std::shared_ptr<Button> docked(
        const std::string& id, const std::string& text, Size size,
        DockStyle dock, ButtonVisualStyle style = ButtonVisualStyle::standard) {
        std::shared_ptr<Button> button = make_control<Button>(StableId(id), text);
        (*button).set_visual_style(style);
        (*button).set_requested_bounds({0.0, 0.0, size.width, size.height});
        (*button).dock = dock;
        return button;
    }

    void build_dock(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> dock_group = group(
            "gallery.dock.group",
            "Dock · Left + Top + Top + Right + Bottom + Fill · z-order commands",
            {930.0, 260.0});
        (*page).add_at(dock_group, {24.0, 92.0, 930.0, 260.0});
        dock_canvas_ = make_control<Panel>(StableId("gallery.dock.canvas"));
        (*dock_canvas_).accessible_name = "Dock layout specimen";
        (*dock_canvas_).set_background(Color::rgba(238, 243, 247));
        (*dock_canvas_).set_border_style(BorderStyle::sunken);
        (*dock_canvas_).padding = {8.0, 8.0, 8.0, 8.0};
        (*dock_group).add_at(dock_canvas_, {18.0, 38.0, 610.0, 190.0});

        // Child order is dock order: Left, Top A, Top B, Right, Bottom, Fill.
        left_ = docked("gallery.dock.left", "Left", {82.0, 30.0}, DockStyle::left);
        top_first_ = docked("gallery.dock.top.first", "Top A · first edge",
                            {100.0, 25.0}, DockStyle::top, ButtonVisualStyle::command);
        top_second_ = docked("gallery.dock.top.second", "Top B · z-order",
                             {100.0, 25.0}, DockStyle::top, ButtonVisualStyle::command);
        (*dock_canvas_).add_child(left_);
        (*dock_canvas_).add_child(top_first_);
        (*dock_canvas_).add_child(top_second_);
        (*dock_canvas_).add_child(docked("gallery.dock.right", "Right",
                                         {76.0, 30.0}, DockStyle::right));
        (*dock_canvas_).add_child(docked("gallery.dock.bottom", "Bottom · 26 px",
                                         {100.0, 26.0}, DockStyle::bottom));
        (*dock_canvas_).add_child(docked("gallery.dock.fill", "Fill · remaining client",
                                         {80.0, 30.0}, DockStyle::fill,
                                         ButtonVisualStyle::accent));

        toggle_left_ = make_control<Button>(StableId("gallery.dock.toggle-left"),
                                            "Hide left edge");
        (*toggle_left_).set_visual_style(ButtonVisualStyle::accent);
        (*dock_group).add_at(toggle_left_, {652.0, 52.0, 236.0, 34.0});
        swap_top_ = make_control<Button>(StableId("gallery.dock.swap-top"),
                                         "Move Top B behind Top A");
        (*dock_group).add_at(swap_top_, {652.0, 98.0, 236.0, 34.0});
        std::shared_ptr<Label> note = label(
            "gallery.dock.note",
            "Hidden docked controls consume zero extent. SetChildIndex changes edge order without rewriting preferred bounds.",
            11.0, 600, green);
        (*note).set_text_wrapping(TextWrapping::word);
        (*note).set_vertical_alignment(VerticalAlignment::near);
        (*dock_group).add_at(note, {652.0, 148.0, 238.0, 70.0});
    }

    void build_anchor(const std::shared_ptr<Surface>& page) {
        anchor_group_ = group(
            "gallery.anchor.group",
            "Anchor · fixed · stretch · centered · bottom-right · live parent resize",
            {930.0, 250.0});
        (*page).add_at(anchor_group_, {24.0, 374.0, 930.0, 250.0});
        anchor_canvas_ = make_control<Panel>(StableId("gallery.anchor.canvas"));
        (*anchor_canvas_).accessible_name = "Compound anchor specimen";
        (*anchor_canvas_).set_background(Color::rgba(247, 249, 251));
        (*anchor_canvas_).set_border_style(BorderStyle::line);
        (*anchor_canvas_).padding = {8.0, 8.0, 8.0, 8.0};
        (*anchor_group_).add_at(anchor_canvas_, {18.0, 38.0, 600.0, 170.0});

        std::shared_ptr<Button> fixed = make_control<Button>(
            StableId("gallery.anchor.fixed"), "Left + Top");
        (*fixed).set_requested_bounds({18.0, 16.0, 126.0, 28.0});
        std::shared_ptr<Button> stretch = make_control<Button>(
            StableId("gallery.anchor.stretch"), "Left + Right · stretches");
        (*stretch).set_visual_style(ButtonVisualStyle::accent);
        (*stretch).set_requested_bounds({18.0, 56.0, 280.0, 28.0});
        (*stretch).anchor = AnchorStyles::left | AnchorStyles::right |
                              AnchorStyles::top;
        std::shared_ptr<Button> centered = make_control<Button>(
            StableId("gallery.anchor.centered"), "No edges · centered");
        (*centered).set_requested_bounds({190.0, 106.0, 152.0, 28.0});
        (*centered).anchor = AnchorStyles::none;
        std::shared_ptr<Button> bottom_right = make_control<Button>(
            StableId("gallery.anchor.bottom-right"), "Right + Bottom");
        (*bottom_right).set_visual_style(ButtonVisualStyle::command);
        (*bottom_right).set_requested_bounds({424.0, 126.0, 142.0, 28.0});
        (*bottom_right).anchor = AnchorStyles::right | AnchorStyles::bottom;
        (*anchor_canvas_).add_child(fixed);
        (*anchor_canvas_).add_child(stretch);
        (*anchor_canvas_).add_child(centered);
        (*anchor_canvas_).add_child(bottom_right);

        resize_ = make_control<Button>(StableId("gallery.anchor.resize"),
                                       "Expand specimen");
        (*resize_).set_visual_style(ButtonVisualStyle::accent);
        (*anchor_group_).add_at(resize_, {758.0, 54.0, 150.0, 34.0});
        std::shared_ptr<Label> note = label(
            "gallery.anchor.note",
            "Runtime size and Anchor changes rebase from committed geometry. Authored bounds remain queryable and unchanged.",
            11.0, 600, green);
        (*note).set_text_wrapping(TextWrapping::word);
        (*note).set_vertical_alignment(VerticalAlignment::near);
        (*anchor_group_).add_at(note, {758.0, 104.0, 150.0, 104.0});
    }

    // The Windows Forms Splitter facade: a left-docked pane, the splitter
    // docked after it, and a fill pane; dragging the bar resizes the pane.
    void build_splitter(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> splitter_group = group(
            "gallery.splitter.group",
            "Splitter · docked after a pane · MinSize and MinExtra · live resize",
            {930.0, 190.0});
        (*page).add_at(splitter_group, {24.0, 646.0, 930.0, 190.0});
        std::shared_ptr<Panel> canvas = make_control<Panel>(StableId("gallery.splitter.canvas"));
        (*canvas).accessible_name = "Splitter specimen";
        (*canvas).set_background(Color::rgba(247, 249, 251));
        (*canvas).set_border_style(BorderStyle::line);
        (*splitter_group).add_at(canvas, {18.0, 38.0, 610.0, 130.0});
        std::shared_ptr<Panel> folders = make_control<Panel>(StableId("gallery.splitter.folders"));
        (*folders).set_background(Color::rgba(225, 235, 243));
        (*folders).dock = DockStyle::left;
        (*folders).set_requested_bounds({0.0, 0.0, 160.0, 0.0});
        std::shared_ptr<Label> folders_title = label("gallery.splitter.folders.title", "Folders",
                                                     11.0, 700, shell_blue_dark);
        (*folders_title).set_requested_bounds({8.0, 8.0, 120.0, 20.0});
        (*folders).add_child(folders_title);
        splitter_ = make_control<Splitter>(StableId("gallery.splitter"));
        (*splitter_).accessible_name = "Folders and files splitter";
        (*splitter_).set_requested_bounds({0.0, 0.0, 4.0, 0.0});
        (*splitter_).set_min_size(80.0);
        (*splitter_).set_min_extra(120.0);
        std::shared_ptr<Panel> files = make_control<Panel>(StableId("gallery.splitter.files"));
        (*files).set_background(paper);
        (*files).dock = DockStyle::fill;
        std::shared_ptr<Label> files_title = label("gallery.splitter.files.title", "Files",
                                                   11.0, 700, shell_blue_dark);
        (*files_title).set_requested_bounds({8.0, 8.0, 120.0, 20.0});
        (*files).add_child(files_title);
        // Child order is dock order: the pane, then the splitter at its
        // edge, then the fill.
        (*canvas).add_child(folders);
        (*canvas).add_child(splitter_);
        (*canvas).add_child(files);
        std::shared_ptr<Label> note = label(
            "gallery.splitter.note",
            "The bar resizes the docked control before it. New code uses SplitContainer; Splitter keeps Windows Forms forms working.",
            11.0, 600, green);
        (*note).set_text_wrapping(TextWrapping::word);
        (*note).set_vertical_alignment(VerticalAlignment::near);
        (*splitter_group).add_at(note, {652.0, 52.0, 238.0, 100.0});
        on((*splitter_).splitter_moved(), *this, &DockAndAnchorPage::on_splitter_moved);
    }

    void report(const std::string& text) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (context && (*context).status) (*(*context).status).set_text(text);
    }

    void on_toggle_left() {
        const bool visible = !(*left_).visible.read();
        (*left_).visible = visible;
        (*toggle_left_).text = visible ? "Hide left edge" : "Restore left edge";
        report(visible ? "Dock · left edge restored · fill contracted"
                       : "Dock · hidden edge released · fill expanded");
    }

    void on_swap_top() {
        const bool next = !top_b_front_;
        const std::shared_ptr<Button>& old_first = next ? top_first_ : top_second_;
        const std::shared_ptr<Button>& new_first = next ? top_second_ : top_first_;
        const std::optional<std::size_t> destination =
            (*dock_canvas_).child_index((*old_first).runtime_id());
        if (!destination ||
            !(*dock_canvas_).set_child_index((*new_first).runtime_id(), *destination)) {
            return;
        }
        top_b_front_ = next;
        (*swap_top_).text = next ? "Move Top A behind Top B"
                                   : "Move Top B behind Top A";
        report(next ? "Dock · Top B now consumes the edge first"
                    : "Dock · Top A now consumes the edge first");
    }

    void on_resize() {
        expanded_ = !expanded_;
        (*anchor_group_).set_design_bounds(
            *anchor_canvas_, expanded_ ? Rect{18.0, 38.0, 730.0, 190.0}
                                       : Rect{18.0, 38.0, 600.0, 170.0});
        (*resize_).text = expanded_ ? "Restore specimen" : "Expand specimen";
        report(expanded_ ? "Anchor · parent expanded · stretch/right/center updated"
                         : "Anchor · parent restored from retained design slot");
    }

    void on_splitter_moved(const SplitterMoveEvent& move) {
        const long width = std::lround(move.split_position);
        report("Splitter · Folders is " + std::to_string(width) + " wide");
    }

    std::weak_ptr<PageContext> context_;
    std::shared_ptr<Panel> dock_canvas_;
    std::shared_ptr<Button> left_;
    std::shared_ptr<Button> top_first_;
    std::shared_ptr<Button> top_second_;
    std::shared_ptr<Button> toggle_left_;
    std::shared_ptr<Button> swap_top_;
    std::shared_ptr<LayoutGroup> anchor_group_;
    std::shared_ptr<Panel> anchor_canvas_;
    std::shared_ptr<Button> resize_;
    std::shared_ptr<Splitter> splitter_;
    bool top_b_front_{};
    bool expanded_{};
};

void add_dock_and_anchor(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<DockAndAnchorPage>(page, context));
}

} // namespace gui_forms::gallery::pages
