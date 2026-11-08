#include "pages.hpp"

#include "responsive-layouts_form.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Responsive Layouts page that demo/pages/responsive-layouts.form
// describes. One Value<double> is the stage width: a TrackBar binds it, and
// the owner narrows four specimens to it so a ResponsiveTrackPanel collapses
// authored tracks, a CommandOverflowPanel collapses command groups into its
// actuator, a MasterDetailView drops to a compact presentation, and a
// ScrollableControl exposes more overflow.
class ResponsiveLayoutsPage final : public gui_forms::Component {
public:
    ResponsiveLayoutsPage(const std::shared_ptr<Surface>& body,
                          const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        bind_width_control();
        bind_tracks();
        bind_shelf();
        build_master_detail();
        on(stage_width_.changed(), *this, &ResponsiveLayoutsPage::on_stage_width_changed);
        on(inspector_protected_.changed(), *this,
           &ResponsiveLayoutsPage::on_inspector_protected_changed);
        for (int index = 0; index < 3; ++index) {
            on((*shelf_bars_[static_cast<std::size_t>(index)]).invoked(), *this,
               &ResponsiveLayoutsPage::on_shelf_command, index);
        }
        on((*document_.gallery_responsive_shelf_more).clicked(), *this, &ResponsiveLayoutsPage::on_more);
        on((*folders_).selection_changed(), *this, &ResponsiveLayoutsPage::on_folder_selected);
        on((*document_.gallery_responsive_master_detail).presentation_changed(), *this,
           &ResponsiveLayoutsPage::on_presentation_changed);
        on(show_detail_.invoked(), *this, &ResponsiveLayoutsPage::on_show_detail);
        on(show_master_.invoked(), *this, &ResponsiveLayoutsPage::on_show_master);
        on(scroll_previous_.invoked(), *this, &ResponsiveLayoutsPage::on_scroll_step, -1);
        on(scroll_next_.invoked(), *this, &ResponsiveLayoutsPage::on_scroll_step, 1);
        on((*document_.gallery_responsive_scroll).scroll(), *this, &ResponsiveLayoutsPage::on_scrolled);
        show_detail_.set_enabled(false);
        show_master_.set_enabled(false);
    }

private:
    static constexpr double stage_minimum = 320.0;
    static constexpr double stage_maximum = 894.0;
    static constexpr std::array<std::string_view, 3> shelf_names{
        "Selection", "Arrange", "Navigate"};
    static constexpr std::array<std::string_view, 3> folder_names{
        "Inbox", "Drafts", "Archive"};

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, a design surface of the same 1000 x 660
        // size. The copy of the child list keeps the iteration valid while
        // the root shrinks.
        Control& root = *document_.gallery_responsive_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        const std::array<std::shared_ptr<ScaledGroupBox>, 5> groups{
            document_.gallery_responsive_width_group, document_.gallery_responsive_tracks_group,
            document_.gallery_responsive_shelf_group, document_.gallery_responsive_master_detail_group,
            document_.gallery_responsive_scroll_group};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
    }

    void bind_width_control() {
        TrackBar& width = *document_.gallery_responsive_width;
        width.set_small_change(10.0);
        width.set_large_change(100.0);
        width.bind(stage_width_);
        describe_width(stage_width_.get());
    }

    void bind_tracks() {
        (*document_.gallery_responsive_tracks_protect).bind(inspector_protected_);
        describe_tracks();
    }

    void bind_shelf() {
        const std::array<std::shared_ptr<CommandBar>, 3> bars{
            document_.gallery_responsive_shelf_selection_bar,
            document_.gallery_responsive_shelf_arrange_bar,
            document_.gallery_responsive_shelf_navigate_bar};
        shelf_bars_ = bars;
        (*bars[0]).set_items({{.id = 1, .label = "Open"}, {.id = 2, .label = "Copy"},
                              {.id = 3, .label = "Rename"}});
        (*bars[1]).set_items({{.id = 1, .label = "View"}, {.id = 2, .label = "Sort"},
                              {.id = 3, .label = "Group"}});
        (*bars[2]).set_items({{.id = 1, .label = "Back"}, {.id = 2, .label = "Up"}});
        (*document_.gallery_responsive_shelf).set_overflow_actuator(*document_.gallery_responsive_shelf_more);
    }

    // The view's two roles are attached by the view itself, not authored as
    // form children: the list box is the master and a tile is the detail.
    void build_master_detail() {
        MasterDetailView& view = *document_.gallery_responsive_master_detail;
        MasterDetailLayout layout;
        layout.master_extent = 240.0;
        layout.master_minimum = 160.0;
        layout.detail_minimum = 300.0;
        layout.compact_threshold = 620.0;
        view.set_master_detail_layout(layout);
        folders_ = make_control<ListBox>(StableId("gallery.responsive.master-detail.master"));
        (*folders_).accessible_name = "Folders";
        (*folders_).item_height = 22.0;
        (*folders_).items = {std::string(folder_names[0]), std::string(folder_names[1]),
                            std::string(folder_names[2])};
        (*folders_).select_index(0U);
        static_cast<void>(view.set_master(folders_));
        std::shared_ptr<Panel> detail = make_control<Panel>(StableId("gallery.responsive.master-detail.detail"));
        (*detail).set_background(paper);
        (*detail).border_style = BorderStyle::line;
        (*detail).dock = DockStyle::fill;
        (*detail).margin = Insets{};
        detail_text_ = label("gallery.responsive.master-detail.detail.text",
                             "Inbox · 12 messages · detail pane", 11.0, 600, ink);
        (*detail_text_).text_wrapping = TextWrapping::word;
        (*detail_text_).padding = {8.0, 4.0, 8.0, 4.0};
        (*detail_text_).dock = DockStyle::fill;
        (*detail).add_child(detail_text_);
        static_cast<void>(view.set_detail(detail));
        (*document_.gallery_responsive_master_detail_show_detail).bind(show_detail_);
        (*document_.gallery_responsive_master_detail_show_master).bind(show_master_);
        // The scroller's face stays the page's fill: a themed face paints its
        // sunken edge as a part raster whose bottom corners differ from the
        // direct stroke by up to 11 levels in 8 pixels.
        (*document_.gallery_responsive_scroll).set_background(Color::rgba(238, 243, 247));
        (*document_.gallery_responsive_scroll_previous).bind(scroll_previous_);
        (*document_.gallery_responsive_scroll_next).bind(scroll_next_);
    }

    void describe_width(const double width) {
        (*document_.gallery_responsive_width_value).set_text("Stage " + std::to_string(static_cast<int>(width)) +
                                  " px · minimum " +
                                  std::to_string(static_cast<int>(stage_minimum)) +
                                  " · maximum " +
                                  std::to_string(static_cast<int>(stage_maximum)));
    }

    void describe_tracks() {
        (*document_.gallery_responsive_tracks_status).set_text(inspector_protected_.get()
            ? "Inspector revealed · navigation (20) then activity (30) collapse first"
            : "Automatic · inspector (10), navigation (20), activity (30) collapse in that order");
    }

    // The slider's state change (user or programmatic) narrows the stage. The
    // layout containers re-solve on the next arrange; no manual relayout.
    void on_stage_width_changed(const double width) {
        describe_width(width);
        (*document_.gallery_responsive_tracks_group).set_design_bounds(
            *document_.gallery_responsive_tracks, {18.0, 30.0, width, 48.0});
        (*document_.gallery_responsive_shelf_group).set_design_bounds(
            *document_.gallery_responsive_shelf, {18.0, 30.0, width, 44.0});
        (*document_.gallery_responsive_master_detail_group).set_design_bounds(
            *document_.gallery_responsive_master_detail, {18.0, 30.0, width, 70.0});
        (*document_.gallery_responsive_scroll_group).set_design_bounds(
            *document_.gallery_responsive_scroll, {18.0, 30.0, std::max(120.0, width - 200.0), 48.0});
    }

    void on_inspector_protected_changed(const bool protect) {
        (*document_.gallery_responsive_tracks).set_track_revealed(2U, protect);
        describe_tracks();
    }

    void on_shelf_command(const int group, const CommandItem& item) {
        (*document_.gallery_responsive_shelf_status).set_text("Shelf · " + std::string(shelf_names[static_cast<std::size_t>(group)]) +
                                  " · " + item.label + " invoked");
    }

    // Command authority stays with the page: the panel only reports which
    // groups it collapsed, by stable id, and the page composes the overflow.
    void on_more() {
        const CommandOverflowSnapshot snapshot = (*document_.gallery_responsive_shelf).layout_snapshot();
        std::string collapsed;
        for (const CommandOverflowGroupResult& group : snapshot.groups) {
            if (!group.collapsed) continue;
            if (!collapsed.empty()) collapsed += ", ";
            collapsed += group.stable_id;
        }
        (*document_.gallery_responsive_shelf_status).set_text(collapsed.empty()
            ? "Shelf · More… · no collapsed group to overflow"
            : "Shelf · More… · overflow holds " + collapsed);
    }

    void on_folder_selected(const ListSelectionChange& change) {
        if (!change.active_index || *change.active_index >= folder_names.size()) return;
        (*detail_text_).set_text(std::string(folder_names[*change.active_index]) +
                                 " · detail pane follows the master selection");
    }

    // State event from the view: enables exactly the navigation command that
    // applies, and both buttons follow their Command's enabled state.
    void on_presentation_changed(const MasterDetailPresentationChange& change) {
        show_detail_.set_enabled(change.current == MasterDetailDisplayMode::master_only);
        show_master_.set_enabled(change.current == MasterDetailDisplayMode::detail_only);
        (*document_.gallery_responsive_master_detail_status).set_text(
            change.current == MasterDetailDisplayMode::side_by_side
                ? "Side by side · both roles visible · compact commands disabled"
                : change.current == MasterDetailDisplayMode::master_only
                ? "Compact · master only · Show detail enabled"
                : "Compact · detail only · Show master enabled");
    }

    void on_show_detail() { (*document_.gallery_responsive_master_detail).show_detail(); }
    void on_show_master() { (*document_.gallery_responsive_master_detail).show_master(); }

    void on_scroll_step(const int direction) {
        static_cast<void>((*document_.gallery_responsive_scroll).scroll_by(
            {static_cast<double>(direction) * 240.0, 0.0},
            direction < 0 ? ScrollEventType::large_decrement
                          : ScrollEventType::large_increment,
            ScrollEventType::small_increment, true));
    }

    void on_scrolled(ScrollEvent& event) {
        (*document_.gallery_responsive_scroll_status).set_text("Scroll · x " +
                                   std::to_string(static_cast<int>(event.new_value)) +
                                   " of " +
                                   std::to_string(static_cast<int>(
                                       (*document_.gallery_responsive_scroll).horizontal_scroll().maximum())));
    }

    ResponsiveLayoutsDocument<ResponsiveLayoutsPage> document_{};
    std::array<std::shared_ptr<CommandBar>, 3> shelf_bars_;
    std::shared_ptr<ListBox> folders_;
    std::shared_ptr<Label> detail_text_;
    Value<double> stage_width_{stage_maximum};
    Value<bool> inspector_protected_{false};
    Command show_detail_{};
    Command show_master_{};
    Command scroll_previous_{};
    Command scroll_next_{};
};

void add_responsive_layouts(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ResponsiveLayoutsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
