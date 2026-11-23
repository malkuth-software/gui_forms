#pragma once
// ButtonTextBox: a text field with buttons in its frame, at its trailing end,
// its leading end or both, such as a path field with a browse button or a
// value field with a reset button. Windows Forms has no counterpart; the
// field is a TextBox (made after construction and owned as the only child)
// inside a well, as SearchField's is, and the buttons are records kept by
// stable ID, not child controls.
//
// A button is drawn as a mark (EditButtonKind: an ellipsis, a drop-down
// chevron, a close mark, a search mark) or as its caption. A click on an
// enabled button, F4 or Alt+Down in the field (the first enabled trailing
// button; the first enabled button when there is none at the trailing end),
// or the semantic press raises button_clicked with the button's ID. A
// button acts on release over it, as a Button does; the field keeps the
// keyboard focus.
//
// To assistive technology the field is one text box node whose value is
// the text (the editor is its implementation, as SearchField's is), with
// one button node per visible button, named by its caption and described
// by its description.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/button_text_box_capabilities.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <stxlib/optional.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

class TextBox;

// What a ButtonTextBox button shows.
enum class EditButtonKind : std::uint8_t {
    ellipsis,   // three dots: opens something to choose the value with
    drop_down,  // a downward chevron: opens a list or panel below the field
    close,      // a cross: clears or removes
    search,     // a magnifier: searches with the text
    caption,    // the button's caption, as text
};

// Which end of the field a button sits at; right to left, the ends swap.
enum class EditButtonPlacement : std::uint8_t {
    trailing,  // after the text: the right, left to right
    leading,   // before the text: the left, left to right
};

// One button. `stable_id` is unique within the field and names the button
// in button_clicked, the published anatomy and the semantics. `caption` is
// the text a caption button shows and every button's accessible name (a
// mark button without one is named after its kind); `description` is its
// accessible description. A hidden button takes no room; a disabled one is
// drawn dimmed and does not act.
struct GUI_FORMS_API EditButton final {
    std::string stable_id{};
    EditButtonKind kind{EditButtonKind::ellipsis};
    GUI_FORMS_LOCALIZABLE(caption) std::string caption{};
    GUI_FORMS_LOCALIZABLE(description) std::string description{};
    EditButtonPlacement placement{EditButtonPlacement::trailing};
    bool enabled{true};
    bool visible{true};
};

// A button that acted, by its ID.
struct GUI_FORMS_API EditButtonClick final {
    std::string button_id{};
};

class GUI_FORMS_API ButtonTextBox final : public Control, public ButtonTextBoxCapabilities<ButtonTextBox> {
public:
    // Development bound, not measured capacity.
    static constexpr std::size_t maximum_buttons = 16U;

    // The inner editor is made after construction (make_control calls
    // initialize_control_tree), since add_child needs a shared owner.
    static constexpr bool initialize_tree_after_construction = true;
    explicit ButtonTextBox(StableId stable_id);
    ~ButtonTextBox() override;
    // Makes the inner editor; make_control calls it once.
    void initialize_control_tree();

    // The inner editor, owned as a child.
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept;
    // Focus on the field goes to its editor (null without one). Defined in
    // the source: TextBox is only declared here.
    [[nodiscard]] Ptr focus_target() const override;

    // -- buttons -------------------------------------------------------------------
    [[nodiscard]] BorrowedSpan<EditButton> buttons() const& noexcept { return detail::lend_view(buttons_); }
    BorrowedSpan<EditButton> buttons() const&& = delete;
    // Replaces the buttons, in the order given at each end (the leading
    // ones from the outer edge in, the trailing ones from the text out).
    // Throws std::invalid_argument for an empty or duplicate ID or one that
    // breaks the StableId segment rule, invalid UTF-8, or more than
    // maximum_buttons buttons; the old buttons then stay.
    void set_buttons(std::vector<EditButton> buttons);
    // Enables or disables one button, or shows or hides it. Throws
    // std::out_of_range for an unknown button.
    void set_button_enabled(std::string_view button_id, bool enabled);
    void set_button_visible(std::string_view button_id, bool visible);
    // The position of the button with this ID, or none.
    [[nodiscard]] stxlib::Optional<std::size_t> index_of(std::string_view button_id) const;
    // The visible button under a point in local coordinates, if any.
    [[nodiscard]] stxlib::Optional<std::size_t> button_at(Point local) const;
    // Raises button_clicked for the button, as a click would (Windows
    // Forms' PerformClick). False, raising nothing, for a hidden or
    // disabled button, or a disabled field. Throws std::out_of_range for an
    // unknown button.
    bool perform_button_click(std::string_view button_id);

    // Raised after the text changes, with the new text.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept;
    // Raised when a button acts.
    [[nodiscard]] Event<const EditButtonClick&>& button_clicked() noexcept { return button_clicked_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] SemanticTextResult on_semantic_text_layout(SemanticTextLayout& layout) override;
    [[nodiscard]] SemanticTextResult on_set_semantic_text_selection(SemanticTextSelection selection) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members (see Control::Parts). `well` is the
    // field's ground, `content` the inner editor's slot and `focus` the cue
    // shown while the editor has the keyboard focus.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry content;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The "button" item system: one visible button, its identity the
    // button's. `button` is its face, `button_glyph` its mark (absent for a
    // caption button) and `button_label` its caption (absent for a mark
    // button).
    struct GUI_FORMS_API ButtonParts final {
        PartGeometry button;
        PartGeometry button_glyph;
        PartGeometry button_label;
    };
    struct GUI_FORMS_API ButtonStates final {
        bool hot{};
        bool pressed{};
        bool disabled{};
    };
    struct GUI_FORMS_API RealizedButton final {
        std::size_t index{};
        std::string stable_id;
        ButtonParts parts;
        ButtonStates states;
    };
    // Every visible button, leading ones first.
    [[nodiscard]] std::vector<RealizedButton> realized_buttons() const;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    // A press on a button whose capture is taken away is forgotten, so a
    // later release over it does not act.
    void on_mouse_capture_changed() override;
    // F4 and Alt+Down from the editor act on the default button.
    void on_key_bubble(KeyEvent& event) override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;

private:
    // Defined in button_text_box.cpp.
    // Where everything sits for a size: the well, the editor's slot, and
    // each button's face (empty for a hidden one), by button position.
    struct Placement;
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] double face_width(const EditButton& button) const;
    [[nodiscard]] ButtonParts button_parts(const Placement& placed, std::size_t button) const;
    [[nodiscard]] AnatomyStateSet button_states(std::size_t button) const;
    [[nodiscard]] bool button_acts(std::size_t button) const noexcept;
    // The button F4 and Alt+Down act on, if any.
    [[nodiscard]] stxlib::Optional<std::size_t> default_button() const;
    [[nodiscard]] bool focus_visible() const noexcept;
    [[nodiscard]] stxlib::Optional<std::size_t> button_at_window(Point window_position) const;
    void raise_click(std::size_t button);
    void set_hot_button(stxlib::Optional<std::size_t> button);
    void editor_text_changed(const std::string& value);
    void editor_focus_changed(bool focused);

    // The private state, defined in button_text_box.cpp.
    struct State;
    [[nodiscard]] static stxlib::Unique<State> make_state();
    stxlib::Unique<State> state_{make_state()};
    SubscriptionToken editor_text_;
    SubscriptionToken editor_focus_;
    detail::LendingVector<EditButton> buttons_{};
    Event<const EditButtonClick&> button_clicked_;
    // The editor's natural height from the last measure.
    double editor_height_{};
    stxlib::Optional<std::size_t> hot_button_{};
    stxlib::Optional<std::size_t> pressed_button_{};
    bool editor_focused_{};
    // property_codemod begin ButtonTextBox (tools/property_families/button_text_box.json; generated, do not edit)
public:
    // ButtonTextBox's property table: Control's entries, then its own.
    static constexpr std::size_t button_text_box_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, button_text_box_property_count>& button_text_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event (the class is final: nothing overrides it in
    // C++). Only the properties with a Windows Forms On<Property>Changed
    // have one.
    void on_text_changed();

private:
    friend struct ButtonTextBoxTextCapability<ButtonTextBox>;
    friend struct ButtonTextBoxPlaceholderTextCapability<ButtonTextBox>;
    friend struct ButtonTextBoxReadOnlyCapability<ButtonTextBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(text)& cell) const;
    void write_view(const decltype(placeholder_text)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(placeholder_text)& cell) const;
    void write_view(const decltype(read_only)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(read_only)& cell) const;
    // property_codemod end ButtonTextBox
};

} // namespace gui_forms
