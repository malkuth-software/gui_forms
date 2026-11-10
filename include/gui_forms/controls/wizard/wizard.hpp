#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/wizard/wizard_capabilities/wizard_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

class Button;
class StepIndicator;
class WizardPage;

// The argument of Wizard::page_changing: the page shown, the page asked
// for, and whether a handler refuses the move.
struct WizardPageChangingEvent final {
    std::uint32_t from{0U};
    std::uint32_t to{0U};
    bool cancel{false};
};

// A wizard (the superset's P1 rest): a guided sequence of WizardPage
// children shown one at a time, with the current page's title and
// description in a header, an optional step indicator naming every page,
// and Back, Next (Finish on the last page) and Cancel buttons along the
// bottom. The pages are the WizardPage children in child order; other
// children are ignored (the wizard's own header, indicator and buttons are
// internal).
//
// Next is available while the current page's can_advance is set; Back on
// every page but the first. go_next and go_back (and the buttons) raise
// page_changing, which may refuse, then show the page and raise
// page_changed; a press on a completed step in the indicator goes back to
// it the same way. Finish raises finished and Cancel raises cancelled; the
// wizard itself stays as it is (its owner closes it). Enter left unhandled
// by the focused control acts as Next or Finish, and Escape as Cancel. A
// write of current_page moves at once, without page_changing.
//
// Theme: the header's `caption` (the title, in the heading face) and
// `label` (the description, secondary), and the `divider` above the
// buttons, are of the panel family; the indicator and buttons are stock
// controls. To assistive technology it is a group named by the current
// page's title.
class Wizard final : public Control, public WizardCapabilities<Wizard> {
public:
    explicit Wizard(StableId stable_id);
    ~Wizard() override;

    // The WizardPage children, in order.
    [[nodiscard]] std::vector<std::shared_ptr<WizardPage>> pages() const;
    [[nodiscard]] std::shared_ptr<WizardPage> page(std::size_t index) const;
    [[nodiscard]] std::size_t page_count() const;
    // Moves to the next page, or finishes on the last; false when the
    // current page cannot advance or a handler refused.
    bool go_next();
    // Moves to the previous page; false on the first page or when refused.
    bool go_back();
    // Moves to `index` through page_changing; false when it is out of
    // range, the current page, or refused.
    bool go_to(std::uint32_t index);
    // Raises cancelled.
    void cancel();
    [[nodiscard]] bool can_go_next() const;
    [[nodiscard]] bool can_go_back() const noexcept;

    [[nodiscard]] std::shared_ptr<Button> back_button() const noexcept { return back_; }
    [[nodiscard]] std::shared_ptr<Button> next_button() const noexcept { return next_; }
    [[nodiscard]] std::shared_ptr<Button> cancel_button() const noexcept { return cancel_; }
    [[nodiscard]] std::shared_ptr<StepIndicator> step_indicator() const noexcept { return indicator_; }

    [[nodiscard]] Event<WizardPageChangingEvent&>& page_changing() noexcept { return page_changing_; }
    [[nodiscard]] Event<std::uint32_t>& page_changed() noexcept { return page_changed_; }
    [[nodiscard]] Event<>& finished() noexcept { return finished_; }
    [[nodiscard]] Event<>& cancelled() noexcept { return cancelled_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry caption;
        PartGeometry label;
        PartGeometry divider;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_dispose() noexcept override;

private:
    friend class WizardPage;
    // Where the parts lie in a wizard of `size`.
    struct Placement final {
        Rect caption{};
        Rect label{};
        Rect indicator{};
        Rect page{};
        Rect divider{};
        Rect back{};
        Rect next{};
        Rect cancel{};
    };
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] FontSpec heading_font() const noexcept;
    [[nodiscard]] FontSpec description_font() const noexcept;
    // Brings the indicator's steps, the buttons' texts and availability and
    // the pages' visibility in line with the pages and current_page.
    void sync();
    void on_page_updated(WizardPage& page);
    void on_back(ButtonBase& button);
    void on_next(ButtonBase& button);
    void on_cancel(ButtonBase& button);
    void on_step_clicked(std::size_t step);
    [[nodiscard]] bool internal_child(const Control& child) const noexcept;

    std::shared_ptr<StepIndicator> indicator_{};
    std::shared_ptr<Button> back_{};
    std::shared_ptr<Button> next_{};
    std::shared_ptr<Button> cancel_{};
    std::vector<SubscriptionToken> tokens_{};
    // The button sizes measured at the last arrange.
    Size back_size_{};
    Size next_size_{};
    Size cancel_size_{};
    Event<WizardPageChangingEvent&> page_changing_;
    Event<std::uint32_t> page_changed_;
    Event<> finished_;
    Event<> cancelled_;
    // property_codemod begin Wizard (tools/property_families/wizard.json; generated, do not edit)
public:
    // Wizard's property table: Control's entries, then its own.
    static constexpr std::size_t wizard_property_count = Control::control_property_count + 6U;
    [[nodiscard]] static const std::array<PropertyEntry, wizard_property_count>& wizard_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct WizardCurrentPageCapability<Wizard>;
    friend struct WizardShowStepIndicatorCapability<Wizard>;
    friend struct WizardBackTextCapability<Wizard>;
    friend struct WizardNextTextCapability<Wizard>;
    friend struct WizardFinishTextCapability<Wizard>;
    friend struct WizardCancelTextCapability<Wizard>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(current_page)& cell, std::uint32_t& incoming);
    void written(const decltype(current_page)& cell);
    void prepare_write(const decltype(show_step_indicator)& cell, bool& incoming);
    void written(const decltype(show_step_indicator)& cell);
    void prepare_write(const decltype(back_text)& cell, std::string& incoming);
    void written(const decltype(back_text)& cell);
    void prepare_write(const decltype(next_text)& cell, std::string& incoming);
    void written(const decltype(next_text)& cell);
    void prepare_write(const decltype(finish_text)& cell, std::string& incoming);
    void written(const decltype(finish_text)& cell);
    void prepare_write(const decltype(cancel_text)& cell, std::string& incoming);
    void written(const decltype(cancel_text)& cell);
    // property_codemod end Wizard
};

} // namespace gui_forms
