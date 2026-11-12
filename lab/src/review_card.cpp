#include "gui_forms_lab/review_card.hpp"

#include "gui_forms/text.hpp"
#include "panel/card/card_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

bool valid_review_disposition(ReviewDisposition disposition) noexcept {
    switch (disposition) {
    case ReviewDisposition::neutral:
    case ReviewDisposition::information:
    case ReviewDisposition::accepted:
    case ReviewDisposition::pending:
    case ReviewDisposition::warning:
    case ReviewDisposition::rejected:
        return true;
    }
    return false;
}

bool valid_review_record(const ReviewRecord& record) noexcept {
    return !record.key.empty() && record.key.size() <= 128U &&
           !record.title.empty() && record.title.size() <= 512U &&
           record.summary.size() <= 8192U && record.verdict.size() <= 1024U &&
           validate_utf8(record.key).valid() &&
           validate_utf8(record.title).valid() &&
           validate_utf8(record.summary).valid() &&
           validate_utf8(record.verdict).valid() &&
           valid_review_disposition(record.disposition);
}

} // namespace

ReviewCard::ReviewCard(StableId stable_id)
    : Card(std::move(stable_id)),
      title_label_(make_control<Label>(
          StableId(std::string((*this).stable_id().value()) + ".title"))),
      summary_label_(make_control<Label>(
          StableId(std::string((*this).stable_id().value()) + ".summary"))),
      verdict_label_(make_control<Label>(
          StableId(std::string((*this).stable_id().value()) + ".verdict"))) {
    (*title_label_).text_style_role = TextStyleRole::heading;
    (*title_label_).vertical_alignment = VerticalAlignment::center;
    (*summary_label_).text_style_role = TextStyleRole::body;
    (*summary_label_).text_wrapping = TextWrapping::word;
    (*summary_label_).line_spacing = 1.25;
    (*summary_label_).vertical_alignment = VerticalAlignment::near;
    (*verdict_label_).text_style_role = TextStyleRole::caption;
    (*verdict_label_).vertical_alignment = VerticalAlignment::center;
}

void ReviewCard::initialize_control_tree() {
    if (initialized_) return;
    static_cast<void>(set_header(title_label_));
    static_cast<void>(set_body(summary_label_));
    static_cast<void>(set_footer(verdict_label_));
    initialized_ = true;
}

void ReviewCard::prepare_write(const decltype(record)&, ReviewRecord& incoming) {
    require_mutable();
    if (!valid_review_record(incoming)) {
        throw std::invalid_argument(
            "review record identity/text/disposition is invalid or unbounded");
    }
}

// The labels, the accessibility text and the visual status follow the
// record; record_changed is raised last.
void ReviewCard::written(const decltype(record)&) {
    initialize_control_tree();
    const ReviewRecord& current = record.read();
    (*title_label_).text = current.title;
    (*summary_label_).text = current.summary;
    (*verdict_label_).text = current.verdict;
    accessible_name = current.title;
    accessible_description = current.summary;
    set_visual_status(current.disposition == ReviewDisposition::pending
        ? ControlVisualStatus::pending
        : current.disposition == ReviewDisposition::rejected
            ? ControlVisualStatus::invalid
            : ControlVisualStatus::normal);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::semantics);
    publish_change(record_changed_, record.read());
}

Size ReviewCard::measure(Size available) {
    initialize_control_tree();
    return Card::measure(available);
}

void ReviewCard::arrange(Rect final_bounds) {
    initialize_control_tree();
    Card::arrange(final_bounds);
}

SemanticDescriptor ReviewCard::semantic_descriptor() const {
    SemanticDescriptor descriptor = Card::semantic_descriptor();
    descriptor.name = record.read().title;
    descriptor.description = record.read().summary;
    descriptor.value = record.read().verdict;
    descriptor.exposed = !record.read().key.empty();
    if (record.read().disposition == ReviewDisposition::pending) {
        descriptor.states |= SemanticState::busy;
    } else if (record.read().disposition == ReviewDisposition::rejected) {
        descriptor.states |= SemanticState::invalid;
    }
    return descriptor;
}

namespace {

constexpr ControlAnatomy review_card_anatomy{
    .control_id = "review_card",
    .parts = panel_detail::parts,
    .states = card_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ReviewCard::anatomy() const noexcept {
    return review_card_anatomy;
}

namespace {

// The events ReviewCard adds to those of Card, in declaration order.
constexpr std::array<InteractionDeclaration, 1> review_card_interactions{{
    {.name = "record_changed", .payload = "ReviewRecord", .mutable_payload = false},
}};

} // namespace

void ReviewCard::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Card::append_interactions(interactions);
    interactions.insert(interactions.end(), review_card_interactions.begin(), review_card_interactions.end());
}

// property_codemod begin ReviewCard (tools/property_families/review_card.json; generated, do not edit)

// property_codemod end ReviewCard

} // namespace gui_forms
