// Generated from manifests/abi/class_tables.json by `python3 tools/abi_class_tables.py`. Do not edit.
// The classes classes.create makes, their segments in slot order, and the
// base function of every entry.

#include "abi_class_segments.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/card.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/controls/dial.hpp"
#include "gui_forms/controls/dock_navigator.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/gauge_base.hpp"
#include "gui_forms/controls/group_box.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/link_label.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/list_control.hpp"
#include "gui_forms/controls/material_panel.hpp"
#include "gui_forms/controls/navigation_pane.hpp"
#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/controls/page_view.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/picture_box.hpp"
#include "gui_forms/controls/progress_bar.hpp"
#include "gui_forms/controls/radio_button.hpp"
#include "gui_forms/controls/range_control.hpp"
#include "gui_forms/controls/raster_canvas.hpp"
#include "gui_forms/controls/scaled_panel.hpp"
#include "gui_forms/controls/scroll_bar.hpp"
#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/controls/sweep_scope.hpp"
#include "gui_forms/controls/text_box_base.hpp"
#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/controls/track_bar.hpp"
#include "gui_forms/controls/up_down_base.hpp"
#include "gui_forms/controls/user_control.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace gui_forms::abi1 {

gf_status GF_CALL base_control_on_auto_size_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 0U});
    return ran;
}

gf_status GF_CALL base_control_on_back_color_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 1U});
    return ran;
}

gf_status GF_CALL base_control_on_causes_validation_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 2U});
    return ran;
}

gf_status GF_CALL base_control_on_cursor_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 3U});
    return ran;
}

gf_status GF_CALL base_control_on_dock_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 4U});
    return ran;
}

gf_status GF_CALL base_control_on_enabled_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 5U});
    return ran;
}

gf_status GF_CALL base_control_on_fore_color_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 6U});
    return ran;
}

gf_status GF_CALL base_control_on_margin_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 7U});
    return ran;
}

gf_status GF_CALL base_control_on_padding_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 8U});
    return ran;
}

gf_status GF_CALL base_control_on_right_to_left_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 9U});
    return ran;
}

gf_status GF_CALL base_control_on_tab_index_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 10U});
    return ran;
}

gf_status GF_CALL base_control_on_tab_stop_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 11U});
    return ran;
}

gf_status GF_CALL base_control_on_visible_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 12U});
    return ran;
}

gf_status GF_CALL base_control_on_change_ui_cues(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 13U});
    return ran;
}

gf_status GF_CALL base_control_on_click(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 14U});
    return ran;
}

gf_status GF_CALL base_control_on_double_click(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 15U});
    return ran;
}

gf_status GF_CALL base_control_on_drag_drop(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 16U});
    return ran;
}

gf_status GF_CALL base_control_on_drag_enter(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 17U});
    return ran;
}

gf_status GF_CALL base_control_on_drag_leave(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 18U});
    return ran;
}

gf_status GF_CALL base_control_on_drag_over(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 19U});
    return ran;
}

gf_status GF_CALL base_control_on_enter(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 20U});
    return ran;
}

gf_status GF_CALL base_control_on_got_focus(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 21U});
    return ran;
}

gf_status GF_CALL base_control_on_help_requested(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 22U});
    return ran;
}

gf_status GF_CALL base_control_on_key_down(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 23U});
    return ran;
}

gf_status GF_CALL base_control_on_key_press(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 24U});
    return ran;
}

gf_status GF_CALL base_control_on_key_up(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 25U});
    return ran;
}

gf_status GF_CALL base_control_on_layout(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 26U});
    return ran;
}

gf_status GF_CALL base_control_on_leave(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 27U});
    return ran;
}

gf_status GF_CALL base_control_on_lost_focus(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 28U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_capture_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 29U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_click(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 30U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_double_click(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 31U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_down(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 32U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_enter(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 33U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_hover(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 34U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_leave(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 35U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_move(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 36U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_up(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 37U});
    return ran;
}

gf_status GF_CALL base_control_on_mouse_wheel(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 38U});
    return ran;
}

gf_status GF_CALL base_control_on_paint_background(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 39U});
    return ran;
}

gf_status GF_CALL base_control_on_preview_key_down(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 40U});
    return ran;
}

gf_status GF_CALL base_control_on_validated(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 41U});
    return ran;
}

gf_status GF_CALL base_control_on_validating(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CONTROL, 42U});
    return ran;
}

gf_status GF_CALL base_masked_text_box_on_mask_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_MASKED_TEXT_BOX, 0U});
    return ran;
}

gf_status GF_CALL base_picture_box_on_size_mode_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_PICTURE_BOX, 0U});
    return ran;
}

gf_status GF_CALL base_combo_box_on_drop_down_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_COMBO_BOX, 0U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_allow_user_to_delete_rows_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 0U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_allow_user_to_order_columns_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 1U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_allow_user_to_resize_columns_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 2U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_allow_user_to_resize_rows_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 3U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_alternating_rows_default_cell_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 4U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_auto_size_columns_mode_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 5U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_auto_size_rows_mode_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 6U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_background_color_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 7U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_border_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 8U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_cell_border_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 9U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_column_headers_border_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 10U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_column_headers_default_cell_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 11U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_column_headers_height_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 12U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_default_cell_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 13U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_edit_mode_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 14U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_grid_color_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 15U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_multi_select_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 16U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_read_only_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 17U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_row_headers_border_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 18U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_row_headers_default_cell_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 19U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_row_headers_width_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 20U});
    return ran;
}

gf_status GF_CALL base_data_grid_view_on_rows_default_cell_style_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATA_GRID_VIEW, 21U});
    return ran;
}

gf_status GF_CALL base_check_box_on_appearance_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CHECK_BOX, 0U});
    return ran;
}

gf_status GF_CALL base_check_box_on_checked_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CHECK_BOX, 1U});
    return ran;
}

gf_status GF_CALL base_check_box_on_check_state_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_CHECK_BOX, 2U});
    return ran;
}

gf_status GF_CALL base_radio_button_on_checked_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_RADIO_BUTTON, 0U});
    return ran;
}

gf_status GF_CALL base_range_control_on_value_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_RANGE_CONTROL, 0U});
    return ran;
}

gf_status GF_CALL base_numeric_up_down_on_value_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NUMERIC_UP_DOWN, 0U});
    return ran;
}

gf_status GF_CALL base_date_time_picker_on_format_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATE_TIME_PICKER, 0U});
    return ran;
}

gf_status GF_CALL base_date_time_picker_on_value_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_DATE_TIME_PICKER, 1U});
    return ran;
}

gf_status GF_CALL base_node_canvas_on_edit_ended(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NODE_CANVAS, 0U});
    return ran;
}

gf_status GF_CALL base_node_canvas_on_graph_edited(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NODE_CANVAS, 1U});
    return ran;
}

gf_status GF_CALL base_node_canvas_on_link_connected(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NODE_CANVAS, 2U});
    return ran;
}

gf_status GF_CALL base_node_canvas_on_palette_requested(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NODE_CANVAS, 3U});
    return ran;
}

gf_status GF_CALL base_node_canvas_on_selection_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NODE_CANVAS, 4U});
    return ran;
}

gf_status GF_CALL base_node_canvas_on_view_changed(gf_object self, gf_event* event) noexcept {
    const gf_status ran = run_class_base(self, event, HookKey{GF_CLASS_NODE_CANVAS, 5U});
    return ran;
}

namespace {

using ButtonBaseLayers = ControlHooksLayer<BridgeState<ButtonBase>>;
constexpr std::array<gf_class_id, 1> button_base_segments{GF_CLASS_CONTROL};

using ButtonLayers = ControlHooksLayer<BridgeState<Button>>;
constexpr std::array<gf_class_id, 1> button_segments{GF_CLASS_CONTROL};

using LinkLabelLayers = ControlHooksLayer<BridgeState<LinkLabel>>;
constexpr std::array<gf_class_id, 1> link_label_segments{GF_CLASS_CONTROL};

using LabelLayers = ControlHooksLayer<BridgeState<Label>>;
constexpr std::array<gf_class_id, 1> label_segments{GF_CLASS_CONTROL};

using PictureBoxLayers = PictureBoxHooksLayer<ControlHooksLayer<BridgeState<PictureBox>>>;
constexpr std::array<gf_class_id, 2> picture_box_segments{GF_CLASS_CONTROL, GF_CLASS_PICTURE_BOX};

using ProgressBarLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<ProgressBar>>>;
constexpr std::array<gf_class_id, 2> progress_bar_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using SweepScopeLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<SweepScope>>>;
constexpr std::array<gf_class_id, 2> sweep_scope_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using ListBoxLayers = ControlHooksLayer<BridgeState<ListBox>>;
constexpr std::array<gf_class_id, 1> list_box_segments{GF_CLASS_CONTROL};

using CheckBoxLayers = CheckBoxHooksLayer<ControlHooksLayer<BridgeState<CheckBox>>>;
constexpr std::array<gf_class_id, 2> check_box_segments{GF_CLASS_CONTROL, GF_CLASS_CHECK_BOX};

using RadioButtonLayers = RadioButtonHooksLayer<ControlHooksLayer<BridgeState<RadioButton>>>;
constexpr std::array<gf_class_id, 2> radio_button_segments{GF_CLASS_CONTROL, GF_CLASS_RADIO_BUTTON};

using RangeControlLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<RangeControl>>>;
constexpr std::array<gf_class_id, 2> range_control_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using TrackBarLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<TrackBar>>>;
constexpr std::array<gf_class_id, 2> track_bar_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using PanelLayers = ControlHooksLayer<BridgeState<Panel>>;
constexpr std::array<gf_class_id, 1> panel_segments{GF_CLASS_CONTROL};

using GroupBoxLayers = ControlHooksLayer<BridgeState<GroupBox>>;
constexpr std::array<gf_class_id, 1> group_box_segments{GF_CLASS_CONTROL};

using MaterialPanelLayers = ControlHooksLayer<BridgeState<MaterialPanel>>;
constexpr std::array<gf_class_id, 1> material_panel_segments{GF_CLASS_CONTROL};

using CardLayers = ControlHooksLayer<BridgeState<Card>>;
constexpr std::array<gf_class_id, 1> card_segments{GF_CLASS_CONTROL};

using ContainerControlLayers = ControlHooksLayer<BridgeState<ContainerControl>>;
constexpr std::array<gf_class_id, 1> container_control_segments{GF_CLASS_CONTROL};

using UserControlLayers = ControlHooksLayer<BridgeState<UserControl>>;
constexpr std::array<gf_class_id, 1> user_control_segments{GF_CLASS_CONTROL};

using PageStripLayers = ControlHooksLayer<BridgeState<PageStrip>>;
constexpr std::array<gf_class_id, 1> page_strip_segments{GF_CLASS_CONTROL};

using PageViewLayers = ControlHooksLayer<BridgeState<PageView>>;
constexpr std::array<gf_class_id, 1> page_view_segments{GF_CLASS_CONTROL};

using NavigationPaneLayers = ControlHooksLayer<BridgeState<NavigationPane>>;
constexpr std::array<gf_class_id, 1> navigation_pane_segments{GF_CLASS_CONTROL};

using DockSiteLayers = ControlHooksLayer<BridgeState<DockSite>>;
constexpr std::array<gf_class_id, 1> dock_site_segments{GF_CLASS_CONTROL};

using DockNavigatorLayers = ControlHooksLayer<BridgeState<DockNavigator>>;
constexpr std::array<gf_class_id, 1> dock_navigator_segments{GF_CLASS_CONTROL};

using ScaledPanelLayers = ControlHooksLayer<BridgeState<ScaledPanel>>;
constexpr std::array<gf_class_id, 1> scaled_panel_segments{GF_CLASS_CONTROL};

using ScrollableControlLayers = ControlHooksLayer<BridgeState<ScrollableControl>>;
constexpr std::array<gf_class_id, 1> scrollable_control_segments{GF_CLASS_CONTROL};

using ScrollBarLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<ScrollBar>>>;
constexpr std::array<gf_class_id, 2> scroll_bar_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using ToolStripLayers = ControlHooksLayer<BridgeState<ToolStrip>>;
constexpr std::array<gf_class_id, 1> tool_strip_segments{GF_CLASS_CONTROL};

using GaugeBaseLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<GaugeBase>>>;
constexpr std::array<gf_class_id, 2> gauge_base_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using DialLayers = RangeControlHooksLayer<ControlHooksLayer<BridgeState<Dial>>>;
constexpr std::array<gf_class_id, 2> dial_segments{GF_CLASS_CONTROL, GF_CLASS_RANGE_CONTROL};

using RasterCanvasLayers = ControlHooksLayer<BridgeState<RasterCanvas>>;
constexpr std::array<gf_class_id, 1> raster_canvas_segments{GF_CLASS_CONTROL};

using TextBoxBaseLayers = ControlHooksLayer<BridgeState<TextBoxBase>>;
constexpr std::array<gf_class_id, 1> text_box_base_segments{GF_CLASS_CONTROL};

using ListControlLayers = ControlHooksLayer<BridgeState<ListControl>>;
constexpr std::array<gf_class_id, 1> list_control_segments{GF_CLASS_CONTROL};

using UpDownBaseLayers = ControlHooksLayer<BridgeState<UpDownBase>>;
constexpr std::array<gf_class_id, 1> up_down_base_segments{GF_CLASS_CONTROL};

using NodeCanvasLayers = NodeCanvasHooksLayer<ControlHooksLayer<BridgeState<NodeCanvas>>>;
constexpr std::array<gf_class_id, 2> node_canvas_segments{GF_CLASS_CONTROL, GF_CLASS_NODE_CANVAS};

constexpr std::array<gf_base_raiser, 43> control_base_raisers{
    &base_control_on_auto_size_changed,
    &base_control_on_back_color_changed,
    &base_control_on_causes_validation_changed,
    &base_control_on_cursor_changed,
    &base_control_on_dock_changed,
    &base_control_on_enabled_changed,
    &base_control_on_fore_color_changed,
    &base_control_on_margin_changed,
    &base_control_on_padding_changed,
    &base_control_on_right_to_left_changed,
    &base_control_on_tab_index_changed,
    &base_control_on_tab_stop_changed,
    &base_control_on_visible_changed,
    &base_control_on_change_ui_cues,
    &base_control_on_click,
    &base_control_on_double_click,
    &base_control_on_drag_drop,
    &base_control_on_drag_enter,
    &base_control_on_drag_leave,
    &base_control_on_drag_over,
    &base_control_on_enter,
    &base_control_on_got_focus,
    &base_control_on_help_requested,
    &base_control_on_key_down,
    &base_control_on_key_press,
    &base_control_on_key_up,
    &base_control_on_layout,
    &base_control_on_leave,
    &base_control_on_lost_focus,
    &base_control_on_mouse_capture_changed,
    &base_control_on_mouse_click,
    &base_control_on_mouse_double_click,
    &base_control_on_mouse_down,
    &base_control_on_mouse_enter,
    &base_control_on_mouse_hover,
    &base_control_on_mouse_leave,
    &base_control_on_mouse_move,
    &base_control_on_mouse_up,
    &base_control_on_mouse_wheel,
    &base_control_on_paint_background,
    &base_control_on_preview_key_down,
    &base_control_on_validated,
    &base_control_on_validating,
};

constexpr std::array<gf_base_raiser, 1> masked_text_box_base_raisers{
    &base_masked_text_box_on_mask_changed,
};

constexpr std::array<gf_base_raiser, 1> picture_box_base_raisers{
    &base_picture_box_on_size_mode_changed,
};

constexpr std::array<gf_base_raiser, 1> combo_box_base_raisers{
    &base_combo_box_on_drop_down_style_changed,
};

constexpr std::array<gf_base_raiser, 22> data_grid_view_base_raisers{
    &base_data_grid_view_on_allow_user_to_delete_rows_changed,
    &base_data_grid_view_on_allow_user_to_order_columns_changed,
    &base_data_grid_view_on_allow_user_to_resize_columns_changed,
    &base_data_grid_view_on_allow_user_to_resize_rows_changed,
    &base_data_grid_view_on_alternating_rows_default_cell_style_changed,
    &base_data_grid_view_on_auto_size_columns_mode_changed,
    &base_data_grid_view_on_auto_size_rows_mode_changed,
    &base_data_grid_view_on_background_color_changed,
    &base_data_grid_view_on_border_style_changed,
    &base_data_grid_view_on_cell_border_style_changed,
    &base_data_grid_view_on_column_headers_border_style_changed,
    &base_data_grid_view_on_column_headers_default_cell_style_changed,
    &base_data_grid_view_on_column_headers_height_changed,
    &base_data_grid_view_on_default_cell_style_changed,
    &base_data_grid_view_on_edit_mode_changed,
    &base_data_grid_view_on_grid_color_changed,
    &base_data_grid_view_on_multi_select_changed,
    &base_data_grid_view_on_read_only_changed,
    &base_data_grid_view_on_row_headers_border_style_changed,
    &base_data_grid_view_on_row_headers_default_cell_style_changed,
    &base_data_grid_view_on_row_headers_width_changed,
    &base_data_grid_view_on_rows_default_cell_style_changed,
};

constexpr std::array<gf_base_raiser, 3> check_box_base_raisers{
    &base_check_box_on_appearance_changed,
    &base_check_box_on_checked_changed,
    &base_check_box_on_check_state_changed,
};

constexpr std::array<gf_base_raiser, 1> radio_button_base_raisers{
    &base_radio_button_on_checked_changed,
};

constexpr std::array<gf_base_raiser, 1> range_control_base_raisers{
    &base_range_control_on_value_changed,
};

constexpr std::array<gf_base_raiser, 1> numeric_up_down_base_raisers{
    &base_numeric_up_down_on_value_changed,
};

constexpr std::array<gf_base_raiser, 2> date_time_picker_base_raisers{
    &base_date_time_picker_on_format_changed,
    &base_date_time_picker_on_value_changed,
};

constexpr std::array<gf_base_raiser, 6> node_canvas_base_raisers{
    &base_node_canvas_on_edit_ended,
    &base_node_canvas_on_graph_edited,
    &base_node_canvas_on_link_connected,
    &base_node_canvas_on_palette_requested,
    &base_node_canvas_on_selection_changed,
    &base_node_canvas_on_view_changed,
};

constexpr std::array<ClassDescription, 34> descriptions{{
    {GF_CLASS_BUTTON_BASE, "button_base", std::span<const gf_class_id>(button_base_segments),
     &create_bridge<ButtonBaseLayers>},
    {GF_CLASS_BUTTON, "button", std::span<const gf_class_id>(button_segments),
     &create_bridge<ButtonLayers>},
    {GF_CLASS_LINK_LABEL, "link_label", std::span<const gf_class_id>(link_label_segments),
     &create_bridge<LinkLabelLayers>},
    {GF_CLASS_LABEL, "label", std::span<const gf_class_id>(label_segments),
     &create_bridge<LabelLayers>},
    {GF_CLASS_PICTURE_BOX, "picture_box", std::span<const gf_class_id>(picture_box_segments),
     &create_bridge<PictureBoxLayers>},
    {GF_CLASS_PROGRESS_BAR, "progress_bar", std::span<const gf_class_id>(progress_bar_segments),
     &create_bridge<ProgressBarLayers>},
    {GF_CLASS_SWEEP_SCOPE, "sweep_scope", std::span<const gf_class_id>(sweep_scope_segments),
     &create_bridge<SweepScopeLayers>},
    {GF_CLASS_LIST_BOX, "list_box", std::span<const gf_class_id>(list_box_segments),
     &create_bridge<ListBoxLayers>},
    {GF_CLASS_CHECK_BOX, "check_box", std::span<const gf_class_id>(check_box_segments),
     &create_bridge<CheckBoxLayers>},
    {GF_CLASS_RADIO_BUTTON, "radio_button", std::span<const gf_class_id>(radio_button_segments),
     &create_bridge<RadioButtonLayers>},
    {GF_CLASS_RANGE_CONTROL, "range_control", std::span<const gf_class_id>(range_control_segments),
     &create_bridge<RangeControlLayers>},
    {GF_CLASS_TRACK_BAR, "track_bar", std::span<const gf_class_id>(track_bar_segments),
     &create_bridge<TrackBarLayers>},
    {GF_CLASS_PANEL, "panel", std::span<const gf_class_id>(panel_segments),
     &create_bridge<PanelLayers>},
    {GF_CLASS_GROUP_BOX, "group_box", std::span<const gf_class_id>(group_box_segments),
     &create_bridge<GroupBoxLayers>},
    {GF_CLASS_MATERIAL_PANEL, "material_panel", std::span<const gf_class_id>(material_panel_segments),
     &create_bridge<MaterialPanelLayers>},
    {GF_CLASS_CARD, "card", std::span<const gf_class_id>(card_segments),
     &create_bridge<CardLayers>},
    {GF_CLASS_CONTAINER_CONTROL, "container_control", std::span<const gf_class_id>(container_control_segments),
     &create_bridge<ContainerControlLayers>},
    {GF_CLASS_USER_CONTROL, "user_control", std::span<const gf_class_id>(user_control_segments),
     &create_bridge<UserControlLayers>},
    {GF_CLASS_PAGE_STRIP, "page_strip", std::span<const gf_class_id>(page_strip_segments),
     &create_bridge<PageStripLayers>},
    {GF_CLASS_PAGE_VIEW, "page_view", std::span<const gf_class_id>(page_view_segments),
     &create_bridge<PageViewLayers>},
    {GF_CLASS_NAVIGATION_PANE, "navigation_pane", std::span<const gf_class_id>(navigation_pane_segments),
     &create_bridge<NavigationPaneLayers>},
    {GF_CLASS_DOCK_SITE, "dock_site", std::span<const gf_class_id>(dock_site_segments),
     &create_bridge<DockSiteLayers>},
    {GF_CLASS_DOCK_NAVIGATOR, "dock_navigator", std::span<const gf_class_id>(dock_navigator_segments),
     &create_bridge<DockNavigatorLayers>},
    {GF_CLASS_SCALED_PANEL, "scaled_panel", std::span<const gf_class_id>(scaled_panel_segments),
     &create_bridge<ScaledPanelLayers>},
    {GF_CLASS_SCROLLABLE_CONTROL, "scrollable_control", std::span<const gf_class_id>(scrollable_control_segments),
     &create_bridge<ScrollableControlLayers>},
    {GF_CLASS_SCROLL_BAR, "scroll_bar", std::span<const gf_class_id>(scroll_bar_segments),
     &create_bridge<ScrollBarLayers>},
    {GF_CLASS_TOOL_STRIP, "tool_strip", std::span<const gf_class_id>(tool_strip_segments),
     &create_bridge<ToolStripLayers>},
    {GF_CLASS_GAUGE_BASE, "gauge_base", std::span<const gf_class_id>(gauge_base_segments),
     &create_bridge<GaugeBaseLayers>},
    {GF_CLASS_DIAL, "dial", std::span<const gf_class_id>(dial_segments),
     &create_bridge<DialLayers>},
    {GF_CLASS_RASTER_CANVAS, "raster_canvas", std::span<const gf_class_id>(raster_canvas_segments),
     &create_bridge<RasterCanvasLayers>},
    {GF_CLASS_TEXT_BOX_BASE, "text_box_base", std::span<const gf_class_id>(text_box_base_segments),
     &create_bridge<TextBoxBaseLayers>},
    {GF_CLASS_LIST_CONTROL, "list_control", std::span<const gf_class_id>(list_control_segments),
     &create_bridge<ListControlLayers>},
    {GF_CLASS_UP_DOWN_BASE, "up_down_base", std::span<const gf_class_id>(up_down_base_segments),
     &create_bridge<UpDownBaseLayers>},
    {GF_CLASS_NODE_CANVAS, "node_canvas", std::span<const gf_class_id>(node_canvas_segments),
     &create_bridge<NodeCanvasLayers>},
}};

} // namespace

std::span<const ClassDescription> class_descriptions() noexcept {
    const std::span<const ClassDescription> all(descriptions);
    return all;
}

gf_base_raiser find_base_raiser(gf_class_id class_id, std::uint32_t entry) noexcept {
    std::span<const gf_base_raiser> segment;
    switch (class_id) {
    case GF_CLASS_CONTROL:
        segment = std::span<const gf_base_raiser>(control_base_raisers);
        break;
    case GF_CLASS_MASKED_TEXT_BOX:
        segment = std::span<const gf_base_raiser>(masked_text_box_base_raisers);
        break;
    case GF_CLASS_PICTURE_BOX:
        segment = std::span<const gf_base_raiser>(picture_box_base_raisers);
        break;
    case GF_CLASS_COMBO_BOX:
        segment = std::span<const gf_base_raiser>(combo_box_base_raisers);
        break;
    case GF_CLASS_DATA_GRID_VIEW:
        segment = std::span<const gf_base_raiser>(data_grid_view_base_raisers);
        break;
    case GF_CLASS_CHECK_BOX:
        segment = std::span<const gf_base_raiser>(check_box_base_raisers);
        break;
    case GF_CLASS_RADIO_BUTTON:
        segment = std::span<const gf_base_raiser>(radio_button_base_raisers);
        break;
    case GF_CLASS_RANGE_CONTROL:
        segment = std::span<const gf_base_raiser>(range_control_base_raisers);
        break;
    case GF_CLASS_NUMERIC_UP_DOWN:
        segment = std::span<const gf_base_raiser>(numeric_up_down_base_raisers);
        break;
    case GF_CLASS_DATE_TIME_PICKER:
        segment = std::span<const gf_base_raiser>(date_time_picker_base_raisers);
        break;
    case GF_CLASS_NODE_CANVAS:
        segment = std::span<const gf_base_raiser>(node_canvas_base_raisers);
        break;
    default:
        // A class with no segment has no base function.
        break;
    }
    const gf_base_raiser found = entry < segment.size() ? segment[entry] : nullptr;
    return found;
}

} // namespace gui_forms::abi1
