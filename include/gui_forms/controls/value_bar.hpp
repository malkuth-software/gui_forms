#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/range_control.hpp"
#include "gui_forms/controls/value_bar_values.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/value_bar_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class ValueBarScrub;

// The two halves of a ValueBar's label for its contrast check (the value
// bar study, section 3.6): the ratio of the label's text over the track and
// of its fill text over the fill, against `minimum` (4.5:1, 7:1 in high
// contrast). A ratio of 0 was not measured (a face holding an image).
struct GUI_FORMS_API ValueBarContrast final {
    double over_track{};
    double over_fill{};
    double minimum{};
    bool passes{};
};

// A Blender-style value bar (the node engine and value bar study, section
// 3; behaviour from Blender's user manual only, clean-room): one number in
// the range family, shown as a fill (the slider form) or a field (the
// number form), changed by scrubbing, typing and stepping.
//
// Ranges: minimum and maximum (the range family's) are the hard limits,
// soft_minimum and soft_maximum the scrub and fill range (none: the hard
// limits; a soft limit outside the hard range is held at it). Scrubbing
// stays within the soft range; typed values may leave it but never the
// hard range, and the fill then shows full or empty with an overrange
// marker at that end.
//
// Gestures (study 3.4): press and drag sideways scrubs (the shared
// ScrubGesture on the window's drag engine): Shift slows it, Ctrl snaps to
// snap_step, Escape or a right press restores the start value, and the
// release raises value_committed once. A click without a drag, Enter or F2
// opens the inline editor with the value selected; Enter, Tab or focus
// leaving it commits the typed text (bounded arithmetic and units, clamped
// to the hard range), Escape cancels, and a refused text keeps the editor
// open with the defect on the validation part; focus leaving it with a
// refused text cancels the edit. Tab commits and keeps the focus on the
// bar. In the field style the
// arrows at both ends step by the small change. Focused: Left/Right and
// Down/Up step (Shift a tenth, Ctrl the large change), Page Up/Down by the
// large change, Home and End go to the soft limits, Backspace resets to
// default_value, Minus negates, Ctrl+C and Ctrl+V copy and paste the value
// as text. Ctrl+wheel steps while hovered; a plain wheel is left to the
// parent, so a bar never steals scrolling. The begin_keyboard_drag rule
// action scrubs from the keyboard.
//
// Events: value_changed (the range family's) follows every change,
// scrub previews included; value_committed follows each completed user
// edit (a scrub's release, a typed commit, a step, a reset, a negation, a
// paste), never a programmatic write, a preview or a restore.
//
// Semantics (study 3.7): the fill style publishes a slider and the field
// style a numeric field (a spin button), each with its range and the value
// text formatted_value() gives; the inline editor publishes as an edit
// while it is open.
//
// Theme (study 3.6): parts track, fill, label, value, the arrows and
// their glyphs, overrange, validation and focus. The label and value text
// are painted twice, clipped: over the track in their own colour, over the
// fill in the label part's fill-text-color (else the fill's selected text
// colour), so the text stays readable where the fill crosses a glyph.
class GUI_FORMS_API ValueBar final : public RangeControl, public ValueBarCapabilities<ValueBar> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit ValueBar(StableId stable_id);
    ~ValueBar() override;
    void initialize_control_tree();

    // The value as shown: in the unit's display unit at the precision.
    [[nodiscard]] std::string formatted_value() const;
    // The soft range in force: the soft limits (else the hard ones), held
    // within the hard range and ordered.
    [[nodiscard]] double effective_soft_minimum() const noexcept;
    [[nodiscard]] double effective_soft_maximum() const noexcept;
    // The value's proportion of the soft range, 0..1 (the fill's width).
    [[nodiscard]] double soft_proportion() const noexcept;
    // Whether the value lies outside the soft range.
    [[nodiscard]] bool overrange() const noexcept;

    // The inline editor (study 3.5). begin_edit opens it with the
    // formatted value selected; false while disabled or scrubbing.
    // commit_edit parses its text: accepted, the value (clamped to the hard
    // range) is applied, value_committed raised and the editor closed;
    // refused, the editor stays open and edit_defect() says why.
    // cancel_edit closes it, changing nothing.
    bool begin_edit();
    bool commit_edit();
    void cancel_edit();
    [[nodiscard]] bool editing() const noexcept { return editing_; }
    [[nodiscard]] ValueTextDefect edit_defect() const noexcept { return edit_defect_; }
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }
    // Applies `text` as typed (paste and the semantic set_value use it):
    // the defect, or none when it was applied (clamped to the hard range).
    ValueTextDefect apply_text(std::string_view text);

    // The user's edits, each raising value_committed when the value
    // changed: a step of `delta` (clamped to the hard range), the reset to
    // default_value, and the negation (within the hard range).
    bool step_by(double delta);
    bool reset_to_default();
    bool negate();
    // The clipboard (the window's host services): false without them.
    bool copy_value();
    bool paste_value();

    // Whether a scrub is running (past its threshold, or by keyboard).
    [[nodiscard]] bool scrubbing() const noexcept;
    // The contrast of the label's two halves in the current theme and
    // state (study 3.6).
    [[nodiscard]] ValueBarContrast fill_text_contrast() const;

    [[nodiscard]] Event<double>& value_committed() noexcept { return value_committed_; }

    // Public part geometry in local coordinates.
    [[nodiscard]] Rect track_bounds() const noexcept;
    // The fill: the track's start to the value's proportion; empty in the
    // field style.
    [[nodiscard]] Rect fill_bounds() const noexcept;
    // The text area between the arrows (field style) or the track's
    // padding (fill style); the editor lies here while open.
    [[nodiscard]] Rect content_bounds() const noexcept;
    // The arrows, at both ends in the field style; empty in the fill style.
    [[nodiscard]] Rect decrement_bounds() const noexcept;
    [[nodiscard]] Rect increment_bounds() const noexcept;
    // The overrange marker at the end the value passed; empty within.
    [[nodiscard]] Rect overrange_bounds() const noexcept;

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // The anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry track;
        PartGeometry fill;
        PartGeometry label;
        PartGeometry value;
        PartGeometry decrement_arrow;
        PartGeometry increment_arrow;
        PartGeometry decrement_glyph;
        PartGeometry increment_glyph;
        PartGeometry overrange;
        PartGeometry validation;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool invalid{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    bool begin_keyboard_drag() override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value_text) override;

protected:
    void on_key_down(KeyEventArgs& arguments) override;

    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;

    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    friend class ValueBarScrub;

    struct EditorValidatingCallback final {
        std::weak_ptr<ValueBar> target;
        void operator()(ControlValidationEvent& event) const;
    };

    // Applies a user edit's value (clamped to the hard range) and raises
    // value_committed when it changed.
    bool commit_value(double requested, RangeAction action);
    // commit_edit, returning the focus to the bar when `restore_focus` (not
    // while focus is already moving elsewhere).
    bool commit_editor_text(bool restore_focus);
    void close_editor(bool restore_focus);
    // The step a key or the wheel takes under `modifiers`: the small
    // change, a tenth of it with Shift, the large change with Ctrl.
    [[nodiscard]] double key_step(Modifier modifiers) const noexcept;
    [[nodiscard]] double scrub_rate_in_force() const noexcept;
    // The label and value text; `over_fill` is the colour of the half drawn
    // over the fill, none for the half over the track.
    void paint_texts(Painter& painter, AnatomyStateSet states, std::optional<Color> over_fill) const;
    // The text colour over the fill: the label part's fill-text-color, else
    // the fill's selected text colour.
    [[nodiscard]] Color over_fill_text_colour(AnatomyStateSet states) const;

    std::shared_ptr<TextBox> editor_{};
    SubscriptionToken editor_validating_{};
    bool editing_{};
    // Set while close_editor hides the editor, so the focus change it
    // causes does not commit again.
    bool closing_{};
    bool focused_{};
    // The editor's text when opened: committed unchanged, it leaves the
    // value as it was (not rounded to the shown precision).
    std::string opened_text_{};
    ValueTextDefect edit_defect_{ValueTextDefect::none};
    // The drag engine's client and the scrub's target (value_bar.cpp).
    std::unique_ptr<ValueBarScrub> scrub_{};
    Event<double> value_committed_{};
    // property_codemod begin ValueBar (tools/property_families/value_bar.json; generated, do not edit)
public:
    // ValueBar's property table: RangeControl's entries, then its own.
    static constexpr std::size_t value_bar_property_count = RangeControl::range_control_property_count + 9U;
    [[nodiscard]] static const std::array<PropertyEntry, value_bar_property_count>& value_bar_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    // The mouse raisers' handling, by arguments.action.
    void handle_mouse(MouseEventArgs& arguments);
    friend struct ValueBarSoftMinimumCapability<ValueBar>;
    friend struct ValueBarSoftMaximumCapability<ValueBar>;
    friend struct ValueBarSnapStepCapability<ValueBar>;
    friend struct ValueBarPrecisionCapability<ValueBar>;
    friend struct ValueBarUnitCapability<ValueBar>;
    friend struct ValueBarDefaultValueCapability<ValueBar>;
    friend struct ValueBarPresentationCapability<ValueBar>;
    friend struct ValueBarLabelCapability<ValueBar>;
    friend struct ValueBarScrubRateCapability<ValueBar>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(soft_minimum)& cell, std::optional<double>& incoming);
    void written(const decltype(soft_minimum)& cell);
    void prepare_write(const decltype(soft_maximum)& cell, std::optional<double>& incoming);
    void written(const decltype(soft_maximum)& cell);
    void prepare_write(const decltype(snap_step)& cell, std::optional<double>& incoming);
    void written(const decltype(snap_step)& cell);
    void prepare_write(const decltype(precision)& cell, std::int32_t& incoming);
    void written(const decltype(precision)& cell);
    void prepare_write(const decltype(unit)& cell, ValueUnit& incoming);
    void written(const decltype(unit)& cell);
    void prepare_write(const decltype(default_value)& cell, double& incoming);
    void written(const decltype(default_value)& cell);
    void prepare_write(const decltype(presentation)& cell, ValueBarStyle& incoming);
    void written(const decltype(presentation)& cell);
    void prepare_write(const decltype(label)& cell, std::string& incoming);
    void written(const decltype(label)& cell);
    void prepare_write(const decltype(scrub_rate)& cell, std::optional<double>& incoming);
    void written(const decltype(scrub_rate)& cell);
    // property_codemod end ValueBar
};

} // namespace gui_forms
