/* Generated from manifests/abi/class_tables.json by `python3 tools/abi_class_tables.py`. Do not edit. */

#ifndef GUI_FORMS_C_GF_ABI_CLASSES_H
#define GUI_FORMS_C_GF_ABI_CLASSES_H

/*
 * GUI.Forms C ABI class tables (since 1.3): Windows Forms' protected On*
 * hooks for C and C#. Included by gf_abi.h only, inside its extern "C" block;
 * include gf_abi.h.
 *
 * A raiser is the protected C++ virtual that runs when its event happens and
 * whose base raises the event to the subscribers (Control::on_visible_changed
 * raises visible_changed). A class table overrides raisers for the objects
 * classes.create makes from it:
 *
 * - A segment (gf_<class>_hooks) holds one class's own raisers as entries. It
 *   begins with struct_size (the caller's sizeof) and class_id (the class's
 *   GF_CLASS_* id). Entries are only appended in a later minor; the library
 *   reads entries up to the caller's struct_size and treats the rest as
 *   absent. A null entry is no override: the C++ raiser runs as if there were
 *   no table.
 * - A class table (gf_<class>_class) is a gf_class_header, then one segment
 *   pointer per segment of the class's chain, base first. Segment pointers are
 *   only appended in a later minor; the library finds each segment by its
 *   class_id, never by its position, so a table compiled before a segment was
 *   appended still works. A null segment pointer is no override. The segment
 *   pointers begin header.struct_size bytes after the header's start.
 * - GF_CLASS_* ids are never renumbered or reused.
 *
 * The order contract (the ADR-003 amendment, decisions/ABI_CLASS_TABLES):
 * a raiser runs at one documented point, after the state change it reports
 * and its invalidation (P-8). Where an object's table has an entry for it,
 * the library calls the entry with the caller's instance, the object (zero
 * while it has no identity), an event and a base function, before any
 * subscriber. The subscribers run only inside base: base runs the C++ raiser
 * below the table (the stock class's own override where it has one, then its
 * bases, ending in the base raiser that emits the event). An entry that never
 * calls base suppresses the subscribers; calling it twice raises twice.
 * Nested raises are nested calls, and the subscription rules of
 * GF_TABLE_EVENTS hold. Everything runs on the UI thread. Once the object is
 * disposed no entry is called again; base after disposal gives
 * GF_STATUS_STALE_HANDLE and runs nothing, base from another thread
 * GF_STATUS_WRONG_THREAD. A property raiser's event is named after the
 * property, carries no payload and no fields, and its cancel is ignored.
 *
 * An interaction raiser's event (Control's on_click, on_key_down and the
 * rest) is named after its event (click, key_down). Where the raiser has an
 * argument with a field form, the event carries it as fields, named as
 * GF_TABLE_EVENTS names that event's fields, and writable for an argument
 * the raiser may change (a KeyEventArgs's handled). What the entry writes
 * before base is what the subscribers see; after base returns the event
 * carries the argument as the subscribers left it (read its fields again:
 * the records read before base are no longer valid), and what the entry
 * writes then is what the control sees. A base function called outside an
 * entry gives the raiser a default argument, except for a raiser whose
 * argument only a raise in progress can lend (on_paint_background's
 * painter): that gives GF_STATUS_INVALID_ARGUMENT and runs nothing.
 */

/* gf_class_id: a class that has a segment or a class table. */
typedef uint32_t gf_class_id;
#define GF_CLASS_COMPONENT 1u
#define GF_CLASS_CONTROL 2u
#define GF_CLASS_BUTTON_BASE 3u
#define GF_CLASS_BUTTON 4u
#define GF_CLASS_LINK_LABEL 5u
#define GF_CLASS_LABEL 6u
#define GF_CLASS_SCROLLABLE_CONTROL 7u
#define GF_CLASS_PANEL 8u
#define GF_CLASS_PICTURE_BOX 9u
#define GF_CLASS_RANGE_CONTROL 10u
#define GF_CLASS_PROGRESS_BAR 11u
#define GF_CLASS_SWEEP_SCOPE 12u
#define GF_CLASS_LIST_CONTROL 13u
#define GF_CLASS_LIST_BOX 14u
#define GF_CLASS_CHECK_BOX 15u
#define GF_CLASS_RADIO_BUTTON 16u
#define GF_CLASS_TRACK_BAR 17u
#define GF_CLASS_GROUP_BOX 18u
#define GF_CLASS_MATERIAL_PANEL 19u
#define GF_CLASS_CARD 20u
#define GF_CLASS_CONTAINER_CONTROL 21u
#define GF_CLASS_USER_CONTROL 22u
#define GF_CLASS_PAGE_STRIP 23u
#define GF_CLASS_PAGE_VIEW 24u
#define GF_CLASS_NAVIGATION_PANE 25u
#define GF_CLASS_DOCK_SITE 26u
#define GF_CLASS_DOCK_NAVIGATOR 27u
#define GF_CLASS_SCALED_PANEL 28u
#define GF_CLASS_SCROLL_BAR 29u
#define GF_CLASS_TOOL_STRIP 30u
#define GF_CLASS_GAUGE_BASE 31u
#define GF_CLASS_DIAL 32u
#define GF_CLASS_RASTER_CANVAS 33u
#define GF_CLASS_TEXT_BOX_BASE 34u
#define GF_CLASS_UP_DOWN_BASE 35u
#define GF_CLASS_MASKED_TEXT_BOX 36u
#define GF_CLASS_COMBO_BOX 37u
#define GF_CLASS_DATA_GRID_VIEW 38u
#define GF_CLASS_NUMERIC_UP_DOWN 39u
#define GF_CLASS_DATE_TIME_PICKER 40u
#define GF_CLASS_NODE_CANVAS 41u

/* Runs the C++ raiser below the class table for the object self, which
 * emits the event to the subscribers; event is the one the entry received
 * (or, called outside an entry, one the caller made). */
typedef gf_status (GF_CALL* gf_base_raiser)(gf_object self, gf_event* event);
/* One override: called where the raiser runs, with the instance the object
 * was made with. base is valid on the UI thread for the object's life. */
typedef void (GF_CALL* gf_hook_entry)(void* instance, gf_object self, gf_event* event, gf_base_raiser base);

/* The first eight bytes of every segment. */
typedef struct gf_segment_header {
    uint32_t struct_size;
    gf_class_id class_id;
} gf_segment_header;

/*
 * The start of every class table. struct_size is the caller's sizeof of
 * this header (at least the 1.3 size, a multiple of 8): the segment pointers
 * begin that many bytes after it. abi_minor is the GF_ABI_MINOR the caller
 * compiled with (at least 3). class_id is the most derived class the table is
 * for; segment_count the segment pointers that follow (at most the class's
 * own count). release, which may be null, is called with the instance
 * exactly once, on the UI thread, when the object is destroyed.
 */
typedef struct gf_class_header {
    uint32_t struct_size;
    uint32_t abi_minor;
    gf_class_id class_id;
    uint32_t segment_count;
    void (GF_CALL* release)(void* instance);
} gf_class_header;

/* Control's raisers (GF_CLASS_CONTROL). */
typedef struct gf_control_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* Control::on_auto_size_changed: after a changed write of auto_size; base raises auto_size_changed. Since 1.3. */
    gf_hook_entry on_auto_size_changed;
    /* Control::on_back_color_changed: after a changed write of back_color; base raises no event yet. Since 1.3. */
    gf_hook_entry on_back_color_changed;
    /* Control::on_causes_validation_changed: after a changed write of causes_validation; base raises causes_validation_changed. Since 1.3. */
    gf_hook_entry on_causes_validation_changed;
    /* Control::on_cursor_changed: after a changed write of cursor; base raises no event yet. Since 1.3. */
    gf_hook_entry on_cursor_changed;
    /* Control::on_dock_changed: after a changed write of dock; base raises no event yet. Since 1.3. */
    gf_hook_entry on_dock_changed;
    /* Control::on_enabled_changed: after a changed write of enabled; base raises enabled_changed. Since 1.3. */
    gf_hook_entry on_enabled_changed;
    /* Control::on_fore_color_changed: after a changed write of fore_color; base raises no event yet. Since 1.3. */
    gf_hook_entry on_fore_color_changed;
    /* Control::on_margin_changed: after a changed write of margin; base raises no event yet. Since 1.3. */
    gf_hook_entry on_margin_changed;
    /* Control::on_padding_changed: after a changed write of padding; base raises no event yet. Since 1.3. */
    gf_hook_entry on_padding_changed;
    /* Control::on_right_to_left_changed: after a changed write of right_to_left; base raises right_to_left_changed. Since 1.3. */
    gf_hook_entry on_right_to_left_changed;
    /* Control::on_tab_index_changed: after a changed write of tab_index; base raises no event yet. Since 1.3. */
    gf_hook_entry on_tab_index_changed;
    /* Control::on_tab_stop_changed: after a changed write of tab_stop; base raises no event yet. Since 1.3. */
    gf_hook_entry on_tab_stop_changed;
    /* Control::on_visible_changed: after a changed write of visible; base raises visible_changed. Since 1.3. */
    gf_hook_entry on_visible_changed;
    /* Control::on_change_ui_cues: where change_ui_cues happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_change_ui_cues;
    /* Control::on_click: where click happens; base raises click. Since 1.3. */
    gf_hook_entry on_click;
    /* Control::on_double_click: where double_click happens; base raises double_click. Since 1.3. */
    gf_hook_entry on_double_click;
    /* Control::on_drag_drop: where drag_drop happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_drag_drop;
    /* Control::on_drag_enter: where drag_enter happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_drag_enter;
    /* Control::on_drag_leave: where drag_leave happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_drag_leave;
    /* Control::on_drag_over: where drag_over happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_drag_over;
    /* Control::on_enter: where enter happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_enter;
    /* Control::on_got_focus: where got_focus happens; base raises focus_observed. Since 1.3. */
    gf_hook_entry on_got_focus;
    /* Control::on_help_requested: where help_requested happens; base raises help_requested; the event's fields are its HelpRequestEvent, writable. Since 1.3. */
    gf_hook_entry on_help_requested;
    /* Control::on_key_down: where key_down happens; base raises key_down; the event's fields are its KeyEventArgs, writable. Since 1.3. */
    gf_hook_entry on_key_down;
    /* Control::on_key_press: where key_press happens; base raises key_press; the event's fields are its KeyPressEventArgs, writable. Since 1.3. */
    gf_hook_entry on_key_press;
    /* Control::on_key_up: where key_up happens; base raises key_up; the event's fields are its KeyEventArgs, writable. Since 1.3. */
    gf_hook_entry on_key_up;
    /* Control::on_layout: where layout happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_layout;
    /* Control::on_leave: where leave happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_leave;
    /* Control::on_lost_focus: where lost_focus happens; base raises focus_observed. Since 1.3. */
    gf_hook_entry on_lost_focus;
    /* Control::on_mouse_capture_changed: where mouse_capture_changed happens; base raises mouse_capture_changed. Since 1.3. */
    gf_hook_entry on_mouse_capture_changed;
    /* Control::on_mouse_click: where mouse_click happens; base raises mouse_click; the event's fields are its MouseEventArgs, writable. Since 1.3. */
    gf_hook_entry on_mouse_click;
    /* Control::on_mouse_double_click: where mouse_double_click happens; base raises mouse_double_click; the event's fields are its MouseEventArgs, writable. Since 1.3. */
    gf_hook_entry on_mouse_double_click;
    /* Control::on_mouse_down: where mouse_down happens; base raises mouse_down; the event's fields are its MouseEventArgs, writable. Since 1.3. */
    gf_hook_entry on_mouse_down;
    /* Control::on_mouse_enter: where mouse_enter happens; base raises mouse_enter. Since 1.3. */
    gf_hook_entry on_mouse_enter;
    /* Control::on_mouse_hover: where mouse_hover happens; base raises mouse_hover. Since 1.3. */
    gf_hook_entry on_mouse_hover;
    /* Control::on_mouse_leave: where mouse_leave happens; base raises mouse_leave. Since 1.3. */
    gf_hook_entry on_mouse_leave;
    /* Control::on_mouse_move: where mouse_move happens; base raises mouse_move; the event's fields are its MouseEventArgs, writable. Since 1.3. */
    gf_hook_entry on_mouse_move;
    /* Control::on_mouse_up: where mouse_up happens; base raises mouse_up; the event's fields are its MouseEventArgs, writable. Since 1.3. */
    gf_hook_entry on_mouse_up;
    /* Control::on_mouse_wheel: where mouse_wheel happens; base raises mouse_wheel; the event's fields are its MouseEventArgs, writable. Since 1.3. */
    gf_hook_entry on_mouse_wheel;
    /* Control::on_paint_background: where paint_background happens; base raises no event yet. Since 1.3. */
    gf_hook_entry on_paint_background;
    /* Control::on_preview_key_down: where preview_key_down happens; base raises preview_key_down; the event's fields are its PreviewKeyDownEventArgs, writable. Since 1.3. */
    gf_hook_entry on_preview_key_down;
    /* Control::on_validated: where validated happens; base raises validated. Since 1.3. */
    gf_hook_entry on_validated;
    /* Control::on_validating: where validating happens; base raises validating; the event's fields are its ControlValidationEvent, writable. Since 1.3. */
    gf_hook_entry on_validating;
} gf_control_hooks;

/* MaskedTextBox's raisers (GF_CLASS_MASKED_TEXT_BOX). */
typedef struct gf_masked_text_box_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* MaskedTextBox::on_mask_changed: after a changed write of mask; base raises mask_changed. Since 1.3. */
    gf_hook_entry on_mask_changed;
} gf_masked_text_box_hooks;

/* PictureBox's raisers (GF_CLASS_PICTURE_BOX). */
typedef struct gf_picture_box_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* PictureBox::on_size_mode_changed: after a changed write of size_mode; base raises no event yet. Since 1.3. */
    gf_hook_entry on_size_mode_changed;
} gf_picture_box_hooks;

/* ComboBox's raisers (GF_CLASS_COMBO_BOX). */
typedef struct gf_combo_box_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* ComboBox::on_drop_down_style_changed: after a changed write of drop_down_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_drop_down_style_changed;
} gf_combo_box_hooks;

/* DataGridView's raisers (GF_CLASS_DATA_GRID_VIEW). */
typedef struct gf_data_grid_view_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* DataGridView::on_allow_user_to_delete_rows_changed: after a changed write of allow_user_to_delete_rows; base raises no event yet. Since 1.3. */
    gf_hook_entry on_allow_user_to_delete_rows_changed;
    /* DataGridView::on_allow_user_to_order_columns_changed: after a changed write of allow_user_to_order_columns; base raises no event yet. Since 1.3. */
    gf_hook_entry on_allow_user_to_order_columns_changed;
    /* DataGridView::on_allow_user_to_resize_columns_changed: after a changed write of allow_user_to_resize_columns; base raises no event yet. Since 1.3. */
    gf_hook_entry on_allow_user_to_resize_columns_changed;
    /* DataGridView::on_allow_user_to_resize_rows_changed: after a changed write of allow_user_to_resize_rows; base raises no event yet. Since 1.3. */
    gf_hook_entry on_allow_user_to_resize_rows_changed;
    /* DataGridView::on_alternating_rows_default_cell_style_changed: after a changed write of alternating_rows_default_cell_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_alternating_rows_default_cell_style_changed;
    /* DataGridView::on_auto_size_columns_mode_changed: after a changed write of auto_size_columns_mode; base raises no event yet. Since 1.3. */
    gf_hook_entry on_auto_size_columns_mode_changed;
    /* DataGridView::on_auto_size_rows_mode_changed: after a changed write of auto_size_rows_mode; base raises no event yet. Since 1.3. */
    gf_hook_entry on_auto_size_rows_mode_changed;
    /* DataGridView::on_background_color_changed: after a changed write of background_color; base raises no event yet. Since 1.3. */
    gf_hook_entry on_background_color_changed;
    /* DataGridView::on_border_style_changed: after a changed write of border_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_border_style_changed;
    /* DataGridView::on_cell_border_style_changed: after a changed write of cell_border_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_cell_border_style_changed;
    /* DataGridView::on_column_headers_border_style_changed: after a changed write of column_headers_border_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_column_headers_border_style_changed;
    /* DataGridView::on_column_headers_default_cell_style_changed: after a changed write of column_headers_default_cell_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_column_headers_default_cell_style_changed;
    /* DataGridView::on_column_headers_height_changed: after a changed write of column_headers_height; base raises no event yet. Since 1.3. */
    gf_hook_entry on_column_headers_height_changed;
    /* DataGridView::on_default_cell_style_changed: after a changed write of default_cell_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_default_cell_style_changed;
    /* DataGridView::on_edit_mode_changed: after a changed write of edit_mode; base raises no event yet. Since 1.3. */
    gf_hook_entry on_edit_mode_changed;
    /* DataGridView::on_grid_color_changed: after a changed write of grid_color; base raises no event yet. Since 1.3. */
    gf_hook_entry on_grid_color_changed;
    /* DataGridView::on_multi_select_changed: after a changed write of multi_select; base raises no event yet. Since 1.3. */
    gf_hook_entry on_multi_select_changed;
    /* DataGridView::on_read_only_changed: after a changed write of read_only; base raises no event yet. Since 1.3. */
    gf_hook_entry on_read_only_changed;
    /* DataGridView::on_row_headers_border_style_changed: after a changed write of row_headers_border_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_row_headers_border_style_changed;
    /* DataGridView::on_row_headers_default_cell_style_changed: after a changed write of row_headers_default_cell_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_row_headers_default_cell_style_changed;
    /* DataGridView::on_row_headers_width_changed: after a changed write of row_headers_width; base raises no event yet. Since 1.3. */
    gf_hook_entry on_row_headers_width_changed;
    /* DataGridView::on_rows_default_cell_style_changed: after a changed write of rows_default_cell_style; base raises no event yet. Since 1.3. */
    gf_hook_entry on_rows_default_cell_style_changed;
} gf_data_grid_view_hooks;

/* CheckBox's raisers (GF_CLASS_CHECK_BOX). */
typedef struct gf_check_box_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* CheckBox::on_appearance_changed: after a changed write of appearance; base raises no event yet. Since 1.3. */
    gf_hook_entry on_appearance_changed;
    /* CheckBox::on_checked_changed: after a changed write of checked; base raises checked_changed. Since 1.3. */
    gf_hook_entry on_checked_changed;
    /* CheckBox::on_check_state_changed: after a changed write of check_state; base raises check_state_changed. Since 1.3. */
    gf_hook_entry on_check_state_changed;
} gf_check_box_hooks;

/* RadioButton's raisers (GF_CLASS_RADIO_BUTTON). */
typedef struct gf_radio_button_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* RadioButton::on_checked_changed: after a changed write of checked; base raises checked_changed. Since 1.3. */
    gf_hook_entry on_checked_changed;
} gf_radio_button_hooks;

/* RangeControl's raisers (GF_CLASS_RANGE_CONTROL). */
typedef struct gf_range_control_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* RangeControl::on_value_changed: after a changed write of value; base raises value_changed. Since 1.3. */
    gf_hook_entry on_value_changed;
} gf_range_control_hooks;

/* NumericUpDown's raisers (GF_CLASS_NUMERIC_UP_DOWN). */
typedef struct gf_numeric_up_down_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* NumericUpDown::on_value_changed: after a changed write of value; base raises value_changed. Since 1.3. */
    gf_hook_entry on_value_changed;
} gf_numeric_up_down_hooks;

/* DateTimePicker's raisers (GF_CLASS_DATE_TIME_PICKER). */
typedef struct gf_date_time_picker_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* DateTimePicker::on_format_changed: after a changed write of format; base raises no event yet. Since 1.3. */
    gf_hook_entry on_format_changed;
    /* DateTimePicker::on_value_changed: after a changed write of value; base raises value_changed. Since 1.3. */
    gf_hook_entry on_value_changed;
} gf_date_time_picker_hooks;

/* NodeCanvas's raisers (GF_CLASS_NODE_CANVAS). */
typedef struct gf_node_canvas_hooks {
    uint32_t struct_size;
    gf_class_id class_id;
    /* NodeCanvas::on_edit_ended: where edit_ended happens; base raises edit_ended; the event's fields are its NodeEditEnded. Since 1.3. */
    gf_hook_entry on_edit_ended;
    /* NodeCanvas::on_graph_edited: where graph_edited happens; base raises graph_edited; the event's fields are its NodeGraphEdited. Since 1.3. */
    gf_hook_entry on_graph_edited;
    /* NodeCanvas::on_link_connected: where link_connected happens; base raises link_connected; the event's fields are its NodeLinkConnected. Since 1.3. */
    gf_hook_entry on_link_connected;
    /* NodeCanvas::on_palette_requested: where palette_requested happens; base raises palette_requested; the event's fields are its NodePaletteRequest. Since 1.3. */
    gf_hook_entry on_palette_requested;
    /* NodeCanvas::on_selection_changed: where selection_changed happens; base raises selection_changed; the event's fields are its NodeCanvasSelection. Since 1.3. */
    gf_hook_entry on_selection_changed;
    /* NodeCanvas::on_view_changed: where view_changed happens; base raises view_changed; the event's fields are its SceneViewTransform. Since 1.3. */
    gf_hook_entry on_view_changed;
} gf_node_canvas_hooks;

/* A class table for ButtonBase (GF_CLASS_BUTTON_BASE): header.segment_count is 1. */
typedef struct gf_button_base_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_button_base_class;

/* A class table for Button (GF_CLASS_BUTTON): header.segment_count is 1. */
typedef struct gf_button_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_button_class;

/* A class table for LinkLabel (GF_CLASS_LINK_LABEL): header.segment_count is 1. */
typedef struct gf_link_label_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_link_label_class;

/* A class table for Label (GF_CLASS_LABEL): header.segment_count is 1. */
typedef struct gf_label_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_label_class;

/* A class table for PictureBox (GF_CLASS_PICTURE_BOX): header.segment_count is 2. */
typedef struct gf_picture_box_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_picture_box_hooks* picture_box;
} gf_picture_box_class;

/* A class table for ProgressBar (GF_CLASS_PROGRESS_BAR): header.segment_count is 2. */
typedef struct gf_progress_bar_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_progress_bar_class;

/* A class table for SweepScope (GF_CLASS_SWEEP_SCOPE): header.segment_count is 2. */
typedef struct gf_sweep_scope_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_sweep_scope_class;

/* A class table for ListBox (GF_CLASS_LIST_BOX): header.segment_count is 1. */
typedef struct gf_list_box_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_list_box_class;

/* A class table for CheckBox (GF_CLASS_CHECK_BOX): header.segment_count is 2. */
typedef struct gf_check_box_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_check_box_hooks* check_box;
} gf_check_box_class;

/* A class table for RadioButton (GF_CLASS_RADIO_BUTTON): header.segment_count is 2. */
typedef struct gf_radio_button_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_radio_button_hooks* radio_button;
} gf_radio_button_class;

/* A class table for RangeControl (GF_CLASS_RANGE_CONTROL): header.segment_count is 2. */
typedef struct gf_range_control_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_range_control_class;

/* A class table for TrackBar (GF_CLASS_TRACK_BAR): header.segment_count is 2. */
typedef struct gf_track_bar_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_track_bar_class;

/* A class table for Panel (GF_CLASS_PANEL): header.segment_count is 1. */
typedef struct gf_panel_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_panel_class;

/* A class table for GroupBox (GF_CLASS_GROUP_BOX): header.segment_count is 1. */
typedef struct gf_group_box_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_group_box_class;

/* A class table for MaterialPanel (GF_CLASS_MATERIAL_PANEL): header.segment_count is 1. */
typedef struct gf_material_panel_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_material_panel_class;

/* A class table for Card (GF_CLASS_CARD): header.segment_count is 1. */
typedef struct gf_card_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_card_class;

/* A class table for ContainerControl (GF_CLASS_CONTAINER_CONTROL): header.segment_count is 1. */
typedef struct gf_container_control_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_container_control_class;

/* A class table for UserControl (GF_CLASS_USER_CONTROL): header.segment_count is 1. */
typedef struct gf_user_control_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_user_control_class;

/* A class table for PageStrip (GF_CLASS_PAGE_STRIP): header.segment_count is 1. */
typedef struct gf_page_strip_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_page_strip_class;

/* A class table for PageView (GF_CLASS_PAGE_VIEW): header.segment_count is 1. */
typedef struct gf_page_view_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_page_view_class;

/* A class table for NavigationPane (GF_CLASS_NAVIGATION_PANE): header.segment_count is 1. */
typedef struct gf_navigation_pane_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_navigation_pane_class;

/* A class table for DockSite (GF_CLASS_DOCK_SITE): header.segment_count is 1. */
typedef struct gf_dock_site_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_dock_site_class;

/* A class table for DockNavigator (GF_CLASS_DOCK_NAVIGATOR): header.segment_count is 1. */
typedef struct gf_dock_navigator_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_dock_navigator_class;

/* A class table for ScaledPanel (GF_CLASS_SCALED_PANEL): header.segment_count is 1. */
typedef struct gf_scaled_panel_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_scaled_panel_class;

/* A class table for ScrollableControl (GF_CLASS_SCROLLABLE_CONTROL): header.segment_count is 1. */
typedef struct gf_scrollable_control_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_scrollable_control_class;

/* A class table for ScrollBar (GF_CLASS_SCROLL_BAR): header.segment_count is 2. */
typedef struct gf_scroll_bar_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_scroll_bar_class;

/* A class table for ToolStrip (GF_CLASS_TOOL_STRIP): header.segment_count is 1. */
typedef struct gf_tool_strip_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_tool_strip_class;

/* A class table for GaugeBase (GF_CLASS_GAUGE_BASE): header.segment_count is 2. */
typedef struct gf_gauge_base_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_gauge_base_class;

/* A class table for Dial (GF_CLASS_DIAL): header.segment_count is 2. */
typedef struct gf_dial_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_range_control_hooks* range_control;
} gf_dial_class;

/* A class table for RasterCanvas (GF_CLASS_RASTER_CANVAS): header.segment_count is 1. */
typedef struct gf_raster_canvas_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_raster_canvas_class;

/* A class table for TextBoxBase (GF_CLASS_TEXT_BOX_BASE): header.segment_count is 1. */
typedef struct gf_text_box_base_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_text_box_base_class;

/* A class table for ListControl (GF_CLASS_LIST_CONTROL): header.segment_count is 1. */
typedef struct gf_list_control_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_list_control_class;

/* A class table for UpDownBase (GF_CLASS_UP_DOWN_BASE): header.segment_count is 1. */
typedef struct gf_up_down_base_class {
    gf_class_header header;
    const gf_control_hooks* control;
} gf_up_down_base_class;

/* A class table for NodeCanvas (GF_CLASS_NODE_CANVAS): header.segment_count is 2. */
typedef struct gf_node_canvas_class {
    gf_class_header header;
    const gf_control_hooks* control;
    const gf_node_canvas_hooks* node_canvas;
} gf_node_canvas_class;

/*
 * GF_TABLE_CLASSES (since 1.3): objects made from class tables.
 */
typedef struct gf_classes_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Creates an object of table's class (header.class_id) with stable_id, as
     * objects.create makes the stock type, and gives the caller one reference.
     * The table is borrowed: the caller keeps it alive and unchanged for as
     * long as any object made from it lives (in practice it is static, one per
     * class per program). instance is lent until release(instance) returns.
     * A class with no class table gives GF_STATUS_NOT_FOUND; a malformed table
     * (a header smaller than the 1.3 size or not a multiple of 8, a segment
     * count above the class's, a segment whose class_id is not of the class's
     * chain or repeats, a segment smaller than its header) gives
     * GF_STATUS_INVALID_ARGUMENT, and an abi_minor newer than the library's
     * GF_STATUS_UNSUPPORTED_VERSION: nothing is created and release is not
     * called. The class of an object is fixed at creation. */
    gf_status (GF_CALL* create)(const gf_class_header* table, gf_utf8_view stable_id, void* instance,
                                gf_object* object);
    /* The base function of entry (its position in the segment, from zero) of
     * class_id's segment, for a caller that runs a raiser outside an entry (a
     * C# OnX called directly). It needs an object of a class whose chain has
     * that segment (GF_STATUS_WRONG_HANDLE_KIND otherwise). An unknown class
     * or entry gives GF_STATUS_NOT_FOUND. Any thread. */
    gf_status (GF_CALL* base_raiser)(gf_class_id class_id, uint32_t entry, gf_base_raiser* raiser);
} gf_classes_table;

#endif
