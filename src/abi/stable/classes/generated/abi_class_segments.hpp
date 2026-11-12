// Generated from manifests/abi/class_tables.json by `python3 tools/abi_class_tables.py`. Do not edit.
// One bridge layer per segment: Base is the stock class, or a layer over it.
// Included only by abi_class_registry.cpp.

#pragma once

#include "../abi_class_bridge.hpp"

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

    // Runs the raiser below this layer for one of Control's hooks; another
    // class's hook goes to the layers further in.
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_CONTROL) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
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
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_MASKED_TEXT_BOX) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_mask_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_PICTURE_BOX) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_size_mode_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_COMBO_BOX) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_drop_down_style_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_DATA_GRID_VIEW) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
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
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_CHECK_BOX) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
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
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_RADIO_BUTTON) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_checked_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_RANGE_CONTROL) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_value_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_NUMERIC_UP_DOWN) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_value_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
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
    [[nodiscard]] bool run_layer_base(HookKey hook) {
        if (hook.class_id != GF_CLASS_DATE_TIME_PICKER) {
            const bool inner = Base::run_layer_base(hook);
            return inner;
        }
        bool ran = true;
        switch (hook.entry) {
        case 0U:
            Base::on_format_changed();
            break;
        case 1U:
            Base::on_value_changed();
            break;
        default:
            ran = false;
            break;
        }
        return ran;
    }
};

} // namespace gui_forms::abi1
