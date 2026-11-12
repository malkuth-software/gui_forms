#include "pages.hpp"

#include "docking-workspace_form.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Docking workspace page (stage DK phase D8) that
// demo/pages/docking-workspace.form describes: one DockWorkspace
// shown by a DockSite filling most of the page, with tool panels on three
// sides, one auto-hidden, and three documents; beside it the controls that
// drive the workspace's program-facing operations: the closing prompt
// (phase D6), Close All Documents, saving and restoring the layout and named
// layouts (phase D7). The Gallery's window attaches no application windows,
// so Float stays disabled here (tests/dock_floating_tests.cpp covers it).
// Every handler is a named member connected with gui_forms::on; the owner
// lives in the Gallery's ComponentContainer for exactly the Gallery lifetime.
class DockingWorkspacePage final : public gui_forms::Component {
public:
    DockingWorkspacePage(const std::shared_ptr<Surface>& body, const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_form_children(*document_.gallery_docking_document, *body);
        build_workspace(*document_.gallery_docking_group);
        on((*workspace_).activation_changed(), *this, &DockingWorkspacePage::on_activation);
        on((*workspace_).document_closing(), *this, &DockingWorkspacePage::on_closing);
        on((*document_.gallery_docking_combined).checked_changed(), *this, &DockingWorkspacePage::on_combined);
        on((*document_.gallery_docking_close_all).clicked(), *this, &DockingWorkspacePage::on_close_all);
        on((*document_.gallery_docking_reopen).clicked(), *this, &DockingWorkspacePage::on_reopen);
        on((*document_.gallery_docking_save).clicked(), *this, &DockingWorkspacePage::on_save);
        on((*document_.gallery_docking_restore).clicked(), *this, &DockingWorkspacePage::on_restore);
        on((*document_.gallery_docking_save_named).clicked(), *this, &DockingWorkspacePage::on_save_named);
        on((*document_.gallery_docking_apply_named).clicked(), *this, &DockingWorkspacePage::on_apply_named);
    }

private:
    // Solution Explorer and Class View tabbed on the left, Properties on the
    // right, Output and Error List tabbed at the bottom, the Toolbox pinned
    // out to the left edge, and three documents. The site is placed first in
    // the group, ahead of the document's controls.
    void build_workspace(ScaledGroupBox& docking) {
        workspace_ = std::make_unique<DockWorkspace>("gallery.docking.workspace");
        const Delegate<DockPanelCreation&> factory =
            Delegate<DockPanelCreation&>::bind<DockingWorkspacePage, &DockingWorkspacePage::make_panel>(*this);
        constexpr std::array<std::string_view, 6> tool_ids{"solution", "classes", "properties",
                                                           "output",   "errors",  "toolbox"};
        constexpr std::array<std::string_view, 6> tool_titles{"Solution Explorer", "Class View", "Properties",
                                                              "Output",            "Error List", "Toolbox"};
        constexpr std::array<DockSide, 6> tool_sides{DockSide::left,   DockSide::left,   DockSide::right,
                                                     DockSide::bottom, DockSide::bottom, DockSide::left};
        constexpr std::array<std::int32_t, 6> tool_priorities{2, 2, 1, 0, 0, 3};
        for (std::size_t index = 0U; index < tool_ids.size(); ++index) {
            DockPanelRegistration registration{};
            registration.descriptor.id = DockPanelId(std::string(tool_ids[index]));
            registration.descriptor.default_placement.side = tool_sides[index];
            registration.descriptor.default_placement.ratio = 0.24;
            registration.descriptor.collapse_priority = tool_priorities[index];
            registration.descriptor.minimum_size = {120.0, 70.0};
            registration.descriptor.dock_as_document = tool_ids[index] == "properties";
            registration.title = std::string(tool_titles[index]);
            registration.factory = factory;
            static_cast<void>((*workspace_).register_panel(registration));
        }
        for (std::size_t index = 0U; index < document_ids.size(); ++index) {
            DockPanelRegistration registration{};
            registration.descriptor.id = DockPanelId(std::string(document_ids[index]));
            registration.descriptor.kind = DockPanelKind::document;
            registration.descriptor.minimum_size = {180.0, 90.0};
            registration.title = std::string(document_titles[index]);
            registration.factory = factory;
            static_cast<void>((*workspace_).register_panel(registration));
            static_cast<void>((*workspace_).show_panel(registration.descriptor.id));
        }
        static_cast<void>((*workspace_).show_panel(DockPanelId("solution")));
        tab_into("classes", "solution");
        static_cast<void>((*workspace_).show_panel(DockPanelId("properties")));
        static_cast<void>((*workspace_).show_panel(DockPanelId("output")));
        tab_into("errors", "output");
        static_cast<void>((*workspace_).show_panel(DockPanelId("toolbox")));
        DockTransaction pinned{};
        const DockNodeId toolbox_stack = (*workspace_).layout().locate(DockPanelId("toolbox")).stack;
        pinned.edits.push_back(DockEdit::auto_hide(DockSubject::of_stack(toolbox_stack), DockSide::left, 160.0));
        static_cast<void>((*workspace_).apply(pinned));
        // Each stack opens on its first tab: tabbing a panel in selected it.
        DockTransaction first_tabs{};
        first_tabs.edits.push_back(DockEdit::select(DockPanelId("solution")));
        first_tabs.edits.push_back(DockEdit::select(DockPanelId("output")));
        first_tabs.edits.push_back(DockEdit::select(DockPanelId(std::string(document_ids[0]))));
        static_cast<void>((*workspace_).apply(first_tabs));
        site_ = make_control<DockSite>(StableId("gallery.docking.site"), *workspace_);
        (*site_).accessible_name = "Docking workspace";
        insert_before(docking, site_, {18.0, 36.0, 636.0, 504.0}, *document_.gallery_docking_keep);
    }

    // Docks `panel` as a tab of the stack holding `beside`.
    void tab_into(std::string_view panel, std::string_view beside) {
        static_cast<void>((*workspace_).show_panel(DockPanelId(std::string(panel))));
        DockTransaction tabbed{};
        const DockNodeId stack = (*workspace_).layout().locate(DockPanelId(std::string(beside))).stack;
        tabbed.edits.push_back(DockEdit::dock_into(DockSubject::of_panel(DockPanelId(std::string(panel))), stack));
        static_cast<void>((*workspace_).apply(tabbed));
    }

    // The page's factory: a panel's control, made the first time it shows.
    void make_panel(DockPanelCreation& creation) {
        const std::string title = (*workspace_).panel_title(creation.panel);
        std::shared_ptr<Panel> content = make_control<Panel>(StableId("gallery.docking.content." + creation.panel.value));
        std::shared_ptr<Label> copy =
            label("gallery.docking.copy." + creation.panel.value, title, 11.0, 400, "ink");
        (*copy).set_requested_bounds({8.0, 8.0, 160.0, 22.0});
        (*content).add_child(copy);
        std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("gallery.docking.field." + creation.panel.value));
        (*field).accessible_name = title;
        (*field).set_requested_bounds({8.0, 36.0, 100.0, 24.0});
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
        (*document_.gallery_docking_status).text = text;
    }

    // The program's answer to a closing prompt: the page keeps the
    // documents while "Keep documents when asked" is checked.
    void on_closing(DockDocumentClosing& closing) {
        std::string text = "Asked about ";
        for (std::size_t index = 0U; index < closing.documents.size(); ++index) {
            if (index != 0U) text += ", ";
            text += (*workspace_).panel_title(closing.documents[index]);
        }
        closing.cancel = (*document_.gallery_docking_keep).checked.read();
        text += closing.cancel ? ": kept" : ": closing";
        (*document_.gallery_docking_closing).text = text;
    }

    void on_combined(bool combined) {
        (*workspace_).close_prompt = combined ? DockClosePrompt::combined : DockClosePrompt::each;
    }

    void on_close_all() { static_cast<void>((*workspace_).close_all_documents()); }

    void on_reopen() {
        for (const std::string_view document : document_ids) {
            static_cast<void>((*workspace_).show_panel(DockPanelId(std::string(document))));
        }
    }

    void on_save() {
        const DockLayoutWriteResult written = (*workspace_).save_layout();
        if (!written) return;
        saved_ = written.text;
        (*document_.gallery_docking_restore).enabled = true;
        (*document_.gallery_docking_closing).text = "Layout saved";
    }

    void on_restore() {
        if (saved_.empty()) return;
        const DockRestoreResult result = (*workspace_).load_layout(saved_);
        report_restore(result);
    }

    void on_save_named() {
        const std::string name((*document_.gallery_docking_name).text.read());
        if (name.empty()) return;
        if (!(*workspace_).save_named_layout(name)) return;
        std::vector<std::string> names{};
        for (const DockNamedLayout& layout : (*workspace_).named_layouts()) names.push_back(layout.name);
        (*document_.gallery_docking_named).items = std::move(names);
    }

    void on_apply_named() {
        const std::optional<std::size_t> selected = (*document_.gallery_docking_named).selected_index();
        const std::span<const DockNamedLayout> layouts = (*workspace_).named_layouts();
        if (!selected || *selected >= layouts.size()) return;
        // The name is copied: the restore may not touch the list, but the
        // borrow would end with any change to it.
        const std::string name = layouts[*selected].name;
        const DockRestoreResult result = (*workspace_).apply_named_layout(name);
        report_restore(result);
    }

    void report_restore(const DockRestoreResult& result) {
        const bool restored = result.status == DockRestoreStatus::restored;
        std::string text = restored ? "Layout restored" : "Layout kept";
        text += " · " + std::to_string(result.diagnostics.size()) + " diagnostics";
        (*document_.gallery_docking_closing).text = text;
    }

    static constexpr std::array<std::string_view, 3> document_ids{"form.cs", "program.cs", "readme.md"};
    static constexpr std::array<std::string_view, 3> document_titles{"Form.cs", "Program.cs", "README.md"};

    // Destroying the workspace unbinds the site, so the control tree may
    // outlive the page owner.
    std::unique_ptr<DockWorkspace> workspace_;
    std::shared_ptr<DockSite> site_;
    DockingWorkspaceDocument<DockingWorkspacePage> document_{};
    std::string saved_{};
};

void add_docking_workspace(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<DockingWorkspacePage>(page, context));
}

} // namespace gui_forms::gallery::pages
