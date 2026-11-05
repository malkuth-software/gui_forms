#include "pages.hpp"

#include "gui_forms/feedback/semantic_feedback/semantic_feedback.hpp"

namespace gui_forms::gallery::pages {

// Owns the semantic-feedback page: one Command per SemanticFeedbackKind on a
// CommandBar (the completion Command is shared with a plain Button), the
// Value<bool> the sound check box binds, and the SemanticFeedback component
// whose bounded record history is published back into the page.
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

    void build_commands(const std::shared_ptr<Surface>& body);
    void build_policy(const std::shared_ptr<Surface>& body);
    void build_records(const std::shared_ptr<Surface>& body);
    [[nodiscard]] bool ensure_feedback();
    void on_announce(int index);
    void on_clear();
    void on_sound_changed(bool enabled);
    void on_emitted(const SemanticFeedbackRecord& record);
    void refresh_records();

    std::array<Command, 7> commands_{};
    Command clear_{};
    Value<bool> sound_{true};
    std::shared_ptr<CommandBar> bar_;
    std::shared_ptr<Label> latest_;
    std::shared_ptr<TextBox> trace_;
    std::shared_ptr<Label> accounting_;
    std::unique_ptr<SemanticFeedback> feedback_;
};

SemanticFeedbackPage::SemanticFeedbackPage(const std::shared_ptr<Surface>& body) {
    (*body).add_at(label("gallery.semantic-feedback.heading", "SEMANTIC FEEDBACK", 22.0,
                         700, shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
    (*body).add_at(label("gallery.semantic-feedback.lead",
                         "One-shot application transitions become bounded, traceable host sound cues; ordinary hover, focus and selection never do.",
                         12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
    build_commands(body);
    build_policy(body);
    build_records(body);
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        on(commands_[index].invoked(), *this, &SemanticFeedbackPage::on_announce,
           static_cast<int>(index));
    }
    on(clear_.invoked(), *this, &SemanticFeedbackPage::on_clear);
    on(sound_.changed(), *this, &SemanticFeedbackPage::on_sound_changed);
    refresh_records();
}

void SemanticFeedbackPage::build_commands(const std::shared_ptr<Surface>& body) {
    std::shared_ptr<LayoutGroup> transitions = group(
        "gallery.semantic.transitions",
        "SemanticFeedback · application transitions mapped to host cues", {930.0, 150.0});
    (*body).add_at(transitions, {24.0, 92.0, 930.0, 150.0});
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
    bar_ = make_control<CommandBar>(StableId("gallery.semantic.commands"));
    (*bar_).set_style(page_style());
    (*bar_).accessible_name = "Semantic feedback transitions";
    (*bar_).set_items(std::move(items));
    (*transitions).add_at(bar_, {22.0, 40.0, 886.0, 40.0});
    std::shared_ptr<Label> note = label(
        "gallery.semantic.transitions.note",
        "Each item borrows an application-owned Command. Arrow keys move along the toolbar's single roving tab stop; Tab leaves it. The cue for each kind is fixed by SemanticFeedback::cue_for.",
        10.0, 400, muted);
    (*note).set_text_wrapping(TextWrapping::word);
    (*note).set_vertical_alignment(VerticalAlignment::near);
    (*transitions).add_at(note, {22.0, 92.0, 886.0, 44.0});
}

void SemanticFeedbackPage::build_policy(const std::shared_ptr<Surface>& body) {
    std::shared_ptr<LayoutGroup> policy = group(
        "gallery.semantic.policy", "Shared Command and sound policy", {930.0, 112.0});
    (*body).add_at(policy, {24.0, 262.0, 930.0, 112.0});
    std::shared_ptr<Button> complete = make_control<Button>(
        StableId("gallery.semantic.complete"), "Announce completion");
    (*complete).set_visual_style(ButtonVisualStyle::accent);
    (*complete).bind(commands_[completed_index]);
    (*policy).add_at(complete, {22.0, 40.0, 200.0, 34.0});

    std::shared_ptr<CheckBox> sound =
        make_control<CheckBox>(StableId("gallery.semantic.sound"), "Sound cues enabled");
    (*sound).set_indicator_style(ChoiceIndicatorStyle::toggle);
    (*sound).bind(sound_);
    (*policy).add_at(sound, {240.0, 42.0, 220.0, 28.0});

    clear_.set_text("Clear history");
    clear_.set_description("Clear the retained feedback records");
    std::shared_ptr<Button> clear =
        make_control<Button>(StableId("gallery.semantic.clear"), "Clear history");
    (*clear).set_visual_style(ButtonVisualStyle::command);
    (*clear).bind(clear_);
    (*policy).add_at(clear, {480.0, 40.0, 160.0, 34.0});

    accounting_ = label("gallery.semantic.accounting", "", 10.0, 700, shell_blue_dark);
    (*accounting_).set_text_wrapping(TextWrapping::word);
    (*accounting_).set_vertical_alignment(VerticalAlignment::near);
    (*policy).add_at(accounting_, {660.0, 40.0, 248.0, 56.0});
}

void SemanticFeedbackPage::build_records(const std::shared_ptr<Surface>& body) {
    std::shared_ptr<LayoutGroup> records = group(
        "gallery.semantic.records", "Published records · bounded history", {930.0, 230.0});
    (*body).add_at(records, {24.0, 392.0, 930.0, 230.0});
    latest_ = label("gallery.semantic.latest", "No transition announced yet", 11.0,
                    600, green);
    (*latest_).set_text_wrapping(TextWrapping::word);
    (*latest_).set_vertical_alignment(VerticalAlignment::near);
    (*records).add_at(latest_, {22.0, 40.0, 886.0, 30.0});
    trace_ = make_control<TextBox>(StableId("gallery.semantic.trace"));
    (*trace_).set_multiline(true);
    (*trace_).set_read_only(true);
    (*trace_).accessible_name = "Semantic feedback trace";
    (*trace_).set_placeholder_text("feedback=<kind> sequence=<n> cue=<cue> sound=<on|off> accepted=<bool> presented=<bool>");
    (*records).add_at(trace_, {22.0, 78.0, 886.0, 134.0});
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
