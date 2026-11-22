#pragma once

// The DockSite's private children, shared by its sources: the PageView
// realizing each stack, the drag layer, and the document frames of the
// frames presentation.

#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

namespace dock_site_detail {
inline constexpr std::string_view document_pane_name = "Documents";
} // namespace dock_site_detail

// -- the stack view ------------------------------------------------------------

// A stack's PageView: a `pane` region named by its selected panel ("Documents"
// for a document stack), whose caption buttons (painted by the site) are its
// semantic children.
class DockSite::StackView final : public PageView {
public:
    StackView(StableId stable_id, DockSite& site, DockNodeKind kind)
        : PageView(std::move(stable_id), options_for(kind)), site_(&site), kind_(kind) {}

    void release_site() noexcept { site_ = nullptr; }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.role = SemanticRole::pane;
        if (kind_ == DockNodeKind::document_stack) {
            descriptor.name = std::string(dock_site_detail::document_pane_name);
        } else if (site_ != nullptr) {
            const std::size_t index = (*site_).slot_index_of(*this);
            if (index < (*site_).stacks_.size()) descriptor.name = (*site_).stacks_[index].title;
        }
        descriptor.exposed = true;
        descriptor.include_descendants = true;
        return descriptor;
    }

    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        std::vector<SemanticNode> nodes{};
        if (site_ == nullptr) return nodes;
        const std::size_t index = (*site_).slot_index_of(*this);
        if (index >= (*site_).stacks_.size()) return nodes;
        nodes = (*site_).caption_nodes((*site_).stacks_[index], index);
        return nodes;
    }

    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override {
        if (site_ != nullptr) {
            const std::size_t index = (*site_).slot_index_of(*this);
            if (index < (*site_).stacks_.size() && (*site_).caption_action(index, stable_id, action)) return true;
        }
        const bool handled = PageView::on_semantic_child_action(stable_id, action, value);
        return handled;
    }

private:
    [[nodiscard]] static PageSetOptions options_for(DockNodeKind kind) noexcept {
        PageSetOptions options{};
        options.fallback = kind == DockNodeKind::document_stack ? PageSelectionFallback::most_recent
                                                                : PageSelectionFallback::neighbour;
        return options;
    }

    DockSite* site_{};
    DockNodeKind kind_{DockNodeKind::tool_stack};
};

// -- the drag layer ------------------------------------------------------------

// The layer the drag paints on: a child of the site kept above the stacks,
// covering the site, transparent to hit testing, taking no focus and
// publishing nothing (its parts are the site's). The site's motion values
// move what it paints; the site invalidates it. The layer hosts the
// drop-cue pulse itself (dock_site_drag.cpp), since its paint is what the
// pulse's frames must invalidate: the site calls paint_drop_pulse after it
// painted the drop part, and the layer enlists itself on the motion clock
// while the pulse may move. A frame that finds no selected target, or no
// paint of the pulse since the last frame, drops it from the clock.
class DockSite::DragLayer final : public Control {
public:
    DragLayer(StableId stable_id, DockSite& site) : Control(std::move(stable_id)), site_(&site) {
        set_paint_plane(PaintPlane::control);
        visible = false;
    }

    void release_site() noexcept { site_ = nullptr; }

    // Draws the pulse over the drop part at `highlight` (layer and site
    // coordinates are the same) and enlists or withdraws its frames.
    void paint_drop_pulse(Painter& painter, Rect highlight);
    [[nodiscard]] const EffectInstance& drop_pulse() const noexcept { return pulse_; }
    // A pulse frame: advances the pulse and invalidates the drop part.
    bool on_ambient_frame(FrameTime now) override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

    void on_paint(Painter& painter, Rect) override {
        if (site_ != nullptr) (*site_).paint_drag_layer(painter);
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.exposed = false;
        descriptor.include_descendants = false;
        return descriptor;
    }

private:
    // Whether the pulse may move: attached, ambient motion on and motion
    // not reduced.
    [[nodiscard]] bool pulse_moves() const noexcept;

    DockSite* site_{};
    EffectInstance pulse_{};
    // The drop part the pulse last painted over, in layer coordinates.
    Rect pulse_bounds_{};
    // Paints of the pulse, and the count the last frame saw: equal at a
    // frame means the pulse was not painted since, so it is not shown.
    std::uint64_t pulse_paints_{};
    std::uint64_t pulse_paints_seen_{};
};

// -- the document frame ----------------------------------------------------------

// A document's frame in the frames presentation (DK decisions 60 to 63;
// dock_site_frames.cpp): a child of the site, an exposed `pane` named by the
// document's title whose caption buttons are its semantic children, holding
// the document's content (its only child) in the client area below its
// caption. The site paints its chrome and takes its pointer input from the
// frame geometry; the frame lays its content out from the same geometry and
// takes no focus itself.
class DockSite::DocumentFrame final : public Control {
public:
    DocumentFrame(StableId stable_id, DockSite& site, DockPanelId panel)
        : Control(std::move(stable_id)), site_(&site), panel_(std::move(panel)) {
        set_paint_plane(PaintPlane::control);
        cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
        focusable = false;
    }

    void release_site() noexcept { site_ = nullptr; }
    [[nodiscard]] const DockPanelId& panel() const& noexcept { return panel_; }
    const DockPanelId& panel() const&& = delete;
    // The content shown, or null.
    [[nodiscard]] Control::Ptr content() const { return content_.lock(); }
    // Shows `content` in the frame, taking it out of another parent first
    // (a stack view of another site that gave the panel up), and the
    // previous content out of the frame.
    void show_content(const Control::Ptr& content);
    // Takes the content out of the frame; null when there was none.
    Control::Ptr take_content() noexcept;

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect) override {
        if (site_ != nullptr) (*site_).paint_frame(painter, *this, panel_);
    }
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;

private:
    // The stable ID of a caption button's semantic node.
    [[nodiscard]] std::string button_identity(std::string_view button) const;

    DockSite* site_{};
    DockPanelId panel_{};
    Control::WeakPtr content_{};
};

} // namespace gui_forms
