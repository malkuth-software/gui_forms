#pragma once

// The DockSite's private children, shared by its two sources: the PageView
// realizing each stack, and the drag layer.

#include "gui_forms/controls/dock_site.hpp"

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
// move what it paints; the site invalidates it.
class DockSite::DragLayer final : public Control {
public:
    DragLayer(StableId stable_id, DockSite& site) : Control(std::move(stable_id)), site_(&site) {
        set_paint_plane(PaintPlane::control);
        visible = false;
    }

    void release_site() noexcept { site_ = nullptr; }

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
    DockSite* site_{};
};

} // namespace gui_forms
