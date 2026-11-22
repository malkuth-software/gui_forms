#pragma once
// PopupContainerBox: a field whose drop-down is a control the application
// gives it (`popup_content`), such as a calendar, a tree of folders or a
// small form, shown in an anchored overlay below the field (above it when
// there is no room below). Windows Forms has no counterpart; the field looks
// like a drop-down-list ComboBox: its text, set by the application, and a
// drop-down button.
//
// A click on the field, F4, Alt+Down or Alt+Up, or the semantic expand
// opens the popup, and the content takes the keyboard focus when it can.
// The application ends it with close_popup, as when the content's choice is
// made (accepted) or abandoned (cancelled); a click away or Escape closes it
// as cancelled. popup_closed reports how, and the application writes the
// field's text from the content then. Focus inside the popup returns to the
// field when it closes.
//
// The content is the application's control, held while it is set and
// lent to the overlay while the popup is open; it stays alive and keeps its
// state between openings.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/popup_container_box_capabilities.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class AnchoredOverlayLayer;
enum class OverlayDismissReason : std::uint8_t;

// How a PopupContainerBox's popup ended.
enum class PopupCloseReason : std::uint8_t {
    accepted,    // close_popup(accepted): the content's choice was made
    cancelled,   // close_popup(cancelled), or the semantic collapse
    click_away,  // a press outside the popup and the field
    escape,      // Escape in the popup
};

// A popup that closed, and how.
struct GUI_FORMS_API PopupClosed final {
    PopupCloseReason reason{PopupCloseReason::cancelled};
    [[nodiscard]] bool accepted() const noexcept { return reason == PopupCloseReason::accepted; }
};

class GUI_FORMS_API PopupContainerBox final : public Control, public PopupContainerBoxCapabilities<PopupContainerBox> {
public:
    explicit PopupContainerBox(StableId stable_id);
    ~PopupContainerBox() override;

    // -- content -------------------------------------------------------------------
    [[nodiscard]] Control::Ptr popup_content() const noexcept { return content_; }
    // Sets the control the popup shows, or none. Throws std::logic_error
    // while the popup is open and std::invalid_argument for a control that
    // that has a parent, is in a window, or is the field itself.
    void set_popup_content(Control::Ptr content);
    // The popup's size: zero on an axis takes the content's measured size
    // there (and at least the field's width). Throws std::invalid_argument
    // for a negative or non-finite size, or one past maximum_popup_extent.
    [[nodiscard]] Size popup_size() const noexcept { return popup_size_; }
    void set_popup_size(Size size);
    // Development bound, not measured capacity.
    static constexpr double maximum_popup_extent = 4096.0;

    // -- popup ---------------------------------------------------------------------
    [[nodiscard]] bool popup_shown() const noexcept { return shown_; }
    // Opens the popup below the field. False when it is already open, the
    // field is not in a window or not enabled, or there is no content.
    bool show_popup();
    // Closes an open popup and raises popup_closed with `reason`; false
    // when it is not open.
    bool close_popup(PopupCloseReason reason);

    // Raised after the popup opens.
    [[nodiscard]] Event<>& popup_opened() noexcept { return popup_opened_; }
    // Raised after the popup closes.
    [[nodiscard]] Event<const PopupClosed&>& popup_closed() noexcept { return popup_closed_; }
    // Raised after the text changes, with the new text.
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    // Stage T1 anatomy, as dot members (see Control::Parts). `well` is the
    // field's ground, `label` its text, `actuator` the drop-down button and
    // `glyph` its arrow; the field is `open` while the popup is.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry label;
        PartGeometry actuator;
        PartGeometry glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;

private:
    struct Placement final {
        Rect well{};
        Rect label{};
        Rect actuator{};
        Rect glyph{};
    };
    [[nodiscard]] Placement placement() const;
    [[nodiscard]] FontSpec field_font() const noexcept;
    [[nodiscard]] bool focus_within_popup() const;
    void on_dismiss(OverlayDismissReason reason);
    void on_overlay_revoked();
    // Gives the content back and drops the overlay; raises nothing.
    void release_overlay() noexcept;

    Control::Ptr content_{};
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    OverlayToken overlay_{};
    SubscriptionToken overlay_revoked_{};
    SubscriptionToken dismissal_{};
    Size popup_size_{};
    Event<> popup_opened_;
    Event<const PopupClosed&> popup_closed_;
    Event<const std::string&> text_changed_;
    bool shown_{};
    bool hot_{};
    bool focused_{};
    // property_codemod begin PopupContainerBox (tools/property_families/popup_container_box.json; generated, do not edit)
public:
    // PopupContainerBox's property table: Control's entries, then its own.
    static constexpr std::size_t popup_container_box_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, popup_container_box_property_count>& popup_container_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event (the class is final: nothing overrides it in
    // C++). Only the properties with a Windows Forms On<Property>Changed
    // have one.
    void on_text_changed();

private:
    friend struct PopupContainerBoxTextCapability<PopupContainerBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    // property_codemod end PopupContainerBox
};

} // namespace gui_forms
