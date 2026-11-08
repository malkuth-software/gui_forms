#include "pages.hpp"

#include "containers-and-focus_form.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Containers and Focus page that
// demo/pages/containers-and-focus.form describes: one SplitContainer whose
// seam is dragged, keyed and collapsed, a base ContainerControl with
// descendant focus authority, a UserControl whose one-shot Loaded lifecycle
// is observed, and a small docked workspace (DockWorkspace, DockSite,
// DockNavigator). The split container makes its own panels, so their
// content is made here; so is the DockSite, which needs its workspace.
// Every handler is a named member connected with gui_forms::on; the owner
// lives in the Gallery's ComponentContainer for exactly the Gallery lifetime.
class ContainersPage final : public gui_forms::Component {
public:
    ContainersPage(const std::shared_ptr<Surface>& body,
                   const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_form_children(*document_.gallery_containers_document, *body);
        (*document_.gallery_containers_docking_group).set_style(page_style());
        (*document_.gallery_containers_base_face).set_background(Color::rgba(232, 239, 245));
        (*document_.gallery_containers_user_face).set_background(Color::rgba(238, 244, 236));
        build_split();
        build_docking(*document_.gallery_containers_docking_group);
        on((*collapse_).clicked(), *this, &ContainersPage::on_collapse);
        on((*focus_splitter_).clicked(), *this, &ContainersPage::on_focus_splitter);
        on((*document_.gallery_containers_base_focus).clicked(), *this, &ContainersPage::on_focus_descendant);
        on((*document_.gallery_containers_user).loaded(), *this, &ContainersPage::on_user_loaded);
    }

private:
    // The split container's own panels: the navigation pane's items and the
    // content pane's explanation and actions.
    void build_split() {
        SplitContainer& split = *document_.gallery_containers_split;
        split.set_splitter_distance(310.0);
        split.set_first_minimum(180.0);
        split.set_second_minimum(280.0);
        const std::shared_ptr<SplitterPanel> first = split.first_panel();
        const std::shared_ptr<SplitterPanel> second = split.second_panel();
        (*first).set_background(Color::rgba(225, 235, 243));
        (*second).set_background(paper);

        std::shared_ptr<Label> first_title = label(
            "gallery.containers.first.title", "NAVIGATION PANE", 13.0, 700,
            shell_blue_dark);
        (*first_title).set_requested_bounds({20.0, 22.0, 240.0, 28.0});
        (*first).add_child(first_title);
        for (std::size_t index = 0; index < 6U; ++index) {
            std::shared_ptr<Button> item = make_control<Button>(
                StableId("gallery.containers.item." + std::to_string(index)),
                "Retained item " + std::to_string(index + 1U));
            (*item).set_visual_style(ButtonVisualStyle::command);
            (*item).set_requested_bounds(
                {20.0, 62.0 + static_cast<double>(index) * 48.0, 255.0, 36.0});
            (*first).add_child(item);
        }

        std::shared_ptr<Label> second_title = label(
            "gallery.containers.second.title", "CONTENT PANE", 13.0, 700,
            shell_blue_dark);
        (*second_title).set_requested_bounds({24.0, 22.0, 260.0, 28.0});
        (*second).add_child(second_title);
        std::shared_ptr<Label> explanation = label(
            "gallery.containers.second.copy",
            "Drag the seam continuously. Focus it and use arrow keys.\nCollapse and restore preserve allocation.",
            12.0, 400, ink);
        (*explanation).text_wrapping = TextWrapping::word;
        (*explanation).vertical_alignment = VerticalAlignment::near;
        (*explanation).set_requested_bounds({24.0, 62.0, 520.0, 52.0});
        (*second).add_child(explanation);

        collapse_ = make_control<Button>(StableId("gallery.containers.collapse"),
                                         "Collapse navigation");
        (*collapse_).set_requested_bounds({24.0, 132.0, 190.0, 34.0});
        (*second).add_child(collapse_);
        focus_splitter_ = make_control<Button>(StableId("gallery.containers.focus"),
                                               "Focus the splitter");
        (*focus_splitter_).set_visual_style(ButtonVisualStyle::accent);
        (*focus_splitter_).set_requested_bounds({228.0, 132.0, 170.0, 34.0});
        (*second).add_child(focus_splitter_);
    }

    // A docked workspace: Toolbox and Classes tabbed on the left, Properties
    // on the right, Output auto-hidden on the bottom edge, two documents in
    // the document area. Panel
    // controls are made by the page's factory the first time each panel is
    // shown; the status line follows the two activation tracks. Beside it, a
    // navigator on its own shows the Ctrl+Tab switcher's two lists.
    void build_docking(ScaledGroupBox& docking) {
        workspace_ = std::make_unique<DockWorkspace>("gallery.containers.workspace");
        const Delegate<DockPanelCreation&> factory =
            Delegate<DockPanelCreation&>::bind<ContainersPage, &ContainersPage::make_panel>(*this);
        constexpr std::array<std::string_view, 4> tool_ids{"toolbox", "classes", "properties", "output"};
        constexpr std::array<std::string_view, 4> tool_titles{"Toolbox", "Classes", "Properties", "Output"};
        constexpr std::array<DockSide, 4> tool_sides{DockSide::left, DockSide::left, DockSide::right,
                                                     DockSide::bottom};
        constexpr std::array<std::int32_t, 4> tool_priorities{2, 2, 1, 0};
        for (std::size_t index = 0U; index < tool_ids.size(); ++index) {
            DockPanelRegistration registration{};
            registration.descriptor.id = DockPanelId(std::string(tool_ids[index]));
            registration.descriptor.default_placement.side = tool_sides[index];
            registration.descriptor.collapse_priority = tool_priorities[index];
            registration.descriptor.minimum_size = {100.0, 60.0};
            registration.descriptor.dock_as_document = index == 2U;
            registration.title = std::string(tool_titles[index]);
            registration.factory = factory;
            static_cast<void>((*workspace_).register_panel(registration));
        }
        constexpr std::array<std::string_view, 2> document_ids{"form.cs", "program.cs"};
        constexpr std::array<std::string_view, 2> document_titles{"Form.cs", "Program.cs"};
        for (std::size_t index = 0U; index < document_ids.size(); ++index) {
            DockPanelRegistration registration{};
            registration.descriptor.id = DockPanelId(std::string(document_ids[index]));
            registration.descriptor.kind = DockPanelKind::document;
            registration.descriptor.minimum_size = {160.0, 80.0};
            registration.title = std::string(document_titles[index]);
            registration.factory = factory;
            static_cast<void>((*workspace_).register_panel(registration));
            static_cast<void>((*workspace_).show_panel(registration.descriptor.id));
        }
        static_cast<void>((*workspace_).show_panel(DockPanelId("toolbox")));
        DockTransaction tabbed{};
        const DockNodeId toolbox_stack = (*workspace_).layout().locate(DockPanelId("toolbox")).stack;
        tabbed.edits.push_back(DockEdit::dock_into(DockSubject::of_panel(DockPanelId("classes")), toolbox_stack));
        static_cast<void>((*workspace_).apply(tabbed));
        static_cast<void>((*workspace_).show_panel(DockPanelId("properties")));
        static_cast<void>((*workspace_).show_panel(DockPanelId("output")));
        // Output is pinned out to the bottom edge: its side tab opens a
        // flyout on hover or click.
        DockTransaction pinned{};
        const DockNodeId output_stack = (*workspace_).layout().locate(DockPanelId("output")).stack;
        pinned.edits.push_back(DockEdit::auto_hide(DockSubject::of_stack(output_stack), DockSide::bottom, 120.0));
        static_cast<void>((*workspace_).apply(pinned));
        dock_site_ = make_control<DockSite>(StableId("gallery.containers.dock"), *workspace_);
        (*dock_site_).accessible_name = "Docked workspace specimen";
        insert_before(docking, dock_site_, {20.0, 36.0, 600.0, 344.0},
                      *document_.gallery_containers_docking_status);
        on((*workspace_).activation_changed(), *this, &ContainersPage::on_activation);
        DockNavigator& navigator = *document_.gallery_containers_navigator;
        std::vector<DockNavigatorEntry> tools{{DockPanelId("output"), "Output"},
                                              {DockPanelId("properties"), "Properties"},
                                              {DockPanelId("toolbox"), "Toolbox"}};
        std::vector<DockNavigatorEntry> files{{DockPanelId("program.cs"), "Program.cs"},
                                              {DockPanelId("form.cs"), "Form.cs"}};
        navigator.set_entries(std::move(tools), std::move(files));
        static_cast<void>(navigator.step(1));
    }

    // The page's factory: a panel's control, made the first time it shows.
    void make_panel(DockPanelCreation& creation) {
        const std::string title = (*workspace_).panel_title(creation.panel);
        std::shared_ptr<Panel> content =
            make_control<Panel>(StableId("gallery.containers.dock.content." + creation.panel.value));
        std::shared_ptr<Label> copy = label("gallery.containers.dock.copy." + creation.panel.value,
                                            title + " · made when first shown", 11.0, 400, ink);
        (*copy).set_requested_bounds({8.0, 8.0, 240.0, 22.0});
        (*content).add_child(copy);
        std::shared_ptr<TextBox> field =
            make_control<TextBox>(StableId("gallery.containers.dock.field." + creation.panel.value));
        (*field).accessible_name = title;
        (*field).set_requested_bounds({8.0, 36.0, 150.0, 24.0});
        (*content).add_child(field);
        creation.content = content;
    }

    void on_activation(const DockActivationChange&) {
        std::string text = "Active: ";
        const std::optional<DockPanelId>& panel = (*workspace_).active_panel();
        text += panel ? (*workspace_).panel_title(*panel) : std::string("none");
        text += " · document: ";
        const std::optional<DockPanelId>& document = (*workspace_).active_document();
        text += document ? (*workspace_).panel_title(*document) : std::string("none");
        (*document_.gallery_containers_docking_status).set_text(text);
    }

    // Input-only: the user clicked. The collapse state itself lives in the
    // public SplitContainer, so the button text follows the container.
    void on_collapse() {
        const bool next = !(*document_.gallery_containers_split).first_collapsed();
        (*document_.gallery_containers_split).set_first_collapsed(next);
        (*collapse_).text = next ? "Restore navigation" : "Collapse navigation";
    }

    void on_focus_splitter() {
        static_cast<void>(
            (*document_.gallery_containers_split).request_active_control((*document_.gallery_containers_split).splitter_control()));
    }

    void on_focus_descendant() {
        static_cast<void>((*document_.gallery_containers_base).request_active_control(document_.gallery_containers_base_focus));
    }

    void on_user_loaded() {
        (*document_.gallery_containers_user_status).set_text(
            "UserControl · Loaded fired once · attachment committed");
    }

    std::shared_ptr<Button> collapse_;
    std::shared_ptr<Button> focus_splitter_;
    // Destroying the workspace unbinds the site, so the control tree may
    // outlive the page owner.
    std::unique_ptr<DockWorkspace> workspace_;
    std::shared_ptr<DockSite> dock_site_;
    ContainersDocument<ContainersPage> document_{};
};

void add_containers(const std::shared_ptr<Surface>& page,
                    const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ContainersPage>(page, context));
}

} // namespace gui_forms::gallery::pages
