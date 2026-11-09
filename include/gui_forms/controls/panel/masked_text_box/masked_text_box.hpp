#pragma once

#include "gui_forms/controls/panel/masked_text_box/masked_text_provider/masked_text_provider.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/controls/panel/masked_text_box/masked_text_box_capabilities/masked_text_box_capabilities.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Input the mask refused: the position the character was tested at, why,
// and the refused text (one character typed, or the whole value offered).
struct MaskInputRejection final {
    MaskPosition position{};
    MaskResultHint hint{MaskResultHint::invalid_input};
    std::string text{};
};

// One run of the validation hook (Windows Forms' TypeValidationCompleted).
// The hook reads `text` (the text in text_mask_format) and writes `valid`
// and, when it refuses, a `message`; setting `cancel` keeps the focus in the
// box when validation runs because the focus is leaving.
struct MaskValidation final {
    std::string text{};
    bool mask_completed{};
    bool valid{true};
    std::string message{};
    bool cancel{};
};

// A text box that takes input through a mask (Windows Forms' MaskedTextBox).
// The mask and the assigned characters live in a MaskedTextProvider; the
// field is a child TextBox showing the provider's display text, with a prompt
// character in each unassigned position and the mask's literals in place.
// Typing, deletion, paste and assistive set-value all go through the
// provider: input that does not fit is refused and reported by
// mask_input_rejected, and the caret passes over literals. An empty mask
// takes any text, as a plain TextBox.
class MaskedTextBox final : public Panel, public MaskedTextBoxCapabilities<MaskedTextBox> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit MaskedTextBox(StableId stable_id, std::string initial_mask = {});
    ~MaskedTextBox() override;
    void initialize_control_tree();

    // The mask and the assigned characters, for reading; edits go through
    // the box so that the field and its events follow.
    [[nodiscard]] const MaskedTextProvider& provider() const noexcept { return provider_; }

    // The text in text_mask_format.
    [[nodiscard]] std::string text() const;
    // Clears the box and places `text` from the first position, literals in
    // the text matching the mask's literals. Refused text leaves the box
    // unchanged and raises mask_input_rejected.
    MaskEditResult set_text(std::string_view text);
    void clear();
    // Every required position is assigned.
    [[nodiscard]] bool mask_completed() const noexcept { return provider_.mask_completed(); }
    // Every edit position is assigned.
    [[nodiscard]] bool mask_full() const noexcept { return provider_.mask_full(); }

    // The validation hook, run by validate_text and when the focus leaves
    // the box. The delegate does not own its target; clear it (an empty
    // Delegate) before the target goes away. Without a hook validation
    // reports valid and raises nothing.
    void set_validator(Delegate<MaskValidation&> validator) noexcept { validator_ = validator; }
    // Runs the hook and raises type_validation_completed; returns whether
    // the text is valid.
    bool validate_text();

    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }

    // Raised for each refused input, before the edit returns.
    [[nodiscard]] Event<const MaskInputRejection&>& mask_input_rejected() noexcept {
        return mask_input_rejected_;
    }
    // Raised after the text (in text_mask_format) changes.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }
    [[nodiscard]] Event<const std::string&>& mask_changed() noexcept { return mask_changed_; }
    [[nodiscard]] Event<bool>& overwrite_mode_changed() noexcept { return overwrite_mode_changed_; }
    [[nodiscard]] Event<const MaskValidation&>& type_validation_completed() noexcept {
        return type_validation_completed_;
    }

    void arrange(Rect final_bounds) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    // Forwarded to the editor while it shows the published value (prompts
    // and literals in place), so offsets index that value; no_text otherwise.
    [[nodiscard]] SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout) override;
    [[nodiscard]] SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry field;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool read_only{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    struct EditorChangeCallback final {
        std::weak_ptr<MaskedTextBox> target;
        void operator()(const std::string& text) const;
    };

    // The editor's edit filter: every user edit becomes a provider edit.
    void filter_edit(TextEdit& edit);
    void on_validating(ControlValidationEvent& event);
    void filter_typing(TextEdit& edit, MaskPosition start, MaskPosition end);
    void filter_deletion(TextEdit& edit, MaskPosition start, MaskPosition end);
    void filter_paste(TextEdit& edit, MaskPosition start, MaskPosition end);
    void filter_value(TextEdit& edit);
    [[nodiscard]] MaskValidation run_validation();
    void reject(MaskEditResult result, std::string_view text);
    // Writes the candidate's display text and caret into `edit` as a rewrite
    // and keeps the candidate until the editor reports the new text.
    void propose(TextEdit& edit, MaskedTextProvider candidate, MaskPosition caret);
    // Where the caret goes after input that ended at `last`: past it, and on
    // past literals to the next edit position.
    [[nodiscard]] MaskPosition caret_after(const MaskedTextProvider& candidate, MaskPosition last) const;
    // Shows the provider's display text in the editor (program changes).
    void show_provider();
    // Puts the editor's caret on the first unassigned edit position, or at
    // the end when every position is assigned.
    void place_caret();
    void editor_text_changed(const std::string& display);
    // Rebuilds the provider from the editor's display text (after undo or
    // redo, which the filter does not see).
    void adopt_display_text(std::string_view display);
    // Whether the editor's text is the value the descriptor publishes.
    [[nodiscard]] bool editor_shows_published_value() const;
    void publish_text_if_changed();
    // A provider with the given mask and culture and this box's settings.
    [[nodiscard]] MaskedTextProvider configured_provider(std::string next_mask, MaskCulture next_culture) const;
    void replace_provider(MaskedTextProvider next);

    MaskedTextProvider provider_;
    // A filter's rewrite waiting for the editor to apply it.
    std::optional<MaskedTextProvider> pending_;
    std::shared_ptr<TextBox> editor_;
    SubscriptionToken editor_change_;
    SubscriptionToken validating_;
    Delegate<MaskValidation&> validator_{};
    bool synchronizing_{};
    // The text in text_mask_format as last published by text_changed.
    std::string published_text_;
    Event<const MaskInputRejection&> mask_input_rejected_;
    Event<const std::string&> text_changed_;
    Event<const std::string&> mask_changed_;
    Event<bool> overwrite_mode_changed_;
    Event<const MaskValidation&> type_validation_completed_;
    // property_codemod begin MaskedTextBox (tools/property_families/masked_text_box.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_mask_changed();
    virtual void on_read_only_changed();

private:
    friend struct MaskedTextBoxMaskCapability<MaskedTextBox>;
    friend struct MaskedTextBoxCultureCapability<MaskedTextBox>;
    friend struct MaskedTextBoxTextMaskFormatCapability<MaskedTextBox>;
    friend struct MaskedTextBoxPromptCharacterCapability<MaskedTextBox>;
    friend struct MaskedTextBoxAllowPromptAsInputCapability<MaskedTextBox>;
    friend struct MaskedTextBoxAsciiOnlyCapability<MaskedTextBox>;
    friend struct MaskedTextBoxSkipLiteralsCapability<MaskedTextBox>;
    friend struct MaskedTextBoxResetOnPromptCapability<MaskedTextBox>;
    friend struct MaskedTextBoxResetOnSpaceCapability<MaskedTextBox>;
    friend struct MaskedTextBoxRejectInputOnFirstFailureCapability<MaskedTextBox>;
    friend struct MaskedTextBoxOverwriteModeCapability<MaskedTextBox>;
    friend struct MaskedTextBoxReadOnlyCapability<MaskedTextBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(mask)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(mask)& cell) const;
    void write_view(const decltype(culture)& cell, MaskCulture incoming);
    [[nodiscard]] MaskCulture read_view(const decltype(culture)& cell) const;
    void prepare_write(const decltype(text_mask_format)& cell, MaskTextFormat& incoming);
    void written(const decltype(text_mask_format)& cell);
    void write_view(const decltype(prompt_character)& cell, char32_t incoming);
    [[nodiscard]] char32_t read_view(const decltype(prompt_character)& cell) const;
    void write_view(const decltype(allow_prompt_as_input)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(allow_prompt_as_input)& cell) const;
    void write_view(const decltype(ascii_only)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(ascii_only)& cell) const;
    void write_view(const decltype(skip_literals)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(skip_literals)& cell) const;
    void write_view(const decltype(reset_on_prompt)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(reset_on_prompt)& cell) const;
    void write_view(const decltype(reset_on_space)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(reset_on_space)& cell) const;
    void prepare_write(const decltype(reject_input_on_first_failure)& cell, bool& incoming);
    void written(const decltype(reject_input_on_first_failure)& cell);
    void prepare_write(const decltype(overwrite_mode)& cell, bool& incoming);
    void written(const decltype(overwrite_mode)& cell);
    void prepare_write(const decltype(read_only)& cell, bool& incoming);
    void written(const decltype(read_only)& cell);
    // property_codemod end MaskedTextBox
};

} // namespace gui_forms
