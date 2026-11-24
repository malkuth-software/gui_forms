#include "gui_forms/controls/dialog_presentation.hpp"

#include "provisional_child.hpp"

#include "gui_forms/window.hpp"

#include <utility>

namespace gui_forms {

// -- ProvisionalChild ------------------------------------------------------------

ProvisionalChild::ProvisionalChild(Control::Ptr parent, Control::Ptr child)
    : parent_(std::move(parent)), child_(std::move(child)) {
    (*parent_).add_child(child_);
}

ProvisionalChild::~ProvisionalChild() {
    undo();
}

void ProvisionalChild::commit() noexcept {
    settled_ = true;
}

void ProvisionalChild::undo() noexcept {
    if (settled_) return;
    settled_ = true;
    try {
        static_cast<void>((*parent_).remove_child((*child_).runtime_id()));
    } catch (...) {
        // Unwinding must not throw; a child that cannot be removed stays
        // with a parent that is released with this guard.
    }
}

// -- DialogPresentation ----------------------------------------------------------

DialogPresentation::DialogPresentation() noexcept : state_(Closed{}) {}

DialogPresentation::DialogPresentation(State state) noexcept : state_(std::move(state)) {}

DialogPresentation::~DialogPresentation() {
    abandon();
}

DialogPresentation::DialogPresentation(DialogPresentation&& other) noexcept : state_(std::move(other.state_)) {
    other.state_ = Closed{};
}

DialogPresentation& DialogPresentation::operator=(DialogPresentation&& other) noexcept {
    if (this != &other) {
        abandon();
        state_ = std::move(other.state_);
        other.state_ = Closed{};
    }
    return *this;
}

DialogPresentation DialogPresentation::closed() noexcept {
    DialogPresentation result{State{Closed{}}};
    return result;
}

DialogPresentation DialogPresentation::overlay(PresentationLayer owner) noexcept {
    Overlay shown{};
    shown.owner = std::move(owner);
    DialogPresentation result{State{std::move(shown)}};
    return result;
}

DialogPresentation DialogPresentation::native(PresentationLayer owner, PresentationPopup popup) noexcept {
    Native shown{};
    shown.owner = std::move(owner);
    shown.popup = std::move(popup);
    DialogPresentation result{State{std::move(shown)}};
    return result;
}

PresentationLayer* DialogPresentation::owner_half() noexcept {
    if (Overlay* const shown = std::get_if<Overlay>(&state_)) return &(*shown).owner;
    if (Native* const shown = std::get_if<Native>(&state_)) return &(*shown).owner;
    return nullptr;
}

const PresentationLayer* DialogPresentation::owner_half() const noexcept {
    if (const Overlay* const shown = std::get_if<Overlay>(&state_)) return &(*shown).owner;
    if (const Native* const shown = std::get_if<Native>(&state_)) return &(*shown).owner;
    return nullptr;
}

PresentationPopup* DialogPresentation::popup_half() noexcept {
    if (Native* const shown = std::get_if<Native>(&state_)) return &(*shown).popup;
    return nullptr;
}

bool DialogPresentation::shown() const noexcept {
    const bool result = !std::holds_alternative<Closed>(state_);
    return result;
}

NativePopupSurface DialogPresentation::surface() const noexcept {
    if (std::holds_alternative<Native>(state_)) return NativePopupSurface::native;
    if (std::holds_alternative<Overlay>(state_)) return NativePopupSurface::overlay;
    return NativePopupSurface::none;
}

Control::Ptr DialogPresentation::layer() const noexcept {
    const PresentationLayer* const owner = owner_half();
    if (owner == nullptr) return Control::Ptr{};
    return (*owner).layer;
}

bool DialogPresentation::shown_with(const Control& layer) const noexcept {
    const PresentationLayer* const owner = owner_half();
    const bool result = owner != nullptr && (*owner).layer.get() == &layer;
    return result;
}

Window* DialogPresentation::popup_window() const noexcept {
    const Native* const shown = std::get_if<Native>(&state_);
    if (shown == nullptr) return nullptr;
    Window* const result = (*shown).popup.popup.window();
    return result;
}

Control::Ptr DialogPresentation::popup_root() const noexcept {
    const Native* const shown = std::get_if<Native>(&state_);
    if (shown == nullptr) return Control::Ptr{};
    return (*shown).popup.root;
}

bool DialogPresentation::adopt_scope(const Control& layer, FocusScopeId scope) noexcept {
    if (!shown_with(layer)) return false;
    PresentationLayer* const owner = owner_half();
    (*owner).scope = scope;
    return true;
}

DialogPresentation DialogPresentation::take() noexcept {
    DialogPresentation taken{std::move(state_)};
    state_ = Closed{};
    return taken;
}

// The steps run in the order the dialogs used before the state was one
// record: subscriptions first, so the leases' own closings are not heard;
// the scope while the layer is still in its window; the tree; then the
// popup's lease before the owner's, since the popup is owned by the
// owner's window.
void DialogPresentation::dismantle(Control& presented, FocusScopeCloseReason reason) {
    PresentationLayer* const owner = owner_half();
    if (owner == nullptr) return;
    PresentationPopup* const popup = popup_half();
    (*owner).overlay_closed.disconnect();
    if (popup != nullptr) (*popup).popup_closed.disconnect();
    // The scope is the owner's window's, where the layer is.
    Window* const host = (*owner).layer ? (*(*owner).layer).attached_window() : nullptr;
    if (host != nullptr && (*owner).scope) static_cast<void>((*host).end_focus_scope((*owner).scope, reason));
    (*owner).scope = {};
    if ((*owner).layer) static_cast<void>((*(*owner).layer).remove_child(presented.runtime_id()));
    if (popup != nullptr) {
        const Control::Ptr root = std::move((*popup).root);
        if (root) static_cast<void>((*root).remove_child(presented.runtime_id()));
        (*popup).popup.disconnect();
    }
    (*owner).overlay.disconnect();
    (*owner).layer.reset();
    state_ = Closed{};
}

void DialogPresentation::dismantle_on_dispose() noexcept {
    PresentationLayer* const owner = owner_half();
    if (owner == nullptr) return;
    (*owner).overlay_closed.disconnect();
    PresentationPopup* const popup = popup_half();
    if (popup != nullptr) (*popup).popup_closed.disconnect();
    Window* const host = (*owner).layer ? (*(*owner).layer).attached_window() : nullptr;
    if ((*owner).scope && host != nullptr) {
        try {
            static_cast<void>((*host).end_focus_scope((*owner).scope));
        } catch (...) {
            // Disposal must not throw; the window ends the scope with the overlay.
        }
    }
    (*owner).scope = {};
    abandon();
}

void DialogPresentation::abandon() noexcept {
    PresentationLayer* const owner = owner_half();
    if (owner == nullptr) return;
    PresentationPopup* const popup = popup_half();
    (*owner).overlay_closed.disconnect();
    if (popup != nullptr) {
        (*popup).popup_closed.disconnect();
        (*popup).popup.disconnect();
        (*popup).root.reset();
    }
    (*owner).overlay.disconnect();
    (*owner).layer.reset();
    state_ = Closed{};
}

} // namespace gui_forms
