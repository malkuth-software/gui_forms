#include "demo_parts.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/gallery_command/gallery_command.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/choice_group.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/controls/dock_workspace_values.hpp"
#include "gui_forms/controls/ribbon_gallery.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/track_bar.hpp"
#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/theme/familiar_themes/familiar_themes.hpp"
#include "gui_forms/timer/timer/timer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The workspaces-and-themes page: three DevExpress application-level takes
// the survey rates "demo only", composed from stock parts with no new API.
// - Workspaces: one DockWorkspace with named layouts saved for three
//   tasks (coding, debugging, writing) and switched by a ChoiceGroup; "Save
//   as Custom" keeps the arrangement the user made (WorkspaceManager).
// - Theme gallery: a RibbonGallery of the familiar themes; hovering an item
//   previews it on a group of sample controls (set_theme_override) and a
//   click keeps it (the skin gallery).
// - AI chat panel: a transcript, a prompt box and a reply that streams in
//   word by word from a local canned responder, which needs no service. The
//   full chat stress demonstrator lives in the demonstrators repository
//   (chat_stress); this panel points there.
namespace gui_forms::gallery::pages {
namespace {

// -- workspaces ---------------------------------------------------------------
struct WorkspacePanel final {
    std::string_view id{};
    std::string_view title{};
    DockSide side{DockSide::left};
};

constexpr std::array<WorkspacePanel, 4> workspace_tools{{
    {"explorer", "Explorer", DockSide::left},
    {"properties", "Properties", DockSide::right},
    {"output", "Output", DockSide::bottom},
    {"watch", "Watch", DockSide::bottom},
}};

constexpr std::array<std::string_view, 3> workspace_names{"Coding", "Debugging", "Writing"};
// Which tools each workspace shows, in workspace_tools order.
constexpr std::array<std::array<bool, 4>, 3> workspace_tools_shown{{
    {true, true, false, false},
    {true, false, true, true},
    {false, false, false, false},
}};

// -- themes -------------------------------------------------------------------
struct ThemeEntry final {
    std::string_view id{};
    std::string_view name{};
    FamiliarTheme theme{FamiliarTheme::classic};
};

constexpr std::array<ThemeEntry, 6> theme_entries{{
    {"theme.professional", "Professional", FamiliarTheme::windows_professional},
    {"theme.modern", "Modern flat", FamiliarTheme::modern_flat},
    {"theme.classic", "Classic", FamiliarTheme::classic},
    {"theme.xp", "XP style", FamiliarTheme::xp_style},
    {"theme.seven", "Windows 7 style", FamiliarTheme::windows7_style},
    {"theme.mac", "macOS style", FamiliarTheme::macos_style},
}};

[[nodiscard]] const ThemeEntry* find_theme(std::string_view id) {
    for (const ThemeEntry& entry : theme_entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

// -- chat ---------------------------------------------------------------------
struct CannedReply final {
    std::string_view keyword{};
    std::string_view reply{};
};

constexpr std::array<CannedReply, 4> canned_replies{{
    {"grid", "A DataGridView with virtual rows keeps only the visible cells; the data workbench demonstrator "
             "shows fifty thousand orders with a find panel and a column chooser."},
    {"theme", "The theme gallery beside this panel previews a familiar theme on the sample group while you hover, "
              "and keeps it when you click."},
    {"dock", "Each workspace above is a named layout of one DockWorkspace; switching applies the saved tree."},
    {"chat", "For thousands of streamed messages, open the chat stress demonstrator in the demonstrators "
             "repository; this panel only shows the composition."},
}};

constexpr std::string_view fallback_reply =
    "This responder is canned and offline: ask about the grid, themes, docking or chat, and it streams its "
    "answer word by word with a Timer.";

// The transcript keeps at most this many bytes, dropping the oldest lines,
// well inside a multiline TextBox's limits.
constexpr std::size_t transcript_limit = 16U * 1024U;

class WorkspacesAndThemesPage final : public gui_forms::Component {
public:
    explicit WorkspacesAndThemesPage(const std::shared_ptr<Surface>& body) : body_(body) {}

    void build() {
        Surface& body = *body_;
        add_page_heading(body, "workspaces", "WORKSPACES, THEMES AND CHAT",
                         "Named docking workspaces, a live-preview theme gallery and a streaming chat panel, "
                         "composed from DockWorkspace, RibbonGallery and text controls.");
        build_workspaces(body);
        build_themes(body);
        build_chat(body);
        status_ = label("gallery.workspaces.status", "Switch a workspace, hover a theme or ask the chat panel.", 11.0,
                        400, "muted");
        body.add_at(status_, {24.0, 640.0, 940.0, 22.0});
    }

    void connect() {
        on((*workspace_choice_).changed(), *this, &WorkspacesAndThemesPage::on_workspace_chosen);
        on((*workspace_save_).clicked(), *this, &WorkspacesAndThemesPage::on_workspace_save);
        on((*theme_command_).preview_requested(), *this, &WorkspacesAndThemesPage::on_theme_preview);
        on((*theme_command_).preview_cancelled(), *this, &WorkspacesAndThemesPage::on_theme_cancel);
        on((*theme_command_).value_changed(), *this, &WorkspacesAndThemesPage::on_theme_value);
        on((*chat_send_).clicked(), *this, &WorkspacesAndThemesPage::on_chat_send);
        on((*chat_prompt_).committed(), *this, &WorkspacesAndThemesPage::on_chat_send);
    }

private:
    // -- workspaces -----------------------------------------------------------
    void build_workspaces(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.workspaces.dock", "Workspaces", {600.0, 300.0});
        body.add_at(box, {24.0, 92.0, 600.0, 300.0});
        workspace_choice_ = make_control<ChoiceGroup>(StableId("gallery.workspaces.dock.choice"));
        (*workspace_choice_).accessible_name = "Workspace";
        (*workspace_choice_).set_items({ChoiceItem{0, "Coding"}, ChoiceItem{1, "Debugging"}, ChoiceItem{2, "Writing"},
                                        ChoiceItem{3, "Custom"}});
        (*box).add_at(workspace_choice_, {12.0, 28.0, 400.0, 26.0});
        workspace_save_ = make_control<Button>(StableId("gallery.workspaces.dock.save"), "Save as Custom");
        (*box).add_at(workspace_save_, {448.0, 28.0, 140.0, 26.0});

        workspace_ = std::make_unique<DockWorkspace>("gallery.workspaces.dock.workspace");
        const Delegate<DockPanelCreation&> factory =
            Delegate<DockPanelCreation&>::bind<WorkspacesAndThemesPage, &WorkspacesAndThemesPage::make_panel>(*this);
        for (const WorkspacePanel& tool : workspace_tools) {
            DockPanelRegistration registration{};
            registration.descriptor.id = DockPanelId(std::string(tool.id));
            registration.descriptor.default_placement.side = tool.side;
            registration.descriptor.default_placement.ratio = 0.28;
            registration.descriptor.minimum_size = {100.0, 60.0};
            registration.title = std::string(tool.title);
            registration.factory = factory;
            static_cast<void>((*workspace_).register_panel(registration));
        }
        DockPanelRegistration document{};
        document.descriptor.id = DockPanelId("editor");
        document.descriptor.kind = DockPanelKind::document;
        document.descriptor.minimum_size = {160.0, 80.0};
        document.title = "Editor";
        document.factory = factory;
        static_cast<void>((*workspace_).register_panel(document));
        static_cast<void>((*workspace_).show_panel(DockPanelId("editor")));
        // Each preset is the editor plus its tools, saved by name.
        for (std::size_t preset = 0; preset < workspace_names.size(); ++preset) {
            for (std::size_t tool = 0; tool < workspace_tools.size(); ++tool) {
                const DockPanelId id(std::string(workspace_tools[tool].id));
                if (workspace_tools_shown[preset][tool]) {
                    static_cast<void>((*workspace_).show_panel(id));
                } else {
                    static_cast<void>((*workspace_).hide_panel(id));
                }
            }
            static_cast<void>((*workspace_).save_named_layout(std::string(workspace_names[preset])));
        }
        static_cast<void>((*workspace_).save_named_layout("Custom"));
        site_ = make_control<DockSite>(StableId("gallery.workspaces.dock.site"), *workspace_);
        (*site_).accessible_name = "Workspace";
        (*box).add_at(site_, {12.0, 62.0, 576.0, 226.0});
        (*workspace_choice_).selected_index = 0;
        apply_workspace(0);
    }

    // The factory: a panel's content, made the first time it shows.
    void make_panel(DockPanelCreation& creation) {
        const std::string title = (*workspace_).panel_title(creation.panel);
        std::shared_ptr<Panel> content =
            make_control<Panel>(StableId("gallery.workspaces.dock.content." + creation.panel.value));
        std::shared_ptr<Label> caption =
            label("gallery.workspaces.dock.caption." + creation.panel.value, title, 11.0, 400, "ink");
        (*caption).set_requested_bounds({8.0, 8.0, 160.0, 20.0});
        (*content).add_child(caption);
        creation.content = content;
    }

    void apply_workspace(int index) {
        if (index < 0 || index > 3) return;
        const std::string name = index == 3 ? std::string("Custom") : std::string(workspace_names[static_cast<std::size_t>(index)]);
        const DockRestoreResult result = (*workspace_).apply_named_layout(name);
        const bool restored = result.status == DockRestoreStatus::restored;
        report(restored ? "Workspace " + name + " applied" : "Workspace " + name + " kept the current layout");
    }

    void on_workspace_chosen(int index) { apply_workspace(index); }

    void on_workspace_save() {
        static_cast<void>((*workspace_).save_named_layout("Custom"));
        (*workspace_choice_).selected_index = 3;
        report("The current arrangement is saved as Custom");
    }

    // -- theme gallery --------------------------------------------------------
    void build_themes(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.workspaces.themes", "Theme gallery", {336.0, 300.0});
        body.add_at(box, {640.0, 92.0, 336.0, 300.0});
        theme_command_ = std::make_shared<GalleryCommand>(
            std::make_shared<Command>("gallery.workspaces.themes.command", "Theme"));
        theme_gallery_ = make_control<RibbonGallery>(StableId("gallery.workspaces.themes.gallery"));
        (*theme_gallery_).accessible_name = "Themes";
        (*theme_gallery_).item_size = Size{100.0, 26.0};
        (*theme_gallery_).show_labels = true;
        (*theme_gallery_).visible_rows = 2U;
        (*theme_gallery_).set_column_range(2U, 3U);
        (*theme_gallery_).columns = 3U;
        std::vector<RibbonGalleryCategory> categories{{"familiar", "Familiar themes"}};
        std::vector<RibbonGalleryItem> items{};
        for (const ThemeEntry& entry : theme_entries) {
            items.push_back({std::string(entry.id), std::string(entry.name), {}, "familiar"});
        }
        (*theme_gallery_).set_items(std::move(categories), std::move(items));
        (*theme_gallery_).command = theme_command_;
        (*box).add_at(theme_gallery_, {12.0, 28.0, 312.0, 64.0});

        theme_sample_ = group("gallery.workspaces.themes.sample", "Preview", {312.0, 160.0});
        (*box).add_at(theme_sample_, {12.0, 100.0, 312.0, 160.0});
        (*theme_sample_).add_at(make_control<Button>(StableId("gallery.workspaces.themes.sample.button"), "Button"),
                                {12.0, 30.0, 100.0, 28.0});
        (*theme_sample_).add_at(make_control<CheckBox>(StableId("gallery.workspaces.themes.sample.check"), "Check box"),
                                {124.0, 32.0, 176.0, 24.0});
        std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("gallery.workspaces.themes.sample.field"));
        (*field).accessible_name = "Sample field";
        (*field).text = "Text box";
        (*theme_sample_).add_at(field, {12.0, 70.0, 288.0, 26.0});
        std::shared_ptr<TrackBar> track = make_control<TrackBar>(StableId("gallery.workspaces.themes.sample.track"));
        (*track).accessible_name = "Sample slider";
        (*track).set_range(0.0, 100.0);
        (*track).value = 40.0;
        (*theme_sample_).add_at(track, {12.0, 108.0, 288.0, 28.0});
        theme_label_ = label("gallery.workspaces.themes.value", "Kept: the Gallery's own theme", 10.0, 400, "muted");
        (*box).add_at(theme_label_, {12.0, 266.0, 312.0, 22.0});
    }

    void on_theme_preview(const GalleryPreview& preview) {
        const ThemeEntry* const entry = find_theme(preview.item_id);
        if (entry == nullptr) return;
        (*theme_sample_).set_theme_override(familiar_theme((*entry).theme));
        report("Previewing " + std::string((*entry).name));
    }

    // The preview ends: the sample goes back to the kept theme.
    void on_theme_cancel(const GalleryPreview& preview) {
        static_cast<void>(preview);
        show_kept_theme();
    }

    void on_theme_value(const std::optional<std::string>& value) {
        kept_theme_ = value.value_or(std::string());
        show_kept_theme();
        const ThemeEntry* const entry = find_theme(kept_theme_);
        report(entry != nullptr ? "Kept " + std::string((*entry).name) : std::string("Theme cleared"));
    }

    void show_kept_theme() {
        const ThemeEntry* const entry = find_theme(kept_theme_);
        if (entry == nullptr) {
            (*theme_sample_).clear_theme_override();
            (*theme_label_).text = "Kept: the Gallery's own theme";
            return;
        }
        (*theme_sample_).set_theme_override(familiar_theme((*entry).theme));
        (*theme_label_).text = "Kept: " + std::string((*entry).name);
    }

    // -- chat -----------------------------------------------------------------
    void build_chat(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.workspaces.chat", "AI chat panel", {952.0, 232.0});
        body.add_at(box, {24.0, 404.0, 952.0, 232.0});
        chat_transcript_ = make_control<TextBox>(StableId("gallery.workspaces.chat.transcript"));
        (*chat_transcript_).accessible_name = "Conversation";
        (*chat_transcript_).multiline = true;
        (*chat_transcript_).word_wrap = true;
        (*chat_transcript_).read_only = true;
        (*chat_transcript_).text = "Assistant: Hello. Ask me about the grid, themes, docking or chat.";
        (*box).add_at(chat_transcript_, {12.0, 28.0, 620.0, 158.0});
        chat_prompt_ = make_control<TextBox>(StableId("gallery.workspaces.chat.prompt"));
        (*chat_prompt_).accessible_name = "Message";
        (*chat_prompt_).placeholder_text = "Type a message and press Enter";
        (*box).add_at(chat_prompt_, {12.0, 194.0, 520.0, 26.0});
        chat_send_ = make_control<Button>(StableId("gallery.workspaces.chat.send"), "Send");
        (*box).add_at(chat_send_, {540.0, 194.0, 92.0, 26.0});
        (*box).add_at(wrapped_label("gallery.workspaces.chat.pointer",
                            "This is the composition only: a transcript, a prompt and a reply streamed by a Timer "
                            "from a canned, offline responder. The chat stress demonstrator (repository "
                            "demonstrators, program chat_stress) runs thousands of streamed messages through a "
                            "virtualized transcript; open it for scale.",
                            10.0, 400, "muted"),
                      {648.0, 28.0, 292.0, 150.0});
    }

    void on_chat_send() {
        // A transcript line stays far below a multiline box's 4096-byte line
        // limit; a long message is cut, and a cut code point dropped whole.
        std::string message = (*chat_prompt_).text.read();
        if (message.empty() || streaming_) return;
        if (message.size() > 1000U) {
            message.resize(1000U);
            while (!message.empty() && static_cast<unsigned char>(message.back()) >= 0x80U) message.pop_back();
        }
        (*chat_prompt_).text = std::string();
        append_line("You: " + message);
        const std::string lowered = ascii_lower(message);
        std::string_view reply = fallback_reply;
        for (const CannedReply& canned : canned_replies) {
            if (lowered.find(canned.keyword) != std::string::npos) {
                reply = canned.reply;
                break;
            }
        }
        pending_reply_ = std::string(reply);
        reply_shown_ = 0U;
        append_line("Assistant: ");
        Window* const window = (*chat_send_).attached_window();
        if (window == nullptr || reduced_motion_for(*chat_send_)) {
            finish_reply();
            return;
        }
        if (!chat_timer_) {
            chat_timer_ = std::make_unique<Timer>(*window, std::chrono::milliseconds(45));
            on((*chat_timer_).tick(), *this, &WorkspacesAndThemesPage::on_chat_tick);
        }
        streaming_ = true;
        (*chat_send_).enabled = false;
        (*chat_timer_).start();
    }

    // One more word of the reply.
    void on_chat_tick() {
        std::size_t next = pending_reply_.find(' ', reply_shown_ + 1U);
        if (next == std::string::npos) next = pending_reply_.size();
        const std::string word = pending_reply_.substr(reply_shown_, next - reply_shown_);
        reply_shown_ = next;
        (*chat_transcript_).text = (*chat_transcript_).text.read() + word;
        if (reply_shown_ >= pending_reply_.size()) {
            (*chat_timer_).stop();
            streaming_ = false;
            (*chat_send_).enabled = true;
            report("Reply streamed");
        }
    }

    void finish_reply() {
        (*chat_transcript_).text = (*chat_transcript_).text.read() + pending_reply_.substr(reply_shown_);
        reply_shown_ = pending_reply_.size();
    }

    void append_line(const std::string& line) {
        std::string text = (*chat_transcript_).text.read();
        text += "\n";
        text += line;
        while (text.size() > transcript_limit) {
            const std::size_t cut = text.find('\n');
            if (cut == std::string::npos) break;
            text.erase(0, cut + 1U);
        }
        (*chat_transcript_).text = std::move(text);
    }

    void report(std::string text) { (*status_).text = std::move(text); }

    std::shared_ptr<Surface> body_;
    std::shared_ptr<Label> status_;

    std::shared_ptr<ChoiceGroup> workspace_choice_;
    std::shared_ptr<Button> workspace_save_;
    std::unique_ptr<DockWorkspace> workspace_;
    std::shared_ptr<DockSite> site_;

    std::shared_ptr<GalleryCommand> theme_command_;
    std::shared_ptr<RibbonGallery> theme_gallery_;
    std::shared_ptr<LayoutGroup> theme_sample_;
    std::shared_ptr<Label> theme_label_;
    std::string kept_theme_{};

    std::shared_ptr<TextBox> chat_transcript_;
    std::shared_ptr<TextBox> chat_prompt_;
    std::shared_ptr<Button> chat_send_;
    std::unique_ptr<Timer> chat_timer_;
    std::string pending_reply_{};
    std::size_t reply_shown_{};
    bool streaming_{};
};

} // namespace

void add_workspaces_and_themes(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    const std::shared_ptr<WorkspacesAndThemesPage> owner = std::make_shared<WorkspacesAndThemesPage>(page);
    (*owner).build();
    (*context).components.add(owner);
    (*owner).connect();
}

} // namespace gui_forms::gallery::pages
