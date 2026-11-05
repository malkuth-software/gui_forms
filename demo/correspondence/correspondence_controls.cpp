#include "correspondence_controls.hpp"

#include "gui_forms/host/services/host_services.hpp"

#include <charconv>
#include <chrono>
#include <optional>
#include <utility>

namespace gui_forms::correspondence {
namespace {

constexpr Color shell_blue_dark = Color::rgba(24, 45, 66);
constexpr Color canvas = Color::rgba(239, 243, 247);
constexpr Color paper = Color::rgba(255, 255, 255);
constexpr Color navigation = Color::rgba(225, 235, 243);
constexpr Color ink = Color::rgba(28, 39, 50);
constexpr Color muted = Color::rgba(91, 107, 121);

// Chrome uses the control face; message text uses the content face.
constexpr FontSpec chrome_font{FontRole::control, 11.5, 400, false, 0.12};
constexpr FontSpec status_font{FontRole::control, 11.0, 400, false, 0.12};
constexpr FontSpec folder_font{FontRole::content, 12.5, 400, false};
constexpr FontSpec list_font{FontRole::content, 11.5, 400, false};
constexpr FontSpec subject_font{FontRole::content, 16.0, 700, false};
constexpr FontSpec detail_font{FontRole::content, 12.0, 400, false};
constexpr FontSpec body_font{FontRole::content, 12.5, 400, false};

constexpr double command_row_height = 46.0;
constexpr double status_height = 26.0;
constexpr double search_width = 220.0;

[[nodiscard]] std::optional<std::uint32_t> parse_suffix(const std::string_view value,
                                                        const std::string_view prefix) {
    if (value.size() <= prefix.size() || value.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }
    const std::string_view digits = value.substr(prefix.size());
    std::uint32_t result{};
    const std::from_chars_result parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::shared_ptr<Label> make_label(const std::string_view id,
                                                const FontSpec font, const Color color,
                                                const double height) {
    std::shared_ptr<Label> label = make_control<Label>(StableId(std::string(id)));
    (*label).set_font(font);
    (*label).set_foreground(color);
    (*label).dock = DockStyle::top;
    (*label).set_requested_bounds({0.0, 0.0, 0.0, height});
    (*label).set_use_mnemonic(false);
    return label;
}

[[nodiscard]] std::shared_ptr<Command> make_command(const std::string_view id,
                                                    const std::string_view text,
                                                    const std::string_view description,
                                                    const std::string_view shortcut) {
    std::shared_ptr<Command> command =
        std::make_shared<Command>(std::string(id), std::string(text));
    (*command).set_description(std::string(description));
    if (!shortcut.empty()) {
        (*command).set_shortcut(std::string(shortcut));
    }
    return command;
}

[[nodiscard]] MenuItemSpec menu_item(const std::string_view id,
                                     const std::shared_ptr<Command>& command) {
    return MenuItemSpec(std::string(id), MenuItemKind::command, command);
}

[[nodiscard]] MenuItemSpec separator(const std::string_view id) {
    return MenuItemSpec(std::string(id), MenuItemKind::separator);
}

// English plural for the fixture nouns: "message"/"messages", "match"/"matches".
[[nodiscard]] std::string plural(const std::size_t count, const std::string_view noun) {
    std::string result = std::to_string(count) + " " + std::string(noun);
    if (count != 1U) result += noun.ends_with("ch") ? "es" : "s";
    return result;
}

}  // namespace

struct CorrespondenceShell::SynchronizeGuard final {
    explicit SynchronizeGuard(bool& flag) noexcept : flag_(flag) { flag_ = true; }
    ~SynchronizeGuard() { flag_ = false; }
    SynchronizeGuard(const SynchronizeGuard&) = delete;
    SynchronizeGuard& operator=(const SynchronizeGuard&) = delete;
    bool& flag_;
};

std::string command_button_id(const int command) {
    return std::string(ids::command_bar) + ".item." + std::to_string(command);
}

std::string folder_id(const Folder folder) {
    return std::string(ids::folder_prefix) + std::string(folder_token(folder));
}

std::string message_id(const std::uint32_t id) {
    return std::string(ids::message_prefix) + std::to_string(id);
}

BasicControlStyle correspondence_style() {
    BasicControlStyle style;
    style.face = Color::rgba(226, 232, 238);
    style.face_light = Color::rgba(248, 250, 252);
    style.paper = paper;
    style.highlight = Color::rgba(255, 255, 255);
    style.border = Color::rgba(144, 159, 173);
    style.dark_border = Color::rgba(67, 86, 103);
    style.text = ink;
    style.disabled_text = Color::rgba(133, 143, 152);
    style.accent = Color::rgba(39, 116, 184);
    style.accent_light = Color::rgba(203, 229, 247);
    style.link = Color::rgba(27, 88, 143);
    style.visited_link = Color::rgba(116, 82, 153);
    return style;
}

CorrespondenceShell::CorrespondenceShell(StableId stable_id)
    : Panel(std::move(stable_id)) {
    set_background(canvas);
    set_style(correspondence_style());
    accessible_name = "GUI.Forms Correspondence";
}

void CorrespondenceShell::initialize_control_tree() {
    if (initialized_) return;
    initialized_ = true;
    build_commands();
    // Dock order is the child order: the menu and command row take the top,
    // the status line the bottom, and the split container fills the rest.
    build_menu();
    build_command_row();
    status_ = make_control<Label>(StableId(std::string(ids::status)));
    (*status_).set_font(status_font);
    (*status_).set_foreground(muted);
    (*status_).dock = DockStyle::bottom;
    (*status_).set_requested_bounds({0.0, 0.0, 0.0, status_height});
    (*status_).set_use_mnemonic(false);
    (*status_).accessible_name = "Status";
    add_child(status_);
    build_panes();
    synchronize();
}

void CorrespondenceShell::build_commands() {
    new_message_command_ = make_command("correspondence.command.new", "New message",
                                        "Create a draft in the Drafts folder", "Ctrl+N");
    reply_command_ = make_command("correspondence.command.reply", "Reply",
                                  "Draft a reply to the selected message", "Ctrl+R");
    forward_command_ = make_command("correspondence.command.forward", "Forward",
                                    "Draft a forward of the selected message", "Ctrl+Shift+F");
    archive_command_ = make_command("correspondence.command.archive", "Archive",
                                    "Move the selected message to Archive", "Ctrl+E");
    delete_command_ = make_command("correspondence.command.delete", "Delete",
                                   "Move the selected message to Trash", "Delete");
    flag_command_ = make_command("correspondence.command.flag", "Flag",
                                 "Toggle the flag on the selected message", "Ctrl+L");
    mark_unread_command_ = make_command("correspondence.command.mark-unread", "Mark as unread",
                                        "Mark the selected message unread", "");
    search_command_ = make_command("correspondence.command.search", "Search",
                                   "Move focus to the search field", "");
    about_command_ = make_command("correspondence.command.about",
                                  "About GUI.Forms Correspondence",
                                  "Show version and instrumentation", "");
    commands_ = {new_message_command_, reply_command_, forward_command_, archive_command_,
                 delete_command_, flag_command_, mark_unread_command_, search_command_,
                 about_command_};
    for (const Folder folder : all_folders) {
        std::shared_ptr<Command> command = make_command(
            "correspondence.command.view." + std::string(folder_token(folder)),
            folder_name(folder), "Show the " + std::string(folder_name(folder)) + " folder", "");
        on((*command).invoked(), *this, &CorrespondenceShell::on_view_folder, folder);
        folder_commands_.push_back(command);
        commands_.push_back(std::move(command));
    }
    on((*new_message_command_).invoked(), *this, &CorrespondenceShell::on_new_message);
    on((*reply_command_).invoked(), *this, &CorrespondenceShell::on_reply);
    on((*forward_command_).invoked(), *this, &CorrespondenceShell::on_forward);
    on((*archive_command_).invoked(), *this, &CorrespondenceShell::on_archive);
    on((*delete_command_).invoked(), *this, &CorrespondenceShell::on_delete);
    on((*flag_command_).invoked(), *this, &CorrespondenceShell::on_flag);
    on((*mark_unread_command_).invoked(), *this, &CorrespondenceShell::on_mark_unread);
    on((*search_command_).invoked(), *this, &CorrespondenceShell::on_focus_search);
    on((*about_command_).invoked(), *this, &CorrespondenceShell::on_about);
}

void CorrespondenceShell::build_menu() {
    menu_ = make_control<MenuStrip>(StableId(std::string(ids::menu)));
    (*menu_).set_font(chrome_font);
    (*menu_).dock = DockStyle::top;
    std::vector<MenuItemSpec> view_items;
    for (std::size_t index = 0; index < all_folders.size(); ++index) {
        view_items.push_back(menu_item(
            "correspondence.menu.view." + std::string(folder_token(all_folders[index])),
            folder_commands_[index]));
    }
    std::vector<MenuStripItemSpec> items;
    items.push_back({"correspondence.menu.file", "File",
                     {menu_item("correspondence.menu.file.new", new_message_command_)}});
    items.push_back({"correspondence.menu.edit", "Edit",
                     {menu_item("correspondence.menu.edit.mark-unread", mark_unread_command_),
                      menu_item("correspondence.menu.edit.flag", flag_command_),
                      separator("correspondence.menu.edit.separator"),
                      menu_item("correspondence.menu.edit.search", search_command_)}});
    items.push_back({"correspondence.menu.view", "View", std::move(view_items)});
    items.push_back({"correspondence.menu.message", "Message",
                     {menu_item("correspondence.menu.message.reply", reply_command_),
                      menu_item("correspondence.menu.message.forward", forward_command_),
                      separator("correspondence.menu.message.separator"),
                      menu_item("correspondence.menu.message.archive", archive_command_),
                      menu_item("correspondence.menu.message.delete", delete_command_)}});
    items.push_back({"correspondence.menu.help", "Help",
                     {menu_item("correspondence.menu.help.about", about_command_)}});
    (*menu_).set_items(std::move(items));
    add_child(menu_);

    context_menu_ = std::make_shared<ContextMenu>(std::string(ids::context_menu));
    (*context_menu_).set_items({
        menu_item("correspondence.messages.menu.reply", reply_command_),
        menu_item("correspondence.messages.menu.forward", forward_command_),
        menu_item("correspondence.messages.menu.flag", flag_command_),
        menu_item("correspondence.messages.menu.mark-unread", mark_unread_command_),
        separator("correspondence.messages.menu.separator"),
        menu_item("correspondence.messages.menu.archive", archive_command_),
        menu_item("correspondence.messages.menu.delete", delete_command_)});
}

void CorrespondenceShell::build_command_row() {
    command_row_ = make_control<Panel>(StableId(std::string(ids::commands)));
    (*command_row_).set_background(Color::rgba(248, 250, 252));
    (*command_row_).set_style(correspondence_style());
    (*command_row_).dock = DockStyle::top;
    (*command_row_).set_requested_bounds({0.0, 0.0, 0.0, command_row_height});
    (*command_row_).padding = {10.0, 7.0, 10.0, 7.0};
    add_child(command_row_);

    // The search field docks right before the bar fills the remainder.
    search_ = make_control<TextBox>(StableId(std::string(ids::search)));
    (*search_).set_placeholder_text("Search this folder");
    (*search_).set_font(detail_font);
    (*search_).set_style(correspondence_style());
    (*search_).dock = DockStyle::right;
    (*search_).set_requested_bounds({0.0, 0.0, search_width, 0.0});
    (*search_).accessible_name = "Search this folder";
    (*command_row_).add_child(search_);
    on((*search_).text_changed(), *this, &CorrespondenceShell::on_search_changed);

    bar_ = make_control<CommandBar>(StableId(std::string(ids::command_bar)));
    (*bar_).set_background(Color::rgba(248, 250, 252));
    (*bar_).set_style(correspondence_style());
    (*bar_).dock = DockStyle::fill;
    (*bar_).accessible_name = "Message commands";
    (*command_row_).add_child(bar_);
    std::vector<CommandItem> items;
    items.push_back({.id = command_new_message, .label = "New message",
                     .accessible_name = "New message",
                     .shortcut = KeyGesture{PhysicalKey::n, Modifier::control},
                     .command = new_message_command_.get()});
    items.push_back({.id = command_reply, .label = "Reply", .accessible_name = "Reply",
                     .shortcut = KeyGesture{PhysicalKey::r, Modifier::control},
                     .command = reply_command_.get()});
    items.push_back({.id = command_forward, .label = "Forward", .accessible_name = "Forward",
                     .shortcut = KeyGesture{PhysicalKey::f, Modifier::control | Modifier::shift},
                     .command = forward_command_.get()});
    items.push_back({.id = command_archive, .label = "Archive", .accessible_name = "Archive",
                     .shortcut = KeyGesture{PhysicalKey::e, Modifier::control},
                     .command = archive_command_.get()});
    items.push_back({.id = command_delete, .label = "Delete", .accessible_name = "Delete",
                     .shortcut = KeyGesture{PhysicalKey::delete_forward, Modifier::none},
                     .command = delete_command_.get()});
    items.push_back({.id = command_flag, .label = "Flag", .accessible_name = "Flag",
                     .shortcut = KeyGesture{PhysicalKey::l, Modifier::control},
                     .command = flag_command_.get()});
    (*bar_).set_items(std::move(items));
    for (int index = 0; index < static_cast<int>((*bar_).items().size()); ++index) {
        Button& button = (*bar_).item(index);
        button.set_style(correspondence_style());
        button.font = chrome_font;
        button.set_visual_style(ButtonVisualStyle::command);
    }
}

void CorrespondenceShell::build_panes() {
    split_ = make_control<SplitContainer>(StableId(std::string(ids::split)));
    (*split_).set_orientation(Orientation::vertical);
    (*split_).set_splitter_distance(200.0);
    (*split_).set_first_minimum(140.0);
    (*split_).set_second_minimum(320.0);
    (*split_).dock = DockStyle::fill;
    add_child(split_);
    (*(*split_).first_panel()).set_background(navigation);
    (*(*split_).second_panel()).set_background(canvas);

    folders_ = make_control<TreeView>(StableId(std::string(ids::folders)));
    (*folders_).set_show_expanders(false);
    (*folders_).set_item_height(28.0);
    (*folders_).set_indentation(8.0);
    (*folders_).set_font(folder_font);
    (*folders_).set_background(navigation);
    (*folders_).set_style(correspondence_style());
    (*folders_).dock = DockStyle::fill;
    (*folders_).accessible_name = "Folders";
    (*(*split_).first_panel()).add_child(folders_);
    on((*folders_).selection_changed(), *this, &CorrespondenceShell::on_folder_selected);

    content_split_ = make_control<SplitContainer>(StableId(std::string(ids::content_split)));
    (*content_split_).set_orientation(Orientation::horizontal);
    (*content_split_).set_splitter_distance(290.0);
    // At the 600x420 minimum the list yields to the reading pane: its header
    // block takes about 90 logical pixels, so 180 keeps the body readable.
    (*content_split_).set_first_minimum(100.0);
    (*content_split_).set_second_minimum(180.0);
    (*content_split_).dock = DockStyle::fill;
    (*(*split_).second_panel()).add_child(content_split_);
    (*(*content_split_).first_panel()).set_background(paper);
    (*(*content_split_).second_panel()).set_background(paper);

    messages_ = make_control<CorrespondenceView>(StableId(std::string(ids::messages)));
    (*messages_).set_font(list_font);
    (*messages_).set_compact_height(48.0);
    (*messages_).set_expanded_height(132.0);
    (*messages_).set_style(correspondence_style());
    (*messages_).dock = DockStyle::fill;
    (*messages_).accessible_name = "Messages";
    (*(*content_split_).first_panel()).add_child(messages_);
    on((*messages_).selection_changed(), *this, &CorrespondenceShell::on_message_selected);
    on((*messages_).item_activated(), *this, &CorrespondenceShell::on_message_activated);
    on((*messages_).context_requested(), *this, &CorrespondenceShell::on_context_requested);

    reading_ = make_control<Panel>(StableId(std::string(ids::reading)));
    (*reading_).set_background(paper);
    (*reading_).set_style(correspondence_style());
    (*reading_).padding = {18.0, 14.0, 18.0, 14.0};
    (*reading_).dock = DockStyle::fill;
    (*reading_).accessible_name = "Reading pane";
    (*(*content_split_).second_panel()).add_child(reading_);

    reading_subject_ = make_label(ids::reading_subject, subject_font, shell_blue_dark, 30.0);
    (*reading_subject_).accessible_name = "Subject";
    (*reading_).add_child(reading_subject_);
    reading_correspondent_ = make_label(ids::reading_correspondent, detail_font, ink, 22.0);
    (*reading_correspondent_).accessible_name = "Correspondent";
    (*reading_).add_child(reading_correspondent_);
    reading_date_ = make_label(ids::reading_date, detail_font, muted, 26.0);
    (*reading_date_).accessible_name = "Date";
    (*reading_).add_child(reading_date_);

    reading_body_ = make_control<TextBox>(StableId(std::string(ids::reading_body)));
    (*reading_body_).set_multiline(true);
    (*reading_body_).set_word_wrap(true);
    (*reading_body_).set_read_only(true);
    (*reading_body_).set_font(body_font);
    (*reading_body_).set_style(correspondence_style());
    (*reading_body_).set_border_style(BorderStyle::none);
    (*reading_body_).dock = DockStyle::fill;
    (*reading_body_).accessible_name = "Message body";
    (*reading_).add_child(reading_body_);
}

void CorrespondenceShell::synchronize() {
    if (synchronizing_ || !initialized_) return;
    SynchronizeGuard guard(synchronizing_);
    std::optional<UpdateScope> scope;
    if (Window* window = attached_window()) {
        scope.emplace((*window).begin_update());
    }
    project_folders();
    project_messages();
    project_reading_pane();
    project_status();
    project_commands();
}

void CorrespondenceShell::project_folders() {
    std::vector<TreeViewItem> items;
    for (const Folder folder : all_folders) {
        TreeViewItem item;
        item.stable_id = folder_id(folder);
        item.text = std::string(folder_name(folder));
        const std::size_t unread = model_.unread_count(folder);
        if (unread > 0U) {
            item.text += "  (" + std::to_string(unread) + ")";
        }
        items.push_back(std::move(item));
    }
    (*folders_).set_items(std::move(items));
    (*folders_).set_selected_id(folder_id(model_.selected_folder()));
}

void CorrespondenceShell::project_messages() {
    std::vector<CorrespondenceItem> items;
    const std::string& search = model_.search();
    for (const std::uint32_t id : model_.visible_messages()) {
        const Message* message = model_.find(id);
        if (message == nullptr) continue;
        CorrespondenceItem item;
        item.stable_id = message_id(id);
        item.title = (*message).subject;
        item.secondary_text = (*message).outgoing
            ? "To: " + (*message).correspondent : (*message).correspondent;
        item.metadata = (*message).date;
        item.excerpt = (*message).preview;
        item.metric = (*message).read ? std::string() : std::string("Unread");
        item.glyph = (*message).folder == Folder::archive ? ObjectGlyph::archive
                                                           : ObjectGlyph::document;
        item.information_heading = "Status";
        if (!(*message).read) item.information_tags.push_back("Unread");
        if ((*message).flagged) item.information_tags.push_back("Flagged");
        if ((*message).folder == Folder::drafts) item.information_tags.push_back("Draft");
        item.information_detail = (*message).address;
        if (!search.empty()) item.emphasis_terms.push_back(search);
        items.push_back(std::move(item));
    }
    (*messages_).set_items(std::move(items));
    const std::optional<std::uint32_t> selected = model_.selected_message();
    (*messages_).set_selected_id(selected ? message_id(*selected) : std::string());
}

void CorrespondenceShell::project_reading_pane() {
    const std::optional<std::uint32_t> selected = model_.selected_message();
    const Message* message = selected ? model_.find(*selected) : nullptr;
    if (message == nullptr) {
        (*reading_subject_).set_text("No message selected");
        (*reading_correspondent_).set_text("");
        (*reading_date_).set_text(std::string(folder_name(model_.selected_folder())) +
                                  " · " + plural(model_.visible_messages().size(), "message"));
        (*reading_body_).set_text(
            "Select a message in the list to read it here.\n\n"
            "Every folder and message is a deterministic in-memory fixture: nothing is "
            "sent, received, or written to disk.");
        return;
    }
    (*reading_subject_).set_text((*message).subject);
    const std::string party = (*message).correspondent.empty()
        ? std::string("(no recipient)")
        : (*message).address.empty()
            ? (*message).correspondent
            : (*message).correspondent + " <" + (*message).address + ">";
    (*reading_correspondent_).set_text(((*message).outgoing ? "To: " : "From: ") + party);
    std::string date_line = (*message).date + " · " + std::string(folder_name((*message).folder));
    if ((*message).flagged) date_line += " · Flagged";
    (*reading_date_).set_text(date_line);
    (*reading_body_).set_text((*message).body);
}

void CorrespondenceShell::project_status() {
    const Folder folder = model_.selected_folder();
    std::string text = std::string(folder_name(folder)) + " · " +
                       plural(model_.count(folder), "message") + " · " +
                       std::to_string(model_.unread_count(folder)) + " unread · " +
                       plural(model_.total_count(), "message") + " in all folders";
    if (!model_.search().empty()) {
        text += " · " + plural(model_.visible_messages().size(), "match") + " for \"" +
                model_.search() + "\"";
    }
    (*status_).set_text(std::move(text));
}

void CorrespondenceShell::project_commands() {
    const std::optional<std::uint32_t> selected = model_.selected_message();
    const Message* message = selected ? model_.find(*selected) : nullptr;
    const bool has_selection = message != nullptr;
    (*reply_command_).set_enabled(has_selection);
    (*forward_command_).set_enabled(has_selection);
    (*archive_command_).set_enabled(has_selection && (*message).folder != Folder::archive);
    (*delete_command_).set_enabled(has_selection);
    (*delete_command_).set_description(has_selection && (*message).folder == Folder::trash
        ? "Remove the selected message permanently"
        : "Move the selected message to Trash");
    (*flag_command_).set_enabled(has_selection);
    (*flag_command_).set_checked(has_selection && (*message).flagged);
    (*mark_unread_command_).set_enabled(has_selection && (*message).read);
    for (std::size_t index = 0; index < all_folders.size(); ++index) {
        (*folder_commands_[index]).set_checked(all_folders[index] == model_.selected_folder());
    }
}

void CorrespondenceShell::attach_tooltips() {
    Window* window = attached_window();
    if (window == nullptr || tooltips_) return;
    tooltips_ = std::make_shared<ToolTip>(*window);
    (*tooltips_).set_initial_delay(std::chrono::milliseconds(400));
    for (int index = 0; index < static_cast<int>((*bar_).items().size()); ++index) {
        const CommandItem& item = (*bar_).items()[static_cast<std::size_t>(index)];
        std::string text = item.label;
        if (item.command != nullptr) {
            const CommandState& state = (*item.command).state();
            text = state.description;
            if (!state.shortcut.empty()) text += " (" + state.shortcut + ")";
        }
        (*tooltips_).set_tool_tip((*bar_).item(index).shared_from_this(), std::move(text));
    }
    (*tooltips_).set_tool_tip(search_, "Filter the current folder by sender, subject, or preview");
}

std::string CorrespondenceShell::about_text() const {
    std::string text = "GUI.Forms Correspondence · fixture mail client · CPU renderer";
    if (const Window* window = attached_window()) {
        const MetricsSnapshot metrics = (*window).metrics_snapshot();
        text += " · " + std::to_string(metrics.control_count) + " retained controls · " +
                std::to_string(metrics.paint_passes) + " paint passes · " +
                std::to_string(metrics.mutations) + " mutations";
    }
    text += " · model revision " + std::to_string(model_.revision());
    return text;
}

void CorrespondenceShell::show_about() {
    const std::string text = about_text();
    Window* window = attached_window();
    HostServices* services = window == nullptr ? nullptr : (*window).host_services();
    if (services != nullptr) {
        HostMessageDialogRequest message;
        message.title = "About GUI.Forms Correspondence";
        message.message = text;
        message.buttons = HostMessageButtons::ok;
        message.icon = HostMessageIcon::information;
        HostDialogRequest request;
        request.request_id = next_dialog_request_++;
        request.owner_id = std::string(stable_id().value());
        request.payload = std::move(message);
        const HostDialogResult result = (*services).show_dialog(request);
        if (result.status.accepted()) return;
    }
    // Headless or dialog-less hosts still surface the text in the status line.
    (*status_).set_text(text);
}

void CorrespondenceShell::on_folder_selected(const TreeSelectionChange& change) {
    if (synchronizing_) return;
    const std::string_view current = change.current_id;
    if (current.size() <= ids::folder_prefix.size()) return;
    const std::optional<Folder> folder =
        folder_from_token(current.substr(ids::folder_prefix.size()));
    if (!folder) return;
    model_.select_folder(*folder);
    synchronize();
}

void CorrespondenceShell::on_message_selected(const CorrespondenceSelectionChange& change) {
    if (synchronizing_) return;
    const std::optional<std::uint32_t> id = parse_suffix(change.current_id, ids::message_prefix);
    model_.select_message(id);
    synchronize();
}

void CorrespondenceShell::on_message_activated(const std::string& stable_id) {
    if (synchronizing_) return;
    const std::optional<std::uint32_t> id = parse_suffix(stable_id, ids::message_prefix);
    if (!id) return;
    model_.select_message(id);
    model_.mark_read(*id, true);
    synchronize();
}

void CorrespondenceShell::on_context_requested(const ObjectContextRequest& request) {
    if (synchronizing_) return;
    const std::optional<std::uint32_t> id = parse_suffix(request.stable_id, ids::message_prefix);
    if (!id) return;
    model_.select_message(id);
    synchronize();
    if (attached_window() != nullptr) {
        (*context_menu_).show(messages_, request.screen_position, MenuOpenMode::pointer);
    }
}

void CorrespondenceShell::on_search_changed(const std::string& text) {
    if (synchronizing_) return;
    model_.set_search(text);
    synchronize();
}

void CorrespondenceShell::on_new_message() {
    model_.create_draft();
    synchronize();
}

void CorrespondenceShell::on_reply() {
    if (const std::optional<std::uint32_t> selected = model_.selected_message()) {
        model_.reply_to(*selected);
        synchronize();
    }
}

void CorrespondenceShell::on_forward() {
    if (const std::optional<std::uint32_t> selected = model_.selected_message()) {
        model_.forward(*selected);
        synchronize();
    }
}

void CorrespondenceShell::on_archive() {
    if (const std::optional<std::uint32_t> selected = model_.selected_message()) {
        model_.archive(*selected);
        synchronize();
    }
}

void CorrespondenceShell::on_delete() {
    if (const std::optional<std::uint32_t> selected = model_.selected_message()) {
        model_.delete_message(*selected);
        synchronize();
    }
}

void CorrespondenceShell::on_flag() {
    if (const std::optional<std::uint32_t> selected = model_.selected_message()) {
        model_.toggle_flag(*selected);
        synchronize();
    }
}

void CorrespondenceShell::on_mark_unread() {
    if (const std::optional<std::uint32_t> selected = model_.selected_message()) {
        model_.mark_read(*selected, false);
        synchronize();
    }
}

void CorrespondenceShell::on_focus_search() {
    if (Window* window = attached_window()) {
        static_cast<void>((*window).request_focus(search_));
    }
}

void CorrespondenceShell::on_about() {
    show_about();
}

void CorrespondenceShell::on_view_folder(const Folder folder) {
    model_.select_folder(folder);
    synchronize();
}

CorrespondenceTree build_correspondence_tree() {
    CorrespondenceTree tree;
    tree.root = make_control<CorrespondenceShell>(StableId(std::string(ids::shell)));
    return tree;
}

}  // namespace gui_forms::correspondence
