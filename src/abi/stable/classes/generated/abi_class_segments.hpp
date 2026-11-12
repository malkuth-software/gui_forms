// Generated from manifests/abi/class_tables.json by `python3 tools/abi_class_tables.py`. Do not edit.
// One bridge layer per segment: Base is the stock class, or a layer over it.
// Included only by abi_class_registry.cpp.

#pragma once

#include "../abi_class_bridge.hpp"

// The field forms of the raisers' arguments (append_field and read_field).
#include "../../events/abi_event_catalog.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/date_time_picker.hpp"
#include "gui_forms/controls/masked_text_box.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/picture_box.hpp"
#include "gui_forms/controls/radio_button.hpp"
#include "gui_forms/controls/range_control.hpp"

namespace gui_forms::abi1 {

// The base functions an entry receives, one per raiser.
gf_status GF_CALL base_control_on_auto_size_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_back_color_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_causes_validation_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_cursor_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_dock_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_enabled_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_fore_color_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_margin_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_padding_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_right_to_left_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_tab_index_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_tab_stop_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_visible_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_change_ui_cues(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_click(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_double_click(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_drag_drop(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_drag_enter(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_drag_leave(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_drag_over(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_enter(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_got_focus(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_help_requested(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_key_down(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_key_press(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_key_up(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_layout(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_leave(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_lost_focus(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_capture_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_click(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_double_click(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_down(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_enter(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_hover(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_leave(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_move(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_up(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_mouse_wheel(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_paint_background(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_preview_key_down(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_validated(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_control_on_validating(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_masked_text_box_on_mask_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_picture_box_on_size_mode_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_combo_box_on_drop_down_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_allow_user_to_delete_rows_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_allow_user_to_order_columns_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_allow_user_to_resize_columns_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_allow_user_to_resize_rows_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_alternating_rows_default_cell_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_auto_size_columns_mode_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_auto_size_rows_mode_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_background_color_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_border_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_cell_border_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_column_headers_border_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_column_headers_default_cell_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_column_headers_height_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_default_cell_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_edit_mode_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_grid_color_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_multi_select_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_read_only_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_row_headers_border_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_row_headers_default_cell_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_row_headers_width_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_data_grid_view_on_rows_default_cell_style_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_check_box_on_appearance_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_check_box_on_checked_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_check_box_on_check_state_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_radio_button_on_checked_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_range_control_on_value_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_numeric_up_down_on_value_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_date_time_picker_on_format_changed(gf_object self, gf_event* event) noexcept;
gf_status GF_CALL base_date_time_picker_on_value_changed(gf_object self, gf_event* event) noexcept;

// Control's raisers (GF_CLASS_CONTROL).
template <typename Base>
class ControlHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_auto_size_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "auto_size",
                                                &base_control_on_auto_size_changed);
        if (!entered) {
            Base::on_auto_size_changed();
        }
    }

    void on_back_color_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 1U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "back_color",
                                                &base_control_on_back_color_changed);
        if (!entered) {
            Base::on_back_color_changed();
        }
    }

    void on_causes_validation_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 2U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "causes_validation",
                                                &base_control_on_causes_validation_changed);
        if (!entered) {
            Base::on_causes_validation_changed();
        }
    }

    void on_cursor_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 3U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "cursor",
                                                &base_control_on_cursor_changed);
        if (!entered) {
            Base::on_cursor_changed();
        }
    }

    void on_dock_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 4U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "dock",
                                                &base_control_on_dock_changed);
        if (!entered) {
            Base::on_dock_changed();
        }
    }

    void on_enabled_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 5U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "enabled",
                                                &base_control_on_enabled_changed);
        if (!entered) {
            Base::on_enabled_changed();
        }
    }

    void on_fore_color_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 6U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "fore_color",
                                                &base_control_on_fore_color_changed);
        if (!entered) {
            Base::on_fore_color_changed();
        }
    }

    void on_margin_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 7U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "margin",
                                                &base_control_on_margin_changed);
        if (!entered) {
            Base::on_margin_changed();
        }
    }

    void on_padding_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 8U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "padding",
                                                &base_control_on_padding_changed);
        if (!entered) {
            Base::on_padding_changed();
        }
    }

    void on_right_to_left_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 9U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "right_to_left",
                                                &base_control_on_right_to_left_changed);
        if (!entered) {
            Base::on_right_to_left_changed();
        }
    }

    void on_tab_index_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 10U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "tab_index",
                                                &base_control_on_tab_index_changed);
        if (!entered) {
            Base::on_tab_index_changed();
        }
    }

    void on_tab_stop_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 11U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "tab_stop",
                                                &base_control_on_tab_stop_changed);
        if (!entered) {
            Base::on_tab_stop_changed();
        }
    }

    void on_visible_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 12U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "visible",
                                                &base_control_on_visible_changed);
        if (!entered) {
            Base::on_visible_changed();
        }
    }

    void on_change_ui_cues(bool visible) override {
        const HookKey hook{GF_CLASS_CONTROL, 13U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<false>(
                                 *this, entry, hook, "change_ui_cues", &base_control_on_change_ui_cues, std::as_const(visible));
        if (!entered) {
            Base::on_change_ui_cues(visible);
        }
    }

    void on_click() override {
        const HookKey hook{GF_CLASS_CONTROL, 14U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "click",
                                                &base_control_on_click);
        if (!entered) {
            Base::on_click();
        }
    }

    void on_double_click() override {
        const HookKey hook{GF_CLASS_CONTROL, 15U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "double_click",
                                                &base_control_on_double_click);
        if (!entered) {
            Base::on_double_click();
        }
    }

    void on_drag_drop(DragEvent& event) override {
        const HookKey hook{GF_CLASS_CONTROL, 16U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<false>(
                                 *this, entry, hook, "drag_drop", &base_control_on_drag_drop, event);
        if (!entered) {
            Base::on_drag_drop(event);
        }
    }

    void on_drag_enter(DragEvent& event) override {
        const HookKey hook{GF_CLASS_CONTROL, 17U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<false>(
                                 *this, entry, hook, "drag_enter", &base_control_on_drag_enter, event);
        if (!entered) {
            Base::on_drag_enter(event);
        }
    }

    void on_drag_leave(DragEvent& event) override {
        const HookKey hook{GF_CLASS_CONTROL, 18U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<false>(
                                 *this, entry, hook, "drag_leave", &base_control_on_drag_leave, event);
        if (!entered) {
            Base::on_drag_leave(event);
        }
    }

    void on_drag_over(DragEvent& event) override {
        const HookKey hook{GF_CLASS_CONTROL, 19U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<false>(
                                 *this, entry, hook, "drag_over", &base_control_on_drag_over, event);
        if (!entered) {
            Base::on_drag_over(event);
        }
    }

    void on_enter() override {
        const HookKey hook{GF_CLASS_CONTROL, 20U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "enter",
                                                &base_control_on_enter);
        if (!entered) {
            Base::on_enter();
        }
    }

    void on_got_focus() override {
        const HookKey hook{GF_CLASS_CONTROL, 21U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "got_focus",
                                                &base_control_on_got_focus);
        if (!entered) {
            Base::on_got_focus();
        }
    }

    void on_help_requested(HelpRequestEvent& request) override {
        const HookKey hook{GF_CLASS_CONTROL, 22U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "help_requested", &base_control_on_help_requested, request);
        if (!entered) {
            Base::on_help_requested(request);
        }
    }

    void on_key_down(KeyEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 23U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "key_down", &base_control_on_key_down, arguments);
        if (!entered) {
            Base::on_key_down(arguments);
        }
    }

    void on_key_press(KeyPressEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 24U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "key_press", &base_control_on_key_press, arguments);
        if (!entered) {
            Base::on_key_press(arguments);
        }
    }

    void on_key_up(KeyEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 25U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "key_up", &base_control_on_key_up, arguments);
        if (!entered) {
            Base::on_key_up(arguments);
        }
    }

    void on_layout() override {
        const HookKey hook{GF_CLASS_CONTROL, 26U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "layout",
                                                &base_control_on_layout);
        if (!entered) {
            Base::on_layout();
        }
    }

    void on_leave() override {
        const HookKey hook{GF_CLASS_CONTROL, 27U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "leave",
                                                &base_control_on_leave);
        if (!entered) {
            Base::on_leave();
        }
    }

    void on_lost_focus() override {
        const HookKey hook{GF_CLASS_CONTROL, 28U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "lost_focus",
                                                &base_control_on_lost_focus);
        if (!entered) {
            Base::on_lost_focus();
        }
    }

    void on_mouse_capture_changed() override {
        const HookKey hook{GF_CLASS_CONTROL, 29U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "mouse_capture_changed",
                                                &base_control_on_mouse_capture_changed);
        if (!entered) {
            Base::on_mouse_capture_changed();
        }
    }

    void on_mouse_click(const MouseEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 30U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "mouse_click", &base_control_on_mouse_click, arguments);
        if (!entered) {
            Base::on_mouse_click(arguments);
        }
    }

    void on_mouse_double_click(const MouseEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 31U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "mouse_double_click", &base_control_on_mouse_double_click, arguments);
        if (!entered) {
            Base::on_mouse_double_click(arguments);
        }
    }

    void on_mouse_down(const MouseEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 32U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "mouse_down", &base_control_on_mouse_down, arguments);
        if (!entered) {
            Base::on_mouse_down(arguments);
        }
    }

    void on_mouse_enter() override {
        const HookKey hook{GF_CLASS_CONTROL, 33U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "mouse_enter",
                                                &base_control_on_mouse_enter);
        if (!entered) {
            Base::on_mouse_enter();
        }
    }

    void on_mouse_hover() override {
        const HookKey hook{GF_CLASS_CONTROL, 34U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "mouse_hover",
                                                &base_control_on_mouse_hover);
        if (!entered) {
            Base::on_mouse_hover();
        }
    }

    void on_mouse_leave() override {
        const HookKey hook{GF_CLASS_CONTROL, 35U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "mouse_leave",
                                                &base_control_on_mouse_leave);
        if (!entered) {
            Base::on_mouse_leave();
        }
    }

    void on_mouse_move(const MouseEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 36U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "mouse_move", &base_control_on_mouse_move, arguments);
        if (!entered) {
            Base::on_mouse_move(arguments);
        }
    }

    void on_mouse_up(const MouseEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 37U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "mouse_up", &base_control_on_mouse_up, arguments);
        if (!entered) {
            Base::on_mouse_up(arguments);
        }
    }

    void on_mouse_wheel(const MouseEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 38U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "mouse_wheel", &base_control_on_mouse_wheel, arguments);
        if (!entered) {
            Base::on_mouse_wheel(arguments);
        }
    }

    void on_paint_background(PaintEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 39U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<false>(
                                 *this, entry, hook, "paint_background", &base_control_on_paint_background, arguments);
        if (!entered) {
            Base::on_paint_background(arguments);
        }
    }

    void on_preview_key_down(PreviewKeyDownEventArgs& arguments) override {
        const HookKey hook{GF_CLASS_CONTROL, 40U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "preview_key_down", &base_control_on_preview_key_down, arguments);
        if (!entered) {
            Base::on_preview_key_down(arguments);
        }
    }

    void on_validated() override {
        const HookKey hook{GF_CLASS_CONTROL, 41U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "validated",
                                                &base_control_on_validated);
        if (!entered) {
            Base::on_validated();
        }
    }

    void on_validating(ControlValidationEvent& event) override {
        const HookKey hook{GF_CLASS_CONTROL, 42U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).template enter_class_hook_with<true>(
                                 *this, entry, hook, "validating", &base_control_on_validating, event);
        if (!entered) {
            Base::on_validating(event);
        }
    }

    // Runs the raiser below this layer for one of Control's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_CONTROL) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_auto_size_changed();
            break;
        case 1U:
            Base::on_back_color_changed();
            break;
        case 2U:
            Base::on_causes_validation_changed();
            break;
        case 3U:
            Base::on_cursor_changed();
            break;
        case 4U:
            Base::on_dock_changed();
            break;
        case 5U:
            Base::on_enabled_changed();
            break;
        case 6U:
            Base::on_fore_color_changed();
            break;
        case 7U:
            Base::on_margin_changed();
            break;
        case 8U:
            Base::on_padding_changed();
            break;
        case 9U:
            Base::on_right_to_left_changed();
            break;
        case 10U:
            Base::on_tab_index_changed();
            break;
        case 11U:
            Base::on_tab_stop_changed();
            break;
        case 12U:
            Base::on_visible_changed();
            break;
        case 13U: {
            const bool* given = static_cast<const bool*>(arguments.constant_value);
            bool made{};
            const bool& used = given != nullptr ? *given : made;
            Base::on_change_ui_cues(used);
            break;
        }
        case 14U:
            Base::on_click();
            break;
        case 15U:
            Base::on_double_click();
            break;
        case 16U: {
            DragEvent* given = static_cast<DragEvent*>(arguments.value);
            DragEvent made{};
            DragEvent& used = given != nullptr ? *given : made;
            Base::on_drag_drop(used);
            break;
        }
        case 17U: {
            DragEvent* given = static_cast<DragEvent*>(arguments.value);
            DragEvent made{};
            DragEvent& used = given != nullptr ? *given : made;
            Base::on_drag_enter(used);
            break;
        }
        case 18U: {
            DragEvent* given = static_cast<DragEvent*>(arguments.value);
            DragEvent made{};
            DragEvent& used = given != nullptr ? *given : made;
            Base::on_drag_leave(used);
            break;
        }
        case 19U: {
            DragEvent* given = static_cast<DragEvent*>(arguments.value);
            DragEvent made{};
            DragEvent& used = given != nullptr ? *given : made;
            Base::on_drag_over(used);
            break;
        }
        case 20U:
            Base::on_enter();
            break;
        case 21U:
            Base::on_got_focus();
            break;
        case 22U: {
            HelpRequestEvent* given = static_cast<HelpRequestEvent*>(arguments.value);
            HelpRequestEvent made{};
            HelpRequestEvent& used = given != nullptr ? *given : made;
            Base::on_help_requested(used);
            break;
        }
        case 23U: {
            KeyEventArgs* given = static_cast<KeyEventArgs*>(arguments.value);
            KeyEventArgs made{};
            KeyEventArgs& used = given != nullptr ? *given : made;
            Base::on_key_down(used);
            break;
        }
        case 24U: {
            KeyPressEventArgs* given = static_cast<KeyPressEventArgs*>(arguments.value);
            KeyPressEventArgs made{};
            KeyPressEventArgs& used = given != nullptr ? *given : made;
            Base::on_key_press(used);
            break;
        }
        case 25U: {
            KeyEventArgs* given = static_cast<KeyEventArgs*>(arguments.value);
            KeyEventArgs made{};
            KeyEventArgs& used = given != nullptr ? *given : made;
            Base::on_key_up(used);
            break;
        }
        case 26U:
            Base::on_layout();
            break;
        case 27U:
            Base::on_leave();
            break;
        case 28U:
            Base::on_lost_focus();
            break;
        case 29U:
            Base::on_mouse_capture_changed();
            break;
        case 30U: {
            const MouseEventArgs* given = static_cast<const MouseEventArgs*>(arguments.constant_value);
            MouseEventArgs made{};
            const MouseEventArgs& used = given != nullptr ? *given : made;
            Base::on_mouse_click(used);
            break;
        }
        case 31U: {
            const MouseEventArgs* given = static_cast<const MouseEventArgs*>(arguments.constant_value);
            MouseEventArgs made{};
            const MouseEventArgs& used = given != nullptr ? *given : made;
            Base::on_mouse_double_click(used);
            break;
        }
        case 32U: {
            const MouseEventArgs* given = static_cast<const MouseEventArgs*>(arguments.constant_value);
            MouseEventArgs made{};
            const MouseEventArgs& used = given != nullptr ? *given : made;
            Base::on_mouse_down(used);
            break;
        }
        case 33U:
            Base::on_mouse_enter();
            break;
        case 34U:
            Base::on_mouse_hover();
            break;
        case 35U:
            Base::on_mouse_leave();
            break;
        case 36U: {
            const MouseEventArgs* given = static_cast<const MouseEventArgs*>(arguments.constant_value);
            MouseEventArgs made{};
            const MouseEventArgs& used = given != nullptr ? *given : made;
            Base::on_mouse_move(used);
            break;
        }
        case 37U: {
            const MouseEventArgs* given = static_cast<const MouseEventArgs*>(arguments.constant_value);
            MouseEventArgs made{};
            const MouseEventArgs& used = given != nullptr ? *given : made;
            Base::on_mouse_up(used);
            break;
        }
        case 38U: {
            const MouseEventArgs* given = static_cast<const MouseEventArgs*>(arguments.constant_value);
            MouseEventArgs made{};
            const MouseEventArgs& used = given != nullptr ? *given : made;
            Base::on_mouse_wheel(used);
            break;
        }
        case 39U: {
            PaintEventArgs* given = static_cast<PaintEventArgs*>(arguments.value);
            if (given == nullptr) {
                run = HookRun::needs_arguments;
                break;
            }
            Base::on_paint_background(*given);
            break;
        }
        case 40U: {
            PreviewKeyDownEventArgs* given = static_cast<PreviewKeyDownEventArgs*>(arguments.value);
            PreviewKeyDownEventArgs made{};
            PreviewKeyDownEventArgs& used = given != nullptr ? *given : made;
            Base::on_preview_key_down(used);
            break;
        }
        case 41U:
            Base::on_validated();
            break;
        case 42U: {
            ControlValidationEvent* given = static_cast<ControlValidationEvent*>(arguments.value);
            ControlValidationEvent made{};
            ControlValidationEvent& used = given != nullptr ? *given : made;
            Base::on_validating(used);
            break;
        }
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// MaskedTextBox's raisers (GF_CLASS_MASKED_TEXT_BOX).
template <typename Base>
class MaskedTextBoxHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_mask_changed() override {
        const HookKey hook{GF_CLASS_MASKED_TEXT_BOX, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "mask",
                                                &base_masked_text_box_on_mask_changed);
        if (!entered) {
            Base::on_mask_changed();
        }
    }

    // Runs the raiser below this layer for one of MaskedTextBox's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_MASKED_TEXT_BOX) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_mask_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// PictureBox's raisers (GF_CLASS_PICTURE_BOX).
template <typename Base>
class PictureBoxHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_size_mode_changed() override {
        const HookKey hook{GF_CLASS_PICTURE_BOX, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "size_mode",
                                                &base_picture_box_on_size_mode_changed);
        if (!entered) {
            Base::on_size_mode_changed();
        }
    }

    // Runs the raiser below this layer for one of PictureBox's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_PICTURE_BOX) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_size_mode_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// ComboBox's raisers (GF_CLASS_COMBO_BOX).
template <typename Base>
class ComboBoxHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_drop_down_style_changed() override {
        const HookKey hook{GF_CLASS_COMBO_BOX, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "drop_down_style",
                                                &base_combo_box_on_drop_down_style_changed);
        if (!entered) {
            Base::on_drop_down_style_changed();
        }
    }

    // Runs the raiser below this layer for one of ComboBox's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_COMBO_BOX) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_drop_down_style_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// DataGridView's raisers (GF_CLASS_DATA_GRID_VIEW).
template <typename Base>
class DataGridViewHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_allow_user_to_delete_rows_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "allow_user_to_delete_rows",
                                                &base_data_grid_view_on_allow_user_to_delete_rows_changed);
        if (!entered) {
            Base::on_allow_user_to_delete_rows_changed();
        }
    }

    void on_allow_user_to_order_columns_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 1U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "allow_user_to_order_columns",
                                                &base_data_grid_view_on_allow_user_to_order_columns_changed);
        if (!entered) {
            Base::on_allow_user_to_order_columns_changed();
        }
    }

    void on_allow_user_to_resize_columns_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 2U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "allow_user_to_resize_columns",
                                                &base_data_grid_view_on_allow_user_to_resize_columns_changed);
        if (!entered) {
            Base::on_allow_user_to_resize_columns_changed();
        }
    }

    void on_allow_user_to_resize_rows_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 3U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "allow_user_to_resize_rows",
                                                &base_data_grid_view_on_allow_user_to_resize_rows_changed);
        if (!entered) {
            Base::on_allow_user_to_resize_rows_changed();
        }
    }

    void on_alternating_rows_default_cell_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 4U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "alternating_rows_default_cell_style",
                                                &base_data_grid_view_on_alternating_rows_default_cell_style_changed);
        if (!entered) {
            Base::on_alternating_rows_default_cell_style_changed();
        }
    }

    void on_auto_size_columns_mode_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 5U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "auto_size_columns_mode",
                                                &base_data_grid_view_on_auto_size_columns_mode_changed);
        if (!entered) {
            Base::on_auto_size_columns_mode_changed();
        }
    }

    void on_auto_size_rows_mode_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 6U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "auto_size_rows_mode",
                                                &base_data_grid_view_on_auto_size_rows_mode_changed);
        if (!entered) {
            Base::on_auto_size_rows_mode_changed();
        }
    }

    void on_background_color_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 7U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "background_color",
                                                &base_data_grid_view_on_background_color_changed);
        if (!entered) {
            Base::on_background_color_changed();
        }
    }

    void on_border_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 8U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "border_style",
                                                &base_data_grid_view_on_border_style_changed);
        if (!entered) {
            Base::on_border_style_changed();
        }
    }

    void on_cell_border_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 9U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "cell_border_style",
                                                &base_data_grid_view_on_cell_border_style_changed);
        if (!entered) {
            Base::on_cell_border_style_changed();
        }
    }

    void on_column_headers_border_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 10U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "column_headers_border_style",
                                                &base_data_grid_view_on_column_headers_border_style_changed);
        if (!entered) {
            Base::on_column_headers_border_style_changed();
        }
    }

    void on_column_headers_default_cell_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 11U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "column_headers_default_cell_style",
                                                &base_data_grid_view_on_column_headers_default_cell_style_changed);
        if (!entered) {
            Base::on_column_headers_default_cell_style_changed();
        }
    }

    void on_column_headers_height_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 12U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "column_headers_height",
                                                &base_data_grid_view_on_column_headers_height_changed);
        if (!entered) {
            Base::on_column_headers_height_changed();
        }
    }

    void on_default_cell_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 13U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "default_cell_style",
                                                &base_data_grid_view_on_default_cell_style_changed);
        if (!entered) {
            Base::on_default_cell_style_changed();
        }
    }

    void on_edit_mode_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 14U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "edit_mode",
                                                &base_data_grid_view_on_edit_mode_changed);
        if (!entered) {
            Base::on_edit_mode_changed();
        }
    }

    void on_grid_color_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 15U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "grid_color",
                                                &base_data_grid_view_on_grid_color_changed);
        if (!entered) {
            Base::on_grid_color_changed();
        }
    }

    void on_multi_select_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 16U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "multi_select",
                                                &base_data_grid_view_on_multi_select_changed);
        if (!entered) {
            Base::on_multi_select_changed();
        }
    }

    void on_read_only_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 17U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "read_only",
                                                &base_data_grid_view_on_read_only_changed);
        if (!entered) {
            Base::on_read_only_changed();
        }
    }

    void on_row_headers_border_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 18U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "row_headers_border_style",
                                                &base_data_grid_view_on_row_headers_border_style_changed);
        if (!entered) {
            Base::on_row_headers_border_style_changed();
        }
    }

    void on_row_headers_default_cell_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 19U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "row_headers_default_cell_style",
                                                &base_data_grid_view_on_row_headers_default_cell_style_changed);
        if (!entered) {
            Base::on_row_headers_default_cell_style_changed();
        }
    }

    void on_row_headers_width_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 20U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "row_headers_width",
                                                &base_data_grid_view_on_row_headers_width_changed);
        if (!entered) {
            Base::on_row_headers_width_changed();
        }
    }

    void on_rows_default_cell_style_changed() override {
        const HookKey hook{GF_CLASS_DATA_GRID_VIEW, 21U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "rows_default_cell_style",
                                                &base_data_grid_view_on_rows_default_cell_style_changed);
        if (!entered) {
            Base::on_rows_default_cell_style_changed();
        }
    }

    // Runs the raiser below this layer for one of DataGridView's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_DATA_GRID_VIEW) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_allow_user_to_delete_rows_changed();
            break;
        case 1U:
            Base::on_allow_user_to_order_columns_changed();
            break;
        case 2U:
            Base::on_allow_user_to_resize_columns_changed();
            break;
        case 3U:
            Base::on_allow_user_to_resize_rows_changed();
            break;
        case 4U:
            Base::on_alternating_rows_default_cell_style_changed();
            break;
        case 5U:
            Base::on_auto_size_columns_mode_changed();
            break;
        case 6U:
            Base::on_auto_size_rows_mode_changed();
            break;
        case 7U:
            Base::on_background_color_changed();
            break;
        case 8U:
            Base::on_border_style_changed();
            break;
        case 9U:
            Base::on_cell_border_style_changed();
            break;
        case 10U:
            Base::on_column_headers_border_style_changed();
            break;
        case 11U:
            Base::on_column_headers_default_cell_style_changed();
            break;
        case 12U:
            Base::on_column_headers_height_changed();
            break;
        case 13U:
            Base::on_default_cell_style_changed();
            break;
        case 14U:
            Base::on_edit_mode_changed();
            break;
        case 15U:
            Base::on_grid_color_changed();
            break;
        case 16U:
            Base::on_multi_select_changed();
            break;
        case 17U:
            Base::on_read_only_changed();
            break;
        case 18U:
            Base::on_row_headers_border_style_changed();
            break;
        case 19U:
            Base::on_row_headers_default_cell_style_changed();
            break;
        case 20U:
            Base::on_row_headers_width_changed();
            break;
        case 21U:
            Base::on_rows_default_cell_style_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// CheckBox's raisers (GF_CLASS_CHECK_BOX).
template <typename Base>
class CheckBoxHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_appearance_changed() override {
        const HookKey hook{GF_CLASS_CHECK_BOX, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "appearance",
                                                &base_check_box_on_appearance_changed);
        if (!entered) {
            Base::on_appearance_changed();
        }
    }

    void on_checked_changed() override {
        const HookKey hook{GF_CLASS_CHECK_BOX, 1U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "checked",
                                                &base_check_box_on_checked_changed);
        if (!entered) {
            Base::on_checked_changed();
        }
    }

    void on_check_state_changed() override {
        const HookKey hook{GF_CLASS_CHECK_BOX, 2U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "check_state",
                                                &base_check_box_on_check_state_changed);
        if (!entered) {
            Base::on_check_state_changed();
        }
    }

    // Runs the raiser below this layer for one of CheckBox's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_CHECK_BOX) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_appearance_changed();
            break;
        case 1U:
            Base::on_checked_changed();
            break;
        case 2U:
            Base::on_check_state_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// RadioButton's raisers (GF_CLASS_RADIO_BUTTON).
template <typename Base>
class RadioButtonHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_checked_changed() override {
        const HookKey hook{GF_CLASS_RADIO_BUTTON, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "checked",
                                                &base_radio_button_on_checked_changed);
        if (!entered) {
            Base::on_checked_changed();
        }
    }

    // Runs the raiser below this layer for one of RadioButton's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_RADIO_BUTTON) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_checked_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// RangeControl's raisers (GF_CLASS_RANGE_CONTROL).
template <typename Base>
class RangeControlHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_value_changed() override {
        const HookKey hook{GF_CLASS_RANGE_CONTROL, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "value",
                                                &base_range_control_on_value_changed);
        if (!entered) {
            Base::on_value_changed();
        }
    }

    // Runs the raiser below this layer for one of RangeControl's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_RANGE_CONTROL) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_value_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// NumericUpDown's raisers (GF_CLASS_NUMERIC_UP_DOWN).
template <typename Base>
class NumericUpDownHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_value_changed() override {
        const HookKey hook{GF_CLASS_NUMERIC_UP_DOWN, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "value",
                                                &base_numeric_up_down_on_value_changed);
        if (!entered) {
            Base::on_value_changed();
        }
    }

    // Runs the raiser below this layer for one of NumericUpDown's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_NUMERIC_UP_DOWN) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_value_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

// DateTimePicker's raisers (GF_CLASS_DATE_TIME_PICKER).
template <typename Base>
class DateTimePickerHooksLayer : public Base {
public:
    using Base::Base;

protected:
    void on_format_changed() override {
        const HookKey hook{GF_CLASS_DATE_TIME_PICKER, 0U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "format",
                                                &base_date_time_picker_on_format_changed);
        if (!entered) {
            Base::on_format_changed();
        }
    }

    void on_value_changed() override {
        const HookKey hook{GF_CLASS_DATE_TIME_PICKER, 1U};
        const gf_hook_entry entry = (*this).class_hook_entry(hook);
        const bool entered = entry != nullptr &&
                             (*this).enter_class_hook(*this, entry, hook, "value",
                                                &base_date_time_picker_on_value_changed);
        if (!entered) {
            Base::on_value_changed();
        }
    }

    // Runs the raiser below this layer for one of DateTimePicker's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] HookRun run_layer_base(HookKey hook, const HookArguments& arguments) {
        if (hook.class_id != GF_CLASS_DATE_TIME_PICKER) {
            const HookRun inner = Base::run_layer_base(hook, arguments);
            return inner;
        }
        HookRun run = HookRun::ran;
        switch (hook.entry) {
        case 0U:
            Base::on_format_changed();
            break;
        case 1U:
            Base::on_value_changed();
            break;
        default:
            run = HookRun::no_segment;
            break;
        }
        return run;
    }
};

} // namespace gui_forms::abi1
