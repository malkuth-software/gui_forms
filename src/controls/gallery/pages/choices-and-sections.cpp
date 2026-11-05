#include "pages.hpp"

namespace gui_forms::gallery::pages {

// A rollout review. The deployment ring is one Value<int> shown by two
// ChoiceGroups (a labelled row and a glyph row); three Value<bool>s are each
// borrowed by an ExpandableSections item and bound by a toggle CheckBox in a
// GroupBox; a Card and a ReviewCard project the chosen ring. The owner
// observes the state events (every change) separately from the input events
// (user activation) and never copies state between controls.
class ChoicesPage final : public gui_forms::Component {
public:
    explicit ChoicesPage(const std::shared_ptr<Surface>& body) {
        (*body).add_at(label("gallery.choices-and-sections.heading", "CHOICES AND SECTIONS", 22.0,
                             700, shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
        (*body).add_at(label("gallery.choices-and-sections.lead",
                             "One Value<int> behind two ChoiceGroups, three Value<bool>s behind sections and toggles, and cards that project the choice.",
                             12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
        build_choices(body);
        build_sections(body);
        build_state(body);
        build_cards(body);
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

    void build_choices(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> choices = group(
            "gallery.choices.ring-group", "ChoiceGroup · one Value<int>, two views", {930.0, 120.0});
        (*body).add_at(choices, {24.0, 92.0, 930.0, 120.0});
        ring_row_ = make_control<ChoiceGroup>(StableId("gallery.choices.ring"));
        (*ring_row_).accessible_name = "Deployment ring";
        (*ring_row_).set_items({{.id = 10, .label = "Canary", .accessible_name = "Canary ring"},
                                {.id = 20, .label = "Staged", .accessible_name = "Staged ring"},
                                {.id = 30, .label = "Full", .accessible_name = "Full ring"}});
        (*ring_row_).bind(ring_);
        (*choices).add_at(ring_row_, {20.0, 38.0, 480.0, 32.0});
        ring_chips_ = make_control<ChoiceGroup>(StableId("gallery.choices.ring-chips"));
        (*ring_chips_).accessible_name = "Deployment ring (glyphs)";
        (*ring_chips_).set_items({{.id = 10, .glyph = "◔", .accessible_name = "Canary ring"},
                                  {.id = 20, .glyph = "◑", .accessible_name = "Staged ring"},
                                  {.id = 30, .glyph = "●", .accessible_name = "Full ring"}});
        (*ring_chips_).bind(ring_);
        (*choices).add_at(ring_chips_, {540.0, 38.0, 180.0, 32.0});
        ring_status_ = label("gallery.choices.ring-status", "", 10.0, 600, green);
        (*choices).add_at(ring_status_, {20.0, 84.0, 890.0, 24.0});
    }

    [[nodiscard]] static std::shared_ptr<Label> section_body(std::string id, std::string text,
                                                             const double height) {
        std::shared_ptr<Label> body = label(std::move(id), std::move(text), 11.0, 400, ink);
        (*body).set_text_wrapping(TextWrapping::word);
        (*body).set_vertical_alignment(VerticalAlignment::near);
        (*body).set_requested_bounds({0.0, 0.0, 0.0, height});
        return body;
    }

    void build_sections(const std::shared_ptr<Surface>& body) {
        sections_ = make_control<ExpandableSections>(StableId("gallery.choices.sections"));
        (*sections_).accessible_name = "Review sections";
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
        (*body).add_at(sections_, {24.0, 230.0, 450.0, 300.0});
        sections_status_ = label("gallery.choices.sections-status",
                                 "Checklist open · headers, Space and the toggles share one state",
                                 10.0, 600, green);
        (*body).add_at(sections_status_, {24.0, 540.0, 450.0, 24.0});
    }

    void build_state(const std::shared_ptr<Surface>& body) {
        state_ = make_control<GroupBox>(StableId("gallery.choices.state"), "Section state");
        (*state_).set_background(paper);
        (*state_).set_style(page_style());
        (*state_).set_font({FontRole::control, 12.0, 700, false, 0.24});
        (*state_).accessible_name = "Section state";
        (*body).add_at(state_, {494.0, 230.0, 460.0, 120.0});
        add_toggle("gallery.choices.state.checklist", "Checklist", checklist_open_, 0.0);
        add_toggle("gallery.choices.state.notes", "Notes", notes_open_, 1.0);
        add_toggle("gallery.choices.state.history", "History", history_open_, 2.0);
    }

    void add_toggle(std::string id, std::string text, Value<bool>& model, const double column) {
        std::shared_ptr<CheckBox> toggle = make_control<CheckBox>(StableId(std::move(id)),
                                                                  std::move(text));
        (*toggle).set_indicator_style(ChoiceIndicatorStyle::toggle);
        (*toggle).set_style(page_style());
        (*toggle).bind(model);
        (*toggle).set_requested_bounds({20.0 + column * 140.0, 44.0, 130.0, 28.0});
        (*state_).add_child(toggle);
    }

    void build_cards(const std::shared_ptr<Surface>& body) {
        card_ = make_control<Card>(StableId("gallery.choices.card"));
        (*card_).accessible_name = "Rollout plan card";
        (*card_).set_interactive(true);
        (*card_).set_selection_behavior(CardSelectionBehavior::select_on_activation);
        static_cast<void>((*card_).set_header(
            label("gallery.choices.card.title", "Rollout plan", 12.0, 700, shell_blue_dark)));
        card_body_ = label("gallery.choices.card.body", "", 11.0, 400, ink);
        (*card_body_).set_text_wrapping(TextWrapping::word);
        (*card_body_).set_vertical_alignment(VerticalAlignment::near);
        static_cast<void>((*card_).set_body(card_body_));
        card_footer_ = label("gallery.choices.card.footer", "Click or press Space to select",
                             10.0, 600, muted);
        static_cast<void>((*card_).set_footer(card_footer_));
        (*body).add_at(card_, {494.0, 368.0, 220.0, 196.0});

        review_ = make_control<ReviewCard>(StableId("gallery.choices.review"));
        (*review_).accessible_name = "Rollout review";
        (*body).add_at(review_, {730.0, 368.0, 224.0, 196.0});
    }

    // State: every change of the ring, from either ChoiceGroup or a setter.
    void ring_changed(const int index) {
        const std::string name = ring_name(index);
        (*ring_status_).set_text("Ring " + name + " (index " + std::to_string(index) +
                                 ") · user activations: " + std::to_string(activations_));
        (*card_body_).set_text(index == 0 ? "Canary: 5 % of hosts for one hour, then review."
                               : index == 1 ? "Staged: one region at a time, an hour each."
                               : index == 2 ? "Full: every host at once; rollback armed."
                                            : "No ring chosen.");
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
        (*review_).set_record(std::move(record));
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
        (*sections_status_).set_text(name + (expanded ? " open" : " closed") +
                                     " · headers, Space and the toggles share one state");
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
        (*card_footer_).set_text((*card_).selected() ? "Selected · " + activations : activations);
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
    std::shared_ptr<ChoiceGroup> ring_row_;
    std::shared_ptr<ChoiceGroup> ring_chips_;
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

void add_choices_and_sections(const std::shared_ptr<Surface>& page,
                              const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<ChoicesPage>(page));
}

}  // namespace gui_forms::gallery::pages
