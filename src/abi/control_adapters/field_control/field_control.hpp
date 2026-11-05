#pragma once

#include "../support/abi_control_adapter_support.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"

namespace gui_forms::abi::detail {

[[nodiscard]] inline std::uint8_t field_disabled_channel(
    std::uint8_t foreground_channel,
    std::uint8_t background_channel) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(foreground_channel) * 45U +
         static_cast<unsigned>(background_channel) * 55U) / 100U);
}

// ABI-facing fields retain the platform-neutral text editor state. The managed
// compatibility facade projects WinForms properties and events, while this
// object owns Unicode mutation, directional selection, history, glyph geometry,
// hit testing, clipping, and the horizontal viewport.
class FieldControl final : public gui_forms::Panel {
public:
    ~FieldControl() override;

    FieldControl(StableId stable_id, FieldControlKind kind)
        : Panel(std::move(stable_id)), kind_(kind) {
        // Panel is a container and therefore defaults to the backplane. ABI
        // fields are leaf controls: keeping them there lets later container
        // siblings erase their border and text even when z-order is correct.
        set_paint_plane(gui_forms::PaintPlane::control);
        const gui_forms::BasicControlStyle style = style_;
        set_background(kind_ == FieldControlKind::tool_strip ? style.face : style.paper);
        set_border_style(kind_ == FieldControlKind::tool_strip
                             ? gui_forms::BorderStyle::none
                             : gui_forms::BorderStyle::sunken);
        if (kind_ == FieldControlKind::text_box ||
            kind_ == FieldControlKind::combo_box ||
            kind_ == FieldControlKind::list_box ||
            kind_ == FieldControlKind::data_grid ||
            kind_ == FieldControlKind::numeric_up_down) {
            set_focusable(true);
        }
        if (kind_ == FieldControlKind::text_box ||
            kind_ == FieldControlKind::combo_box ||
            kind_ == FieldControlKind::numeric_up_down) {
            cursor = gui_forms::CursorKind::text;
        }
    }

    void set_colors(gui_forms::Color foreground, gui_forms::Color background) {
        require_mutable();
        style_.text = foreground;
        style_.disabled_text = gui_forms::Color::rgba(
            field_disabled_channel(foreground.red, background.red),
            field_disabled_channel(foreground.green, background.green),
            field_disabled_channel(foreground.blue, background.blue),
            foreground.alpha);
        set_background(background);
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
    }

    void set_text(std::string text) {
        require_mutable();
        if (text_ == text) {
            return;
        }
        text_ = std::move(text);
        text_store_.set_text(text_);
        anchor_ = std::min(anchor_, static_cast<std::uint64_t>(text_.size()));
        caret_ = std::min(caret_, static_cast<std::uint64_t>(text_.size()));
        if (!text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(anchor_))) {
            anchor_ = text_store_.utf8_offset(
                text_store_.grapheme_index(gui_forms::Utf8Offset(anchor_))).value();
        }
        if (!text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(caret_))) {
            caret_ = text_store_.utf8_offset(
                text_store_.grapheme_index(gui_forms::Utf8Offset(caret_))).value();
        }
        layout_positions_.clear();
        layout_offsets_.clear();
        clear_history();
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
    }

    [[nodiscard]] bool replace(std::uint64_t start, std::uint64_t length,
                               std::string_view replacement,
                               gf_field_edit_result& result) {
        require_mutable();
        if (start > text_.size() || length > text_.size() - start ||
            !text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(start)) ||
            !text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(start + length)) ||
            !gui_forms::validate_utf8(replacement).valid()) {
            return false;
        }
        const bool text_changes =
            std::string_view(text_).substr(static_cast<std::size_t>(start),
                                           static_cast<std::size_t>(length)) !=
            replacement;
        if (text_changes) {
            push_undo(snapshot());
            clear_redo();
            static_cast<void>(text_store_.replace(
                {gui_forms::Utf8Offset(start),
                 gui_forms::Utf8Offset(start + length)}, replacement));
            text_.assign(text_store_.utf8());
            layout_positions_.clear();
            layout_offsets_.clear();
        }
        const std::uint64_t next = start + replacement.size();
        const bool state_changes = anchor_ != next || caret_ != next;
        anchor_ = next;
        caret_ = next;
        caret_visible_ = true;
        if (text_changes || state_changes) {
            invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
        }
        result = edit_result(text_changes);
        return true;
    }

    [[nodiscard]] bool history(std::int32_t direction,
                               gf_field_edit_result& result) {
        require_mutable();
        if (direction != -1 && direction != 1) return false;
        std::deque<FieldSnapshot>& source = direction < 0 ? undo_ : redo_;
        std::deque<FieldSnapshot>& destination = direction < 0 ? redo_ : undo_;
        if (source.empty()) {
            result = edit_result(false);
            return true;
        }
        FieldSnapshot target = std::move(source.back());
        history_bytes_ -= target.text.size();
        source.pop_back();
        push_history(destination, snapshot());
        apply_snapshot(std::move(target));
        result = edit_result(true);
        return true;
    }

    void clear_history() noexcept {
        undo_.clear();
        redo_.clear();
        history_bytes_ = 0;
    }

    [[nodiscard]] std::string_view text() const noexcept { return text_; }

    bool set_selection(std::uint64_t start, std::uint64_t length,
                       bool caret_visible) {
        if (start > text_.size() || length > text_.size() - start) return false;
        return set_edit_state(start, start + length, caret_visible);
    }

    bool set_edit_state(std::uint64_t anchor, std::uint64_t caret,
                        bool caret_visible) {
        require_mutable();
        if (anchor > text_.size() || caret > text_.size() ||
            !text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(anchor)) ||
            !text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(caret))) {
            return false;
        }
        if (anchor_ == anchor && caret_ == caret &&
            caret_visible_ == caret_visible) {
            return true;
        }
        anchor_ = anchor;
        caret_ = caret;
        caret_visible_ = caret_visible;
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
        return true;
    }

    [[nodiscard]] std::uint64_t position_at(double local_x) const noexcept {
        if (layout_positions_.size() != layout_offsets_.size() ||
            layout_positions_.empty()) {
            const double guessed = std::max(0.0, local_x - text_left_ + horizontal_offset_);
            const std::size_t index = std::min<std::size_t>(
                static_cast<std::size_t>(std::lround(guessed / 7.0)),
                text_store_.grapheme_count().value());
            return text_store_.utf8_offset(gui_forms::GraphemeIndex(index)).value();
        }
        const double content_x = std::max(0.0, local_x - text_left_ + horizontal_offset_);
        const std::size_t right_index = gui_forms::detail::lower_bound_index(
            std::span<const double>(layout_positions_), content_x);
        if (right_index == 0U) return layout_offsets_.front();
        if (right_index == layout_positions_.size()) return layout_offsets_.back();
        const double left_distance = content_x - layout_positions_[right_index - 1U];
        const double right_distance = layout_positions_[right_index] - content_x;
        return layout_offsets_[left_distance < right_distance
            ? right_index - 1U : right_index];
    }

    [[nodiscard]] bool navigate(std::uint64_t position, std::int32_t direction,
                                std::uint64_t& result) const noexcept {
        if (position > text_.size() ||
            !text_store_.is_grapheme_boundary(gui_forms::Utf8Offset(position)) ||
            (direction != -1 && direction != 1)) return false;
        const gui_forms::Utf8Offset source = gui_forms::Utf8Offset(position);
        result = (direction < 0
            ? text_store_.previous_grapheme_boundary(source)
            : text_store_.next_grapheme_boundary(source)).value();
        return true;
    }

    void on_paint(gui_forms::Painter& painter, Rect damage) override {
        Panel::on_paint(painter, damage);
        const Rect bounds = local_bounds();
        const gui_forms::BasicControlStyle style = style_;
        const double button_width = kind_ == FieldControlKind::numeric_up_down
            ? std::min(18.0, std::max(0.0, bounds.width)) : 0.0;
        const double text_right = std::max(5.0, bounds.width - button_width -
                                                (kind_ == FieldControlKind::combo_box ? 18.0 : 3.0));
        const gui_forms::FontSpec font{gui_forms::FontRole::content, 12.0, 400, false};
        layout_positions_.clear();
        layout_offsets_.clear();
        const std::size_t graphemes = text_store_.grapheme_count().value();
        layout_positions_.reserve(graphemes + 1U);
        layout_offsets_.reserve(graphemes + 1U);
        for (std::size_t index = 0; index <= graphemes; ++index) {
            const std::uint64_t offset = text_store_.utf8_offset(
                gui_forms::GraphemeIndex(index)).value();
            layout_offsets_.push_back(offset);
            layout_positions_.push_back(painter.measure_text_utf8(
                std::string_view(text_).substr(0, static_cast<std::size_t>(offset)),
                font).width);
        }
        const double viewport_width = std::max(0.0, text_right - text_left_);
        const double caret_content_x = boundary_x(caret_);
        if (caret_content_x < horizontal_offset_) {
            horizontal_offset_ = caret_content_x;
        } else if (caret_content_x > horizontal_offset_ + viewport_width) {
            horizontal_offset_ = caret_content_x - viewport_width;
        }
        const double maximum_offset = std::max(
            0.0, layout_positions_.back() - viewport_width);
        horizontal_offset_ = std::clamp(horizontal_offset_, 0.0, maximum_offset);

        const std::uint64_t selection_start = std::min(anchor_, caret_);
        const std::uint64_t selection_end = std::max(anchor_, caret_);
        const double text_origin_x = text_left_ - horizontal_offset_;
        const double selection_x = text_origin_x + boundary_x(selection_start);
        const double selection_end_x = text_origin_x + boundary_x(selection_end);
        const double baseline = std::max(14.0, bounds.height * 0.5 + 4.0);
        painter.save();
        painter.clip_rect({text_left_, 2.0, viewport_width,
                           std::max(0.0, bounds.height - 4.0)});
        if (focused_ && selection_end > selection_start &&
            selection_end_x > selection_x) {
            painter.fill_rect({selection_x, 3.0, selection_end_x - selection_x,
                               std::max(0.0, bounds.height - 6.0)}, style.accent);
        }
        if (!text_.empty()) {
            painter.draw_text_utf8({text_origin_x, baseline}, text_, font,
                                   enabled.read() ? style.text : style.disabled_text);
            if (focused_ && selection_end > selection_start &&
                selection_end_x > selection_x) {
                painter.save();
                painter.clip_rect({selection_x, 3.0,
                                   selection_end_x - selection_x,
                                   std::max(0.0, bounds.height - 6.0)});
                painter.draw_text_utf8({text_origin_x, baseline}, text_, font,
                                       style.highlight);
                painter.restore();
            }
        }
        if (focused_ && caret_visible_ && anchor_ == caret_) {
            const double caret_x = text_origin_x + caret_content_x;
            painter.draw_line({caret_x, 4.0},
                              {caret_x, std::max(4.0, bounds.height - 4.0)},
                              enabled.read() ? style.text : style.disabled_text, 1.0);
        }
        painter.restore();
        if (kind_ == FieldControlKind::combo_box && bounds.width >= 18.0) {
            const double x = bounds.width - 14.0;
            const double y = bounds.height * 0.5 - 1.0;
            painter.draw_line({x, y}, {x + 4.0, y + 4.0}, style.dark_border, 1.0);
            painter.draw_line({x + 4.0, y + 4.0}, {x + 8.0, y}, style.dark_border, 1.0);
        } else if (kind_ == FieldControlKind::data_grid && bounds.height >= 24.0) {
            painter.fill_rect({1.0, 1.0, std::max(0.0, bounds.width - 2.0), 21.0},
                              style.face);
            painter.draw_line({1.0, 22.0}, {std::max(1.0, bounds.width - 1.0), 22.0},
                              style.border, 1.0);
        } else if (kind_ == FieldControlKind::numeric_up_down &&
                   button_width > 0.0 && bounds.height >= 12.0) {
            const double left = bounds.width - button_width;
            const double middle = std::floor(bounds.height * 0.5);
            painter.fill_rect({left, 1.0, button_width - 1.0,
                               std::max(0.0, bounds.height - 2.0)}, style.face);
            painter.draw_line({left, 1.0}, {left, bounds.height - 1.0},
                              style.border, 1.0);
            painter.draw_line({left, middle}, {bounds.width - 1.0, middle},
                              style.border, 1.0);
            const double center = left + button_width * 0.5;
            painter.draw_line({center - 3.0, middle - 3.0},
                              {center, middle - 6.0}, style.dark_border, 1.0);
            painter.draw_line({center, middle - 6.0},
                              {center + 3.0, middle - 3.0}, style.dark_border, 1.0);
            painter.draw_line({center - 3.0, middle + 4.0},
                              {center, middle + 7.0}, style.dark_border, 1.0);
            painter.draw_line({center, middle + 7.0},
                              {center + 3.0, middle + 4.0}, style.dark_border, 1.0);
        }
        if (focused_ && bounds.width > 2.0 && bounds.height > 2.0) {
            const Color focus = style.accent;
            painter.draw_line({1.0, 1.0}, {bounds.width - 1.0, 1.0}, focus, 1.0);
            painter.draw_line({1.0, 1.0}, {1.0, bounds.height - 1.0}, focus, 1.0);
            painter.draw_line({1.0, bounds.height - 1.0},
                              {bounds.width - 1.0, bounds.height - 1.0}, focus, 1.0);
            painter.draw_line({bounds.width - 1.0, 1.0},
                              {bounds.width - 1.0, bounds.height - 1.0}, focus, 1.0);
        }
    }

    void on_focus_changed(bool focused) override {
        focused_ = focused;
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
    }

    [[nodiscard]] gui_forms::Event<const RasterPointerSample&>&
    pointer_input() noexcept {
        return pointer_input_;
    }

    [[nodiscard]] gui_forms::Event<const RasterKeySample&>& key_input() noexcept {
        return key_input_;
    }

    [[nodiscard]] gui_forms::Event<const RasterTextSample&>& text_input() noexcept {
        return text_input_;
    }

    void on_pointer(gui_forms::PointerEvent& event) override {
        const Rect absolute = absolute_bounds();
        const std::uint32_t kind = pointer_event_kind(event.action);
        pointer_input_.emit({kind,
                             event.position.x - absolute.x,
                             event.position.y - absolute.y,
                             event.wheel_delta.y,
                             static_cast<std::uint32_t>(event.button)});
        event.handled = true;
    }

    void on_key(gui_forms::KeyEvent& event) override {
        key_input_.emit({event.action == gui_forms::KeyAction::down
                             ? GF_EVENT_KEY_DOWN : GF_EVENT_KEY_UP,
                         event.physical_key,
                         static_cast<std::uint32_t>(event.modifiers),
                         event.repeat});
        event.handled = true;
    }

    void on_text_input(gui_forms::TextInputEvent& event) override {
        text_input_.emit({event.text_utf8, event.composing,
                          event.replacement_start, event.replacement_length});
        event.handled = true;
    }

private:
    struct FieldSnapshot final {
        std::string text;
        std::uint64_t anchor{};
        std::uint64_t caret{};
    };

    [[nodiscard]] double boundary_x(std::uint64_t offset) const noexcept {
        const std::size_t index = text_store_.grapheme_index(
            gui_forms::Utf8Offset(offset)).value();
        return layout_positions_[
            std::min(index, layout_positions_.size() - 1U)];
    }

    [[nodiscard]] FieldSnapshot snapshot() const {
        return {text_, anchor_, caret_};
    }

    void apply_snapshot(FieldSnapshot snapshot) {
        text_ = std::move(snapshot.text);
        text_store_.set_text(text_);
        anchor_ = snapshot.anchor;
        caret_ = snapshot.caret;
        caret_visible_ = true;
        layout_positions_.clear();
        layout_offsets_.clear();
        invalidate(gui_forms::Dirty::paint | gui_forms::Dirty::semantics);
    }

    void push_history(std::deque<FieldSnapshot>& history,
                      FieldSnapshot snapshot) {
        history_bytes_ += snapshot.text.size();
        history.push_back(std::move(snapshot));
        while (history_bytes_ > maximum_history_bytes_ ||
               undo_.size() + redo_.size() > maximum_history_entries_) {
            if (!undo_.empty()) {
                history_bytes_ -= undo_.front().text.size();
                undo_.pop_front();
            } else if (!redo_.empty()) {
                history_bytes_ -= redo_.front().text.size();
                redo_.pop_front();
            } else {
                break;
            }
        }
    }

    void push_undo(FieldSnapshot snapshot) {
        push_history(undo_, std::move(snapshot));
    }

    void clear_redo() noexcept {
        for (const gui_forms::abi::detail::FieldControl::FieldSnapshot& snapshot : redo_) history_bytes_ -= snapshot.text.size();
        redo_.clear();
    }

    [[nodiscard]] gf_field_edit_result edit_result(bool changed) const noexcept {
        return {anchor_, caret_, text_store_.revision(), changed ? 1U : 0U,
                undo_.empty() ? 0U : 1U, redo_.empty() ? 0U : 1U};
    }

    std::string text_;
    gui_forms::TextStore text_store_;
    FieldControlKind kind_;
    gui_forms::BasicControlStyle style_;
    std::uint64_t anchor_{};
    std::uint64_t caret_{};
    std::vector<double> layout_positions_;
    std::vector<std::uint64_t> layout_offsets_;
    double horizontal_offset_{};
    static constexpr double text_left_{5.0};
    bool caret_visible_{true};
    bool focused_{};
    std::deque<FieldSnapshot> undo_;
    std::deque<FieldSnapshot> redo_;
    std::size_t history_bytes_{};
    static constexpr std::size_t maximum_history_entries_{128U};
    static constexpr std::size_t maximum_history_bytes_{8U * 1024U * 1024U};
    gui_forms::Event<const RasterPointerSample&> pointer_input_;
    gui_forms::Event<const RasterKeySample&> key_input_;
    gui_forms::Event<const RasterTextSample&> text_input_;
};

} // namespace gui_forms::abi::detail
