#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/task_dialog_capabilities.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Button;
class CheckBox;
class Label;
class ProgressBar;
class RadioButton;

// The standard icons of a task dialog page (Windows Forms' TaskDialogIcon).
// The vocabulary has no information, warning, error or shield mark yet; see
// TaskDialog's notes for how each is drawn.
enum class TaskDialogIcon : std::uint8_t { none, information, warning, error, shield };

// The standard buttons (Windows Forms' TaskDialogButton.OK and the rest).
// `none` marks a custom button.
enum class TaskDialogStandardButton : std::uint8_t {
    none,
    ok,
    cancel,
    yes,
    no,
    retry,
    abort,
    ignore,
    try_again,
    continue_,
    close,
    help,
};

// One command button. `stable_id` names the button in results and events
// and is unique within the page; a standard button with an empty ID takes
// its standard name ("ok", "cancel", "try_again", ...), and a standard button
// with empty text shows its standard text. A button whose `allow_close` is
// false raises button_clicked and leaves the dialog open.
struct TaskDialogButton final {
    std::string stable_id{};
    std::string text{};
    TaskDialogStandardButton standard{TaskDialogStandardButton::none};
    bool enabled{true};
    bool allow_close{true};
};

// One radio button; at most one of a page's radio buttons is checked.
struct TaskDialogRadioButton final {
    std::string stable_id{};
    std::string text{};
    bool checked{};
    bool enabled{true};
};

// Expanded information, shown below the text while expanded. The expander
// button shows one text or the other.
struct TaskDialogExpander final {
    std::string text{};
    std::string collapsed_button_text{"Show details"};
    std::string expanded_button_text{"Hide details"};
    bool expanded{};
};

// A progress bar below the text: a value between the bounds, or a marquee.
struct TaskDialogProgressBar final {
    double minimum{};
    double maximum{100.0};
    double value{};
    bool marquee{};
};

// One page (Windows Forms' TaskDialogPage). Empty strings leave their part
// out: no caption row, heading, text, verification check box or footnote.
// A page without buttons gets an OK button. `default_button` names the
// button Enter clicks and that takes focus first; empty names the first.
// With `allow_cancel`, Escape closes the dialog as its cancel button would.
struct TaskDialogPage final {
    std::string caption{};
    std::string heading{};
    std::string text{};
    TaskDialogIcon icon{TaskDialogIcon::none};
    // A custom icon image, shown instead of a standard icon when set.
    ImageId icon_image{};
    std::vector<TaskDialogButton> buttons{};
    std::string default_button{};
    std::vector<TaskDialogRadioButton> radio_buttons{};
    std::string verification_text{};
    bool verification_checked{};
    std::optional<TaskDialogExpander> expander{};
    std::string footnote{};
    TaskDialogIcon footnote_icon{TaskDialogIcon::none};
    std::optional<TaskDialogProgressBar> progress_bar{};
    bool allow_cancel{};
};

// A click on a button, before the dialog closes. Setting `keep_open` keeps
// the dialog open, as a button whose allow_close is false does.
struct TaskDialogButtonClick final {
    std::string button_id{};
    bool keep_open{};
};

// How a dialog closed: the button that closed it (the cancel button's ID, or
// "cancel", for Escape and a dialog revoked by its window), and the state of
// the verification check box and the radio buttons at that moment.
struct TaskDialogResult final {
    std::string button_id{};
    bool verification_checked{};
    std::string radio_button_id{};
};

// A message dialog with a heading, text, an icon, command buttons, radio
// buttons, a verification check box, expanded information, a footnote and a
// progress bar (Windows Forms' TaskDialog and TaskDialogPage). It is an
// owned, in-window dialog built from GUI.Forms controls, so it looks and
// behaves the same on every host: show() lays it over the owner's window in
// a full-client layer that takes every pointer press and holds keyboard focus
// in the dialog until it closes. GUI.Forms never runs a nested event loop, so
// show() returns at once and the clicked button arrives through `closed` and
// result(). set_page while shown navigates to another page.
class TaskDialog final : public Control, public TaskDialogCapabilities<TaskDialog> {
public:
    explicit TaskDialog(StableId stable_id);
    ~TaskDialog() override;

    [[nodiscard]] const TaskDialogPage& page() const noexcept { return page_; }
    // Replaces the page; while shown, this navigates and raises `navigated`.
    // Throws std::invalid_argument for invalid UTF-8, an empty or repeated
    // button or radio button ID, more than one checked radio button,
    // a default button the page does not have, or progress bounds that are
    // not finite and ordered.
    void set_page(TaskDialogPage page);

    // Shows the dialog over `owner`'s window. Returns false when it is
    // already shown or `owner` is not attached to a window.
    bool show(const Control::Ptr& owner);
    // How show() presents the panel (DialogSurface; an overlay by default).
    // With native_popup the panel opens in a native popup window owned by
    // the owner's window, centred over its client area, when the run can
    // open one; the full-client layer stays in the owner's window. Takes
    // effect at the next show().
    [[nodiscard]] DialogSurface surface() const noexcept { return surface_; }
    void set_surface(DialogSurface surface) noexcept { surface_ = surface; }
    // Where the shown panel went: native while it is in a popup window,
    // else overlay (or none when it is not shown).
    [[nodiscard]] NativePopupSurface presented_surface() const noexcept;
    [[nodiscard]] bool shown() const noexcept { return shown_; }
    // Closes the dialog as if `button_id` had been clicked, without raising
    // button_clicked. Does nothing when it is not shown.
    void close(std::string_view button_id);
    // The result of the last close, or none before the first.
    [[nodiscard]] const std::optional<TaskDialogResult>& result() const noexcept { return result_; }

    // The checked radio button's ID, or empty.
    [[nodiscard]] std::string checked_radio_button() const;
    // Moves the progress bar; the value is clamped to its bounds. Does
    // nothing for a page without one.
    void set_progress_value(double value);
    // Clicks a button by ID, as the user would. Returns false for an unknown
    // or disabled button.
    bool click(std::string_view button_id);

    [[nodiscard]] Event<TaskDialogButtonClick&>& button_clicked() noexcept { return button_clicked_; }
    [[nodiscard]] Event<const TaskDialogResult&>& closed() noexcept { return closed_; }
    [[nodiscard]] Event<>& navigated() noexcept { return navigated_; }
    [[nodiscard]] Event<bool>& expanded_changed() noexcept { return expanded_changed_; }
    [[nodiscard]] Event<bool>& verification_changed() noexcept { return verification_changed_; }
    [[nodiscard]] Event<const std::string&>& radio_button_changed() noexcept { return radio_button_changed_; }

    // The children for each part of the current page, or null.
    [[nodiscard]] std::shared_ptr<Button> button(std::string_view button_id) const;
    [[nodiscard]] std::shared_ptr<RadioButton> radio_button(std::string_view radio_id) const;
    [[nodiscard]] std::shared_ptr<CheckBox> verification_check_box() const noexcept { return verification_; }
    [[nodiscard]] std::shared_ptr<Button> expander_button() const noexcept { return expander_button_; }
    [[nodiscard]] std::shared_ptr<ProgressBar> progress_bar() const noexcept { return progress_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

    // Stage T1 anatomy, as dot members. Its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry caption_band;
        PartGeometry caption;
        PartGeometry icon_plate;
        PartGeometry icon_image;
        PartGeometry icon_glyph;
        PartGeometry icon_label;
        PartGeometry heading;
        PartGeometry text;
        PartGeometry progress;
        PartGeometry radio_group;
        PartGeometry expander_glyph;
        PartGeometry expander_button;
        PartGeometry expanded_text;
        PartGeometry verification;
        PartGeometry button_band;
        PartGeometry button_divider;
        PartGeometry button_row;
        PartGeometry footnote_band;
        PartGeometry footnote_plate;
        PartGeometry footnote_glyph;
        PartGeometry footnote_label;
        PartGeometry footnote;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Where every part and child sits, from one pass over a width.
    struct Layout final {
        Parts parts{};
        // Child slots, in the order of buttons_ and radios_.
        std::vector<Rect> buttons{};
        std::vector<Rect> radios{};
        double height{};
    };

    [[nodiscard]] Layout layout(double width) const;
    [[nodiscard]] double preferred_width(Size available) const noexcept;
    void rebuild_children();
    // Adds a word-wrapped, top-aligned Label child named <stable_id><suffix>
    // showing `text` in `font`: the heading, text, expanded text and
    // footnote paragraphs.
    [[nodiscard]] std::shared_ptr<Label> make_paragraph(std::string_view suffix, const std::string& text,
                                                        FontSpec font);
    void clear_children_of_page() noexcept;
    void on_child_button(ButtonBase& sender);
    void on_expander(ButtonBase& sender);
    void on_verification(bool checked);
    void on_radio(bool checked);
    void on_overlay_revoked();
    void finish(std::string button_id);
    [[nodiscard]] std::string cancel_button_id() const;
    [[nodiscard]] std::string default_button_id() const;
    [[nodiscard]] std::optional<std::size_t> button_index(std::string_view button_id) const noexcept;
    void paint_icon(Painter& painter, TaskDialogIcon icon, const PartGeometry& plate,
                    const PartGeometry& glyph, const PartGeometry& label) const;

    TaskDialogPage page_{};
    std::vector<std::shared_ptr<Button>> buttons_{};
    std::vector<std::shared_ptr<RadioButton>> radios_{};
    std::shared_ptr<Label> heading_{};
    std::shared_ptr<Label> text_{};
    std::shared_ptr<Label> expanded_text_{};
    std::shared_ptr<Label> footnote_{};
    std::shared_ptr<CheckBox> verification_{};
    std::shared_ptr<Button> expander_button_{};
    std::shared_ptr<ProgressBar> progress_{};
    std::vector<SubscriptionToken> child_tokens_{};
    // The modal layer the dialog sits in while shown, and its overlay lease
    // and focus scope in the owner's window.
    Control::Ptr layer_{};
    OverlayToken overlay_{};
    SubscriptionToken overlay_revoked_{};
    FocusScopeId scope_{};
    // The native popup window the panel is in (DialogSurface::native_popup):
    // its root holds the dialog, so closing detaches the dialog at once
    // even though the window itself is released later.
    DialogSurface surface_{DialogSurface::overlay};
    Control::Ptr native_root_{};
    NativePopupToken native_{};
    SubscriptionToken native_closed_{};
    // Moves the shown dialog from the layer into a native popup window;
    // false (the dialog back in the layer) when the run cannot open one.
    bool open_native(Window& host, const Control::Ptr& owner);
    bool shown_{};
    bool synchronizing_{};
    std::optional<TaskDialogResult> result_{};
    FontSpec font_{FontRole::content, 11.0, 400, false};
    Event<TaskDialogButtonClick&> button_clicked_{};
    Event<const TaskDialogResult&> closed_{};
    Event<> navigated_{};
    Event<bool> expanded_changed_{};
    Event<bool> verification_changed_{};
    Event<const std::string&> radio_button_changed_{};
    // property_codemod begin TaskDialog (tools/property_families/task_dialog.json; generated, do not edit)
private:
    friend struct TaskDialogExpandedCapability<TaskDialog>;
    friend struct TaskDialogVerificationCheckedCapability<TaskDialog>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(expanded)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(expanded)& cell) const;
    void write_view(const decltype(verification_checked)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(verification_checked)& cell) const;
    // property_codemod end TaskDialog
};

} // namespace gui_forms
