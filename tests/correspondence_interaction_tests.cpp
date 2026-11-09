// Headless interaction tests for the Correspondence demonstration, modelled on
// tests/gallery_interaction_tests.cpp: build the window, lay it out, dispatch
// pointer and text input by stable ID, and assert that the reading pane,
// counts and model change together.

#include "../demo/correspondence/correspondence.hpp"
#include "../demo/correspondence/correspondence_controls.hpp"
#include "../demo/correspondence/correspondence_model.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace correspondence = gui_forms::correspondence;
namespace ids = gui_forms::correspondence::ids;
using correspondence::CorrespondenceShell;
using correspondence::Folder;
using correspondence::Message;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "correspondence interaction failure: " << message << '\n';
        std::exit(1);
    }
}

class CountingPainter final : public gui_forms::Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(gui_forms::Point) override {}
    void clip_rect(gui_forms::Rect) override { ++clips; }
    void fill_rect(gui_forms::Rect, gui_forms::Color) override { ++fills; }
    void stroke_rect(gui_forms::Rect, gui_forms::Color, double) override { ++strokes; }
    void draw_line(gui_forms::Point, gui_forms::Point, gui_forms::Color, double) override {
        ++lines;
    }
    void draw_text_utf8(gui_forms::Point,
                        std::string_view text,
                        gui_forms::FontSpec font,
                        gui_forms::Color) override {
        ++texts;
        text_roles.emplace_back(text, font.role);
    }
    void draw_image(gui_forms::ImageId, gui_forms::Rect, double) override { ++images; }

    std::uint64_t saves{};
    std::uint64_t restores{};
    std::uint64_t clips{};
    std::uint64_t fills{};
    std::uint64_t strokes{};
    std::uint64_t lines{};
    std::uint64_t texts{};
    std::uint64_t images{};
    std::vector<std::pair<std::string, gui_forms::FontRole>> text_roles;

    [[nodiscard]] bool observed_role(std::string_view text,
                                     gui_forms::FontRole role) const {
        for (const std::pair<std::string, gui_forms::FontRole>& observed : text_roles) {
            if (observed.first == text && observed.second == role) {
                return true;
            }
        }
        return false;
    }
};

[[nodiscard]] bool contains(gui_forms::Rect parent, gui_forms::Rect child)
{
    constexpr double tolerance = 0.001;
    return child.x + tolerance >= parent.x && child.y + tolerance >= parent.y &&
           child.x + child.width <= parent.x + parent.width + tolerance &&
           child.y + child.height <= parent.y + parent.height + tolerance;
}

[[nodiscard]] gui_forms::Point center_of(const gui_forms::Control::Ptr& control)
{
    const gui_forms::Rect bounds = (*control).absolute_bounds();
    return {bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
}

// Primary press and release at a window point; the hit control must carry
// the expected stable ID so tests never pass by hitting a neighbour.
void click_at(gui_forms::Window& window, gui_forms::Point point, std::string_view expected_id,
              gui_forms::PointerButton button = gui_forms::PointerButton::primary)
{
    const gui_forms::Control::Ptr hit = window.hit_test(point);
    if (hit == nullptr || (*hit).stable_id().value() != expected_id) {
        std::cerr << "expected hit " << expected_id << ", got "
                  << (hit == nullptr ? std::string_view("<none>") : (*hit).stable_id().value())
                  << '\n';
    }
    expect(hit != nullptr && (*hit).stable_id().value() == expected_id,
           "click point must hit the retained target");
    expect(window.dispatch_pointer({gui_forms::PointerAction::down, button, point}),
           "pointer down must be handled");
    expect(window.dispatch_pointer({gui_forms::PointerAction::up, button, point}),
           "pointer up must be handled");
    // A secondary click is followed by the host's context request, as
    // Windows' WM_CONTEXTMENU follows the release.
    if (button == gui_forms::PointerButton::secondary) {
        expect(window.dispatch_context_request(gui_forms::ContextRequestOrigin::pointer, point),
               "the pointer context request must be handled");
    }
}

void click(gui_forms::Window& window, std::string_view stable_id)
{
    const gui_forms::Control::Ptr control = window.find(stable_id);
    expect(control != nullptr, "click target must resolve by stable ID");
    click_at(window, center_of(control), stable_id);
}

// Folder rows are virtual: the TreeView is one control, so the row centre is
// derived from its item height (text scale 1 in these tests).
[[nodiscard]] gui_forms::Point folder_row_point(const gui_forms::TreeView& folders,
                                                std::size_t row)
{
    const gui_forms::Rect bounds = folders.absolute_bounds();
    return {bounds.x + 40.0,
            bounds.y + 2.0 + (static_cast<double>(row) + 0.5) * folders.item_height.read()};
}

[[nodiscard]] gui_forms::Point message_point(const gui_forms::CorrespondenceView& messages,
                                             std::uint32_t id)
{
    const std::optional<gui_forms::Rect> local =
        messages.item_bounds(correspondence::message_id(id));
    expect(local.has_value(), "message row must be realized in the list");
    return messages.point_to_window({(*local).x + 120.0, (*local).y + (*local).height * 0.5});
}

void check_containment(gui_forms::Window& window, const gui_forms::Control::Ptr& control)
{
    if (!(*control).effectively_visible()) return;
    const gui_forms::Control::Ptr parent = (*control).parent();
    if (parent != nullptr && !contains((*parent).absolute_bounds(), (*control).absolute_bounds())) {
        const gui_forms::Rect parent_bounds = (*parent).absolute_bounds();
        const gui_forms::Rect child_bounds = (*control).absolute_bounds();
        std::cerr << "containment parent=" << (*parent).stable_id().value()
                  << " child=" << (*control).stable_id().value()
                  << " parent_bounds=" << parent_bounds.x << ',' << parent_bounds.y
                  << ',' << parent_bounds.width << ',' << parent_bounds.height
                  << " child_bounds=" << child_bounds.x << ',' << child_bounds.y
                  << ',' << child_bounds.width << ',' << child_bounds.height << '\n';
        expect(false, "every visible child must remain inside its parent edge");
    }
    for (const gui_forms::Control::Ptr& child : (*control).children()) {
        check_containment(window, child);
    }
}

[[nodiscard]] std::string_view first_line(std::string_view text)
{
    const std::size_t newline = text.find('\n');
    return newline == std::string_view::npos ? text : text.substr(0, newline);
}

}  // namespace

int main()
{
    std::unique_ptr<gui_forms::Window> window = correspondence::make_correspondence();
    gui_forms::Window& view = *window;

    const std::shared_ptr<CorrespondenceShell> shell =
        std::dynamic_pointer_cast<CorrespondenceShell>(view.find(ids::shell));
    expect(shell != nullptr, "shell resolves by stable ID");
    correspondence::CorrespondenceModel& model = (*shell).model();

    const std::string_view required[] = {
        ids::menu, ids::commands, ids::command_bar, ids::search, ids::split, ids::folders,
        ids::content_split, ids::messages, ids::reading, ids::reading_subject,
        ids::reading_correspondent, ids::reading_date, ids::reading_body, ids::status};
    for (const std::string_view id : required) {
        expect(view.find(id) != nullptr, "every named region resolves by stable ID");
    }
    for (int command = correspondence::command_new_message;
         command <= correspondence::command_flag; ++command) {
        expect(view.find(correspondence::command_button_id(command)) != nullptr,
               "every command-bar button resolves by stable ID");
    }

    const std::shared_ptr<gui_forms::TreeView> folders = (*shell).folders();
    const std::shared_ptr<gui_forms::CorrespondenceView> messages = (*shell).messages();
    const std::shared_ptr<gui_forms::Label> subject = (*shell).reading_subject();
    const std::shared_ptr<gui_forms::Label> correspondent = (*shell).reading_correspondent();
    const std::shared_ptr<gui_forms::Label> date = (*shell).reading_date();
    const std::shared_ptr<gui_forms::TextBox> body = (*shell).reading_body();
    const std::shared_ptr<gui_forms::Label> status = (*shell).status();

    // Initial projection.
    expect(model.selected_folder() == Folder::inbox, "window starts in Inbox");
    expect((*folders).selected_id.read() == correspondence::folder_id(Folder::inbox),
           "folder tree reflects the model folder");
    expect((*folders).items().size() == 6U, "six folders are listed");
    expect((*folders).items()[0].text == "Inbox  (6)", "Inbox shows its unread count");
    expect((*folders).items()[4].text == "Junk  (3)", "Junk shows its unread count");
    expect((*folders).items()[2].text == "Sent", "folders without unread mail show no count");
    expect((*messages).items().size() == 16U, "Inbox lists sixteen messages");
    expect((*messages).selected_id.read().empty(), "no message is selected at start");
    expect((*subject).text.read() == "No message selected", "reading pane starts empty");
    expect((*status).text.read().rfind("Inbox · 16 messages · 6 unread", 0) == 0,
           "status line reports folder, count and unread");
    for (const std::shared_ptr<gui_forms::Command>& command : (*shell).commands()) {
        if ((*command).stable_id() == "correspondence.command.reply" ||
            (*command).stable_id() == "correspondence.command.archive" ||
            (*command).stable_id() == "correspondence.command.delete") {
            expect(!(*command).state().enabled, "selection commands start disabled");
        }
    }
    check_containment(view, view.root());

    // Selecting a message fills the reading pane, marks it read, and updates counts.
    const Message* first_message = model.find(1U);
    expect(first_message != nullptr && !(*first_message).read, "message 1 begins unread");
    click_at(view, message_point(*messages, 1U), ids::messages);
    expect(model.selected_message() == std::optional<std::uint32_t>{1U},
           "clicking a row selects it in the model");
    expect((*messages).selected_id.read() == correspondence::message_id(1U),
           "list selection follows the model");
    expect((*first_message).read, "selecting marks the message read");
    expect((*subject).text.read() == "Dome shutter motor: replacement quote",
           "reading pane shows the subject");
    expect((*correspondent).text.read() == "From: Priya Raman <priya.raman@harbourlane.example>",
           "reading pane shows the sender");
    expect((*date).text.read() == "2026-10-10 17:42 · Inbox · Flagged",
           "reading pane shows date, folder and flag");
    expect((*body).text.read() == (*first_message).body, "reading pane shows the full body");
    expect((*folders).items()[0].text == "Inbox  (5)", "folder count follows the read mark");
    expect((*status).text.read().rfind("Inbox · 16 messages · 5 unread", 0) == 0,
           "status follows the read mark");

    CountingPainter painter;
    view.paint(painter, {0.0, 0.0, 1120.0, 720.0});
    expect(painter.saves == painter.restores, "painter state is balanced");
    expect(painter.fills > 10 && painter.texts > 10, "the three panes paint surfaces and text");
    expect(painter.observed_role("Archive", gui_forms::FontRole::control) &&
               painter.observed_role("Message", gui_forms::FontRole::control),
           "command buttons and menu titles use the control typography role");
    expect(painter.observed_role("Dome shutter motor: replacement quote",
                                 gui_forms::FontRole::content),
           "message text uses the content typography role");

    // Flag toggles through the command bar.
    click(view, correspondence::command_button_id(correspondence::command_flag));
    expect(!(*first_message).flagged, "Flag button toggles the flag off");
    expect((*date).text.read() == "2026-10-10 17:42 · Inbox", "reading pane drops the flag note");
    click(view, correspondence::command_button_id(correspondence::command_flag));
    expect((*first_message).flagged, "Flag button toggles the flag back on");

    // Archive moves the message out of the list and selects its neighbour.
    click(view, correspondence::command_button_id(correspondence::command_archive));
    expect((*first_message).folder == Folder::archive, "Archive button moves the message");
    expect((*messages).items().size() == 15U, "archived message leaves the list");
    expect(model.selected_message() == std::optional<std::uint32_t>{2U} &&
               (*messages).selected_id.read() == correspondence::message_id(2U),
           "the next message becomes selected");
    expect((*subject).text.read() == "Observing conditions, week 41",
           "reading pane follows the new selection");
    expect((*status).text.read().rfind("Inbox · 15 messages", 0) == 0, "status follows the archive");
    expect((*folders).items()[3].text == "Archive", "Archive stays read-only in the tree");

    // Delete moves to Trash.
    click(view, correspondence::command_button_id(correspondence::command_delete));
    expect((*model.find(2U)).folder == Folder::trash, "Delete button moves to Trash");
    expect((*messages).items().size() == 14U, "deleted message leaves the list");
    expect(model.selected_message() == std::optional<std::uint32_t>{3U},
           "the next message becomes selected after delete");

    // Folder rows switch the list and reading pane.
    click_at(view, folder_row_point(*folders, 5U), ids::folders);
    expect(model.selected_folder() == Folder::trash, "clicking the Trash row selects Trash");
    expect((*messages).items().size() == 3U, "Trash lists the fixture pair plus the deletion");
    expect((*subject).text.read() == "No message selected", "folder change clears the reading pane");
    expect((*status).text.read().rfind("Trash · 3 messages", 0) == 0, "status names the folder");
    click_at(view, folder_row_point(*folders, 0U), ids::folders);
    expect(model.selected_folder() == Folder::inbox && (*messages).items().size() == 14U,
           "clicking the Inbox row returns to the Inbox");

    // Search filters the list through the text box.
    click(view, ids::search);
    expect(view.focused_control() == (*shell).search_box(), "clicking the search box focuses it");
    expect(view.dispatch_text({"photometry", false, -1, 0, false}),
           "focused search box accepts committed text");
    expect(model.search() == "photometry", "search text reaches the model");
    expect((*messages).items().size() == 2U &&
               (*messages).items().size() == model.visible_messages().size(),
           "the list shows only the matches");
    expect((*status).text.read().find("2 matches for \"photometry\"") != std::string::npos,
           "status reports the match count");
    (*(*shell).search_box()).text = "";
    expect(model.search().empty() && (*messages).items().size() == 14U,
           "clearing the search restores the list");

    // New message creates a draft and shows it; no secondary window is opened.
    click(view, correspondence::command_button_id(correspondence::command_new_message));
    expect(model.selected_folder() == Folder::drafts, "New message switches to Drafts");
    expect((*folders).selected_id.read() == correspondence::folder_id(Folder::drafts),
           "folder tree follows the compose switch");
    expect((*messages).items().size() == 4U, "Drafts gained the new draft");
    expect((*subject).text.read() == "Untitled message", "reading pane shows the new draft");
    expect((*correspondent).text.read() == "To: (no recipient)", "a new draft has no recipient yet");

    // Reply quotes the selected message into a new draft.
    click_at(view, folder_row_point(*folders, 0U), ids::folders);
    click_at(view, message_point(*messages, 3U), ids::messages);
    click(view, correspondence::command_button_id(correspondence::command_reply));
    expect(model.selected_folder() == Folder::drafts, "Reply switches to Drafts");
    expect((*subject).text.read() == "Re: Rota for the October public nights",
           "reply keeps an existing Re: prefix");
    expect((*correspondent).text.read() == "To: Tomasz Wierzba <t.wierzba@harbourlane.example>",
           "reply addresses the original sender");
    expect((*body).text.read().find("Tomasz Wierzba wrote:") != std::string::npos,
           "reply body quotes the original");

    // Keyboard accelerator reaches the shared command (Ctrl+E archives).
    const std::optional<std::uint32_t> reply_id = model.selected_message();
    expect(reply_id.has_value(), "the reply draft is selected");
    click_at(view, message_point(*messages, *reply_id), ids::messages);
    expect(view.dispatch_key({gui_forms::KeyAction::down, gui_forms::PhysicalKey::e,
                              gui_forms::Modifier::control}),
           "Ctrl+E is handled as a window accelerator");
    expect((*model.find(*reply_id)).folder == Folder::archive,
           "the accelerator archives the selected draft");

    // Secondary click opens the owned context menu on the clicked row.
    click_at(view, folder_row_point(*folders, 0U), ids::folders);
    click_at(view, message_point(*messages, 4U), ids::messages, gui_forms::PointerButton::secondary);
    expect(model.selected_message() == std::optional<std::uint32_t>{4U},
           "secondary click selects the row first");
    expect((*(*shell).context_menu()).is_open(), "secondary click opens the context menu");
    (*(*shell).context_menu()).close();
    expect(!(*(*shell).context_menu()).is_open(), "context menu closes on request");

    // About is surfaced through the status line when no host dialog exists.
    for (const std::shared_ptr<gui_forms::Command>& command : (*shell).commands()) {
        if ((*command).stable_id() == "correspondence.command.about") {
            expect((*command).execute("test"), "About command executes");
        }
    }
    expect((*status).text.read().find("retained controls") != std::string::npos &&
               (*status).text.read().find("model revision") != std::string::npos,
           "About reports instrumentation in the status line headlessly");

    // The layout survives the smallest supported window.
    view.resize({600.0, 420.0});
    view.perform_layout();
    check_containment(view, view.root());
    expect((*messages).absolute_bounds().width > 100.0 &&
               (*body).absolute_bounds().height > 40.0,
           "list and reading pane keep usable extents at 600x420");
    view.resize({1120.0, 720.0});
    view.perform_layout();
    check_containment(view, view.root());

    CountingPainter final_painter;
    view.paint(final_painter, {0.0, 0.0, 1120.0, 720.0});
    expect(final_painter.saves == final_painter.restores, "final paint is balanced");
    expect(first_line((*body).text.read()).size() < 2000U, "body text stays bounded");

    std::cout << "correspondence_interaction_tests: ok\n";
    return 0;
}
