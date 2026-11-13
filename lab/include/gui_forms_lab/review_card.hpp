#pragma once

#include "gui_forms_lab/review_card_values.hpp"
#include "gui_forms/controls/card.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms_lab/review_card_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace gui_forms {

// A typed, content-agnostic review projection over Card. It owns theme-aware
// title/body/verdict labels and projects pending/rejected state into the same
// retained visual and semantic laws as any other Card. Consumers supply data,
// never a private renderer or demo-only layout subclass. Each owned Label is
// exposed so applications can customize its ordinary Label properties without
// subclassing or replacing ReviewCard's state projection.
class ReviewCard final : public Card, public ReviewCardCapabilities<ReviewCard> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit ReviewCard(StableId stable_id);
    void initialize_control_tree();

    [[nodiscard]] std::shared_ptr<Label> title_label() const noexcept {
        return title_label_;
    }
    [[nodiscard]] std::shared_ptr<Label> summary_label() const noexcept {
        return summary_label_;
    }
    [[nodiscard]] std::shared_ptr<Label> verdict_label() const noexcept {
        return verdict_label_;
    }
    [[nodiscard]] Event<const ReviewRecord&>& record_changed() noexcept {
        return record_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    std::shared_ptr<Label> title_label_;
    std::shared_ptr<Label> summary_label_;
    std::shared_ptr<Label> verdict_label_;
    Event<const ReviewRecord&> record_changed_;
    bool initialized_{};
    // property_codemod begin ReviewCard (tools/property_families/review_card.json; generated, do not edit)
private:
    friend struct ReviewCardRecordCapability<ReviewCard>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(record)& cell, ReviewRecord& incoming);
    void written(const decltype(record)& cell);
    // property_codemod end ReviewCard
};

} // namespace gui_forms
