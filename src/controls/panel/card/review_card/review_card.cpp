#include "gui_forms/controls/panel/card/review_card/review_card.hpp"

#include "gui_forms/text.hpp"
#include "../card_anatomy.hpp"

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
    (*title_label_).set_text_style_role(TextStyleRole::heading);
    (*title_label_).set_vertical_alignment(VerticalAlignment::center);
    (*summary_label_).set_text_style_role(TextStyleRole::body);
    (*summary_label_).set_text_wrapping(TextWrapping::word);
    (*summary_label_).set_line_spacing(1.25);
    (*summary_label_).set_vertical_alignment(VerticalAlignment::near);
    (*verdict_label_).set_text_style_role(TextStyleRole::caption);
    (*verdict_label_).set_vertical_alignment(VerticalAlignment::center);
}

void ReviewCard::initialize_control_tree() {
    if (initialized_) return;
    static_cast<void>(set_header(title_label_));
    static_cast<void>(set_body(summary_label_));
    static_cast<void>(set_footer(verdict_label_));
    initialized_ = true;
}

void ReviewCard::set_record(ReviewRecord record) {
    require_mutable();
    if (!valid_review_record(record)) {
        throw std::invalid_argument(
            "review record identity/text/disposition is invalid or unbounded");
    }
    if (record_ == record) return;
    initialize_control_tree();
    record_ = std::move(record);
    (*title_label_).set_text(record_.title);
    (*summary_label_).set_text(record_.summary);
    (*verdict_label_).set_text(record_.verdict);
    accessible_name = record_.title;
    accessible_description = record_.summary;
    set_visual_status(record_.disposition == ReviewDisposition::pending
        ? ControlVisualStatus::pending
        : record_.disposition == ReviewDisposition::rejected
            ? ControlVisualStatus::invalid
            : ControlVisualStatus::normal);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::semantics);
    publish_change(record_changed_, record_);
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
    descriptor.name = record_.title;
    descriptor.description = record_.summary;
    descriptor.value = record_.verdict;
    descriptor.exposed = !record_.key.empty();
    if (record_.disposition == ReviewDisposition::pending) {
        descriptor.states |= SemanticState::busy;
    } else if (record_.disposition == ReviewDisposition::rejected) {
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

} // namespace gui_forms
