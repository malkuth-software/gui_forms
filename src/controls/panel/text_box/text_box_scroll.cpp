#include "gui_forms/controls/panel/text_box/text_box.hpp"

#include "../text_editing/text_editing_engine.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>

namespace gui_forms {

void TextBox::on_auto_scroll_changed(const Point previous_position) {
    Panel::on_auto_scroll_changed(previous_position);
    multiline_client_size_ = {-1.0, -1.0};
    if (!multiline.read() || auto_scroll.read()) return;
    adjust_scrollbars(false);
    horizontal_offset_ = previous_position.x;
    vertical_offset_ = previous_position.y;
}

void TextBox::arrange(const Rect final_bounds) {
    if (!multiline.read()) {
        Panel::arrange(final_bounds);
        return;
    }
    arrange_self(final_bounds);
    ensure_multiline_layout();
    Control::arrange(final_bounds);
}

void TextBox::on_scroll_position_changed() {
    if (!multiline.read() || !auto_scroll.read()) return;
    // Explicit scrolling must not snap back to an offscreen caret on paint.
    // Layout preserves a pending caret reveal around its own range updates.
    reveal_pending_ = false;
    caret_damage_.valid = false;
}

Rect TextBox::multiline_text_rectangle() const noexcept {
    const Rect viewport = auto_scroll.read() ? viewport_rectangle() : local_bounds();
    const Rect result{text_left_, 4.0, std::max(0.0, viewport.width - 10.0),
                      std::max(0.0, viewport.height - 8.0)};
    return result;
}

void TextBox::set_multiline_scroll(Point position) {
    if (auto_scroll.read()) {
        if (word_wrap.read()) position.x = 0.0;
        static_cast<void>(scroll_to(position));
        return;
    }
    const Rect viewport = multiline_text_rectangle();
    const double height = static_cast<double>(visual_lines_.size()) * line_height_;
    vertical_offset_ = std::clamp(position.y, 0.0, std::max(0.0, height - viewport.height));
    horizontal_offset_ = word_wrap.read() ? 0.0 : std::clamp(position.x, 0.0,
        std::max(0.0, document_width_ - viewport.width + 1.0));
}

void TextBox::ensure_multiline_layout() {
    const Rect bounds = local_bounds();
    const Size client{bounds.width, bounds.height};
    const FontSpec content_font = text_font(font.read());
    const Window* owner = window();
    const double scale = owner == nullptr ? 1.0 : (*owner).scale();
    const TextMetricsProvider* provider = owner == nullptr ? nullptr : (*owner).text_metrics_provider();
    if (!visual_lines_.empty() && multiline_width_ >= 0.0 &&
        multiline_revision_ == (*editing_).store().revision() && multiline_font_ == content_font &&
        multiline_provider_ == provider && multiline_device_scale_ == scale &&
        multiline_client_size_ == client && multiline_scroll_enabled_ == auto_scroll.read()) return;

    // Start from the full width so a bar from the previous layout cannot keep
    // itself visible after content shrinks. A vertical bar can only add wrapped
    // rows, so at most one narrower measurement is needed. No iteration on paint.
    multiline_client_size_ = {-1.0, -1.0};
    rebuild_multiline_rows(std::max(1.0, client.width - 10.0));
    bool reveal = reveal_pending_;
    if (auto_scroll.read()) {
        const double content_width = word_wrap.read() ? 0.0 : document_width_ + 11.0;
        double content_height = static_cast<double>(visual_lines_.size()) * line_height_ + 8.0;
        arrange_scroll_viewport(client, {content_width, content_height});
        const Rect viewport = multiline_text_rectangle();
        const double wrap_width = std::max(1.0, viewport.width);
        if (word_wrap.read() && wrap_width != multiline_width_) {
            rebuild_multiline_rows(wrap_width);
            reveal = reveal || reveal_pending_;
            content_height = static_cast<double>(visual_lines_.size()) * line_height_ + 8.0;
            arrange_scroll_viewport(client, {0.0, content_height});
        }
    }
    const Point position = scroll_offset();
    set_multiline_scroll(position);
    reveal_pending_ = reveal;
    multiline_client_size_ = client;
    multiline_scroll_enabled_ = auto_scroll.read();
}

} // namespace gui_forms
