#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"

#include <variant>

namespace gui_forms {

// The owner's half of a shown owned dialog or callout (FontDialog,
// TaskDialog, Callout): the layer in the owner's window that holds the
// presented control (or, for a native presentation, holds the owner's input
// while the panel is elsewhere), the overlay lease that keeps the layer in
// that window, the subscription to the lease's revocation, and the focus
// scope begun in that window. The scope is empty from the moment the
// presentation is installed until begin_focus_scope returns
// (DialogPresentation::adopt_scope), because beginning it runs focus
// handlers that may close the presentation again.
struct GUI_FORMS_API PresentationLayer final {
    Control::Ptr layer{};
    OverlayToken overlay{};
    SubscriptionToken overlay_closed{};
    FocusScopeId scope{};
};

// The native half of a dialog presented in a native popup window
// (DialogSurface::native_popup): the root in the popup window that holds the
// panel, the popup's lease, and the subscription to the popup's closing. The
// focus scope inside the popup window ends with that window, so it is not
// kept.
struct GUI_FORMS_API PresentationPopup final {
    Control::Ptr root{};
    NativePopupToken popup{};
    SubscriptionToken popup_closed{};
};

// Where a presented control is: closed, shown as an overlay in its owner's
// window, or shown in a native popup window with its layer still in the
// owner's window. One record holds every lease, subscription and scope a
// state needs, so a shown state without its layer, or a popup root without
// its popup token, cannot be represented; every query reads the state.
//
// Closing is two steps, so that no handler sees a half-closed control:
// take() leaves the record closed at once and hands back what it held, and
// the caller dismantles that (dismantle, or abandon where nothing may run).
// Handlers that run while it is dismantled (a focus handler, the lease's
// own closing) find the control closed already. UI thread only.
class GUI_FORMS_API DialogPresentation final {
public:
    DialogPresentation() noexcept;
    ~DialogPresentation();
    DialogPresentation(DialogPresentation&& other) noexcept;
    DialogPresentation& operator=(DialogPresentation&& other) noexcept;
    DialogPresentation(const DialogPresentation&) = delete;
    DialogPresentation& operator=(const DialogPresentation&) = delete;

    // The named states.
    [[nodiscard]] static DialogPresentation closed() noexcept;
    // Shown as an overlay: the control is in the owner half's layer. The
    // layer must be set.
    [[nodiscard]] static DialogPresentation overlay(PresentationLayer owner) noexcept;
    // Shown natively: the control is in the popup half's root, and the
    // owner half's layer stays in the owner's window. Both must be set.
    [[nodiscard]] static DialogPresentation native(PresentationLayer owner, PresentationPopup popup) noexcept;

    // Whether it is shown (as an overlay or natively).
    [[nodiscard]] bool shown() const noexcept;
    // native, overlay, or none while closed.
    [[nodiscard]] NativePopupSurface surface() const noexcept;
    // The layer in the owner's window; null while closed.
    [[nodiscard]] Control::Ptr layer() const noexcept;
    // Whether it is shown with `layer`, the layer one show() made: false
    // once a handler closed it, even if it was shown again since.
    [[nodiscard]] bool shown_with(const Control& layer) const noexcept;
    // While shown natively, the popup window's model and the root in it that
    // holds the panel; null otherwise (and the window null once the popup
    // has closed).
    [[nodiscard]] Window* popup_window() const noexcept;
    [[nodiscard]] Control::Ptr popup_root() const noexcept;
    // Records the owner's focus scope once begin_focus_scope returns. False,
    // recording nothing, when the presentation is no longer the one shown
    // with `layer`: a handler run by begin_focus_scope closed it (and may
    // have shown it again with another layer); the caller then ends `scope`
    // itself.
    bool adopt_scope(const Control& layer, FocusScopeId scope) noexcept;

    // Leaves this record closed and returns what it held.
    [[nodiscard]] DialogPresentation take() noexcept;
    // Dismantles a taken record, leaving it closed: unsubscribes from the
    // lease's and the popup's closing, ends the owner's focus scope in the
    // layer's window with `reason`, takes `presented` out of the layer and
    // out of the popup root, closes the popup and then the overlay, and
    // drops the root and the layer. The caller keeps `presented` alive
    // across the call (removing it from its layer may drop its last other
    // reference). Nothing happens for a closed record.
    void dismantle(Control& presented, FocusScopeCloseReason reason);
    // The same for a disposed control: ends the scope where the window
    // accepts it (a refusal is left to the window, which ends the scope with
    // the overlay), leaves `presented`'s parent links to its disposal, and
    // never throws.
    void dismantle_on_dispose() noexcept;
    // Closes the leases and drops the subscriptions without ending the scope
    // or touching the tree: for a destructor, where there is no one left to
    // tell and the window ends the scope with the overlay.
    void abandon() noexcept;

private:
    struct Closed final {};
    struct Overlay final {
        PresentationLayer owner{};
    };
    struct Native final {
        PresentationLayer owner{};
        PresentationPopup popup{};
    };
    using State = std::variant<Closed, Overlay, Native>;

    explicit DialogPresentation(State state) noexcept;
    [[nodiscard]] PresentationLayer* owner_half() noexcept;
    [[nodiscard]] const PresentationLayer* owner_half() const noexcept;
    [[nodiscard]] PresentationPopup* popup_half() noexcept;

    State state_{};
};

} // namespace gui_forms
