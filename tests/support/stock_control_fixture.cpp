#include "stock_control_fixture.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace gui_forms::test_support {

void StockEffectCounter::clicked(ButtonBase& sender) {
    ++total_;
    const std::string identifier(sender.stable_id().value());
    for (Count& count : counts_) {
        if (count.stable_id == identifier) {
            ++count.presses;
            return;
        }
    }
    counts_.push_back(Count{identifier, 1U});
}

void StockEffectCounter::invoked(const CommandInvocation&) { ++total_; }

void StockEffectCounter::drop_down_requested(DropDownButton&) { ++total_; }

void StockEffectCounter::dialog_closed(const TaskDialogResult&) { ++total_; }

void StockEffectCounter::list_column_clicked(const ListViewColumnClick&) { ++total_; }

void StockEffectCounter::grid_column_clicked(std::size_t) { ++total_; }

std::uint64_t StockEffectCounter::presses(std::string_view stable_id) const {
    for (const Count& count : counts_) {
        if (count.stable_id == stable_id) return count.presses;
    }
    return 0U;
}

void StockDockContent::make_panel(DockPanelCreation& creation) {
    std::shared_ptr<TextBox> field =
        make_control<TextBox>(StableId("dock-site.field." + creation.panel.value));
    (*field).accessible_name = "Panel field " + creation.panel.value;
    (*field).set_requested_bounds({8.0, 8.0, 150.0, 24.0});
    creation.content = field;
}

namespace {

// Places controls top to bottom in columns of one width, starting a new
// column when the next control would pass the fixture's bottom edge.
class ColumnPlacer final {
public:
    static constexpr double margin = 8.0;
    static constexpr double column_width = 300.0;
    static constexpr double gap = 6.0;

    Rect next(double height) {
        if (y_ + height > stock_control_fixture_size.height - margin && y_ > margin) {
            x_ += column_width + margin;
            y_ = margin;
        }
        const Rect placed{x_, y_, column_width, height};
        y_ += height + gap;
        return placed;
    }

private:
    double x_{margin};
    double y_{margin};
};

class FixtureBuilder final {
public:
    explicit FixtureBuilder(StockControlFixture& fixture) : fixture_(fixture) {}

    // Adds `control` under the root at the next place, records it under its
    // stable id, and returns it.
    template <typename ControlType>
    std::shared_ptr<ControlType> place(std::shared_ptr<ControlType> control, double height) {
        const Rect bounds = placer_.next(height);
        (*control).set_requested_bounds(bounds);
        (*fixture_.root).add_child(control);
        record(control);
        return control;
    }

    // Records a control placed inside another placed control.
    void record(const Control::Ptr& control) {
        std::string identifier((*control).stable_id().value());
        std::string manifest_id(identifier);
        std::replace(manifest_id.begin(), manifest_id.end(), '-', '_');
        fixture_.controls.push_back(StockControlEntry{std::move(manifest_id), std::move(identifier), control});
    }

    std::shared_ptr<Command> command(std::string stable_id, std::string text) {
        std::shared_ptr<Command> made = std::make_shared<Command>(std::move(stable_id), std::move(text));
        fixture_.commands.push_back(made);
        return made;
    }

    void omit(std::string manifest_id, std::string reason) {
        fixture_.omissions.push_back(StockControlOmission{std::move(manifest_id), std::move(reason)});
    }

    StockControlFixture& fixture() noexcept { return fixture_; }

private:
    StockControlFixture& fixture_;
    ColumnPlacer placer_;
};

void place_commands(FixtureBuilder& builder) {
    static_cast<void>(builder.place(make_control<Button>(StableId("button"), "Apply"), 28.0));
    static_cast<void>(builder.place(make_control<DropDownButton>(StableId("drop-down-button"), "Export"), 30.0));
    static_cast<void>(builder.place(make_control<LinkLabel>(StableId("link-label"), "Learn more"), 24.0));
    std::shared_ptr<Command> save = builder.command("stock.save", "&Save");
    std::shared_ptr<Command> open = builder.command("stock.open", "&Open");
    std::shared_ptr<CommandBar> bar = builder.place(
        make_control<CommandBar>(StableId("command-bar")), 32.0);
    (*bar).accessible_name = "File commands";
    (*bar).set_items({CommandItem{.id = 1, .label = "Save", .command = save.get()},
                      CommandItem{.id = 2, .label = "Open", .command = open.get()}});
    builder.omit("button_base", "base type; placed as Button, DropDownButton, LinkLabel, CheckBox and RadioButton");
}

void place_text(FixtureBuilder& builder) {
    static_cast<void>(builder.place(make_control<Label>(StableId("label"), "Ready"), 22.0));
    std::shared_ptr<TextBox> text = builder.place(
        make_control<TextBox>(StableId("text-box"), "hello"), 28.0);
    (*text).accessible_name = "Artwork text";
    std::shared_ptr<TokenBox> tokens = builder.place(
        make_control<TokenBox>(StableId("token-box")), 30.0);
    (*tokens).accessible_name = "Recipients";
    static_cast<void>((*tokens).add_token("ada"));
    std::shared_ptr<MaskedTextBox> masked = builder.place(
        make_control<MaskedTextBox>(StableId("masked-text-box"), "00000"), 28.0);
    (*masked).accessible_name = "Code";
}

void place_display(FixtureBuilder& builder) {
    std::shared_ptr<PictureBox> picture = builder.place(
        make_control<PictureBox>(StableId("picture-box")), 40.0);
    (*picture).accessible_name = "Preview";
    std::shared_ptr<ProgressBar> progress = builder.place(
        make_control<ProgressBar>(StableId("progress-bar")), 20.0);
    (*progress).accessible_name = "Export progress";
    (*progress).set_value(40);
    std::shared_ptr<Card> card = builder.place(make_control<Card>(StableId("card")), 70.0);
    (*card).accessible_name = "Summary card";
    std::shared_ptr<ReviewCard> review = builder.place(
        make_control<ReviewCard>(StableId("review-card")), 90.0);
    (*review).accessible_name = "Review";
    (*review).interactive = true;
}

void place_lists(FixtureBuilder& builder) {
    std::shared_ptr<ListBox> list = builder.place(make_control<ListBox>(StableId("list-box")), 80.0);
    (*list).accessible_name = "Colours";
    (*list).items = {"Red", "Green", "Blue"};
    std::shared_ptr<CheckedListBox> checks = builder.place(
        make_control<CheckedListBox>(StableId("checked-list-box")), 80.0);
    (*checks).accessible_name = "Options";
    (*checks).items = {"Wrap", "Spell"};
    std::shared_ptr<ComboBox> combo = builder.place(make_control<ComboBox>(StableId("combo-box")), 28.0);
    (*combo).accessible_name = "Size";
    (*combo).items = {"Small", "Large"};

    std::shared_ptr<TreeView> tree = builder.place(make_control<TreeView>(StableId("tree-view")), 90.0);
    (*tree).accessible_name = "Folders";
    TreeViewItem parent;
    parent.stable_id = "tree-view.parent";
    parent.text = "Parent";
    parent.expandable = true;
    parent.expanded = true;
    TreeViewItem child;
    child.stable_id = "tree-view.child";
    child.text = "Child";
    child.depth = 1U;
    TreeViewItem closed;
    closed.stable_id = "tree-view.closed";
    closed.text = "Closed";
    closed.expandable = true;
    (*tree).set_items({parent, child, closed});

    std::shared_ptr<ObjectView> objects = builder.place(
        make_control<ObjectView>(StableId("object-view")), 100.0);
    (*objects).accessible_name = "Objects";
    (*objects).view_mode = ObjectViewMode::details;
    std::vector<ObjectDetailsColumn> object_columns;
    object_columns.push_back({.id = {"name"}, .label = "Name", .width = 150.0});
    object_columns.push_back({.id = {"kind"}, .label = "Kind", .width = 100.0});
    std::vector<ObjectViewItem> object_items;
    for (std::size_t index = 0U; index < 2U; ++index) {
        ObjectViewItem item;
        item.stable_id = "object-view.item." + std::to_string(index);
        item.name = "Object " + std::to_string(index);
        item.cells.push_back({{"name"}, item.name});
        item.cells.push_back({{"kind"}, "File"});
        object_items.push_back(item);
    }
    ObjectDetailsSort sort;
    sort.column = ObjectColumnId{"name"};
    (*objects).set_details_model(std::move(object_columns), std::move(object_items), sort);

    std::shared_ptr<ListView> list_view = builder.place(
        make_control<ListView>(StableId("list-view")), 100.0);
    (*list_view).accessible_name = "Files";
    std::vector<ListViewColumn> list_columns;
    list_columns.push_back({.stable_id = "name", .text = "Name", .width = 140.0});
    list_columns.push_back({.stable_id = "size", .text = "Size", .width = 80.0});
    (*list_view).set_columns(std::move(list_columns));
    std::vector<ListViewItem> list_items;
    for (std::size_t index = 0U; index < 2U; ++index) {
        ListViewItem item;
        item.stable_id = "list-view.item." + std::to_string(index);
        item.text = index == 0U ? "Zebra" : "Apple";
        item.sub_items = {std::to_string(index + 1U) + " KB"};
        list_items.push_back(std::move(item));
    }
    (*list_view).set_items(std::move(list_items));

    std::shared_ptr<TreeListView> tree_list = builder.place(
        make_control<TreeListView>(StableId("tree-list-view")), 100.0);
    (*tree_list).accessible_name = "Sources";
    std::vector<TreeListColumn> tree_columns;
    tree_columns.push_back({.stable_id = "name", .text = "Name", .width = 160.0});
    tree_columns.push_back({.stable_id = "kind", .text = "Kind", .width = 90.0});
    (*tree_list).set_columns(std::move(tree_columns));
    std::vector<TreeListNode> nodes;
    TreeListNode folder;
    folder.stable_id = "tree-list-view.src";
    folder.text = "src";
    folder.depth = 0U;
    folder.cells = {"Folder"};
    folder.expanded = true;
    nodes.push_back(folder);
    TreeListNode file;
    file.stable_id = "tree-list-view.main";
    file.text = "main.cpp";
    file.depth = 1U;
    file.cells = {"C++"};
    nodes.push_back(file);
    (*tree_list).set_nodes(std::move(nodes));

    std::shared_ptr<CorrespondenceView> correspondence = builder.place(
        make_control<CorrespondenceView>(StableId("correspondence-view")), 100.0);
    (*correspondence).accessible_name = "Correspondence";
    CorrespondenceItem first;
    first.stable_id = "correspondence-view.first";
    first.title = "First";
    CorrespondenceItem second;
    second.stable_id = "correspondence-view.second";
    second.title = "Second";
    (*correspondence).set_items({first, second});

    std::shared_ptr<DataGridView> grid = builder.place(
        make_control<DataGridView>(StableId("data-grid-view")), 100.0);
    (*grid).accessible_name = "Grid";
    DataGridViewColumn name_column;
    name_column.name = "name";
    name_column.header_text = "Name";
    (*grid).set_columns({name_column});
    DataGridViewRow zebra;
    zebra.stable_id = DataGridViewRowId{"row0"};
    zebra.cells = {BindingValue{std::string("Zebra")}};
    DataGridViewRow apple;
    apple.stable_id = DataGridViewRowId{"row1"};
    apple.cells = {BindingValue{std::string("Apple")}};
    (*grid).rows = {zebra, apple};

    std::shared_ptr<ChoiceGroup> choices = builder.place(
        make_control<ChoiceGroup>(StableId("choice-group")), 32.0);
    (*choices).accessible_name = "Alignment";
    (*choices).set_items({ChoiceItem{.id = 1, .label = "Left"}, ChoiceItem{.id = 2, .label = "Right"}});

    std::shared_ptr<BreadcrumbTrail> trail = builder.place(
        make_control<BreadcrumbTrail>(StableId("breadcrumb-trail")), 30.0);
    (*trail).accessible_name = "Location";
    BreadcrumbSegment home;
    home.stable_id = "breadcrumb-trail.home";
    home.text = "Home";
    BreadcrumbSegment docs;
    docs.stable_id = "breadcrumb-trail.docs";
    docs.text = "Documents";
    (*trail).set_segments({home, docs});
}

void place_values(FixtureBuilder& builder) {
    static_cast<void>(builder.place(make_control<CheckBox>(StableId("check-box"), "Wrap lines"), 24.0));
    static_cast<void>(builder.place(make_control<RadioButton>(StableId("radio-button"), "Metric"), 24.0));
    std::shared_ptr<RangeControl> range = builder.place(
        make_control<RangeControl>(StableId("range-control")), 24.0);
    (*range).accessible_name = "Range";
    std::shared_ptr<TrackBar> track = builder.place(make_control<TrackBar>(StableId("track-bar")), 32.0);
    (*track).accessible_name = "Volume";
    std::shared_ptr<NumericUpDown> number = builder.place(
        make_control<NumericUpDown>(StableId("numeric-up-down")), 28.0);
    (*number).accessible_name = "Canvas X";
    (*number).set_range(0, 200);
    std::shared_ptr<DomainUpDown> domain = builder.place(
        make_control<DomainUpDown>(StableId("domain-up-down")), 28.0);
    (*domain).accessible_name = "Band";
    (*domain).items = {"AM", "FM", "WFM"};
    std::shared_ptr<ColorValueEditor> colour = builder.place(
        make_control<ColorValueEditor>(StableId("color-value-editor"), Color{40, 80, 160, 255}), 30.0);
    (*colour).accessible_name = "Tint";
    std::shared_ptr<FlagsValueEditor> flags = builder.place(
        make_control<FlagsValueEditor>(
            StableId("flags-value-editor"),
            PropertyEnumDescriptor{"Edges", {{"None", 0}, {"Top", 1}, {"Left", 2}}, true},
            PropertyEnumValue{"Edges", "Top", 1}),
        30.0);
    (*flags).accessible_name = "Edges";
    std::shared_ptr<PropertyList> properties = builder.place(
        make_control<PropertyList>(StableId("property-list")), 90.0);
    (*properties).accessible_name = "Selection properties";
    (*properties).set_groups({{"property-list.identity", "IDENTITY",
                               {{"property-list.kind", "Kind", "PNG image", "The file's kind"}}}});
    std::shared_ptr<DateTimePicker> picker = builder.place(
        make_control<DateTimePicker>(StableId("date-time-picker")), 28.0);
    (*picker).accessible_name = "Due date";
    std::shared_ptr<MonthCalendar> calendar = builder.place(
        make_control<MonthCalendar>(StableId("month-calendar")), 220.0);
    (*calendar).accessible_name = "Calendar";
    std::shared_ptr<InstrumentRack> rack = builder.place(
        make_control<InstrumentRack>(StableId("instrument-rack")), 90.0);
    (*rack).accessible_name = "Criteria rack";
}

void place_property_grid(FixtureBuilder& builder, const Control::Ptr& target) {
    std::shared_ptr<PropertyGrid> grid = builder.place(
        make_control<PropertyGrid>(StableId("property-grid")), 200.0);
    (*grid).accessible_name = "Properties";
    (*grid).set_selected_object(target);
}

void place_grouping(FixtureBuilder& builder) {
    std::shared_ptr<Panel> panel = builder.place(make_control<Panel>(StableId("panel")), 30.0);
    (*panel).accessible_name = "Plain panel";
    std::shared_ptr<GroupBox> group = builder.place(
        make_control<GroupBox>(StableId("group-box"), "Output"), 50.0);
    static_cast<void>(group);
    std::shared_ptr<ScaledGroupBox> scaled_group = builder.place(
        make_control<ScaledGroupBox>(StableId("scaled-group-box"), "Scaled"), 50.0);
    static_cast<void>(scaled_group);
    std::shared_ptr<MaterialPanel> material = builder.place(
        make_control<MaterialPanel>(StableId("material-panel")), 30.0);
    (*material).accessible_name = "Material";
    std::shared_ptr<ExpandableSections> sections = builder.place(
        make_control<ExpandableSections>(StableId("expandable-sections")), 80.0);
    (*sections).accessible_name = "Sections";
    std::shared_ptr<Label> section_body = make_control<Label>(StableId("expandable-sections.body"), "Body");
    (*sections).set_items({SectionItem{.id = 1, .label = "General", .content = section_body}});
    std::shared_ptr<ContainerControl> container = builder.place(
        make_control<ContainerControl>(StableId("container-control")), 30.0);
    (*container).accessible_name = "Container";
    std::shared_ptr<UserControl> user = builder.place(
        make_control<UserControl>(StableId("user-control")), 30.0);
    (*user).accessible_name = "User control";
}

void place_pages(FixtureBuilder& builder) {
    std::shared_ptr<TabControl> tabs = builder.place(
        make_control<TabControl>(StableId("tab-control")), 130.0);
    std::shared_ptr<TabPage> overview = make_control<TabPage>(StableId("tab-page"), "Overview");
    (*tabs).add_page(overview);
    builder.record(overview);
    std::shared_ptr<TabPage> details = make_control<TabPage>(StableId("tab-control.details"), "Details");
    (*tabs).add_page(details);

    std::shared_ptr<PageView> view = builder.place(make_control<PageView>(StableId("page-view")), 130.0);
    (*view).accessible_name = "Page view";
    std::shared_ptr<Label> summary = make_control<Label>(StableId("page-view.summary"), "Summary body");
    PageRecord summary_record{};
    summary_record.id = PageId{"summary"};
    summary_record.title = "Summary";
    static_cast<void>((*view).add_page(summary_record, summary));
    std::shared_ptr<Label> notes = make_control<Label>(StableId("page-view.notes"), "Notes body");
    PageRecord notes_record{};
    notes_record.id = PageId{"notes"};
    notes_record.title = "Notes";
    static_cast<void>((*view).add_page(notes_record, notes));

    StockControlFixture& fixture = builder.fixture();
    fixture.page_strip_pages = std::make_unique<PageSet>();
    PageSet& pages = *fixture.page_strip_pages;
    const std::array<const char*, 2> strip_ids{"first", "second"};
    for (const char* identifier : strip_ids) {
        PageRecord record{};
        record.id = PageId{identifier};
        record.title = identifier;
        static_cast<void>(pages.insert(record, PagePlacement::end_of_group()));
    }
    std::shared_ptr<PageStrip> strip = builder.place(
        make_control<PageStrip>(StableId("page-strip"), pages), 30.0);
    (*strip).accessible_name = "Page strip";

    std::shared_ptr<NavigationPane> navigation = builder.place(
        make_control<NavigationPane>(StableId("navigation-pane")), 160.0);
    (*navigation).accessible_name = "Navigation";
    const std::array<const char*, 2> navigation_ids{"mail", "calendar"};
    for (const char* identifier : navigation_ids) {
        PageRecord record{};
        record.id = PageId{identifier};
        record.title = identifier;
        static_cast<void>((*navigation).pages().insert(record, PagePlacement::end_of_group()));
    }
}

void place_docking(FixtureBuilder& builder) {
    StockControlFixture& fixture = builder.fixture();
    fixture.dock_content = std::make_unique<StockDockContent>();
    fixture.dock_workspace = std::make_unique<DockWorkspace>("stock.workspace");
    DockWorkspace& workspace = *fixture.dock_workspace;
    const Delegate<DockPanelCreation&> factory =
        Delegate<DockPanelCreation&>::bind<StockDockContent, &StockDockContent::make_panel>(*fixture.dock_content);
    DockPanelRegistration tool{};
    tool.descriptor.id = DockPanelId("toolbox");
    tool.descriptor.default_placement.side = DockSide::left;
    tool.descriptor.minimum_size = {100.0, 60.0};
    tool.title = "Toolbox";
    tool.factory = factory;
    static_cast<void>(workspace.register_panel(tool));
    DockPanelRegistration document{};
    document.descriptor.id = DockPanelId("notes");
    document.descriptor.kind = DockPanelKind::document;
    document.descriptor.minimum_size = {120.0, 60.0};
    document.title = "Notes";
    document.factory = factory;
    static_cast<void>(workspace.register_panel(document));
    static_cast<void>(workspace.show_panel(DockPanelId("notes")));
    static_cast<void>(workspace.show_panel(DockPanelId("toolbox")));
    std::shared_ptr<DockSite> site = builder.place(
        make_control<DockSite>(StableId("dock-site"), workspace), 200.0);
    (*site).accessible_name = "Docked workspace";

    std::shared_ptr<DockNavigator> navigator = builder.place(
        make_control<DockNavigator>(StableId("dock-navigator")), 100.0);
    (*navigator).set_entries({{DockPanelId("toolbox"), "Toolbox"}}, {{DockPanelId("notes"), "Notes"}});
    builder.omit("dock_workspace", "component; its layout is published through the placed DockSite");
}

void place_layout(FixtureBuilder& builder) {
    std::shared_ptr<FlowLayoutPanelCompat> flow = builder.place(
        make_control<FlowLayoutPanelCompat>(StableId("flow-layout-panel-compat")), 30.0);
    (*flow).accessible_name = "Flow";
    std::shared_ptr<TableLayoutPanelCompat> table = builder.place(
        make_control<TableLayoutPanelCompat>(StableId("table-layout-panel-compat")), 30.0);
    (*table).accessible_name = "Table";
    std::shared_ptr<ResponsiveTrackPanel> tracks = builder.place(
        make_control<ResponsiveTrackPanel>(StableId("responsive-track-panel")), 30.0);
    (*tracks).accessible_name = "Tracks";
    (*tracks).set_track_specs({{ResponsiveTrackSizeMode::remaining, 80.0, 120.0, 0.0, 1.0}});
    std::shared_ptr<ScaledPanel> scaled = builder.place(
        make_control<ScaledPanel>(StableId("scaled-panel"), Size{100.0, 30.0}), 30.0);
    (*scaled).accessible_name = "Scaled";
    std::shared_ptr<SplitContainer> split = builder.place(
        make_control<SplitContainer>(StableId("split-container")), 60.0);
    (*split).accessible_name = "Split";
    std::shared_ptr<SplitterPanel> splitter_panel = builder.place(
        make_control<SplitterPanel>(StableId("splitter-panel")), 30.0);
    (*splitter_panel).accessible_name = "Splitter panel";
    // Undocked, the splitter has no target and is an inert bar that does not
    // repaint under the pointer.
    std::shared_ptr<Splitter> splitter = builder.place(
        make_control<Splitter>(StableId("splitter")), 8.0);
    (*splitter).dock = DockStyle::none;
    (*splitter).accessible_name = "Splitter";
    std::shared_ptr<CommandOverflowPanel> overflow = builder.place(
        make_control<CommandOverflowPanel>(StableId("command-overflow-panel")), 30.0);
    (*overflow).accessible_name = "Overflow";
    std::shared_ptr<Panel> overflow_group = make_control<Panel>(StableId("command-overflow-panel.group"));
    (*overflow_group).set_requested_bounds({0.0, 0.0, 120.0, 26.0});
    std::shared_ptr<Button> overflow_view = make_control<Button>(StableId("command-overflow-panel.view"), "View");
    (*overflow_view).set_requested_bounds({0.0, 0.0, 80.0, 24.0});
    (*overflow_group).add_child(overflow_view);
    (*overflow).add_child(overflow_group);
    (*overflow).group_spec[*overflow_group] = CommandOverflowGroupSpec{120.0, 120.0, 120.0, 1U};
    std::shared_ptr<MasterDetailView> master_detail = builder.place(
        make_control<MasterDetailView>(StableId("master-detail-view")), 60.0);
    (*master_detail).accessible_name = "Master detail";
    std::shared_ptr<ScrollableControl> scrollable = builder.place(
        make_control<ScrollableControl>(StableId("scrollable-control")), 30.0);
    (*scrollable).accessible_name = "Scrollable";
    std::shared_ptr<ScrollBar> scroll = builder.place(make_control<ScrollBar>(StableId("scroll-bar")), 18.0);
    (*scroll).accessible_name = "Scroll";
    std::shared_ptr<HScrollBar> horizontal = builder.place(
        make_control<HScrollBar>(StableId("h-scroll-bar")), 18.0);
    (*horizontal).accessible_name = "Horizontal";
    std::shared_ptr<VScrollBar> vertical = builder.place(
        make_control<VScrollBar>(StableId("v-scroll-bar")), 80.0);
    (*vertical).accessible_name = "Vertical";
}

void place_menus(FixtureBuilder& builder) {
    std::shared_ptr<Command> open = builder.command("stock.menu.open", "Open");
    std::shared_ptr<Command> save = builder.command("stock.strip.save", "Save");
    std::shared_ptr<Command> cut = builder.command("stock.cut", "Cut");

    std::shared_ptr<MenuStrip> menu = builder.place(make_control<MenuStrip>(StableId("menu-strip")), 26.0);
    MenuStripItemSpec file;
    file.stable_id = "menu-strip.file";
    file.text = "&File";
    file.items = {{"menu-strip.file.open", MenuItemKind::command, open}};
    (*menu).set_items({file});

    std::shared_ptr<ToolStrip> strip = builder.place(make_control<ToolStrip>(StableId("tool-strip")), 26.0);
    (*strip).accessible_name = "Tools";
    std::vector<ToolStripItemSpec> strip_items;
    strip_items.push_back({.stable_id = "tool-strip.save", .text = "Save", .command = save});
    (*strip).set_items(std::move(strip_items));

    std::shared_ptr<StatusStrip> status = builder.place(
        make_control<StatusStrip>(StableId("status-strip")), 24.0);
    (*status).accessible_name = "Status";
    std::vector<ToolStripItemSpec> status_items;
    status_items.push_back({.stable_id = "status-strip.state", .text = "Ready", .command = cut});
    (*status).set_items(std::move(status_items));

    std::shared_ptr<QuickAccessToolbar> quick = builder.place(
        make_control<QuickAccessToolbar>(StableId("quick-access-toolbar")), 26.0);
    (*quick).set_commands({{.command = save}});

    std::shared_ptr<ToolStripPanel> strip_panel = builder.place(
        make_control<ToolStripPanel>(StableId("tool-strip-panel")), 30.0);
    std::shared_ptr<ToolStrip> joined = make_control<ToolStrip>(StableId("tool-strip-panel.strip"));
    (*joined).accessible_name = "Joined tools";
    std::vector<ToolStripItemSpec> joined_items;
    joined_items.push_back({.stable_id = "tool-strip-panel.strip.open", .text = "Open", .command = open});
    (*joined).set_items(std::move(joined_items));
    (*strip_panel).join(joined);

    std::shared_ptr<ToolStripContainer> container = builder.place(
        make_control<ToolStripContainer>(StableId("tool-strip-container")), 60.0);
    (*container).accessible_name = "Frame";

    std::shared_ptr<BindingNavigator> navigator = builder.place(
        make_control<BindingNavigator>(StableId("binding-navigator")), 26.0);
    static_cast<void>(navigator);

    std::shared_ptr<Ribbon> ribbon = builder.place(make_control<Ribbon>(StableId("ribbon")), 130.0);
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "ribbon.home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "ribbon.clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "ribbon.cut", .command = cut});
    home.groups.push_back(std::move(clipboard));
    tabs.push_back(std::move(home));
    (*ribbon).set_tabs(std::move(tabs));
    std::shared_ptr<BackstageView> ribbon_backstage = make_control<BackstageView>(StableId("ribbon.backstage"));
    (*ribbon_backstage).set_items({{.stable_id = "ribbon.backstage.open", .command = open}});
    (*ribbon).backstage = ribbon_backstage;

    std::shared_ptr<BackstageView> backstage = builder.place(
        make_control<BackstageView>(StableId("backstage-view")), 120.0);
    std::shared_ptr<Label> info = make_control<Label>(StableId("backstage-view.info"), "Document information");
    (*backstage).set_items({{.stable_id = "backstage-view.save", .command = save},
                            {.stable_id = "backstage-view.info.topic", .kind = BackstageItemKind::topic,
                             .text = "&Info", .page = info}});

    std::shared_ptr<RibbonGallery> gallery = builder.place(
        make_control<RibbonGallery>(StableId("ribbon-gallery")), 60.0);
    (*gallery).accessible_name = "Styles";
    (*gallery).set_item_size({40.0, 24.0});
    std::vector<RibbonGalleryItem> gallery_items;
    for (std::size_t index = 0U; index < 3U; ++index) {
        RibbonGalleryItem item;
        item.stable_id = "ribbon-gallery.item." + std::to_string(index);
        item.label = "Style " + std::to_string(index);
        gallery_items.push_back(std::move(item));
    }
    (*gallery).set_items({}, std::move(gallery_items));

    std::shared_ptr<RibbonFontControl> font = builder.place(
        make_control<RibbonFontControl>(StableId("ribbon-font-control")), 26.0);
    (*font).set_families({"Carlito", "Portsmouth"});
    std::shared_ptr<RibbonColorPicker> colour = builder.place(
        make_control<RibbonColorPicker>(StableId("ribbon-color-picker")), 30.0);
    (*colour).set_text("Font &colour");
    std::shared_ptr<Command> font_colour = builder.command("stock.font_colour", "Font colour");
    builder.fixture().color_command = std::make_shared<ColorCommand>(font_colour);
    (*colour).set_command(builder.fixture().color_command);

    builder.omit("context_menu", "component; an overlay that exists only while open");
    builder.omit("command", "component; published through the menus, strips and ribbon that present it");
    builder.omit("font_command", "component; published through RibbonFontControl");
    builder.omit("color_command", "component; published through RibbonColorPicker");
    builder.omit("gallery_command", "component; published through RibbonGallery");
}

void place_dialogs(FixtureBuilder& builder, const Control::Ptr& anchor) {
    AnchoredOverlayPlacement placement;
    placement.preferred_size = {160.0, 60.0};
    std::shared_ptr<AnchoredOverlayLayer> layer = builder.place(
        make_control<AnchoredOverlayLayer>(StableId("anchored-overlay-layer"), anchor, placement), 60.0);
    (*layer).accessible_name = "Anchored overlay";
    (*layer).set_content(make_control<Label>(StableId("anchored-overlay-layer.content"), "Overlay body"));
    std::shared_ptr<FontDialog> font = builder.place(make_control<FontDialog>(StableId("font-dialog")), 300.0);
    static_cast<void>(font);
    std::shared_ptr<TaskDialog> task = builder.place(make_control<TaskDialog>(StableId("task-dialog")), 160.0);
    TaskDialogPage page;
    page.caption = "Export";
    page.heading = "Export the bundle?";
    page.buttons = {{.stable_id = "export", .text = "Export"}, {.standard = TaskDialogStandardButton::cancel}};
    (*task).set_page(std::move(page));
    builder.omit("tool_tip", "component; an overlay shown on hover");
    builder.omit("ribbon_tool_tip", "component; an overlay shown on hover");
}

void place_drawing(FixtureBuilder& builder) {
    std::shared_ptr<DrawingSurface> drawing = builder.place(
        make_control<DrawingSurface>(StableId("drawing-surface")), 40.0);
    (*drawing).accessible_name = "Drawing";
    std::shared_ptr<RasterCanvas> canvas = builder.place(
        make_control<RasterCanvas>(StableId("raster-canvas")), 40.0);
    (*canvas).accessible_name = "Canvas";
    builder.omit("live_surface", "service type, not a control; it presents through a control's surface");
}

void place_diagnostics(FixtureBuilder& builder) {
    std::shared_ptr<EasingPreview> easing = builder.place(
        make_control<EasingPreview>(StableId("easing-preview")), 60.0);
    (*easing).accessible_name = "Easing";
    // Paused, so its published value does not move between two readings.
    (*easing).set_motion_policy(MotionPolicy{.enabled = true, .paused = true});
    std::shared_ptr<MetricsView> metrics = builder.place(
        make_control<MetricsView>(StableId("metrics-view")), 80.0);
    static_cast<void>(metrics);
    std::shared_ptr<TypographySpecimen> specimen = builder.place(
        make_control<TypographySpecimen>(StableId("typography-specimen"), "Specimen"), 60.0);
    static_cast<void>(specimen);
    std::shared_ptr<VisualInspectorView> inspector = builder.place(
        make_control<VisualInspectorView>(StableId("visual-inspector-view"), "button"), 80.0);
    static_cast<void>(inspector);
    std::shared_ptr<VisualInspectorOverlay> overlay = builder.place(
        make_control<VisualInspectorOverlay>(StableId("visual-inspector-overlay"), "button"), 40.0);
    static_cast<void>(overlay);
}

// Subscribes the effect counter to every ButtonBase and DropDownButton in
// `control`'s subtree, every task dialog's close, and every list view's and
// grid's column-header click (ordering rows is the application's work).
void observe_subtree(StockControlFixture& fixture, const Control::Ptr& control) {
    StockEffectCounter& effects = *fixture.effects;
    ButtonBase* const button = dynamic_cast<ButtonBase*>(control.get());
    if (button != nullptr) {
        fixture.subscriptions.push_back((*button).clicked().subscribe(
            Delegate<ButtonBase&>::bind<StockEffectCounter, &StockEffectCounter::clicked>(effects)));
    }
    DropDownButton* const drop_down = dynamic_cast<DropDownButton*>(control.get());
    if (drop_down != nullptr) {
        fixture.subscriptions.push_back((*drop_down).drop_down_requested().subscribe(
            Delegate<DropDownButton&>::bind<StockEffectCounter, &StockEffectCounter::drop_down_requested>(effects)));
    }
    TaskDialog* const dialog = dynamic_cast<TaskDialog*>(control.get());
    if (dialog != nullptr) {
        fixture.subscriptions.push_back((*dialog).closed().subscribe(
            Delegate<const TaskDialogResult&>::bind<StockEffectCounter, &StockEffectCounter::dialog_closed>(effects)));
    }
    ListView* const list_view = dynamic_cast<ListView*>(control.get());
    if (list_view != nullptr) {
        fixture.subscriptions.push_back((*list_view).column_click().subscribe(
            Delegate<const ListViewColumnClick&>::bind<StockEffectCounter, &StockEffectCounter::list_column_clicked>(effects)));
    }
    DataGridView* const grid = dynamic_cast<DataGridView*>(control.get());
    if (grid != nullptr) {
        fixture.subscriptions.push_back((*grid).column_header_click().subscribe(
            Delegate<std::size_t>::bind<StockEffectCounter, &StockEffectCounter::grid_column_clicked>(effects)));
    }
    for (const Control::Ptr& child : (*control).children()) observe_subtree(fixture, child);
}

void observe_effects(StockControlFixture& fixture) {
    StockEffectCounter& effects = *fixture.effects;
    for (const std::shared_ptr<Command>& command : fixture.commands) {
        fixture.subscriptions.push_back((*command).invoked().subscribe(
            Delegate<const CommandInvocation&>::bind<StockEffectCounter, &StockEffectCounter::invoked>(effects)));
    }
    observe_subtree(fixture, fixture.root);
}

void omit_components(FixtureBuilder& builder) {
    const std::array<const char*, 15> components{
        "timer", "background_worker", "notify_icon", "thread_exception_dialog",
        "image_list", "error_provider", "help_provider",
        "semantic_feedback", "binding", "binding_source", "binding_context", "open_file_dialog",
        "save_file_dialog", "folder_browser_dialog", "color_dialog"};
    for (const char* identifier : components) {
        builder.omit(identifier, "component without a place in the control tree");
    }
}

} // namespace

StockControlFixture make_stock_control_fixture() {
    StockControlFixture fixture;
    fixture.effects = std::make_shared<StockEffectCounter>();
    fixture.root = make_control<Panel>(StableId("stock"));
    FixtureBuilder builder(fixture);
    place_commands(builder);
    place_text(builder);
    place_display(builder);
    place_values(builder);
    place_lists(builder);
    place_grouping(builder);
    place_pages(builder);
    place_layout(builder);
    place_menus(builder);
    place_docking(builder);
    // Copied out of the entry list, which grows as controls are placed.
    const Control::Ptr property_target = (*find_stock_control(fixture, "label")).control;
    const Control::Ptr overlay_anchor = (*find_stock_control(fixture, "button")).control;
    place_property_grid(builder, property_target);
    place_dialogs(builder, overlay_anchor);
    place_drawing(builder);
    place_diagnostics(builder);
    omit_components(builder);
    observe_effects(fixture);
    return fixture;
}

const StockControlEntry* find_stock_control(const StockControlFixture& fixture,
                                            std::string_view stable_id) {
    for (const StockControlEntry& entry : fixture.controls) {
        if (entry.stable_id == stable_id) return &entry;
    }
    return nullptr;
}

} // namespace gui_forms::test_support
