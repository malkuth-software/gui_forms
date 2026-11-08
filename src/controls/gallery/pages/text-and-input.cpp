#include "pages.hpp"

#include "text-and-input_form.hpp"

#include <array>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// CommandBar item ids. The bar names its retained buttons
// "gallery.text.command.item.<id>"; the numbering is the retired page's.
constexpr int command_select_all = 0;
constexpr int command_replace = 1;
constexpr int command_undo = 2;
constexpr int command_redo = 3;
constexpr int command_copy = 4;
constexpr int command_cut = 5;
constexpr int command_paste = 6;

// Owns the text-and-input page: the controls demo/pages/text-and-input.form
// describes, and their behaviour.
class TextAndInputPage final : public Component {
public:
    TextAndInputPage(const std::shared_ptr<Surface>& body,
                     const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        build_fields();
        build_commands();
        build_masked();
        build_auto_complete();
        build_tokens();
        build_rich_text();
        refresh_state();
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_text_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        const std::array<std::shared_ptr<ScaledGroupBox>, 6> groups{
            document_.gallery_text_editing, document_.gallery_text_commands,
            document_.gallery_text_masked, document_.gallery_text_complete,
            document_.gallery_text_tokens, document_.gallery_text_rich};
        for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
            (*group).set_style(page_style());
        }
        primary_ = document_.gallery_text_primary;
        state_ = document_.gallery_text_state;
        bar_ = document_.gallery_text_command;
        clipboard_status_ = document_.gallery_text_clipboard_status;
        phone_ = document_.gallery_text_masked_phone;
        masked_status_ = document_.gallery_text_masked_status;
        city_ = document_.gallery_text_complete_city;
        band_ = document_.gallery_text_complete_band;
        complete_status_ = document_.gallery_text_complete_status;
        recipients_ = document_.gallery_text_tokens_recipients;
        tokens_status_ = document_.gallery_text_tokens_status;
        note_ = document_.gallery_text_rich_note;
        rich_status_ = document_.gallery_text_rich_status;
    }

    void build_fields() {
        (*document_.gallery_text_password).use_system_password_character = true;
        // Both are state events; the handler reads the field, so it takes no
        // event argument and serves either publisher.
        on((*primary_).text_changed(), *this, &TextAndInputPage::refresh_state);
        on((*primary_).selection_changed(), *this, &TextAndInputPage::refresh_state);
    }

    void build_commands() {
        // Undo and Redo are Commands so their availability (the field's
        // history) projects onto the bar's retained buttons; the other five
        // items dispatch through the bar's single group notification.
        (*bar_).set_style(page_style());
        (*bar_).set_items({
            {.id = command_select_all, .label = "Select all"},
            {.id = command_copy, .label = "Copy"},
            {.id = command_cut, .label = "Cut"},
            {.id = command_paste, .label = "Paste"},
            {.id = command_replace, .label = "Replace"},
            {.id = command_undo, .label = "Undo", .command = &undo_},
            {.id = command_redo, .label = "Redo", .command = &redo_},
        });
        for (int index = 0; index < static_cast<int>((*bar_).items().size()); ++index) {
            (*bar_).item(index).set_style(page_style());
        }
        (*bar_).item(4).set_visual_style(ButtonVisualStyle::accent);
        on((*bar_).invoked(), *this, &TextAndInputPage::on_command);
        on(undo_.invoked(), *this, &TextAndInputPage::on_undo);
        on(redo_.invoked(), *this, &TextAndInputPage::on_redo);
    }

    // MaskedTextBox specimens: a telephone mask, and a part code whose
    // letters are shifted to upper and then lower case. The telephone
    // field's refusals and completion report to the status label.
    void build_masked() {
        (*phone_).mask = "(000) 000-0000";
        (*document_.gallery_text_masked_code).mask = ">LL-0000-<aa";
        on((*phone_).mask_input_rejected(), *this, &TextAndInputPage::masked_rejected);
        on((*phone_).text_changed(), *this, &TextAndInputPage::masked_changed);
    }

    // Auto-complete specimens: a TextBox that suggests and appends from its
    // own collection, and an editable ComboBox that completes from its
    // items. Accepted completions report to the status label.
    void build_auto_complete() {
        (*(*city_).auto_complete_custom_source.read()).add_range(
            {"Calgary", "Cambridge", "Canberra", "Cardiff", "Edinburgh", "Edmonton", "London", "Los Angeles"});
        (*city_).auto_complete_source = AutoCompleteSource::custom_source;
        (*city_).auto_complete_mode = AutoCompleteMode::suggest_append;
        (*band_).items = {"Long wave", "Medium wave", "Short wave", "VHF FM", "UHF", "Microwave"};
        (*band_).set_drop_down_style(ComboBoxStyle::drop_down);
        (*band_).set_auto_complete_source(AutoCompleteSource::list_items);
        (*band_).set_auto_complete_mode(AutoCompleteMode::suggest);
        on((*city_).auto_complete_accepted(), *this, &TextAndInputPage::completion_accepted);
        on((*(*band_).editor()).auto_complete_accepted(), *this, &TextAndInputPage::completion_accepted);
    }

    // TokenBox specimen: recipients entered by Enter, a comma or a
    // suggestion, each a removable token.
    void build_tokens() {
        (*(*recipients_).auto_complete_custom_source.read()).add_range(
            {"Ada Lovelace", "Alan Turing", "Grace Hopper", "Edsger Dijkstra", "Barbara Liskov"});
        (*recipients_).auto_complete_source = AutoCompleteSource::custom_source;
        (*recipients_).auto_complete_mode = AutoCompleteMode::suggest;
        static_cast<void>((*recipients_).add_token("Grace Hopper"));
        on((*recipients_).token_added(), *this, &TextAndInputPage::token_added);
        on((*recipients_).token_removed(), *this, &TextAndInputPage::token_removed);
    }

    // A RichTextBox over markdown: a heading, emphasis, a link, a list and a
    // code block, shown styled with their markup dimmed. Ctrl+click on the
    // link reports its target.
    void build_rich_text() {
        (*note_).set_markdown("## Release notes\n\n"
                              "Text may be **bold**, *italic* or `code`. "
                              "See [the guide](https://example.org/guide).\n\n"
                              "- Ctrl+B and Ctrl+I format the selection\n"
                              "- Ctrl+K makes a link\n\n"
                              "```\nsave_rtf(document);\n```\n");
        on((*note_).link_clicked(), *this, &TextAndInputPage::rich_link_clicked);
    }

    void rich_link_clicked(const std::string& target) {
        (*rich_status_).set_text("Link clicked: " + target);
    }

    void token_added(const Token& token) {
        (*tokens_status_).set_text("Added \"" + token.text + "\" · recipients: " +
                                   std::to_string((*recipients_).tokens().size()));
    }

    void token_removed(const Token& token) {
        (*tokens_status_).set_text("Removed \"" + token.text + "\" · recipients: " +
                                   std::to_string((*recipients_).tokens().size()));
    }

    void completion_accepted(const std::string& text) {
        (*complete_status_).set_text("Accepted \"" + text + "\"");
    }

    void masked_rejected(const MaskInputRejection& rejection) {
        (*masked_status_).set_text("Telephone refused \"" + rejection.text + "\" at position " +
                                   std::to_string(rejection.position.value()) + " · " +
                                   std::string(mask_result_hint_name(rejection.hint)));
    }

    void masked_changed(const std::string& text) {
        (*masked_status_).set_text("Telephone " + text +
                                   ((*phone_).mask_completed() ? " · complete" : " · incomplete"));
    }

    void refresh_state() {
        const TextSelection selection = (*primary_).selection();
        (*state_).set_text("Selection: " + std::to_string(selection.start().value()) + ".." +
                           std::to_string(selection.end().value()) + " bytes · Undo: " +
                           ((*primary_).can_undo() ? "ready" : "clean") + " · Redo: " +
                           ((*primary_).can_redo() ? "ready" : "clean"));
        undo_.set_enabled((*primary_).can_undo());
        redo_.set_enabled((*primary_).can_redo());
    }

    void on_command(const CommandItem& item) {
        switch (item.id) {
        case command_select_all:
            (*primary_).select_all();
            return;
        case command_replace:
            static_cast<void>((*primary_).replace_selection("GUI.Forms"));
            return;
        case command_copy:
            publish_clipboard_status("copy", (*primary_).copy());
            return;
        case command_cut:
            publish_clipboard_status("cut", (*primary_).cut());
            return;
        case command_paste:
            publish_clipboard_status("paste", (*primary_).paste());
            return;
        default:
            return; // Undo and Redo run through their Commands.
        }
    }

    void on_undo() { static_cast<void>((*primary_).undo()); }
    void on_redo() { static_cast<void>((*primary_).redo()); }

    void publish_clipboard_status(const std::string_view command, const bool accepted) {
        (*clipboard_status_).set_text(
            std::string("Clipboard ") + std::string(command) +
            (accepted ? " · accepted" : " · unavailable or empty") + " · " +
            std::to_string((*primary_).text.read().size()) + " UTF-8 bytes retained");
    }

    std::shared_ptr<TextBox> primary_;
    std::shared_ptr<Label> state_;
    std::shared_ptr<Label> clipboard_status_;
    std::shared_ptr<CommandBar> bar_;
    std::shared_ptr<MaskedTextBox> phone_;
    std::shared_ptr<Label> masked_status_;
    std::shared_ptr<TextBox> city_;
    std::shared_ptr<ComboBox> band_;
    std::shared_ptr<Label> complete_status_;
    std::shared_ptr<TokenBox> recipients_;
    std::shared_ptr<Label> tokens_status_;
    std::shared_ptr<RichTextBox> note_;
    std::shared_ptr<Label> rich_status_;
    Command undo_{"gallery.text.command.undo", "Undo"};
    Command redo_{"gallery.text.command.redo", "Redo"};
    // Declared after the Commands its bar borrows, so it is released first.
    TextAndInputDocument<TextAndInputPage> document_{};
};

} // namespace

void add_text_input(const std::shared_ptr<Surface>& page,
                    const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<TextAndInputPage>(page, context));
}

} // namespace gui_forms::gallery::pages
