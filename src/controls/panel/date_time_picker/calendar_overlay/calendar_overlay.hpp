#pragma once

#include "../calendar_grid/calendar_grid.hpp"
#include "../date_time_utilities.hpp"
#include "gui_forms/control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {
using namespace date_time_detail;

// The overlay's day grid, shared in form with MonthCalendar: cells 37 by 25
// on a 39 by 27 pitch, from (5, 66).
inline constexpr calendar_detail::MonthGridMetrics calendar_overlay_grid{
    .cell_left = 5.0, .cell_top = 66.0, .column_pitch = 39.0, .row_pitch = 27.0,
    .cell_width = 37.0, .cell_height = 25.0};

class CalendarOverlay final : public Control {
public:
    ~CalendarOverlay() override;

    CalendarOverlay(StableId stable_id, DateTimeValue selected,
                  DateTimeValue minimum, DateTimeValue maximum,
                  DateTimeFormatProvider provider)
        : Control(std::move(stable_id)), selected_(selected), minimum_(minimum),
          maximum_(maximum), provider_(std::move(provider)),
          display_year_(selected.year), display_month_(selected.month) {
        set_paint_plane(PaintPlane::overlay);
        set_focusable(true);
    }

    [[nodiscard]] Event<DateTimeValue>& committed() noexcept { return committed_; }
    [[nodiscard]] Event<>& cancelled() noexcept { return cancelled_; }

    // The overlay's face, its overlay shadow, month title, month arrows,
    // weekday names, day plates and numbers, rule and hint are parts the
    // theme's engine draws (calendar_overlay.cpp).
    void on_paint(Painter& painter, Rect) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;

    void on_pointer(PointerEvent& event) override {
        if (!eligible_for_input()) return;
        const Rect bounds = absolute_bounds();
        const Point local{event.position.x - bounds.x, event.position.y - bounds.y};
        if (event.action == PointerAction::move) {
            const std::optional<std::size_t> next = cell_at(local);
            if (next != hovered_cell_) {
                hovered_cell_ = next;
                invalidate(Dirty::paint);
            }
            return;
        }
        if (event.action == PointerAction::leave) {
            hovered_cell_.reset();
            invalidate(Dirty::paint);
            return;
        }
        if (event.button != PointerButton::primary) return;
        if (event.action == PointerAction::down) {
            const double s = effective_text_scale();
            if (local.y < 40.0 * s && local.x < 46.0 * s) {
                change_month(-1);
                pressed_cell_.reset();
                header_pressed_ = true;
                event.handled = true;
                return;
            }
            if (local.y < 40.0 * s && local.x > bounds.width - 49.0 * s) {
                change_month(1);
                pressed_cell_.reset();
                header_pressed_ = true;
                event.handled = true;
                return;
            }
            if (const std::optional<std::size_t> cell = cell_at(local)) {
                const DateTimeValue date = cell_dates()[*cell];
                if (civil_in_range(date, minimum_, maximum_)) {
                    selected_ = std::clamp(date, minimum_, maximum_);
                    pressed_cell_ = cell;
                    invalidate(Dirty::paint | Dirty::semantics);
                    event.handled = true;
                }
            }
        } else if (event.action == PointerAction::up) {
            const std::optional<std::size_t> cell = cell_at(local);
            const bool commit = pressed_cell_ && cell == pressed_cell_;
            const bool acknowledge = commit || pressed_cell_.has_value() || header_pressed_;
            pressed_cell_.reset();
            header_pressed_ = false;
            if (commit) committed_.emit(selected_);
            event.handled = acknowledge;
        }
    }

    void on_key(KeyEvent& event) override {
        if (!focused_ || event.action != KeyAction::down) return;
        if (event.physical_key == PhysicalKey::escape) {
            cancelled_.emit();
            event.handled = true;
            return;
        }
        if (event.physical_key == PhysicalKey::enter) {
            committed_.emit(selected_);
            event.handled = true;
            return;
        }
        if (event.physical_key == PhysicalKey::left) move_selection(-1);
        else if (event.physical_key == PhysicalKey::right) move_selection(1);
        else if (event.physical_key == PhysicalKey::up) move_selection(-7);
        else if (event.physical_key == PhysicalKey::down) move_selection(7);
        else if (event.physical_key == PhysicalKey::page_up) change_month(-1);
        else if (event.physical_key == PhysicalKey::page_down) change_month(1);
        else return;
        event.handled = true;
    }

    void on_focus_changed(bool focused) override {
        focused_ = focused;
        invalidate(Dirty::paint | Dirty::semantics);
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::calendar;
        descriptor.name = provider_.month_names[display_month_ - 1U] + " " +
                          std::to_string(display_year_);
        descriptor.value = format_date_time(selected_, provider_.long_date_pattern,
                                            provider_);
        descriptor.actions = {SemanticAction::focus};
        descriptor.exposed = true;
        return descriptor;
    }

    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        std::vector<SemanticNode> nodes;
        const std::array<DateTimeValue, 42> dates = cell_dates();
        const Rect overlay = absolute_bounds();
        const double s = effective_text_scale();
        nodes.reserve(dates.size() + 2U);
        for (const int direction : {-1, 1}) {
            SemanticNode node;
            node.stable_id = std::string(stable_id().value()) +
                (direction < 0 ? ".previous-month" : ".next-month");
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
            node.role = SemanticRole::button;
            node.name = direction < 0 ? "Previous month" : "Next month";
            node.bounds = direction < 0
                ? Rect{overlay.x + 4.0 * s, overlay.y + 3.0 * s,
                       42.0 * s, 34.0 * s}
                : Rect{overlay.x + overlay.width - 49.0 * s, overlay.y + 3.0 * s,
                       42.0 * s, 34.0 * s};
            node.states = SemanticState::visible | SemanticState::focusable;
            if (can_change_month(direction)) {
                node.states |= SemanticState::enabled;
                node.actions = {SemanticAction::press};
            }
            nodes.push_back(std::move(node));
        }
        for (std::size_t cell = 0U; cell < dates.size(); ++cell) {
            const DateTimeValue date = dates[cell];
            SemanticNode node;
            node.stable_id = date_stable_id(date);
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
            node.role = SemanticRole::date_cell;
            node.name = format_date_time(date, provider_.long_date_pattern, provider_);
            node.value = iso_date(date);
            const Rect cell_bounds =
                calendar_detail::month_grid_cell_bounds(calendar_overlay_grid, cell, s);
            node.bounds = {overlay.x + cell_bounds.x, overlay.y + cell_bounds.y,
                           cell_bounds.width, cell_bounds.height};
            node.states |= SemanticState::visible | SemanticState::focusable;
            if (civil_in_range(date, minimum_, maximum_)) {
                node.states |= SemanticState::enabled;
                node.actions = {SemanticAction::select, SemanticAction::press};
            }
            if (date.year == selected_.year && date.month == selected_.month &&
                date.day == selected_.day) {
                node.states |= SemanticState::selected;
                if (focused_) node.states |= SemanticState::focused;
            }
            nodes.push_back(std::move(node));
        }
        return nodes;
    }

    bool on_semantic_child_action(std::string_view stable_id,
                                  SemanticAction action,
                                  std::string_view) override {
        const std::string prefix((*this).stable_id().value());
        if (action == SemanticAction::press &&
            stable_id == prefix + ".previous-month") {
            if (!can_change_month(-1)) return false;
            change_month(-1);
            return true;
        }
        if (action == SemanticAction::press &&
            stable_id == prefix + ".next-month") {
            if (!can_change_month(1)) return false;
            change_month(1);
            return true;
        }
        if (action != SemanticAction::select && action != SemanticAction::press) {
            return false;
        }
        for (const DateTimeValue date : cell_dates()) {
            if (date_stable_id(date) != stable_id) continue;
            if (!civil_in_range(date, minimum_, maximum_)) return false;
            selected_ = std::clamp(date, minimum_, maximum_);
            invalidate(Dirty::paint | Dirty::semantics);
            if (action == SemanticAction::press) committed_.emit(selected_);
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] std::array<DateTimeValue, 42> cell_dates() const noexcept {
        DateTimeValue first = selected_;
        first.year = display_year_;
        first.month = display_month_;
        first.day = 1U;
        // Weeks start on Sunday in the overlay.
        const std::array<DateTimeValue, 42> result = calendar_detail::month_page_dates(first, 0U);
        return result;
    }

    [[nodiscard]] std::optional<std::size_t> cell_at(Point local) const noexcept {
        const double s = effective_text_scale();
        const std::optional<std::size_t> cell =
            calendar_detail::month_grid_cell_at(calendar_overlay_grid, local, s);
        return cell;
    }

    void move_selection(int days) {
        const DateTimeValue candidate = add_days(selected_, days);
        if (!civil_in_range(candidate, minimum_, maximum_)) return;
        const DateTimeValue next = std::clamp(candidate, minimum_, maximum_);
        if (next == selected_) return;
        selected_ = next;
        display_year_ = next.year;
        display_month_ = next.month;
        invalidate(Dirty::paint | Dirty::semantics);
    }

    [[nodiscard]] DateTimeValue month_candidate(int direction) const noexcept {
        const calendar_detail::CalendarMonth target = calendar_detail::shifted_month(
            calendar_detail::CalendarMonth{display_year_, display_month_}, direction);
        const DateTimeValue next = calendar_detail::in_month(selected_, target);
        return std::clamp(next, minimum_, maximum_);
    }

    [[nodiscard]] bool can_change_month(int direction) const noexcept {
        const DateTimeValue next = month_candidate(direction);
        return next.year != display_year_ || next.month != display_month_;
    }

    void change_month(int direction) {
        const DateTimeValue next = month_candidate(direction);
        if (next.year == display_year_ && next.month == display_month_) return;
        selected_ = next;
        display_year_ = next.year;
        display_month_ = next.month;
        invalidate(Dirty::paint | Dirty::semantics);
    }

    [[nodiscard]] std::string date_stable_id(DateTimeValue date) const {
        return std::string(stable_id().value()) + ".day." + iso_date(date);
    }

    [[nodiscard]] static std::string iso_date(DateTimeValue date) {
        return padded(static_cast<unsigned>(date.year), 4U) + "-" +
               padded(date.month, 2U) + "-" + padded(date.day, 2U);
    }

    DateTimeValue selected_;
    DateTimeValue minimum_;
    DateTimeValue maximum_;
    DateTimeFormatProvider provider_;
    std::int32_t display_year_;
    std::uint8_t display_month_;
    std::optional<std::size_t> hovered_cell_;
    std::optional<std::size_t> pressed_cell_;
    bool header_pressed_{};
    bool focused_{};
    Event<DateTimeValue> committed_;
    Event<> cancelled_;
};

} // namespace gui_forms
