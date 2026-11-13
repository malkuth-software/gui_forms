#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"

#include <memory>

namespace gui_forms {

// The up-down controls' base, as Windows Forms' UpDownBase: NumericUpDown
// and DomainUpDown derive from it, so an UpDownBase pointer holds either.
// It cannot be made on its own (its constructor is protected).
//
// What the two share is their parts: an edit field (a TextBox) and a spin
// column (the source-private SpinButtons, spin_buttons.hpp), both children
// of the control, and the two subscriptions the control holds on them. Each
// derived class makes the parts in its initialize_control_tree (the field's
// configuration and the subscriptions' targets are its own); until then
// both are null. Its own virtual functions are the stepping pair Windows
// Forms makes abstract here (UpButton, DownButton; owner-accepted base
// proposal, CP29 I5): each derived class steps its own way, and the text it
// gives the field stays its own.
class GUI_FORMS_API UpDownBase : public Panel {
public:
    ~UpDownBase() override;

    // The edit field, or null before the derived class made it.
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }
    // Focus on the control goes to its edit field (null without one).
    [[nodiscard]] Ptr focus_target() const override { return editor_; }

    // One step up and one step down, as the spin column's halves step: the
    // derived class says what a step is (NumericUpDown adds or subtracts its
    // increment within the range; DomainUpDown moves to the previous or the
    // next item). Neither moves the focus.
    virtual void up_button() = 0;
    virtual void down_button() = 0;

protected:
    explicit UpDownBase(StableId stable_id);

    // The parts, set once by the derived class's initialize_control_tree
    // and added there as children; the child list owns them too, so these
    // stay valid while the control lives. The spin column is a SpinButtons.
    std::shared_ptr<TextBox> editor_;
    Control::Ptr spinner_;
    // The derived class's subscriptions to the field's text_changed and the
    // column's stepped events. Declared after the parts, so they end (and
    // the callbacks stop) before the parts are released.
    SubscriptionToken editor_change_;
    SubscriptionToken spinner_step_;
};

} // namespace gui_forms
