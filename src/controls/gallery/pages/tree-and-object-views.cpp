#include "pages.hpp"

#include "tree-and-object-views_form.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

constexpr std::size_t no_parent = static_cast<std::size_t>(-1);

// One fixture: a small project tree in preorder. Folders feed the TreeView and
// the BreadcrumbTrail; a folder's children feed the ObjectView; the files
// under a folder feed the CorrespondenceView ledger.
struct ProjectNode final {
    std::string_view id;
    std::string_view name;
    std::size_t parent;
    bool folder;
    ObjectGlyph glyph;
    std::string_view kind;
    std::string_view size;
    std::string_view modified;
    std::string_view note;
};

constexpr std::array<ProjectNode, 15> project{{
    {"root", "Portsmouth Gallery", no_parent, true, ObjectGlyph::folder, "Project", "", "2026-10-11", "Project root"},
    {"src", "src", 0, true, ObjectGlyph::folder, "Folder", "", "2026-10-11", "Sources"},
    {"controls", "controls", 1, true, ObjectGlyph::folder, "Folder", "", "2026-10-10", "Retained controls"},
    {"button.cpp", "button.cpp", 2, false, ObjectGlyph::code, "C++ source", "18 KB", "2026-10-09", "Dialog results and the four visual styles"},
    {"tree-view.cpp", "tree_view.cpp", 2, false, ObjectGlyph::code, "C++ source", "42 KB", "2026-10-10", "Visible-row virtualization and type-select"},
    {"core", "core", 1, true, ObjectGlyph::folder, "Folder", "", "2026-10-08", "Window and scheduler"},
    {"window.cpp", "window.cpp", 5, false, ObjectGlyph::code, "C++ source", "96 KB", "2026-10-08", "Overlay leases and focus scopes"},
    {"docs", "docs", 0, true, ObjectGlyph::folder, "Folder", "", "2026-10-07", "Atlas and manual"},
    {"atlas.md", "atlas.md", 7, false, ObjectGlyph::document, "Markdown", "7 KB", "2026-10-07", "Generated control atlas"},
    {"manual.json", "manual.json", 7, false, ObjectGlyph::code, "JSON", "121 KB", "2026-10-06", "Hand-reviewed narratives"},
    {"assets", "assets", 0, true, ObjectGlyph::folder, "Folder", "", "2026-10-05", "Images and fonts"},
    {"logo.png", "logo.png", 10, false, ObjectGlyph::image, "PNG image", "12 KB", "2026-10-05", "The only admitted codec"},
    {"fonts", "fonts", 10, true, ObjectGlyph::folder, "Folder", "", "2026-10-04", "Bundled faces"},
    {"carlito.ttf", "carlito.ttf", 12, false, ObjectGlyph::archive, "Font", "612 KB", "2026-10-04", "Body face"},
    {"readme.md", "README.md", 0, false, ObjectGlyph::document, "Markdown", "3 KB", "2026-10-11", "Start here"},
}};

constexpr std::string_view column_name = "name";
constexpr std::string_view column_kind = "kind";
constexpr std::string_view column_size = "size";
constexpr std::string_view column_modified = "modified";

[[nodiscard]] std::size_t node_index(const std::string_view id) {
    for (std::size_t index = 0; index < project.size(); ++index) {
        if (project[index].id == id) return index;
    }
    return no_parent;
}

[[nodiscard]] std::string node_path(std::size_t index) {
    std::string path(project[index].name);
    while (project[index].parent != no_parent) {
        index = project[index].parent;
        path = std::string(project[index].name) + " / " + path;
    }
    return path;
}

[[nodiscard]] bool descends_from(std::size_t index, const std::size_t ancestor) {
    while (index != no_parent) {
        if (index == ancestor) return true;
        index = project[index].parent;
    }
    return false;
}

[[nodiscard]] std::string_view cell_text(const ProjectNode& node, const std::string_view column) {
    if (column == column_name) return node.name;
    if (column == column_kind) return node.kind;
    if (column == column_size) return node.size;
    return node.modified;
}

// The DataGridView's rows on the stable-ID virtual model, as ObjectView's: the
// page owns each file's values, keyed by row ID, and the order the rows are
// presented in. The grid holds only the order and asks for the values of the
// cells it shows by ID.
class ProjectFileRows final : public DataGridViewCellValueProvider {
public:
    // Name, kind, size, keep, note, the button column's empty value, the
    // preview image, which the Gallery's fixtures install once the window
    // holds the image, and the link column's empty value.
    static constexpr std::size_t column_count = 8U;
    using FileValues = std::array<BindingValue, column_count>;
    using ValuesById = std::unordered_map<std::string, FileValues>;

    ProjectFileRows() {
        for (const ProjectNode& node : project) {
            if (node.folder) continue;
            const DataGridViewRowId row{std::string(node.id)};
            const FileValues values{BindingValue{std::string(node.name)}, BindingValue{std::string(node.kind)},
                                    BindingValue{std::string(node.size)}, BindingValue{true},
                                    BindingValue{std::string(node.note)}, BindingValue{}, BindingValue{},
                                    BindingValue{}};
            values_.emplace(row.value, values);
            order_.push_back(row);
        }
    }

    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        const ValuesById::const_iterator found = values_.find(row.value);
        if (found == values_.end() || column >= column_count) return BindingValue{};
        return (*found).second[column];
    }

    bool push_cell_value(const DataGridViewRowId& row, std::size_t column, const BindingValue& value) override {
        const ValuesById::iterator found = values_.find(row.value);
        if (found == values_.end() || column >= column_count) return false;
        (*found).second[column] = value;
        return true;
    }

    // The rows' IDs in presentation order, for set_virtual_rows and
    // set_virtual_row_order.
    [[nodiscard]] const std::vector<DataGridViewRowId>& order() const noexcept { return order_; }

    // Orders the rows stably by one column's text; the values stay keyed by ID.
    void sort(std::size_t column, bool descending) {
        const RowTextOrder compare{&values_, column, descending};
        std::stable_sort(order_.begin(), order_.end(), compare);
    }

private:
    struct RowTextOrder final {
        const ValuesById* values{};
        std::size_t column{};
        bool descending{};

        bool operator()(const DataGridViewRowId& left, const DataGridViewRowId& right) const {
            const std::string left_text = binding_value_to_string((*values).at(left.value)[column]);
            const std::string right_text = binding_value_to_string((*values).at(right.value)[column]);
            if (descending) return right_text < left_text;
            return left_text < right_text;
        }
    };

    ValuesById values_{};
    std::vector<DataGridViewRowId> order_{};
};

// Owns the trees-and-object-views page: the controls
// demo/pages/tree-and-object-views.form describes, and their behaviour.
class TreeAndObjectViewsPage final : public Component {
public:
    TreeAndObjectViewsPage(const std::shared_ptr<Surface>& body,
                           const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        build_location();
        build_tree();
        build_objects();
        build_ledger();
        build_grid();
        build_tree_list();
        on(view_mode_.changed(), *this, &TreeAndObjectViewsPage::on_view_mode_changed);
        select_folder(node_index("controls"));
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_tree_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        const std::array<std::shared_ptr<ScaledGroupBox>, 6> groups{
            document_.gallery_tree_location, document_.gallery_tree_folders_group,
            document_.gallery_tree_objects_group, document_.gallery_tree_ledger_group,
            document_.gallery_tree_grid_group, document_.gallery_tree_tree_list_group};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
        trail_ = document_.gallery_tree_trail;
        tree_ = document_.gallery_tree_folders;
        objects_ = document_.gallery_tree_objects;
        ledger_ = document_.gallery_tree_ledger;
        grid_ = document_.gallery_tree_grid;
        tree_list_ = document_.gallery_tree_tree_list;
        status_ = document_.gallery_tree_status;
    }

    void build_location() {
        (*trail_).appearance = BreadcrumbAppearance::raised;
        on((*trail_).segment_activated(), *this, &TreeAndObjectViewsPage::on_segment_activated);
        on((*trail_).edit_started(), *this, &TreeAndObjectViewsPage::on_edit_started);
        on((*trail_).edit_committed(), *this, &TreeAndObjectViewsPage::on_edit_committed);
        on((*trail_).edit_cancelled(), *this, &TreeAndObjectViewsPage::on_edit_cancelled);
        // The view-mode row is a ChoiceGroup bound to a page-owned Value.
        ChoiceGroup& view_mode = *document_.gallery_tree_view_mode;
        view_mode.set_style(page_style());
        view_mode.set_items({{.id = 0, .label = "Icons"}, {.id = 1, .label = "Details"}});
        view_mode.bind(view_mode_);
    }

    void build_tree() {
        (*tree_).set_style(page_style());
        std::vector<TreeViewItem> items;
        for (std::size_t index = 0; index < project.size(); ++index) {
            const ProjectNode& node = project[index];
            if (!node.folder) continue;
            TreeViewItem item;
            item.stable_id = std::string(node.id);
            item.text = std::string(node.name);
            item.depth = depth_of(index);
            item.expandable = has_child_folder(index);
            item.expanded = node.id == "root" || node.id == "src";
            items.push_back(std::move(item));
        }
        (*tree_).set_items(std::move(items));
        on((*tree_).selection_changed(), *this, &TreeAndObjectViewsPage::on_folder_selected);
        on((*tree_).expansion_changed(), *this, &TreeAndObjectViewsPage::on_expansion_changed);
        on((*tree_).item_activated(), *this, &TreeAndObjectViewsPage::on_folder_activated);
    }

    void build_objects() {
        (*objects_).set_style(page_style());
        (*objects_).icon_cell_size = {94.0, 84.0};
        (*objects_).view_mode = ObjectViewMode::details;
        on((*objects_).selection_changed(), *this, &TreeAndObjectViewsPage::on_object_selected);
        on((*objects_).item_activated(), *this, &TreeAndObjectViewsPage::on_object_activated);
        on((*objects_).sort_requested(), *this, &TreeAndObjectViewsPage::on_sort_requested);
    }

    void build_ledger() {
        (*ledger_).set_style(page_style());
        (*ledger_).compact_height = 44.0;
        (*ledger_).expanded_height = 124.0;
        on((*ledger_).selection_changed(), *this, &TreeAndObjectViewsPage::on_file_selected);
        on((*ledger_).pin_changed(), *this, &TreeAndObjectViewsPage::on_pin_changed);
        on((*ledger_).expansion_changed(), *this, &TreeAndObjectViewsPage::on_file_expansion);
        on((*ledger_).item_activated(), *this, &TreeAndObjectViewsPage::on_file_activated);
    }

    // A DataGridView over the fixture's files in virtual mode: a text name, a
    // combo box kind, a read-only size, a check box, a button, an image and
    // a link column. The rows are the page's, by stable row ID.
    void build_grid() {
        std::vector<std::string> kinds;
        for (const ProjectNode& node : project) {
            const std::string kind(node.kind);
            if (!node.folder && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) kinds.push_back(kind);
        }
        (*grid_).set_columns({
            {.name = "name", .header_text = "Name", .width = 160.0},
            {.name = "kind", .header_text = "Kind", .kind = DataGridViewColumnKind::combo_box, .width = 130.0,
             .items = kinds},
            {.name = "size", .header_text = "Size", .width = 80.0, .read_only = true,
             .default_cell_style = {.alignment = DataGridViewContentAlignment::middle_end}},
            {.name = "keep", .header_text = "Keep", .kind = DataGridViewColumnKind::check_box, .width = 56.0},
            {.name = "note", .header_text = "Note", .width = 300.0},
            {.name = "open", .header_text = "", .kind = DataGridViewColumnKind::button, .width = 70.0,
             .button_text = "Open"},
            {.name = "preview", .header_text = "Preview", .kind = DataGridViewColumnKind::image, .width = 70.0,
             .image_layout = DataGridViewImageLayout::zoom, .image_description = "Image preview"},
            {.name = "history", .header_text = "History", .kind = DataGridViewColumnKind::link, .width = 80.0,
             .link_text = "Changes"},
        });
        grid_rows_ = std::make_shared<ProjectFileRows>();
        (*grid_).set_virtual_rows(grid_rows_, (*grid_rows_).order());
        on((*grid_).cell_value_changed(), *this, &TreeAndObjectViewsPage::on_grid_value_changed);
        on((*grid_).cell_content_click(), *this, &TreeAndObjectViewsPage::on_grid_content_click);
        on((*grid_).column_header_click(), *this, &TreeAndObjectViewsPage::on_grid_header_click);
    }

    // A TreeListView over the whole fixture: folders and files in one tree
    // with Kind, Size and Modified columns; the note beside it is in the
    // document. A header click sorts siblings;
    // dragging a header moves its column.
    void build_tree_list() {
        (*tree_list_).set_columns({
            {.stable_id = std::string(column_name), .text = "Name", .width = 260.0},
            {.stable_id = std::string(column_kind), .text = "Kind", .width = 120.0},
            {.stable_id = std::string(column_size), .text = "Size", .width = 80.0,
             .alignment = TreeListAlignment::right},
            {.stable_id = std::string(column_modified), .text = "Modified", .width = 110.0},
        });
        std::vector<TreeListNode> nodes;
        for (std::size_t index = 0; index < project.size(); ++index) {
            const ProjectNode& node = project[index];
            TreeListNode item;
            item.stable_id = std::string(node.id);
            item.text = std::string(node.name);
            item.depth = depth_of(index);
            item.cells = {std::string(node.kind), std::string(node.size), std::string(node.modified)};
            item.expanded = node.id == "root" || node.id == "src" || node.id == "controls";
            nodes.push_back(std::move(item));
        }
        (*tree_list_).set_nodes(std::move(nodes));
        on((*tree_list_).selection_changed(), *this, &TreeAndObjectViewsPage::on_tree_list_selected);
        on((*tree_list_).sort_changed(), *this, &TreeAndObjectViewsPage::on_tree_list_sorted);
        on((*tree_list_).column_moved(), *this, &TreeAndObjectViewsPage::on_tree_list_column_moved);
    }

    [[nodiscard]] static std::size_t depth_of(std::size_t index) {
        std::size_t depth = 0;
        while (project[index].parent != no_parent) {
            index = project[index].parent;
            ++depth;
        }
        return depth;
    }

    [[nodiscard]] static bool has_child_folder(const std::size_t index) {
        for (const ProjectNode& node : project) {
            if (node.parent == index && node.folder) return true;
        }
        return false;
    }

    // Projects the fixture around one folder into every view.
    void project_folder(const std::size_t folder) {
        folder_ = folder;
        std::vector<BreadcrumbSegment> segments;
        for (std::size_t index = folder; index != no_parent; index = project[index].parent) {
            segments.insert(segments.begin(),
                            {std::string(project[index].id), std::string(project[index].name),
                             "Open " + std::string(project[index].name)});
        }
        (*trail_).set_segments(std::move(segments));
        (*trail_).active_id = std::string(project[folder].id);
        project_objects();
        project_ledger();
    }

    // Every surface routes a folder change through the TreeView's selection,
    // whose state event projects the views exactly once.
    void select_folder(const std::size_t folder) {
        for (std::size_t index = project[folder].parent; index != no_parent;
             index = project[index].parent) {
            (*tree_).set_expanded(project[index].id, true);
        }
        if ((*tree_).selected_id.read() == project[folder].id) {
            project_folder(folder);
            return;
        }
        (*tree_).selected_id = std::string(project[folder].id);
    }

    void project_objects() {
        std::vector<std::size_t> children;
        for (std::size_t index = 0; index < project.size(); ++index) {
            if (project[index].parent == folder_) children.push_back(index);
        }
        if (!sort_.column.value.empty()) {
            std::stable_sort(children.begin(), children.end(),
                             SortByColumn{sort_.column.value, sort_.direction});
        }
        std::vector<ObjectViewItem> items;
        for (const std::size_t index : children) {
            const ProjectNode& node = project[index];
            ObjectViewItem item;
            item.stable_id = std::string(node.id);
            item.name = std::string(node.name);
            item.secondary_text = std::string(node.kind);
            item.description = std::string(node.note);
            item.glyph = node.glyph;
            item.cells = {{{std::string(column_name)}, std::string(node.name)},
                          {{std::string(column_kind)}, std::string(node.kind)},
                          // A non-available cell must still carry text.
                          {{std::string(column_size)},
                           node.folder ? std::string("—") : std::string(node.size),
                           node.folder ? ObjectCellAvailability::not_applicable
                                       : ObjectCellAvailability::available},
                          {{std::string(column_modified)}, std::string(node.modified)}};
            items.push_back(std::move(item));
        }
        (*objects_).set_details_model(
            {{{std::string(column_name)}, "Name", 118.0},
             {{std::string(column_kind)}, "Kind", 86.0},
             {{std::string(column_size)}, "Size", 58.0, 40.0, 4096.0, ObjectColumnAlignment::right},
             {{std::string(column_modified)}, "Modified", 84.0}},
            std::move(items), sort_);
    }

    void project_ledger() {
        std::vector<CorrespondenceItem> items;
        for (std::size_t index = 0; index < project.size(); ++index) {
            const ProjectNode& node = project[index];
            if (node.folder || !descends_from(index, folder_)) continue;
            CorrespondenceItem item;
            item.stable_id = std::string(node.id);
            item.title = std::string(node.name);
            item.secondary_text = node_path(node.parent);
            item.metadata = std::string(node.modified);
            item.excerpt = std::string(node.note);
            item.metric = std::string(node.size);
            item.glyph = node.glyph;
            item.information_heading = "Details";
            item.information_tags = {std::string(node.kind)};
            item.information_detail = node_path(index);
            items.push_back(std::move(item));
        }
        (*ledger_).set_items(std::move(items));
        if (!(*ledger_).items().empty()) {
            (*ledger_).pinned_id = (*ledger_).items().front().stable_id;
        }
    }

    struct SortByColumn final {
        std::string_view column;
        ObjectSortDirection direction;
        [[nodiscard]] bool operator()(const std::size_t left, const std::size_t right) const {
            const std::string_view first = cell_text(project[left], column);
            const std::string_view second = cell_text(project[right], column);
            return direction == ObjectSortDirection::ascending ? first < second : second < first;
        }
    };

    // TreeView
    void on_folder_selected(const TreeSelectionChange& change) {
        const std::size_t folder = node_index(change.current_id);
        if (folder == no_parent) return;
        project_folder(folder);
        (*status_).set_text("Folder " + node_path(folder) + " selected · " +
                            std::to_string((*objects_).items().size()) + " children · " +
                            std::to_string((*ledger_).items().size()) + " files in the ledger");
    }

    void on_expansion_changed(const TreeExpansionChange& change) {
        (*status_).set_text("Folder " + change.stable_id + (change.expanded ? " expanded" : " collapsed") +
                            " · unrealized descendants keep their identity");
    }

    void on_folder_activated(const std::string& stable_id) {
        (*status_).set_text("Folder " + stable_id + " activated · Enter or double-click");
    }

    // ObjectView
    void on_object_selected(const ObjectSelectionChange& change) {
        (*status_).set_text(std::to_string(change.current_ids.size()) + " object(s) selected" +
                            (change.current_id.empty() ? std::string() : " · primary " + change.current_id));
    }

    void on_object_activated(const std::string& stable_id) {
        const std::size_t index = node_index(stable_id);
        if (index == no_parent) return;
        if (project[index].folder) {
            select_folder(index);
            (*status_).set_text("Opened " + node_path(index) + " from the ObjectView");
            return;
        }
        (*status_).set_text("File " + stable_id + " activated · " + std::string(project[index].note));
    }

    void on_sort_requested(const ObjectDetailsSort& requested) {
        sort_ = requested;
        project_objects();
        (*status_).set_text("Sorted by " + requested.column.value +
                            (requested.direction == ObjectSortDirection::ascending ? " ascending"
                                                                                   : " descending"));
    }

    // CorrespondenceView
    void on_file_selected(const CorrespondenceSelectionChange& change) {
        if (change.current_id.empty()) return;
        (*status_).set_text("Ledger entry " + change.current_id + " selected · " +
                            node_path(node_index(change.current_id)));
    }

    // A single click on a row both selects it and toggles the one pinned
    // (persistently expanded) entry, so this report lands after the
    // selection report above.
    void on_pin_changed(const CorrespondencePinChange& change) {
        if (change.current_id.empty()) {
            (*status_).set_text("Pinned entry cleared · " + change.previous_id +
                                " collapses back to its compact row");
            return;
        }
        (*status_).set_text("Pinned entry: " + change.current_id + " · " +
                            node_path(node_index(change.current_id)) +
                            " · stays expanded while the pointer moves on");
    }

    void on_file_expansion(const CorrespondenceExpansionChange& change) {
        if (change.reason != CorrespondenceExpansionReason::hover_intent) return;
        (*status_).set_text("Hover intent " + std::string(change.expanded ? "expanded " : "collapsed ") +
                            change.stable_id);
    }

    void on_file_activated(const std::string& stable_id) {
        const std::size_t index = node_index(stable_id);
        if (index == no_parent) return;
        select_folder(project[index].parent);
        (*objects_).selected_id = stable_id;
        (*status_).set_text("Ledger entry " + stable_id + " activated · its folder is now selected");
    }

    // BreadcrumbTrail
    void on_segment_activated(const std::string& stable_id) {
        const std::size_t index = node_index(stable_id);
        if (index == no_parent) return;
        select_folder(index);
        (*status_).set_text("Breadcrumb " + stable_id + " activated · tree selection follows");
    }

    void on_edit_started() {
        (*status_).set_text("Editing the exact path · Enter commits, Escape cancels");
    }

    void on_edit_committed(const std::string& text) {
        std::size_t current = no_parent;
        std::size_t start = 0;
        while (start <= text.size()) {
            std::size_t end = text.find('/', start);
            if (end == std::string::npos) end = text.size();
            const std::string_view segment = trim(std::string_view(text).substr(start, end - start));
            if (!segment.empty()) {
                std::size_t found = no_parent;
                for (std::size_t index = 0; index < project.size(); ++index) {
                    if (project[index].folder && project[index].parent == current &&
                        project[index].name == segment) {
                        found = index;
                    }
                }
                if (found == no_parent) {
                    (*status_).set_text("No folder at \"" + text + "\" · path left unchanged");
                    return;
                }
                current = found;
            }
            start = end + 1;
        }
        if (current == no_parent) return;
        select_folder(current);
        (*status_).set_text("Path committed · " + node_path(current));
    }

    void on_edit_cancelled() { (*status_).set_text("Path edit cancelled · segments restored"); }

    [[nodiscard]] static std::string_view trim(std::string_view text) {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        return text;
    }

    // DataGridView
    void on_grid_value_changed(const DataGridViewCellEvent& event) {
        // A preview is installed by the Gallery's fixtures, not edited.
        if ((*grid_).columns()[event.column].kind == DataGridViewColumnKind::image) return;
        (*status_).set_text("Grid cell " + event.row_id.value + "." +
                            (*grid_).columns()[event.column].name + " is now \"" +
                            (*grid_).cell_text(event.row, event.column) + "\"");
    }

    void on_grid_content_click(const DataGridViewCellEvent& event) {
        const DataGridViewColumnKind kind = (*grid_).columns()[event.column].kind;
        if (kind == DataGridViewColumnKind::image) {
            (*status_).set_text("Preview " + (*grid_).cell_text(event.row, 0U) + " · an image cell's click");
            return;
        }
        if (kind == DataGridViewColumnKind::link) {
            (*status_).set_text("History of " + (*grid_).cell_text(event.row, 0U) + " · a link cell's click");
            return;
        }
        if (kind != DataGridViewColumnKind::button) return;
        (*status_).set_text("Open " + (*grid_).cell_text(event.row, 0U) + " · a button cell's click");
    }

    // A header click sorts by that column, toggling the direction. The page
    // sorts its own rows and publishes the new order; the grid keeps the
    // selection, the current cell and any edit on their row IDs.
    void on_grid_header_click(const std::size_t column) {
        const DataGridViewSortDirection shown = (*grid_).columns()[column].sort_glyph;
        const DataGridViewSortDirection next = shown == DataGridViewSortDirection::ascending
            ? DataGridViewSortDirection::descending : DataGridViewSortDirection::ascending;
        (*grid_rows_).sort(column, next == DataGridViewSortDirection::descending);
        (*grid_).set_virtual_row_order((*grid_rows_).order());
        for (std::size_t index = 0; index < (*grid_).columns().size(); ++index) {
            (*grid_).set_column_sort_glyph(index, index == column ? next : DataGridViewSortDirection::none);
        }
        (*status_).set_text("Grid sorted by " + (*grid_).columns()[column].name +
                            (next == DataGridViewSortDirection::ascending ? " ascending" : " descending"));
    }

    // TreeListView
    void on_tree_list_selected(const TreeListSelectionChange& change) {
        if (!change.current_id) return;
        (*status_).set_text("TreeListView node " + *change.current_id + " selected · " +
                            node_path(node_index(*change.current_id)));
    }

    void on_tree_list_sorted(const TreeListSortChange& change) {
        if (!change.column) {
            (*status_).set_text("TreeListView back in fixture order");
            return;
        }
        (*status_).set_text("TreeListView siblings sorted by " +
                            (*tree_list_).columns()[*change.column].text +
                            (change.order == TreeListSortOrder::ascending ? " ascending" : " descending") +
                            " · every folder keeps its children");
    }

    void on_tree_list_column_moved(const TreeListColumnMove& change) {
        (*status_).set_text("TreeListView column " + (*tree_list_).columns()[change.column].text +
                            " moved to position " + std::to_string(change.display_position + 1U));
    }

    // Value
    void on_view_mode_changed(const int mode) {
        (*objects_).view_mode = mode == 0 ? ObjectViewMode::icons : ObjectViewMode::details;
        (*status_).set_text(std::string("ObjectView ") + (mode == 0 ? "icons" : "details") +
                            " · one ChoiceGroup bound to a Value<int>");
    }

    std::shared_ptr<BreadcrumbTrail> trail_;
    std::shared_ptr<TreeView> tree_;
    std::shared_ptr<ObjectView> objects_;
    std::shared_ptr<CorrespondenceView> ledger_;
    std::shared_ptr<DataGridView> grid_;
    // Shared with grid_, which asks it for values while it shows the rows.
    std::shared_ptr<ProjectFileRows> grid_rows_;
    std::shared_ptr<TreeListView> tree_list_;
    std::shared_ptr<Label> status_;
    Value<int> view_mode_{1};
    // Declared after the Value its view-mode row binds, so it is released first.
    TreeAndObjectViewsDocument<TreeAndObjectViewsPage> document_{};
    ObjectDetailsSort sort_{};
    std::size_t folder_{no_parent};
};

} // namespace

void add_tree_and_object_views(const std::shared_ptr<Surface>& page,
                               const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<TreeAndObjectViewsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
