#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/window.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace gui_forms::detail {

// Cut-off item text revealed in place (owner decision, 2026-10-28): when
// the pointer is over a list row or tree node whose text its cell cuts
// off, the whole text is shown over the row, at the item's own position,
// as wide as it needs. This is not a tooltip: there is nothing for the
// program to set, it has no presenter content, and the tooltip switches do
// not apply. It ignores the pointer, so the row under it keeps the hover
// and the clicks.

// The reveal painted as the owner's parts: a face of `family` in `states`,
// then the text as its content part.
struct RevealPaint final {
    ControlVisualRole family{ControlVisualRole::selection};
    AnatomyStateSet states{};
};

// One cut-off text, in window coordinates. `key` names the item (and
// column) it belongs to: a reveal with the key of the one showing is the
// same reveal. `row` is the item's face, whose top and height the reveal
// takes; `cell` is where the owner drew the text, cut off; the text is
// placed in it as a content part places its text.
struct CutOffText final {
    std::string key{};
    Rect row{};
    Rect cell{};
    std::string text{};
    FontSpec font{};
    RevealPaint paint{};
};

class CutOffTextFace;

// Shows at most one reveal for its owner control, in an overlay layer of the
// owner's window. The owner keeps it as a member and calls `reveal` when
// the pointer reaches a new item and `conceal` when it leaves, presses,
// scrolls or the items change. Closing the owner's window or hiding or
// disposing the owner revokes the overlay; the revealer then reveals
// nothing until the next `reveal`. UI thread only.
class CutOffTextRevealer final {
public:
    CutOffTextRevealer() = default;
    ~CutOffTextRevealer();
    CutOffTextRevealer(const CutOffTextRevealer&) = delete;
    CutOffTextRevealer& operator=(const CutOffTextRevealer&) = delete;

    // Reveals `text` over its row when its cell cuts it off, replacing any
    // other reveal; conceals and returns false when the text fits, the
    // owner is not attached to a window or the window refuses the overlay.
    bool reveal(const std::shared_ptr<Control>& owner, CutOffText text);
    void conceal() noexcept;
    [[nodiscard]] bool revealing() const noexcept;
    // The key of the reveal showing, or empty.
    [[nodiscard]] std::string_view key() const noexcept;
    // The revealed face's bounds in window coordinates, or empty.
    [[nodiscard]] Rect bounds() const noexcept;

private:
    std::shared_ptr<Control> layer_{};
    std::shared_ptr<CutOffTextFace> face_{};
    OverlayToken token_{};
    std::string key_{};
    Rect bounds_{};
};

} // namespace gui_forms::detail
