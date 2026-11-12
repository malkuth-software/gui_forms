#include "pages.hpp"

#include "choices-and-sections_form.hpp"
#include "gui_forms_lab/review_card.hpp"

#include <array>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// A rollout review: the controls demo/pages/choices-and-sections.form
// describes, and their behaviour. The deployment ring is one Value<int> shown
// by two ChoiceGroups (a labelled row and a glyph row); three Value<bool>s
// are each borrowed by an ExpandableSections item and bound by a toggle
// CheckBox in a GroupBox; a Card and a ReviewCard project the chosen ring.
// The owner observes the state events (every change) separately from the
// input events (user activation) and never copies state between controls.
class ChoicesPage final : public gui_forms::Component {
public:
    explicit ChoicesPage(const std::shared_ptr<Surface>& body) {
        document_.initialize_component(*this);
        mount_document(body);
        build_choices();
        build_sections();
        build_state();
        build_cards();
        build_switches();
        on(ring_.changed(), *this, &ChoicesPage::ring_changed);
        on((*ring_row_).activated(), *this, &ChoicesPage::ring_activated);
        on((*sections_).toggled(), *this, &ChoicesPage::section_toggled);
        on((*card_).activated(), *this, &ChoicesPage::card_activated);
        on((*card_).selected_changed(), *this, &ChoicesPage::card_selected);
        ring_changed(ring_.get());
    }

private:
    static constexpr std::array<std::string_view, 3> ring_names{"Canary", "Staged", "Full"};
    static constexpr std::array<std::string_view, 3> section_names{
        "Checklist", "Notes", "History"};

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_choices_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        ring_row_ = document_.gallery_choices_ring;
        ring_status_ = document_.gallery_choices_ring_status;
        sections_ = document_.gallery_choices_sections;
        sections_status_ = document_.gallery_choices_sections_status;
        state_ = document_.gallery_choices_state;
        card_ = document_.gallery_choices_card;
        review_ = document_.gallery_choices_review;
    }

    void build_choices() {
        (*ring_row_).set_items({{.id = 10, .label = "Canary", .accessible_name = "Canary ring"},
                                {.id = 20, .label = "Staged", .accessible_name = "Staged ring"},
                                {.id = 30, .label = "Full", .accessible_name = "Full ring"}});
        (*ring_row_).bind(ring_);
        ChoiceGroup& chips = *document_.gallery_choices_ring_chips;
        chips.set_items({{.id = 10, .glyph = "◔", .accessible_name = "Canary ring"},
                         {.id = 20, .glyph = "◑", .accessible_name = "Staged ring"},
                         {.id = 30, .glyph = "●", .accessible_name = "Full ring"}});
        chips.bind(ring_);
    }

    [[nodiscard]] static std::shared_ptr<Label> section_body(std::string id, std::string text,
                                                             const double height) {
        std::shared_ptr<Label> body = label(std::move(id), std::move(text), 11.0, 400, "ink");
        (*body).text_wrapping = TextWrapping::word;
        (*body).vertical_alignment = VerticalAlignment::near;
        (*body).set_requested_bounds({0.0, 0.0, 0.0, height});
        return body;
    }

    void build_sections() {
        (*sections_).set_items({
            {.id = 1, .label = "Checklist",
             .content = section_body("gallery.choices.sections.checklist",
                                     "Change ticket approved · rollback rehearsed · dashboards pinned · on-call briefed",
                                     72.0),
             .expanded = &checklist_open_},
            {.id = 2, .label = "Notes",
             .content = section_body("gallery.choices.sections.notes",
                                     "Hold at the chosen ring for one hour before widening. Error budget: 0.4 % remaining.",
                                     60.0),
             .expanded = &notes_open_},
            {.id = 3, .label = "History",
             .content = section_body("gallery.choices.sections.history",
                                     "2026-08-03 canary ok · 2026-08-04 staged ok · 2026-08-05 pending review",
                                     60.0),
             .expanded = &history_open_}});
    }

    // The toggles' indicator style and placement are in the document; each
    // binds one section's open state.
    void build_state() {
        bind_toggle(*document_.gallery_choices_state_checklist, checklist_open_);
        bind_toggle(*document_.gallery_choices_state_notes, notes_open_);
        bind_toggle(*document_.gallery_choices_state_history, history_open_);
    }

    static void bind_toggle(CheckBox& toggle, Value<bool>& model) {
        toggle.bind(model);
    }

    // The switches report through the group's status line.
    void build_switches() {
        on((*document_.gallery_choices_switches_auto_deploy).checked_changed(), *this,
           &ChoicesPage::switch_changed);
        on((*document_.gallery_choices_switches_notify).checked_changed(), *this, &ChoicesPage::switch_changed);
    }

    // Serves both switches: the handler reads them, so it ignores the value.
    void switch_changed(bool) {
        const bool deploy = (*document_.gallery_choices_switches_auto_deploy).checked.read();
        const bool notify = (*document_.gallery_choices_switches_notify).checked.read();
        std::string report = deploy ? "Automatic deployment is on" : "Automatic deployment is off";
        if (notify) report += " · reviewers are notified";
        (*document_.gallery_choices_switches_status).text = report;
    }

    void build_cards() {
        (*card_).interactive = true;
        (*card_).selection_behavior = CardSelectionBehavior::select_on_activation;
        static_cast<void>((*card_).set_header(
            label("gallery.choices.card.title", "Rollout plan", 12.0, 700, "heading")));
        card_body_ = label("gallery.choices.card.body", "", 11.0, 400, "ink");
        (*card_body_).text_wrapping = TextWrapping::word;
        (*card_body_).vertical_alignment = VerticalAlignment::near;
        static_cast<void>((*card_).set_body(card_body_));
        card_footer_ = label("gallery.choices.card.footer", "Click or press Space to select",
                             10.0, 600, "muted");
        static_cast<void>((*card_).set_footer(card_footer_));
    }

    // State: every change of the ring, from either ChoiceGroup or a setter.
    void ring_changed(const int index) {
        const std::string name = ring_name(index);
        (*ring_status_).text = "Ring " + name + " (index " + std::to_string(index) +
                                 ") · user activations: " + std::to_string(activations_);
        (*card_body_).text = index == 0 ? "Canary: 5 % of hosts for one hour, then review."
                               : index == 1 ? "Staged: one region at a time, an hour each."
                               : index == 2 ? "Full: every host at once; rollback armed."
                                            : "No ring chosen.";
        ReviewRecord record;
        record.key = "ring." + name;
        record.title = "Review: " + name;
        if (index == 0) {
            record.summary = "Low blast radius; telemetry decides the next ring.";
            record.verdict = "AWAITING TELEMETRY";
            record.disposition = ReviewDisposition::pending;
        } else if (index == 1) {
            record.summary = "Region by region with an hour of soak between steps.";
            record.verdict = "APPROVED";
            record.disposition = ReviewDisposition::accepted;
        } else if (index == 2) {
            record.summary = "All hosts at once; only with a rehearsed rollback.";
            record.verdict = "NEEDS SIGN-OFF";
            record.disposition = ReviewDisposition::warning;
        } else {
            record.summary = "Choose a ring to review the plan.";
            record.verdict = "NO RING";
            record.disposition = ReviewDisposition::neutral;
        }
        (*review_).record = std::move(record);
    }

    // Input: only the user's activation of the labelled row.
    void ring_activated() {
        ++activations_;
        ring_changed(ring_.get());
    }

    void section_toggled(const int index, const bool expanded) {
        const std::size_t position = static_cast<std::size_t>(index);
        const std::string name = position < section_names.size()
            ? std::string(section_names[position]) : std::string("Section");
        (*sections_status_).text = name + (expanded ? " open" : " closed") +
                                     " · headers, Space and the toggles share one state";
    }

    // Selection changes before activation is published; both project the
    // same footer so neither handler overwrites the other's report.
    void card_activated() {
        ++card_activations_;
        project_card_footer();
    }

    void card_selected() { project_card_footer(); }

    void project_card_footer() {
        const std::string activations = card_activations_ == 0
            ? std::string("Click or press Space to select")
            : "activated " + std::to_string(card_activations_) +
                  (card_activations_ == 1 ? " time" : " times");
        (*card_footer_).text = (*card_).selected.read() ? "Selected · " + activations : activations;
    }

    [[nodiscard]] static std::string ring_name(const int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < ring_names.size()) {
            return std::string(ring_names[static_cast<std::size_t>(index)]);
        }
        return "none";
    }

    Value<int> ring_{0};
    Value<bool> checklist_open_{true};
    Value<bool> notes_open_{false};
    Value<bool> history_open_{false};
    // Declared after the Values its controls bind, so it is released first.
    ChoicesDocument<ChoicesPage> document_{};
    std::shared_ptr<ChoiceGroup> ring_row_;
    std::shared_ptr<Label> ring_status_;
    std::shared_ptr<ExpandableSections> sections_;
    std::shared_ptr<Label> sections_status_;
    std::shared_ptr<GroupBox> state_;
    std::shared_ptr<Card> card_;
    std::shared_ptr<Label> card_body_;
    std::shared_ptr<Label> card_footer_;
    std::shared_ptr<ReviewCard> review_;
    int activations_{};
    int card_activations_{};
};

} // namespace

void add_choices_and_sections(const std::shared_ptr<Surface>& page,
                              const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ChoicesPage>(page));
}

}  // namespace gui_forms::gallery::pages
