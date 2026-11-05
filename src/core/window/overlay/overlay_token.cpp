#include "overlay_attachment.hpp"

namespace gui_forms {

void OverlayToken::disconnect() noexcept {
    if (attachment_) {
        (*attachment_).disconnect();
        attachment_.reset();
    }
}

bool OverlayToken::connected() const noexcept {
    return attachment_ && (*attachment_).connected();
}

Event<>* OverlayToken::closed_event() noexcept {
    return attachment_ ? &(*attachment_).closed() : nullptr;
}

} // namespace gui_forms
