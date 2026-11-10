#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/controls/modal_layer/modal_layer_capabilities/modal_layer_capabilities.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

// The argument of ModalLayer::closing: the result the layer would close
// with, and whether a handler refuses the closing.
struct ModalLayerClosingEvent final {
    DialogResult result{DialogResult::none};
    bool cancel{false};
};

// An in-window modal layer (the superset's P1 rest): any control shown as a
// modal panel inside its owner's window, without a native window. The layer
// fills the client area over everything else and centres the content in
// it, at the content's measured size within a 16-unit margin; it takes
// every press, release and wheel turn outside the content, so the rest of
// the window cannot be used until it closes, and holds keyboard focus
// inside the content (Tab cycles within it). Focus goes to the content's
// first Tab stop when it opens and returns to where it was when it closes.
// Layers opened over one another stack: the latest is on top and holds
// focus, and closing it returns to the one below.
//
// close(result) asks to close: closing is raised first and may refuse; the
// content then leaves the layer (it is not disposed, and may be shown
// again) and closed is raised with the result. Escape inside the content
// asks to close as cancel while close_on_escape is set. When the owner
// leaves its window the layer closes as cancel without raising closing.
// This is what TaskDialog's overlay surface does, for arbitrary content;
// Form::show_dialog remains the native modal path.
//
// Theme (design language: the scrim role, row 2.5, owner option (a)): a
// `scrim` over the whole client dims the window behind (window family; the
// theme sets its colour and opacity, scrim-color, and high contrast covers
// it opaque), and the content casts the overlay `shadow` (tool_tip family)
// over the scrim. To assistive technology the layer is a modal dialog,
// named by accessible_name.
class ModalLayer final : public Control, public ModalLayerCapabilities<ModalLayer> {
public:
    explicit ModalLayer(StableId stable_id);
    ~ModalLayer() override;

    // Shows `content` over `owner`'s window; false, changing nothing, when
    // the owner is not attached to a window, the content is empty or has a
    // parent, or the layer is already shown or has a parent. UI thread.
    bool show(const Control::Ptr& owner, Control::Ptr content);
    // Asks to close with `result`; false when the layer is not shown or a
    // closing handler refused.
    bool close(DialogResult result);
    [[nodiscard]] bool is_shown() const noexcept { return shown_; }
    // The content while shown, else empty.
    [[nodiscard]] Control::Ptr content() const noexcept { return content_; }
    // The result of the last closing, or none before the first.
    [[nodiscard]] std::optional<DialogResult> result() const noexcept { return result_; }

    [[nodiscard]] Event<ModalLayerClosingEvent&>& closing() noexcept { return closing_; }
    [[nodiscard]] Event<DialogResult>& closed() noexcept { return closed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    void on_pointer(PointerEvent& event) override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `scrim` covers
    // the client while content is shown; `shadow` lies under the content.
    struct Parts final {
        PartGeometry region;
        PartGeometry scrim;
        PartGeometry shadow;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_dispose() noexcept override;

private:
    [[nodiscard]] Rect content_slot() const noexcept;
    void finish(DialogResult result);
    void on_overlay_revoked();

    Control::Ptr content_{};
    Rect content_bounds_{};
    OverlayToken overlay_{};
    SubscriptionToken overlay_revoked_{};
    FocusScopeId scope_{};
    std::optional<DialogResult> result_{};
    bool shown_{false};
    Event<ModalLayerClosingEvent&> closing_;
    Event<DialogResult> closed_;
    // property_codemod begin ModalLayer (tools/property_families/modal_layer.json; generated, do not edit)
public:
    // ModalLayer's property table: Control's entries, then its own.
    static constexpr std::size_t modal_layer_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, modal_layer_property_count>& modal_layer_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ModalLayerCloseOnEscapeCapability<ModalLayer>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(close_on_escape)& cell, bool& incoming);
    void written(const decltype(close_on_escape)& cell);
    // property_codemod end ModalLayer
};

} // namespace gui_forms
