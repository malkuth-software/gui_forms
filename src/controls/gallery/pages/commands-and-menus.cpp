#include "pages.hpp"

#include "commands-and-menus_form.hpp"

namespace gui_forms::gallery::pages {
namespace {

// A gallery item's ID from its style name, as StableId segments: ASCII
// letters lowercased, digits kept, and every other run one '-' between
// them ("Heading 1" is "style.heading-1").
std::string style_id(std::string_view name) {
    std::string identity("style.");
    bool pending_separator = false;
    for (const char character : name) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool upper = character >= 'A' && character <= 'Z';
        const bool digit = character >= '0' && character <= '9';
        if (!lower && !upper && !digit) {
            pending_separator = true;
            continue;
        }
        if (pending_separator && identity.back() != '.') identity.push_back('-');
        pending_separator = false;
        identity.push_back(upper ? static_cast<char>(character - 'A' + 'a') : character);
    }
    return identity;
}

// CommandBar item ids; the bar names its buttons "gallery.commands.bar.item.<id>".
constexpr int bar_new = 1;
constexpr int bar_save = 2;
constexpr int bar_delete = 3;
constexpr int bar_wrap = 4;

// The page demo/pages/commands-and-menus.form describes: one fixture
// document with a modified flag and a word-wrap flag. Every
// surface on the page (MenuStrip, CommandBar, ContextMenu, DropDownButton
// menus, a plain Button, a CheckBox and Ctrl+S) converges on the same
// application-owned Commands, so enabled and checked state is authored once.
class CommandsAndMenusPage final : public Component {
public:
    CommandsAndMenusPage(const std::shared_ptr<Surface>& body,
                         const std::shared_ptr<PageContext>& context)
        : export_menu_("gallery.commands.export.menu"),
          run_menu_("gallery.commands.run.menu"),
          context_menu_("gallery.commands.context") {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        build_commands();
        build_menu_strip();
        build_command_row();
        build_shared_state();
        build_context_menu();
        build_strips();
        build_container();
        build_ribbon();
        build_gallery();
        build_scaling_ribbons();
        build_file_area();
        on(dirty_.changed(), *this, &CommandsAndMenusPage::on_dirty_changed);
        on(wrap_.changed(), *this, &CommandsAndMenusPage::on_wrap_changed);
        on_dirty_changed(dirty_.get());
    }

private:
    // The document binds dirty_ and wrap_ (FD-6).
    template <typename> friend class CommandsDocument;

    void mount_document(const std::shared_ptr<Surface>& body) {
        mount_form_children(*document_.gallery_commands_document, *body);
        const std::array<std::shared_ptr<ScaledGroupBox>, 9> groups{
            document_.gallery_commands_row,          document_.gallery_commands_state,
            document_.gallery_commands_context_group, document_.gallery_commands_strips,
            document_.gallery_commands_container,    document_.gallery_commands_ribbon,
            document_.gallery_commands_gallery,      document_.gallery_commands_ribbon_scaling,
            document_.gallery_commands_ribbon_file_area};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
        status_ = document_.gallery_commands_status;
    }

    [[nodiscard]] static std::shared_ptr<Command> make_command(
        const std::string_view id, const std::string_view text,
        const std::string_view description, const std::string_view shortcut = {}) {
        std::shared_ptr<Command> command =
            std::make_shared<Command>(std::string(id), std::string(text));
        (*command).set_description(std::string(description));
        if (!shortcut.empty()) (*command).set_shortcut(std::string(shortcut));
        return command;
    }

    [[nodiscard]] static MenuItemSpec item(const std::string_view id,
                                           const std::shared_ptr<Command>& command,
                                           const MenuItemKind kind = MenuItemKind::command) {
        return MenuItemSpec(std::string(id), kind, command);
    }

    [[nodiscard]] static MenuItemSpec separator(const std::string_view id) {
        return MenuItemSpec(std::string(id), MenuItemKind::separator);
    }

    [[nodiscard]] std::vector<MenuItemSpec> export_items(const std::string_view prefix) const {
        std::vector<MenuItemSpec> items;
        for (std::size_t index = 0; index < export_formats_.size(); ++index) {
            items.push_back(item(std::string(prefix) + "." + export_names_[index],
                                 export_formats_[index]));
        }
        return items;
    }

    void build_commands() {
        new_ = make_command("gallery.commands.new", "New document", "Start a new fixture document", "Ctrl+N");
        save_ = make_command("gallery.commands.save", "Save", "Save the fixture document", "Ctrl+S");
        delete_ = make_command("gallery.commands.delete", "Delete", "Delete the fixture document");
        (*delete_).set_enabled(false);
        (*delete_).set_destructive(true);
        (*delete_).set_availability_reason("Fixture documents are read-only");
        wrap_command_ = make_command("gallery.commands.wrap", "Word wrap", "Wrap long lines");
        (*wrap_command_).checked = false;
        icons_ = make_command("gallery.commands.view.icons", "Icons", "Show documents as icons");
        details_ = make_command("gallery.commands.view.details", "Details", "Show documents as rows");
        (*icons_).checked = true;
        (*details_).checked = false;
        run_ = make_command("gallery.commands.run", "Run", "Run the fixture document");
        about_ = make_command("gallery.commands.about", "About commands", "Explain this page");
        for (std::size_t index = 0; index < export_names_.size(); ++index) {
            export_formats_[index] = make_command(
                std::string("gallery.commands.export.") + export_names_[index],
                std::string("Export ") + export_labels_[index],
                std::string("Export the document as ") + export_labels_[index]);
            on((*export_formats_[index]).invoked(), *this,
               &CommandsAndMenusPage::on_export, static_cast<int>(index));
        }
        run_options_[0] = make_command("gallery.commands.run.verbose", "Run verbose", "Run with a trace");
        run_options_[1] = make_command("gallery.commands.run.watch", "Run and watch", "Run and re-run on change");
        on((*run_options_[0]).invoked(), *this, &CommandsAndMenusPage::on_run_option, 0);
        on((*run_options_[1]).invoked(), *this, &CommandsAndMenusPage::on_run_option, 1);
        on((*new_).invoked(), *this, &CommandsAndMenusPage::on_new);
        on((*save_).invoked(), *this, &CommandsAndMenusPage::on_save);
        on((*wrap_command_).invoked(), *this, &CommandsAndMenusPage::on_wrap_invoked);
        on((*icons_).invoked(), *this, &CommandsAndMenusPage::on_view_mode, 0);
        on((*details_).invoked(), *this, &CommandsAndMenusPage::on_view_mode, 1);
        on((*run_).invoked(), *this, &CommandsAndMenusPage::on_run);
        on((*about_).invoked(), *this, &CommandsAndMenusPage::on_about);
    }

    void build_menu_strip() {
        const std::shared_ptr<MenuStrip>& menu = document_.gallery_commands_menu;
        (*menu).font = {FontRole::control, 11.5, 400, false, 0.12};
        (*menu).set_items({
            {"gallery.commands.menu.file", "&File",
             {item("file.new", new_), item("file.save", save_), separator("file.separator"),
              MenuItemSpec("file.export", MenuItemKind::submenu, {}, "Export",
                           export_items("file.export"))}},
            {"gallery.commands.menu.edit", "&Edit",
             {item("edit.delete", delete_)}},
            {"gallery.commands.menu.view", "&View",
             {item("view.wrap", wrap_command_, MenuItemKind::check), separator("view.separator"),
              item("view.icons", icons_, MenuItemKind::radio),
              item("view.details", details_, MenuItemKind::radio)}},
            {"gallery.commands.menu.help", "&Help", {item("help.about", about_)}},
        });
        on((*menu).item_invoked(), *this, &CommandsAndMenusPage::on_menu_invoked);
    }

    void build_command_row() {
        const std::shared_ptr<CommandBar>& bar = document_.gallery_commands_bar;
        (*bar).set_style(page_style());
        (*bar).set_items({
            {.id = bar_new, .label = "New", .accessible_name = "New document",
             .shortcut = KeyGesture{PhysicalKey::n, Modifier::control}, .command = new_.get()},
            {.id = bar_save, .label = "Save", .accessible_name = "Save",
             .shortcut = KeyGesture{PhysicalKey::s, Modifier::control}, .command = save_.get()},
            {.id = bar_delete, .label = "Delete", .accessible_name = "Delete",
             .command = delete_.get()},
            {.id = bar_wrap, .label = "Word wrap", .accessible_name = "Word wrap",
             .command = wrap_command_.get()},
        });
        for (int index = 0; index < static_cast<int>((*bar).items().size()); ++index) {
            Button& button = (*bar).item(index);
            button.set_style(page_style());
            button.visual_style = ButtonVisualStyle::command;
        }
        on((*bar).invoked(), *this, &CommandsAndMenusPage::on_bar_invoked);

        export_ = document_.gallery_commands_export;
        (*export_).set_style(page_style());
        on((*export_).drop_down_requested(), *this, &CommandsAndMenusPage::on_export_requested);
        on((*export_).drop_down_close_requested(), *this, &CommandsAndMenusPage::on_export_close);
        export_menu_.set_items(export_items("export"));
        on(export_menu_.open_changed(), *this, &CommandsAndMenusPage::on_export_menu_open);

        run_button_ = document_.gallery_commands_run;
        (*run_button_).drop_down_mode = DropDownButtonMode::split;
        (*run_button_).set_style(page_style());
        (*run_button_).bind(*run_); // Primary region: the ordinary Click path.
        on((*run_button_).drop_down_requested(), *this, &CommandsAndMenusPage::on_run_requested);
        on((*run_button_).drop_down_close_requested(), *this, &CommandsAndMenusPage::on_run_close);
        run_menu_.set_items({item("run.verbose", run_options_[0]), item("run.watch", run_options_[1])});
        on(run_menu_.open_changed(), *this, &CommandsAndMenusPage::on_run_menu_open);
    }

    // The modified and word-wrap check boxes bind dirty_ and wrap_ in the
    // document (FD-6).
    void build_shared_state() {
        Button& save = *document_.gallery_commands_save_button;
        save.set_style(page_style());
        save.bind(*save_);
        Button& remove = *document_.gallery_commands_delete_button;
        remove.set_style(page_style());
        remove.bind(*delete_);
        saves_label_ = document_.gallery_commands_saves;
    }

    void build_context_menu() {
        context_button_ = document_.gallery_commands_context_open;
        (*context_button_).set_style(page_style());
        on((*context_button_).clicked(), *this, &CommandsAndMenusPage::on_context_requested);
        context_menu_.set_items({
            item("save", save_), item("delete", delete_), separator("separator.edit"),
            item("wrap", wrap_command_, MenuItemKind::check), separator("separator.view"),
            MenuItemSpec("export", MenuItemKind::submenu, {}, "Export", export_items("export")),
            separator("separator.help"), item("about", about_),
        });
        on(context_menu_.item_invoked(), *this, &CommandsAndMenusPage::on_context_invoked);
        on(context_menu_.open_changed(), *this, &CommandsAndMenusPage::on_context_open);
    }

    // A QuickAccessToolbar over the same Commands, narrow enough that its
    // last command overflows; a ToolStrip with a hosted combo box, narrow
    // enough that its last buttons overflow; and a StatusStrip with a spring
    // label and a hosted progress bar. The Gallery shell attaches enhanced
    // tooltips to some of the ToolStrip's items once the Window exists.
    void build_strips() {
        const std::shared_ptr<QuickAccessToolbar>& quick_access = document_.gallery_commands_quick_access;
        (*quick_access).set_commands({
            {.command = save_},
            {.command = new_},
            {.command = delete_, .shown = false},
            {.command = wrap_command_},
            {.command = about_},
        });
        quick_access_note_ = document_.gallery_commands_quick_access_note;
        on((*quick_access).placement_changed(), *this, &CommandsAndMenusPage::on_quick_access_placement);
        on((*quick_access).customized(), *this, &CommandsAndMenusPage::on_quick_access_customized);
        const std::shared_ptr<ToolStrip>& tool = document_.gallery_commands_tool_strip;
        std::vector<ToolStripItemSpec> tools;
        tools.push_back({.stable_id = "strip.new", .command = new_});
        tools.push_back({.stable_id = "strip.save", .command = save_});
        tools.push_back({.stable_id = "strip.delete", .command = delete_});
        tools.push_back({.stable_id = "strip.separator", .kind = ToolStripItemKind::separator});
        tools.push_back({.stable_id = "strip.wrap", .command = wrap_command_});
        tools.push_back({.stable_id = "strip.zoom", .kind = ToolStripItemKind::combo_box, .text = "Zoom",
                         .choices = {"50%", "100%", "200%"}, .width = 72.0});
        tools.push_back({.stable_id = "strip.export", .kind = ToolStripItemKind::drop_down_button,
                         .text = "Export", .drop_down = export_items("strip.export")});
        tools.push_back({.stable_id = "strip.run", .kind = ToolStripItemKind::split_button,
                         .command = run_,
                         .drop_down = {item("strip.run.verbose", run_options_[0]),
                                       item("strip.run.watch", run_options_[1])}});
        tools.push_back({.stable_id = "strip.about", .command = about_});
        (*tool).set_items(std::move(tools));
        const std::shared_ptr<StatusStrip>& status = document_.gallery_commands_status_strip;
        (*status).set_items({
            {.stable_id = "status.state", .kind = ToolStripItemKind::label, .text = "Ready", .spring = true},
            {.stable_id = "status.separator", .kind = ToolStripItemKind::separator},
            {.stable_id = "status.position", .kind = ToolStripItemKind::label, .text = "Ln 1, Col 1"},
            {.stable_id = "status.encoding", .kind = ToolStripItemKind::label, .text = "UTF-8"},
            {.stable_id = "status.progress", .kind = ToolStripItemKind::progress_bar, .text = "Indexing"},
        });
        (*(*status).progress_bar("status.progress")).value = 60.0;
    }

    // A RibbonGallery in the ribbon form, a drop-down gallery under a button,
    // and the gallery value command they share, standalone (the scaling
    // specimens below host the same command's galleries in a Ribbon).
    void build_gallery() {
        const std::shared_ptr<Command> styles = std::make_shared<Command>("gallery.commands.styles", "Styles");
        styles_ = std::make_shared<GalleryCommand>(styles);
        std::vector<RibbonGalleryCategory> categories{{"headings", "Headings"}, {"body", "Body text"},
                                                      {"quotes", "Quotations"}};
        std::vector<RibbonGalleryItem> items;
        const std::array<const char*, 4> heading_names{"Title", "Heading 1", "Heading 2", "Heading 3"};
        const std::array<const char*, 4> body_names{"Normal", "Compact", "Emphasis", "Strong"};
        const std::array<const char*, 3> quote_names{"Quote", "Intense", "Reference"};
        for (const char* name : heading_names) items.push_back({style_id(name), name, {}, "headings"});
        for (const char* name : body_names) items.push_back({style_id(name), name, {}, "body"});
        for (const char* name : quote_names) items.push_back({style_id(name), name, {}, "quotes"});
        const std::shared_ptr<RibbonGallery>& in_ribbon = document_.gallery_commands_styles;
        (*in_ribbon).item_size = Size{72.0, 24.0};
        (*in_ribbon).show_labels = true;
        (*in_ribbon).visible_rows = 2U;
        (*in_ribbon).set_column_range(2U, 4U);
        (*in_ribbon).columns = 4U;
        (*in_ribbon).set_drop_down_size(4U, 4U);
        (*in_ribbon).set_items(categories, items);
        (*in_ribbon).command = styles_;
        gallery_button_ = document_.gallery_commands_styles_button;
        drop_down_gallery_ = make_control<RibbonGallery>(StableId("gallery.commands.styles.drop-down"),
                                                        RibbonGalleryForm::drop_down);
        (*drop_down_gallery_).accessible_name = "Styles";
        (*drop_down_gallery_).item_size = Size{72.0, 24.0};
        (*drop_down_gallery_).show_labels = true;
        (*drop_down_gallery_).set_drop_down_size(3U, 5U);
        (*drop_down_gallery_).set_items(std::move(categories), std::move(items));
        (*drop_down_gallery_).command = styles_;
        gallery_status_ = document_.gallery_commands_gallery_status;
        on((*gallery_button_).clicked(), *this, &CommandsAndMenusPage::on_styles_button);
        on((*styles_).preview_requested(), *this, &CommandsAndMenusPage::on_style_preview);
        on((*styles_).preview_cancelled(), *this, &CommandsAndMenusPage::on_style_cancel);
        on((*styles_).value_changed(), *this, &CommandsAndMenusPage::on_style_value);
    }

    // The Home tab of the scaling specimens: Document (two large commands
    // and two small), Styles (an in-ribbon gallery of 2 to 5 columns, sharing
    // the styles value command, beside a large button), View (two small
    // commands and a hosted combo box) and Run (a single large split, which
    // never collapses).
    [[nodiscard]] std::vector<RibbonTabSpec> scaling_tabs(const std::string& prefix,
                                                         const std::shared_ptr<RibbonGallery>& gallery) const {
        RibbonTabSpec home{.stable_id = prefix + ".home", .text = "&Home"};
        RibbonGroupSpec document{.stable_id = prefix + ".document", .caption = "Document", .dialog_launcher = true};
        document.commands.push_back({.stable_id = prefix + ".new", .command = new_});
        document.commands.push_back({.stable_id = prefix + ".save", .kind = RibbonCommandKind::split,
                                     .command = save_, .drop_down = export_items(prefix + ".save.export")});
        document.commands.push_back({.stable_id = prefix + ".delete", .size = RibbonCommandSize::small,
                                     .command = delete_});
        document.commands.push_back({.stable_id = prefix + ".wrap", .kind = RibbonCommandKind::toggle,
                                     .size = RibbonCommandSize::small, .command = wrap_command_});
        RibbonGroupSpec styles{.stable_id = prefix + ".styles", .caption = "Styles"};
        styles.commands.push_back({.stable_id = prefix + ".gallery", .kind = RibbonCommandKind::gallery,
                                   .text = "Styles", .gallery = gallery});
        styles.commands.push_back({.stable_id = prefix + ".about", .command = about_});
        RibbonGroupSpec view{.stable_id = prefix + ".view", .caption = "View"};
        view.commands.push_back({.stable_id = prefix + ".icons", .size = RibbonCommandSize::small, .command = icons_});
        view.commands.push_back({.stable_id = prefix + ".details", .size = RibbonCommandSize::small,
                                 .command = details_});
        view.commands.push_back({.stable_id = prefix + ".zoom", .kind = RibbonCommandKind::combo_box,
                                 .text = "Zoom", .choices = {"50%", "100%", "200%"}, .width = 80.0});
        RibbonGroupSpec run{.stable_id = prefix + ".run-group", .caption = "Run"};
        run.commands.push_back({.stable_id = prefix + ".run", .kind = RibbonCommandKind::split, .command = run_,
                                .drop_down = {item(prefix + ".run.verbose", run_options_[0]),
                                              item(prefix + ".run.watch", run_options_[1])}});
        home.groups.push_back(std::move(document));
        home.groups.push_back(std::move(styles));
        home.groups.push_back(std::move(view));
        home.groups.push_back(std::move(run));
        std::vector<RibbonTabSpec> tabs;
        tabs.push_back(std::move(home));
        return tabs;
    }

    [[nodiscard]] std::shared_ptr<RibbonGallery> scaling_gallery(const std::string& id) const {
        std::shared_ptr<RibbonGallery> gallery = make_control<RibbonGallery>(StableId(id));
        (*gallery).accessible_name = "Styles";
        (*gallery).item_size = Size{56.0, 26.0};
        (*gallery).show_labels = true;
        (*gallery).visible_rows = 1U;
        (*gallery).set_column_range(2U, 5U);
        (*gallery).columns = 5U;
        std::vector<RibbonGalleryItem> items;
        const std::array<const char*, 6> names{"Title", "Heading", "Normal", "Compact", "Quote", "Strong"};
        for (const char* name : names) items.push_back({style_id(name), name, {}, {}});
        (*gallery).set_items({}, std::move(items));
        (*gallery).command = styles_;
        return gallery;
    }

    // Two Ribbons of the same tab at two widths (stage RB, phase 2b): the
    // wider shows the ladder entry that fits (groups shrunk by the default
    // cycle, the gallery giving up columns, collapsed groups as buttons that
    // open the group in an overlay); the narrower is below its narrowest entry
    // and scrolls by whole groups.
    void build_scaling_ribbons() {
        Ribbon& wide = *document_.gallery_commands_ribbon_scaling_wide;
        wide.set_tabs(scaling_tabs("scaling.wide", scaling_gallery("gallery.commands.ribbon-scaling.styles-wide")));
        Ribbon& narrow = *document_.gallery_commands_ribbon_scaling_narrow;
        narrow.set_tabs(
            scaling_tabs("scaling.narrow", scaling_gallery("gallery.commands.ribbon-scaling.styles-narrow")));
    }

    // The ribbon's file area and specialized controls (stage RB, phase 6): a
    // Ribbon whose Font group hosts a RibbonFontControl and a
    // RibbonColorPicker bound to value commands, whose File tab opens
    // Backstage, and a second BackstageView shown in place so that its
    // column and pages can be seen without opening it.
    void build_file_area() {
        font_command_ = std::make_shared<FontCommand>(
            make_command("gallery.commands.font", "Font", "Choose the document's font"));
        (*font_command_).value = {"Portsmouth Rapids", 11.0};
        color_command_ = std::make_shared<ColorCommand>(
            make_command("gallery.commands.colour", "Font colour", "Colour the selected text"));
        std::shared_ptr<RibbonFontControl> font =
            make_control<RibbonFontControl>(StableId("gallery.commands.ribbon-file-area.font"));
        (*font).set_families({"Portsmouth Rapids", "Carlito", "Tahoma"});
        (*font).command = font_command_;
        std::shared_ptr<RibbonColorPicker> colour =
            make_control<RibbonColorPicker>(StableId("gallery.commands.ribbon-file-area.colour"));
        (*colour).text = "Font &colour";
        (*colour).command = color_command_;
        const std::shared_ptr<Ribbon>& ribbon = document_.gallery_commands_ribbon_file_area_bar;
        RibbonTabSpec home{.stable_id = "file-area.home", .text = "&Home"};
        RibbonGroupSpec font_group{.stable_id = "file-area.font", .caption = "Font", .dialog_launcher = true};
        font_group.commands.push_back({.stable_id = "file-area.font-control", .kind = RibbonCommandKind::font_control,
                                       .text = "Font", .font_control = font});
        font_group.commands.push_back({.stable_id = "file-area.colour", .kind = RibbonCommandKind::color_picker,
                                       .text = "Font colour", .color_picker = colour});
        font_group.commands.push_back({.stable_id = "file-area.wrap", .kind = RibbonCommandKind::toggle,
                                       .size = RibbonCommandSize::small, .command = wrap_command_});
        RibbonGroupSpec document{.stable_id = "file-area.document", .caption = "Document"};
        document.commands.push_back({.stable_id = "file-area.save", .command = save_});
        home.groups.push_back(std::move(font_group));
        home.groups.push_back(std::move(document));
        (*ribbon).set_tabs({std::move(home)});
        std::shared_ptr<BackstageView> backstage =
            make_control<BackstageView>(StableId("gallery.commands.ribbon-file-area.backstage"));
        fill_backstage(*backstage);
        (*ribbon).backstage = backstage;
        fill_backstage(*document_.gallery_commands_ribbon_file_area_backstage_in_place);
    }

    // A Backstage column: Save and Save As as fast commands, Info and Recent
    // as topics with their pages, and About as a definitive command. Its
    // items' IDs extend the view's own.
    void fill_backstage(BackstageView& view) const {
        const std::string id(view.stable_id().value());
        std::shared_ptr<Label> info = label(id + ".info", "Fixture document · 2 pages · not saved", 11.0, 400, muted);
        std::shared_ptr<Label> recent = label(id + ".recent", "Report.txt\nNotes.txt", 11.0, 400, muted);
        view.set_items({{.stable_id = id + ".save", .command = save_},
                           {.stable_id = id + ".new", .command = new_},
                           {.stable_id = id + ".info-topic", .kind = BackstageItemKind::topic, .text = "&Info",
                            .page = info},
                           {.stable_id = id + ".recent-topic", .kind = BackstageItemKind::topic, .text = "&Recent",
                            .page = recent},
                           {.stable_id = id + ".about", .command = about_, .definitive = true}});
    }

    void on_styles_button(ButtonBase&) {
        if ((*drop_down_gallery_).overlay_open()) {
            (*drop_down_gallery_).close_overlay();
            return;
        }
        static_cast<void>((*drop_down_gallery_).open_overlay(gallery_button_));
    }

    void on_style_preview(const GalleryPreview& preview) {
        (*gallery_status_).text = "Previewing " + preview.item_id + " from " + preview.source_id;
    }

    void on_style_cancel(const GalleryPreview& preview) {
        (*gallery_status_).text = "Preview of " + preview.item_id + " withdrawn";
    }

    void on_style_value(const std::optional<std::string>& value) {
        (*gallery_status_).text = "Committed " + value.value_or(std::string("nothing"));
    }

    // A ToolStripContainer: a strip in the top panel, a vertical strip in
    // the left panel and a status strip in the bottom panel around content.
    void build_container() {
        const std::shared_ptr<ToolStripContainer>& frame = document_.gallery_commands_container_frame;
        std::shared_ptr<ToolStrip> top = make_control<ToolStrip>(StableId("gallery.commands.container.top-strip"));
        (*top).accessible_name = "File tools";
        (*top).set_items({
            {.stable_id = "frame.new", .command = new_},
            {.stable_id = "frame.save", .command = save_},
            {.stable_id = "frame.rule", .kind = ToolStripItemKind::separator},
            {.stable_id = "frame.find", .kind = ToolStripItemKind::text_box, .text = "Find"},
        });
        (*(*frame).top_panel()).join(top);
        std::shared_ptr<ToolStrip> left = make_control<ToolStrip>(StableId("gallery.commands.container.left-strip"));
        (*left).accessible_name = "Drawing tools";
        (*left).set_items({
            {.stable_id = "frame.pen", .text = "Pen", .check_on_click = true, .checked = true},
            {.stable_id = "frame.brush", .text = "Brush", .check_on_click = true},
            {.stable_id = "frame.tools-rule", .kind = ToolStripItemKind::separator},
            {.stable_id = "frame.fill", .text = "Fill"},
        });
        (*(*frame).left_panel()).join(left);
        std::shared_ptr<StatusStrip> status =
            make_control<StatusStrip>(StableId("gallery.commands.container.status-strip"));
        (*status).accessible_name = "Drawing status";
        (*status).set_items({{.stable_id = "frame.status", .kind = ToolStripItemKind::label,
                              .text = "Content panel between the strips", .spring = true}});
        (*(*frame).bottom_panel()).join(status);
        std::shared_ptr<Label> content = label("gallery.commands.container.content",
                                               "Content: the application's controls go in content_panel().", 11.0,
                                               400, muted);
        (*content).dock = DockStyle::fill;
        (*(*frame).content_panel()).add_child(content);
    }

    // A Ribbon over the same Commands (stage RB, phases 1 and 5): Home with a
    // launcher, a large split, small commands and a toggle; Insert with
    // hosted controls; the File tab's application menu; and a contextual
    // Picture Tools set (Format), made available so that its tint shows on
    // the tab and, when selected, on the plane.
    void build_ribbon() {
        const std::shared_ptr<Ribbon>& ribbon = document_.gallery_commands_ribbon_bar;
        std::vector<RibbonTabSpec> tabs;
        RibbonTabSpec home{.stable_id = "ribbon.home", .text = "&Home"};
        RibbonGroupSpec document{.stable_id = "ribbon.document", .caption = "Document", .dialog_launcher = true};
        document.commands.push_back({.stable_id = "ribbon.new", .command = new_});
        document.commands.push_back({.stable_id = "ribbon.save", .kind = RibbonCommandKind::split, .command = save_,
                                     .drop_down = export_items("ribbon.save.export")});
        document.commands.push_back({.stable_id = "ribbon.delete", .size = RibbonCommandSize::small,
                                     .command = delete_});
        document.commands.push_back({.stable_id = "ribbon.wrap", .kind = RibbonCommandKind::toggle,
                                     .size = RibbonCommandSize::small, .command = wrap_command_});
        document.commands.push_back({.stable_id = "ribbon.export", .kind = RibbonCommandKind::drop_down,
                                     .size = RibbonCommandSize::small, .text = "Export",
                                     .drop_down = export_items("ribbon.export")});
        RibbonGroupSpec view{.stable_id = "ribbon.view", .caption = "View"};
        view.commands.push_back({.stable_id = "ribbon.icons", .size = RibbonCommandSize::small, .command = icons_});
        view.commands.push_back({.stable_id = "ribbon.details", .size = RibbonCommandSize::small,
                                 .command = details_});
        view.commands.push_back({.stable_id = "ribbon.zoom", .kind = RibbonCommandKind::combo_box, .text = "Zoom",
                                 .choices = {"50%", "100%", "200%"}, .width = 80.0});
        RibbonGroupSpec run{.stable_id = "ribbon.run-group", .caption = "Run"};
        run.commands.push_back({.stable_id = "ribbon.run", .kind = RibbonCommandKind::split, .command = run_,
                                .drop_down = {item("ribbon.run.verbose", run_options_[0]),
                                              item("ribbon.run.watch", run_options_[1])}});
        home.groups.push_back(std::move(document));
        home.groups.push_back(std::move(view));
        home.groups.push_back(std::move(run));
        RibbonTabSpec insert{.stable_id = "ribbon.insert", .text = "&Insert"};
        RibbonGroupSpec find{.stable_id = "ribbon.find-group", .caption = "Find", .dialog_launcher = true};
        find.commands.push_back({.stable_id = "ribbon.find", .kind = RibbonCommandKind::text_box, .text = "Find"});
        find.commands.push_back({.stable_id = "ribbon.matches", .kind = RibbonCommandKind::numeric_up_down,
                                 .text = "Matches"});
        find.commands.push_back({.stable_id = "ribbon.case", .kind = RibbonCommandKind::check_box,
                                 .text = "Match case"});
        insert.groups.push_back(std::move(find));
        RibbonTabSpec format{.stable_id = "ribbon.format", .text = "Format", .tab_set = "ribbon.picture"};
        RibbonGroupSpec arrange{.stable_id = "ribbon.arrange", .caption = "Arrange"};
        arrange.commands.push_back({.stable_id = "ribbon.format.about", .command = about_});
        arrange.commands.push_back({.stable_id = "ribbon.format.delete", .size = RibbonCommandSize::small,
                                    .command = delete_});
        format.groups.push_back(std::move(arrange));
        tabs.push_back(std::move(home));
        tabs.push_back(std::move(insert));
        tabs.push_back(std::move(format));
        (*ribbon).set_tabs(std::move(tabs), {{"ribbon.picture", "Picture Tools"}});
        (*ribbon).set_tab_set_availability("ribbon.picture", RibbonTabSetAvailability::available);
        (*ribbon).set_application_menu({item("ribbon.file.new", new_), item("ribbon.file.save", save_),
                                        separator("ribbon.file.separator"), item("ribbon.file.about", about_)});
        on((*ribbon).dialog_launcher_clicked(), *this, &CommandsAndMenusPage::on_ribbon_launcher);
    }

    void on_ribbon_launcher(const RibbonDialogLaunch& launch) {
        (*status_).text = "Ribbon dialog launcher · " + launch.group_id;
    }

    // Shared application state.
    void on_dirty_changed(const bool dirty) {
        (*save_).set_enabled(dirty);
        (*status_).text = dirty ? "Document modified · Save enabled on the bar, the menus, the button and Ctrl+S"
                                  : "Document clean · Save disabled everywhere";
    }

    void on_wrap_changed(const bool wrap) {
        (*wrap_command_).checked = wrap;
        (*status_).text = std::string("Word wrap ") + (wrap ? "on" : "off") +
                            " · Value projected onto the check Command";
    }

    // Command handlers; CommandInvocation names the surface that invoked.
    void on_new(const CommandInvocation& invocation) {
        dirty_.set(true);
        (*status_).text = "New document · invoked from " + invocation.source_id;
    }

    void on_save(const CommandInvocation& invocation) {
        ++saves_;
        (*saves_label_).text = "Saves: " + std::to_string(saves_);
        dirty_.set(false);
        (*status_).text = "Save #" + std::to_string(saves_) + " invoked from " +
                            invocation.source_id + " · document clean again";
    }

    void on_wrap_invoked(const CommandInvocation& invocation) {
        wrap_.set(!wrap_.get());
        (*status_).text = std::string("Word wrap ") + (wrap_.get() ? "on" : "off") +
                            " · toggled from " + invocation.source_id;
    }

    void on_view_mode(const int mode, const CommandInvocation& invocation) {
        (*icons_).checked = mode == 0;
        (*details_).checked = mode == 1;
        (*status_).text = std::string("View ") + (mode == 0 ? "icons" : "details") +
                            " · radio Commands checked from " + invocation.source_id;
    }

    void on_run(const CommandInvocation& invocation) {
        (*status_).text = "Run · split button primary region · " + invocation.source_id;
    }

    void on_run_option(const int option, const CommandInvocation& invocation) {
        (*status_).text = std::string(option == 0 ? "Run verbose" : "Run and watch") +
                            " · split button menu · " + invocation.source_id;
    }

    void on_export(const int format, const CommandInvocation& invocation) {
        (*status_).text = std::string("Export ") + export_labels_[static_cast<std::size_t>(format)] +
                            " · invoked from " + invocation.source_id;
    }

    void on_quick_access_placement(const QuickAccessToolbarPlacementChange& change) {
        const bool below = change.placement == QuickAccessToolbarPlacement::below_ribbon;
        (*quick_access_note_).text = std::string("The customize button lists each command with a check · placement: ") +
                                       (below ? "below" : "above") + " the ribbon";
    }

    void on_quick_access_customized(const QuickAccessToolbarCustomization& change) {
        (*status_).text = "Quick Access Toolbar · " + change.command_id + (change.shown ? " shown" : " hidden");
    }

    void on_about() {
        (*status_).text = "Commands and menus · MenuStrip, CommandBar, ContextMenu and "
                            "DropDownButton all execute shared Command instances";
    }

    // Surface notifications: group events that survive item replacement.
    // The bound Command has already run; the bar reports the item afterwards.
    void on_bar_invoked(const CommandItem& bar_item) {
        (*status_).text = (*status_).text.read() + " · bar item " + bar_item.label;
    }

    void on_menu_invoked(const MenuStripInvocation& invocation) {
        (*status_).text = (*status_).text.read() + " · menu " + invocation.top_level_id;
    }

    void on_context_invoked(const MenuItemInvocation& invocation) {
        (*status_).text = (*status_).text.read() + " · context row " + invocation.item_id;
    }

    void on_context_open(const bool open) {
        if (!open) (*status_).text = "Context menu closed · invoker focus restored";
    }

    void on_context_requested(ButtonBase& source) {
        const Rect bounds = source.absolute_bounds();
        context_menu_.show(source.shared_from_this(), {bounds.x + 12.0, bounds.y + bounds.height},
                           MenuOpenMode::keyboard);
    }

    // Drop-down buttons own the disclosure state; the menus report open/close.
    void on_export_requested(DropDownButton& source) {
        const Rect bounds = source.absolute_bounds();
        export_menu_.show(source.shared_from_this(), {bounds.x, bounds.y + bounds.height},
                          MenuOpenMode::pointer);
    }

    void on_export_close() { export_menu_.close(); }

    void on_export_menu_open(const bool open) { (*export_).set_drop_down_open(open); }

    void on_run_requested(DropDownButton& source) {
        const Rect bounds = source.absolute_bounds();
        run_menu_.show(source.shared_from_this(), {bounds.x, bounds.y + bounds.height},
                       MenuOpenMode::pointer);
    }

    void on_run_close() { run_menu_.close(); }

    void on_run_menu_open(const bool open) { (*run_button_).set_drop_down_open(open); }

    static constexpr std::array<const char*, 3> export_names_{"pdf", "png", "text"};
    static constexpr std::array<const char*, 3> export_labels_{"PDF", "PNG", "plain text"};

    std::shared_ptr<Command> new_;
    std::shared_ptr<Command> save_;
    std::shared_ptr<Command> delete_;
    std::shared_ptr<Command> wrap_command_;
    std::shared_ptr<Command> icons_;
    std::shared_ptr<Command> details_;
    std::shared_ptr<Command> run_;
    std::shared_ptr<Command> about_;
    std::array<std::shared_ptr<Command>, 3> export_formats_;
    std::array<std::shared_ptr<Command>, 2> run_options_;
    ContextMenu export_menu_;
    ContextMenu run_menu_;
    ContextMenu context_menu_;
    std::shared_ptr<DropDownButton> export_;
    std::shared_ptr<DropDownButton> run_button_;
    std::shared_ptr<Button> context_button_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<Label> saves_label_;
    std::shared_ptr<GalleryCommand> styles_;
    std::shared_ptr<FontCommand> font_command_;
    std::shared_ptr<ColorCommand> color_command_;
    std::shared_ptr<Button> gallery_button_;
    std::shared_ptr<RibbonGallery> drop_down_gallery_;
    std::shared_ptr<Label> gallery_status_;
    std::shared_ptr<Label> quick_access_note_;
    Value<bool> dirty_{false};
    Value<bool> wrap_{false};
    unsigned saves_{};
    // Declared after the Commands and Values its surfaces bind, so it is
    // released first.
    CommandsDocument<CommandsAndMenusPage> document_{};
};

} // namespace

void add_commands_and_menus(const std::shared_ptr<Surface>& page,
                            const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<CommandsAndMenusPage>(page, context));
}

} // namespace gui_forms::gallery::pages
