#pragma once

#include "gui_forms/basic_controls.hpp"

#include <cstdint>
#include <memory>

#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_placement/anchored_overlay_placement.hpp"

namespace gui_forms {
enum class OverlayDismissReason : std::uint8_t {
    click_away,
    escape_key,
};

// Full-client retained overlay for interactive overlays. The layer owns one
// ordinary retained content subtree, continuously re-resolves its anchored
// bounds on host resize, and reports (rather than privately acting on)
// click-away and Escape dismissal requests. Overlay lifetime and focus scope stay
// with Window, allowing product composites to implement staged Escape behavior
// without duplicating overlay hit testing or edge avoidance.
class AnchoredOverlayLayer final : public Panel {
public:
    AnchoredOverlayLayer(StableId stable_id, Control::Ptr anchor,
                       AnchoredOverlayPlacement placement = {});

    [[nodiscard]] Control::Ptr anchor() const noexcept { return anchor_.lock(); }
    void set_anchor(Control::Ptr anchor);
    [[nodiscard]] Control::Ptr content() const noexcept { return content_; }
    void set_content(Control::Ptr content);
    // Removes the content and releases the layer's reference to it, leaving
    // the layer empty. A control that owns its own layer (a drop-down that
    // is the layer's content) calls this when its overlay closes: otherwise
    // the owner and its layer would hold each other and never be freed.
    void clear_content();
    [[nodiscard]] AnchoredOverlayPlacement placement() const noexcept {
        return placement_;
    }
    void set_placement(AnchoredOverlayPlacement placement);
    [[nodiscard]] AnchoredOverlayPlacementResult resolved_placement() const noexcept {
        return resolved_;
    }
    [[nodiscard]] bool dismiss_on_click_away() const noexcept {
        return dismiss_on_click_away_;
    }
    void set_dismiss_on_click_away(bool enabled);
    [[nodiscard]] bool dismiss_on_escape() const noexcept {
        return dismiss_on_escape_;
    }
    void set_dismiss_on_escape(bool enabled);
    [[nodiscard]] Event<OverlayDismissReason>& dismiss_requested() noexcept {
        return dismiss_requested_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    void on_pointer(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    void validate_anchor(const Control::Ptr& anchor) const;

    Control::WeakPtr anchor_;
    Control::Ptr content_;
    AnchoredOverlayPlacement placement_;
    AnchoredOverlayPlacementResult resolved_;
    bool dismiss_on_click_away_{true};
    bool dismiss_on_escape_{true};
    Event<OverlayDismissReason> dismiss_requested_;
};

} // namespace gui_forms
