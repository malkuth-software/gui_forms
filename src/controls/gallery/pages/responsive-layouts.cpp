#include "pages.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Responsive Layouts page. One Value<double> is the stage width:
// a TrackBar binds it, and the owner narrows four specimens to it so a
// ResponsiveTrackPanel collapses authored tracks, a CommandOverflowPanel
// collapses command groups into its actuator, a MasterDetailView drops to a
// compact presentation, and a ScrollableControl exposes more overflow.
class ResponsiveLayoutsPage final : public gui_forms::Component {
public:
    ResponsiveLayoutsPage(const std::shared_ptr<Surface>& page,
                          const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        (*page).add_at(label("gallery.responsive-layouts.heading", "RESPONSIVE LAYOUTS",
                             22.0, 700, shell_blue_dark),
                       {24.0, 18.0, 620.0, 34.0});
        (*page).add_at(label("gallery.responsive-layouts.lead",
                             "Narrow the stage: tracks and command groups collapse by authored priority, master/detail goes compact, and the scroller exposes more overflow.",
                             12.0, 400, muted),
                       {26.0, 52.0, 920.0, 24.0});
        build_width_control(page);
        build_tracks(page);
        build_shelf(page);
        build_master_detail(page);
        build_scroller(page);
        on(stage_width_.changed(), *this, &ResponsiveLayoutsPage::on_stage_width_changed);
        on(inspector_protected_.changed(), *this,
           &ResponsiveLayoutsPage::on_inspector_protected_changed);
        for (int index = 0; index < 3; ++index) {
            on((*shelf_bars_[static_cast<std::size_t>(index)]).invoked(), *this,
               &ResponsiveLayoutsPage::on_shelf_command, index);
        }
        on((*more_).clicked(), *this, &ResponsiveLayoutsPage::on_more);
        on((*folders_).selection_changed(), *this, &ResponsiveLayoutsPage::on_folder_selected);
        on((*master_detail_).presentation_changed(), *this,
           &ResponsiveLayoutsPage::on_presentation_changed);
        on(show_detail_.invoked(), *this, &ResponsiveLayoutsPage::on_show_detail);
        on(show_master_.invoked(), *this, &ResponsiveLayoutsPage::on_show_master);
        on(scroll_previous_.invoked(), *this, &ResponsiveLayoutsPage::on_scroll_step, -1);
        on(scroll_next_.invoked(), *this, &ResponsiveLayoutsPage::on_scroll_step, 1);
        on((*scroller_).scroll(), *this, &ResponsiveLayoutsPage::on_scrolled);
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

    [[nodiscard]] static std::shared_ptr<Panel> tile(
        const std::string& id, const std::string& text, Color background) {
        std::shared_ptr<Panel> panel = make_control<Panel>(StableId(id));
        (*panel).set_background(background);
        (*panel).set_border_style(BorderStyle::line);
        (*panel).dock = DockStyle::fill;
        (*panel).margin = Insets{};
        std::shared_ptr<Label> caption = label(id + ".text", text, 11.0, 600, ink);
        (*caption).set_text_wrapping(TextWrapping::word);
        (*caption).padding = {8.0, 4.0, 8.0, 4.0};
        (*caption).dock = DockStyle::fill;
        (*panel).add_child(caption);
        return panel;
    }

    void build_width_control(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> width_group = group(
            "gallery.responsive.width.group",
            "Stage width · one Value<double> bound to the slider and observed by the page",
            {930.0, 64.0});
        (*page).add_at(width_group, {24.0, 90.0, 930.0, 64.0});
        std::shared_ptr<TrackBar> width = make_control<TrackBar>(
            StableId("gallery.responsive.width"));
        (*width).accessible_name = "Stage width";
        (*width).set_range(stage_minimum, stage_maximum);
        (*width).set_small_change(10.0);
        (*width).set_large_change(100.0);
        (*width).set_tick_frequency(100.0);
        (*width).set_visual_style(TrackBarVisualStyle::filled);
        (*width).bind(stage_width_);
        (*width_group).add_at(width, {18.0, 26.0, 640.0, 30.0});
        width_status_ = label("gallery.responsive.width.value", "", 11.0, 600, green);
        (*width_group).add_at(width_status_, {672.0, 28.0, 240.0, 24.0});
        describe_width(stage_width_.get());
    }

    void build_tracks(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> tracks_group = group(
            "gallery.responsive.tracks.group",
            "ResponsiveTrackPanel · fixed + remaining + content tracks · priority collapse · focus reveal",
            {930.0, 118.0});
        (*page).add_at(tracks_group, {24.0, 164.0, 930.0, 118.0});
        tracks_ = make_control<ResponsiveTrackPanel>(StableId("gallery.responsive.tracks"));
        (*tracks_).accessible_name = "Responsive track specimen";
        (*tracks_).set_track_gap(6.0);
        (*tracks_).set_track_specs({
            {ResponsiveTrackSizeMode::fixed, 160.0, 160.0, 160.0, 1.0, false, 20U},
            {ResponsiveTrackSizeMode::remaining, 220.0, 320.0, 0.0, 1.0},
            {ResponsiveTrackSizeMode::fixed, 200.0, 200.0, 200.0, 1.0, false, 10U},
            {ResponsiveTrackSizeMode::content, 120.0, 120.0, 160.0, 1.0, false, 30U},
        });
        (*tracks_group).add_at(tracks_, {18.0, 30.0, stage_maximum, 48.0});
        constexpr std::array<std::string_view, 4> ids{
            "navigation", "content", "inspector", "activity"};
        constexpr std::array<std::string_view, 4> texts{
            "Navigation · fixed 160 · priority 20",
            "Content · remaining · never collapses",
            "Inspector · fixed 200 · priority 10",
            "Activity · content 120 · priority 30"};
        constexpr std::array<Color, 4> faces{
            Color::rgba(225, 235, 243), paper, Color::rgba(245, 243, 235),
            Color::rgba(238, 244, 236)};
        for (std::size_t index = 0U; index < ids.size(); ++index) {
            std::shared_ptr<Panel> child = tile(
                "gallery.responsive.tracks." + std::string(ids[index]),
                std::string(texts[index]), faces[index]);
            (*child).set_requested_bounds({0.0, 0.0, 120.0, 48.0});
            (*tracks_).add_child(child);
            (*tracks_).set_child_track(*child, index);
        }
        std::shared_ptr<CheckBox> protect = make_control<CheckBox>(
            StableId("gallery.responsive.tracks.protect"), "Protect the inspector track");
        (*protect).set_indicator_style(ChoiceIndicatorStyle::modern);
        (*protect).bind(inspector_protected_);
        (*tracks_group).add_at(protect, {18.0, 86.0, 250.0, 24.0});
        tracks_status_ = label("gallery.responsive.tracks.status", "", 11.0, 600, green);
        (*tracks_group).add_at(tracks_status_, {280.0, 86.0, 632.0, 24.0});
        describe_tracks();
    }

    void build_shelf(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> shelf_group = group(
            "gallery.responsive.shelf.group",
            "CommandOverflowPanel · three command groups · consumer-owned overflow actuator",
            {930.0, 112.0});
        (*page).add_at(shelf_group, {24.0, 292.0, 930.0, 112.0});
        shelf_ = make_control<CommandOverflowPanel>(StableId("gallery.responsive.shelf"));
        (*shelf_).accessible_name = "Command shelf specimen";
        (*shelf_).set_group_gap(8.0);
        (*shelf_group).add_at(shelf_, {18.0, 30.0, stage_maximum, 44.0});
        constexpr std::array<std::string_view, 3> ids{"selection", "arrange", "navigate"};
        constexpr std::array<double, 3> widths{270.0, 270.0, 180.0};
        for (std::size_t index = 0U; index < ids.size(); ++index) {
            const std::string id = "gallery.responsive.shelf." + std::string(ids[index]);
            std::shared_ptr<Panel> group_panel = make_control<Panel>(StableId(id));
            (*group_panel).accessible_name = std::string(shelf_names[index]) + " commands";
            (*group_panel).set_requested_bounds({0.0, 0.0, widths[index], 40.0});
            std::shared_ptr<CommandBar> bar = make_control<CommandBar>(StableId(id + ".bar"));
            (*bar).dock = DockStyle::fill;
            if (index == 0U) {
                (*bar).set_items({{.id = 1, .label = "Open"}, {.id = 2, .label = "Copy"},
                                  {.id = 3, .label = "Rename"}});
            } else if (index == 1U) {
                (*bar).set_items({{.id = 1, .label = "View"}, {.id = 2, .label = "Sort"},
                                  {.id = 3, .label = "Group"}});
            } else {
                (*bar).set_items({{.id = 1, .label = "Back"}, {.id = 2, .label = "Up"}});
            }
            (*group_panel).add_child(bar);
            (*shelf_).add_child(group_panel);
            // Arrange collapses first (priority 1), Selection second; Navigate
            // carries no priority and therefore never collapses.
            if (index == 0U) (*shelf_).set_group_spec(*group_panel, {270.0, 270.0, 270.0, 2U});
            if (index == 1U) (*shelf_).set_group_spec(*group_panel, {270.0, 270.0, 270.0, 1U});
            if (index == 2U) (*shelf_).set_group_spec(*group_panel, {180.0, 180.0, 180.0});
            shelf_bars_[index] = bar;
        }
        more_ = make_control<Button>(StableId("gallery.responsive.shelf.more"), "More…");
        (*more_).set_visual_style(ButtonVisualStyle::accent);
        (*more_).set_requested_bounds({0.0, 0.0, 90.0, 40.0});
        (*shelf_).add_child(more_);
        (*shelf_).set_overflow_actuator(*more_);
        shelf_status_ = label("gallery.responsive.shelf.status",
                              "Shelf · no command invoked yet · More… appears only when a group collapses",
                              11.0, 600, green);
        (*shelf_group).add_at(shelf_status_, {18.0, 80.0, 894.0, 24.0});
    }

    void build_master_detail(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> view_group = group(
            "gallery.responsive.master-detail.group",
            "MasterDetailView · side-by-side above 620 px · compact master/detail navigation below",
            {930.0, 142.0});
        (*page).add_at(view_group, {24.0, 414.0, 930.0, 142.0});
        master_detail_ = make_control<MasterDetailView>(
            StableId("gallery.responsive.master-detail"));
        (*master_detail_).accessible_name = "Folder browser specimen";
        MasterDetailLayout layout;
        layout.master_extent = 240.0;
        layout.master_minimum = 160.0;
        layout.detail_minimum = 300.0;
        layout.compact_threshold = 620.0;
        (*master_detail_).set_master_detail_layout(layout);
        (*view_group).add_at(master_detail_, {18.0, 30.0, stage_maximum, 70.0});
        folders_ = make_control<ListBox>(StableId("gallery.responsive.master-detail.master"));
        (*folders_).accessible_name = "Folders";
        (*folders_).set_item_height(22.0);
        (*folders_).set_items({std::string(folder_names[0]), std::string(folder_names[1]),
                               std::string(folder_names[2])});
        (*folders_).select_index(0U);
        static_cast<void>((*master_detail_).set_master(folders_));
        std::shared_ptr<Panel> detail = tile("gallery.responsive.master-detail.detail",
                                             "Inbox · 12 messages · detail pane", paper);
        detail_text_ = std::dynamic_pointer_cast<Label>(
            (*detail).children().front());
        static_cast<void>((*master_detail_).set_detail(detail));

        std::shared_ptr<Button> show_detail = make_control<Button>(
            StableId("gallery.responsive.master-detail.show-detail"), "Show detail");
        (*show_detail).bind(show_detail_);
        (*view_group).add_at(show_detail, {18.0, 106.0, 150.0, 28.0});
        std::shared_ptr<Button> show_master = make_control<Button>(
            StableId("gallery.responsive.master-detail.show-master"), "Show master");
        (*show_master).bind(show_master_);
        (*view_group).add_at(show_master, {180.0, 106.0, 150.0, 28.0});
        view_status_ = label("gallery.responsive.master-detail.status",
                             "Side by side · both roles visible · compact commands disabled",
                             11.0, 600, green);
        (*view_group).add_at(view_status_, {344.0, 108.0, 568.0, 24.0});
    }

    void build_scroller(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> scroll_group = group(
            "gallery.responsive.scroll.group",
            "ScrollableControl · AutoScroll viewport · wheel, track, thumb and command paging",
            {930.0, 84.0});
        (*page).add_at(scroll_group, {24.0, 566.0, 930.0, 84.0});
        scroller_ = make_control<Panel>(StableId("gallery.responsive.scroll"));
        (*scroller_).accessible_name = "Column scroller specimen";
        (*scroller_).set_background(Color::rgba(238, 243, 247));
        (*scroller_).set_border_style(BorderStyle::sunken);
        (*scroller_).auto_scroll = true;
        (*scroll_group).add_at(scroller_, {18.0, 30.0, stage_maximum - 200.0, 48.0});
        for (std::size_t index = 0U; index < 10U; ++index) {
            std::shared_ptr<Button> column = make_control<Button>(
                StableId("gallery.responsive.scroll.column." + std::to_string(index)),
                "Column " + std::to_string(index + 1U));
            (*column).set_visual_style(index % 2U == 0U ? ButtonVisualStyle::command
                                                         : ButtonVisualStyle::standard);
            (*column).set_requested_bounds(
                {8.0 + static_cast<double>(index) * 120.0, 4.0, 110.0, 22.0});
            (*scroller_).add_child(column);
        }
        std::shared_ptr<Button> previous = make_control<Button>(
            StableId("gallery.responsive.scroll.previous"), "Page left");
        (*previous).bind(scroll_previous_);
        (*scroll_group).add_at(previous, {732.0, 30.0, 86.0, 22.0});
        std::shared_ptr<Button> next = make_control<Button>(
            StableId("gallery.responsive.scroll.next"), "Page right");
        (*next).bind(scroll_next_);
        (*scroll_group).add_at(next, {826.0, 30.0, 86.0, 22.0});
        scroll_status_ = label("gallery.responsive.scroll.status",
                               "Scroll · x 0 · no user scroll yet", 11.0, 600, green);
        (*scroll_group).add_at(scroll_status_, {732.0, 56.0, 180.0, 22.0});
    }

    void describe_width(const double width) {
        (*width_status_).set_text("Stage " + std::to_string(static_cast<int>(width)) +
                                  " px · minimum " +
                                  std::to_string(static_cast<int>(stage_minimum)) +
                                  " · maximum " +
                                  std::to_string(static_cast<int>(stage_maximum)));
    }

    void describe_tracks() {
        (*tracks_status_).set_text(inspector_protected_.get()
            ? "Inspector revealed · navigation (20) then activity (30) collapse first"
            : "Automatic · inspector (10), navigation (20), activity (30) collapse in that order");
    }

    // The slider's state change (user or programmatic) narrows the stage. The
    // layout containers re-solve on the next arrange; no manual relayout.
    void on_stage_width_changed(const double width) {
        describe_width(width);
        const std::shared_ptr<LayoutGroup> tracks_group =
            std::dynamic_pointer_cast<LayoutGroup>((*tracks_).parent());
        const std::shared_ptr<LayoutGroup> shelf_group =
            std::dynamic_pointer_cast<LayoutGroup>((*shelf_).parent());
        const std::shared_ptr<LayoutGroup> view_group =
            std::dynamic_pointer_cast<LayoutGroup>((*master_detail_).parent());
        const std::shared_ptr<LayoutGroup> scroll_group =
            std::dynamic_pointer_cast<LayoutGroup>((*scroller_).parent());
        if (tracks_group) (*tracks_group).set_design_bounds(*tracks_, {18.0, 30.0, width, 48.0});
        if (shelf_group) (*shelf_group).set_design_bounds(*shelf_, {18.0, 30.0, width, 44.0});
        if (view_group) {
            (*view_group).set_design_bounds(*master_detail_, {18.0, 30.0, width, 70.0});
        }
        if (scroll_group) {
            (*scroll_group).set_design_bounds(
                *scroller_, {18.0, 30.0, std::max(120.0, width - 200.0), 48.0});
        }
    }

    void on_inspector_protected_changed(const bool protect) {
        (*tracks_).set_track_revealed(2U, protect);
        describe_tracks();
    }

    void on_shelf_command(const int group, const CommandItem& item) {
        (*shelf_status_).set_text("Shelf · " + std::string(shelf_names[static_cast<std::size_t>(group)]) +
                                  " · " + item.label + " invoked");
    }

    // Command authority stays with the page: the panel only reports which
    // groups it collapsed, by stable id, and the page composes the overflow.
    void on_more() {
        const CommandOverflowSnapshot snapshot = (*shelf_).layout_snapshot();
        std::string collapsed;
        for (const CommandOverflowGroupResult& group : snapshot.groups) {
            if (!group.collapsed) continue;
            if (!collapsed.empty()) collapsed += ", ";
            collapsed += group.stable_id;
        }
        (*shelf_status_).set_text(collapsed.empty()
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
        (*view_status_).set_text(
            change.current == MasterDetailDisplayMode::side_by_side
                ? "Side by side · both roles visible · compact commands disabled"
                : change.current == MasterDetailDisplayMode::master_only
                ? "Compact · master only · Show detail enabled"
                : "Compact · detail only · Show master enabled");
    }

    void on_show_detail() { (*master_detail_).show_detail(); }
    void on_show_master() { (*master_detail_).show_master(); }

    void on_scroll_step(const int direction) {
        static_cast<void>((*scroller_).scroll_by(
            {static_cast<double>(direction) * 240.0, 0.0},
            direction < 0 ? ScrollEventType::large_decrement
                          : ScrollEventType::large_increment,
            ScrollEventType::small_increment, true));
    }

    void on_scrolled(ScrollEvent& event) {
        (*scroll_status_).set_text("Scroll · x " +
                                   std::to_string(static_cast<int>(event.new_value)) +
                                   " of " +
                                   std::to_string(static_cast<int>(
                                       (*scroller_).horizontal_scroll().maximum())));
    }

    std::shared_ptr<Label> width_status_;
    std::shared_ptr<ResponsiveTrackPanel> tracks_;
    std::shared_ptr<Label> tracks_status_;
    std::shared_ptr<CommandOverflowPanel> shelf_;
    std::array<std::shared_ptr<CommandBar>, 3> shelf_bars_;
    std::shared_ptr<Button> more_;
    std::shared_ptr<Label> shelf_status_;
    std::shared_ptr<MasterDetailView> master_detail_;
    std::shared_ptr<ListBox> folders_;
    std::shared_ptr<Label> detail_text_;
    std::shared_ptr<Label> view_status_;
    std::shared_ptr<Panel> scroller_;
    std::shared_ptr<Label> scroll_status_;
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
