#include "pages.hpp"

#include "gui_forms/feedback/semantic_feedback/semantic_feedback.hpp"

#include "semantic-feedback_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the semantic-feedback page: the controls
// demo/pages/semantic-feedback.form describes, and their behaviour. One
// Command per SemanticFeedbackKind on the CommandBar (the completion
// Command is shared with a plain Button), the Value<bool> the sound check
// box binds, and the SemanticFeedback component whose bounded record
// history is published back into the page.
class SemanticFeedbackPage final : public gui_forms::Component {
public:
    explicit SemanticFeedbackPage(const std::shared_ptr<Surface>& body);

private:
    static constexpr std::array<SemanticFeedbackKind, 7> kinds{
        SemanticFeedbackKind::location_changed, SemanticFeedbackKind::pane_changed,
        SemanticFeedbackKind::option_committed, SemanticFeedbackKind::operation_started,
        SemanticFeedbackKind::operation_completed, SemanticFeedbackKind::operation_failed,
        SemanticFeedbackKind::conflict};
    static constexpr std::array<std::string_view, 7> labels{
        "Location", "Pane", "Option", "Started", "Completed", "Failed", "Conflict"};
    static constexpr std::size_t completed_index = 4U;
    static constexpr std::size_t retained_records = 6U;

    void mount_document(const std::shared_ptr<Surface>& body);
    void connect_commands();
    void connect_policy();
    [[nodiscard]] bool ensure_feedback();
    void on_announce(int index);
    void on_clear();
    void on_sound_changed(bool enabled);
    void on_emitted(const SemanticFeedbackRecord& record);
    void refresh_records();

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    SemanticFeedbackDocument<SemanticFeedbackPage> document_{};
    std::array<Command, 7> commands_{};
    Command clear_{};
    Value<bool> sound_{true};
    std::shared_ptr<Label> latest_;
    std::shared_ptr<TextBox> trace_;
    std::shared_ptr<Label> accounting_;
    std::unique_ptr<SemanticFeedback> feedback_;
};

SemanticFeedbackPage::SemanticFeedbackPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    connect_commands();
    connect_policy();
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        on(commands_[index].invoked(), *this, &SemanticFeedbackPage::on_announce,
           static_cast<int>(index));
    }
    on(clear_.invoked(), *this, &SemanticFeedbackPage::on_clear);
    on(sound_.changed(), *this, &SemanticFeedbackPage::on_sound_changed);
    refresh_records();
}

void SemanticFeedbackPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size, where each is placed by its bounds in design coordinates. The
    // copy of the child list keeps the iteration valid while the root
    // shrinks.
    Control& root = *document_.gallery_semantic_feedback_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_semantic_transitions, document_.gallery_semantic_policy,
        document_.gallery_semantic_records};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
    latest_ = document_.gallery_semantic_latest;
    trace_ = document_.gallery_semantic_trace;
    accounting_ = document_.gallery_semantic_accounting;
}

void SemanticFeedbackPage::connect_commands() {
    std::vector<CommandItem> items;
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        Command& command = commands_[index];
        command.set_text(std::string(labels[index]));
        command.set_description(std::string("Announce ") +
                                semantic_feedback_kind_name(kinds[index]) +
                                " through SemanticFeedback");
        CommandItem item;
        item.id = static_cast<int>(index);
        item.label = std::string(labels[index]);
        item.accessible_name = std::string("Announce ") +
                               semantic_feedback_kind_name(kinds[index]);
        item.command = &command;
        items.push_back(std::move(item));
    }
    CommandBar& bar = *document_.gallery_semantic_commands;
    bar.set_style(page_style());
    bar.set_items(std::move(items));
}

void SemanticFeedbackPage::connect_policy() {
    (*document_.gallery_semantic_complete).bind(commands_[completed_index]);
    (*document_.gallery_semantic_sound).bind(sound_);
    clear_.set_text("Clear history");
    clear_.set_description("Clear the retained feedback records");
    (*document_.gallery_semantic_clear).bind(clear_);
}

bool SemanticFeedbackPage::ensure_feedback() {
    if (feedback_) return true;
    Window* const window = (*latest_).attached_window();
    if (window == nullptr) {
        (*latest_).set_text("SemanticFeedback needs an attached Window");
        return false;
    }
    feedback_ = std::make_unique<SemanticFeedback>(*window);
    (*feedback_).set_maximum_records(retained_records);
    on((*feedback_).emitted(), *this, &SemanticFeedbackPage::on_emitted);
    return true;
}

void SemanticFeedbackPage::on_announce(const int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= kinds.size()) return;
    if (!ensure_feedback()) return;
    static_cast<void>((*feedback_).emit(kinds[static_cast<std::size_t>(index)]));
    refresh_records();
}

void SemanticFeedbackPage::on_clear() {
    if (feedback_) (*feedback_).clear();
    (*latest_).set_text("History cleared · no transition announced yet");
    refresh_records();
}

void SemanticFeedbackPage::on_sound_changed(const bool enabled) {
    Window* const window = (*latest_).attached_window();
    if (window == nullptr) return;
    PresentationSettings settings = (*window).presentation_settings();
    settings.sound_enabled = enabled;
    (*window).set_presentation_settings(settings);
}

void SemanticFeedbackPage::on_emitted(const SemanticFeedbackRecord& record) {
    (*latest_).set_text(
        std::string("feedback=") + semantic_feedback_kind_name(record.kind) +
        " · cue=" + host_sound_cue_name(record.cue) +
        " · sound " + (record.sound_enabled ? "on" : "off") +
        " · host accepted " + (record.host_accepted ? "yes" : "no") +
        " · presented " + (record.sound_presented ? "yes" : "no") +
        " · sequence " + std::to_string(record.sequence));
}

void SemanticFeedbackPage::refresh_records() {
    if (!feedback_) {
        (*accounting_).set_text("Retained 0 of " + std::to_string(retained_records) +
                                " records · dropped 0");
        (*trace_).set_text({});
        return;
    }
    (*accounting_).set_text(
        "Retained " + std::to_string((*feedback_).records().size()) + " of " +
        std::to_string((*feedback_).maximum_records()) + " records · dropped " +
        std::to_string((*feedback_).dropped_record_count()));
    (*trace_).set_text((*feedback_).trace());
}

void add_semantic_feedback(const std::shared_ptr<Surface>& page,
                           const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<SemanticFeedbackPage>(page));
}

}  // namespace gui_forms::gallery::pages
