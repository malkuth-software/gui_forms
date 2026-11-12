/* Generated from manifests/abi/class_tables.json by `python3 tools/abi_class_tables.py`. Do not edit. */
/* Kit part: the class tables (GF_TABLE_CLASSES, since 1.3), per entry the
 * order contract of gf_abi_classes.h: the entry runs once per raise, before the
 * subscribers; code after base runs after them; an entry that skips base
 * suppresses them; base after disposal gives GF_STATUS_STALE_HANDLE and no
 * entry runs after disposal; release runs once, at destruction, and never for
 * a refused table. Then the refusals, base_raiser, a direct base call and
 * base from another thread. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <string.h>

/* What an entry does with base. */
#define CLASS_MODE_AROUND 0u
#define CLASS_MODE_SUPPRESS 1u
#define CLASS_MODE_DISPOSE 2u

/* One object's instance: the marks its entry and subscriber leave, in order
 * ("B" before base, "S" a subscriber, "A" after base). */
typedef struct class_trace {
    gf_kit* kit;
    uint32_t mode;
    uint32_t entries;
    uint32_t releases;
    uint32_t mark_count;
    gf_status base_status;
    uint32_t reserved;
    char marks[16];
} class_trace;

static void class_mark(class_trace* trace, char mark) {
    if ((*trace).mark_count + 1u < sizeof((*trace).marks)) {
        (*trace).marks[(*trace).mark_count] = mark;
        ++(*trace).mark_count;
        (*trace).marks[(*trace).mark_count] = '\0';
    }
}

static void class_reset(class_trace* trace) {
    (*trace).entries = 0u;
    (*trace).mark_count = 0u;
    (*trace).marks[0] = '\0';
    (*trace).base_status = GF_STATUS_OK;
}

static void GF_CALL class_entry(void* instance, gf_object self, gf_event* event, gf_base_raiser base) {
    class_trace* trace = (class_trace*)instance;
    ++(*trace).entries;
    class_mark(trace, 'B');
    if ((*trace).mode == CLASS_MODE_SUPPRESS) {
        return;
    }
    if ((*trace).mode == CLASS_MODE_DISPOSE) {
        (void)(*(*trace).kit).objects.dispose(self);
        (*trace).base_status = base(self, event);
        return;
    }
    (*trace).base_status = base(self, event);
    class_mark(trace, 'A');
}

static void GF_CALL class_release(void* instance) {
    class_trace* trace = (class_trace*)instance;
    ++(*trace).releases;
}

static void GF_CALL class_subscriber(void* context, gf_event* event) {
    (void)event;
    class_mark((class_trace*)context, 'S');
}

static gf_value class_value(gf_value_kind kind) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.kind = kind;
    return value;
}

/* The checks of one entry in one mode. */
static void class_run_mode(gf_kit* kit, const gf_class_header* table, const char* label, const char* property,
                           const gf_value* first, const gf_value* second, const char* interaction, uint32_t mode) {
    class_trace trace;
    memset(&trace, 0, sizeof(trace));
    trace.kit = kit;
    trace.mode = mode;
    char name[160];
    gf_object object = 0u;
    snprintf(name, sizeof(name), "%s: create", label);
    gf_kit_status(kit, (*kit).classes.create(table, gf_kit_text("kit.classes.case"), &trace, &object), GF_STATUS_OK,
                  name);
    if (object == 0u) {
        return;
    }
    gf_subscription subscription = 0u;
    if (interaction != NULL) {
        const gf_callback callback = gf_kit_callback(&class_subscriber, NULL, &trace);
        snprintf(name, sizeof(name), "%s: subscribe %s", label, interaction);
        gf_kit_status(kit, (*kit).events.subscribe(object, gf_kit_text(interaction), &callback, &subscription),
                      GF_STATUS_OK, name);
    }
    const uint32_t saved_mode = trace.mode;
    trace.mode = CLASS_MODE_AROUND;
    (void)(*kit).properties.set(object, gf_kit_text(property), first);
    class_reset(&trace);
    trace.mode = saved_mode;
    const gf_status written = (*kit).properties.set(object, gf_kit_text(property), second);
    const char* expected = "BA";
    if (mode == CLASS_MODE_AROUND) {
        expected = interaction != NULL ? "BSA" : "BA";
        snprintf(name, sizeof(name), "%s: the write", label);
        gf_kit_status(kit, written, GF_STATUS_OK, name);
        snprintf(name, sizeof(name), "%s: base", label);
        gf_kit_status(kit, trace.base_status, GF_STATUS_OK, name);
    } else if (mode == CLASS_MODE_SUPPRESS) {
        expected = "B";
    } else {
        expected = "B";
        snprintf(name, sizeof(name), "%s: base after disposal", label);
        gf_kit_status(kit, trace.base_status, GF_STATUS_STALE_HANDLE, name);
        const uint32_t entries = trace.entries;
        (void)(*kit).properties.set(object, gf_kit_text(property), first);
        snprintf(name, sizeof(name), "%s: no entry after disposal", label);
        gf_kit_check(kit, trace.entries == entries, name);
    }
    snprintf(name, sizeof(name), "%s: one entry", label);
    gf_kit_check(kit, mode == CLASS_MODE_DISPOSE || trace.entries == 1u, name);
    snprintf(name, sizeof(name), "%s: order %s (got %s)", label, expected, trace.marks);
    gf_kit_check(kit, strcmp(trace.marks, expected) == 0, name);
    if (subscription != 0u && mode != CLASS_MODE_DISPOSE) {
        (void)(*kit).events.disconnect(subscription);
    }
    snprintf(name, sizeof(name), "%s: release", label);
    gf_kit_status(kit, (*kit).objects.release(object), GF_STATUS_OK, name);
    uint64_t invoked = 0u;
    (void)(*kit).dispatch.run_pending(&invoked);
    snprintf(name, sizeof(name), "%s: release(instance) once", label);
    gf_kit_check(kit, trace.releases == 1u, name);
}

static void class_run_case(gf_kit* kit, const gf_class_header* table, const char* label, const char* property,
                           const gf_value* first, const gf_value* second, const char* interaction) {
    class_run_mode(kit, table, label, property, first, second, interaction, CLASS_MODE_AROUND);
    class_run_mode(kit, table, label, property, first, second, interaction, CLASS_MODE_SUPPRESS);
    class_run_mode(kit, table, label, property, first, second, interaction, CLASS_MODE_DISPOSE);
}

/* A refused table creates nothing and calls no release. */
static void class_refused(gf_kit* kit, const gf_class_header* table, gf_status expected, const char* name) {
    class_trace trace;
    memset(&trace, 0, sizeof(trace));
    trace.kit = kit;
    gf_object object = 0u;
    gf_kit_status(kit, (*kit).classes.create(table, gf_kit_text("kit.classes.refused"), &trace, &object), expected,
                  name);
    gf_kit_check(kit, object == 0u && trace.releases == 0u, name);
}

typedef struct class_thread_call {
    gf_base_raiser raiser;
    gf_object object;
    gf_status status;
    uint32_t reserved;
} class_thread_call;

static void* class_call_off_thread(void* argument) {
    class_thread_call* call = (class_thread_call*)argument;
    gf_event event;
    memset(&event, 0, sizeof(event));
    event.struct_size = (uint32_t)sizeof(event);
    (*call).status = (*call).raiser((*call).object, &event);
    return NULL;
}

/* Entries with no case yet:
 * - MaskedTextBox::on_mask_changed: no derivable class reaches MaskedTextBox yet
 * - ComboBox::on_drop_down_style_changed: no derivable class reaches ComboBox yet
 * - DataGridView::on_allow_user_to_delete_rows_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_allow_user_to_order_columns_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_allow_user_to_resize_columns_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_allow_user_to_resize_rows_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_alternating_rows_default_cell_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_auto_size_columns_mode_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_auto_size_rows_mode_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_background_color_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_border_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_cell_border_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_column_headers_border_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_column_headers_default_cell_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_column_headers_height_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_default_cell_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_edit_mode_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_grid_color_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_multi_select_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_read_only_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_row_headers_border_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_row_headers_default_cell_style_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_row_headers_width_changed: no derivable class reaches DataGridView yet
 * - DataGridView::on_rows_default_cell_style_changed: no derivable class reaches DataGridView yet
 * - CheckBox::on_appearance_changed: CheckBox has no writable schema property appearance
 * - NumericUpDown::on_value_changed: no derivable class reaches NumericUpDown yet
 * - DateTimePicker::on_format_changed: no derivable class reaches DateTimePicker yet
 * - DateTimePicker::on_value_changed: no derivable class reaches DateTimePicker yet
 */

/* ButtonBase Control::on_auto_size_changed. */
static void class_case_0(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_auto_size_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "ButtonBase Control::on_auto_size_changed", "auto_size", &first, &second, "auto_size_changed");
}

/* ButtonBase Control::on_back_color_changed. */
static void class_case_1(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_back_color_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_COLOR);
    first.color = 0x112233ffu;
    gf_value second = class_value(GF_VALUE_KIND_COLOR);
    second.color = 0x445566ffu;
    class_run_case(kit, &table.header, "ButtonBase Control::on_back_color_changed", "back_color", &first, &second, NULL);
}

/* ButtonBase Control::on_causes_validation_changed. */
static void class_case_2(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_causes_validation_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "ButtonBase Control::on_causes_validation_changed", "causes_validation", &first, &second, "causes_validation_changed");
}

/* ButtonBase Control::on_cursor_changed. */
static void class_case_3(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_cursor_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_ENUMERATION);
    first.signed_integer = 0;
    gf_value second = class_value(GF_VALUE_KIND_ENUMERATION);
    second.signed_integer = 1;
    class_run_case(kit, &table.header, "ButtonBase Control::on_cursor_changed", "cursor", &first, &second, NULL);
}

/* ButtonBase Control::on_dock_changed. */
static void class_case_4(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_dock_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_ENUMERATION);
    first.signed_integer = 0;
    gf_value second = class_value(GF_VALUE_KIND_ENUMERATION);
    second.signed_integer = 1;
    class_run_case(kit, &table.header, "ButtonBase Control::on_dock_changed", "dock", &first, &second, NULL);
}

/* ButtonBase Control::on_enabled_changed. */
static void class_case_5(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_enabled_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "ButtonBase Control::on_enabled_changed", "enabled", &first, &second, "enabled_changed");
}

/* ButtonBase Control::on_fore_color_changed. */
static void class_case_6(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_fore_color_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_COLOR);
    first.color = 0x112233ffu;
    gf_value second = class_value(GF_VALUE_KIND_COLOR);
    second.color = 0x445566ffu;
    class_run_case(kit, &table.header, "ButtonBase Control::on_fore_color_changed", "fore_color", &first, &second, NULL);
}

/* ButtonBase Control::on_margin_changed. */
static void class_case_7(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_margin_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_INSETS);
    first.insets.left = 1.0;
    first.insets.top = 1.0;
    first.insets.right = 1.0;
    first.insets.bottom = 1.0;
    gf_value second = class_value(GF_VALUE_KIND_INSETS);
    second.insets.left = 2.0;
    second.insets.top = 2.0;
    second.insets.right = 2.0;
    second.insets.bottom = 2.0;
    class_run_case(kit, &table.header, "ButtonBase Control::on_margin_changed", "margin", &first, &second, NULL);
}

/* ButtonBase Control::on_padding_changed. */
static void class_case_8(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_padding_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_INSETS);
    first.insets.left = 1.0;
    first.insets.top = 1.0;
    first.insets.right = 1.0;
    first.insets.bottom = 1.0;
    gf_value second = class_value(GF_VALUE_KIND_INSETS);
    second.insets.left = 2.0;
    second.insets.top = 2.0;
    second.insets.right = 2.0;
    second.insets.bottom = 2.0;
    class_run_case(kit, &table.header, "ButtonBase Control::on_padding_changed", "padding", &first, &second, NULL);
}

/* ButtonBase Control::on_right_to_left_changed. */
static void class_case_9(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_right_to_left_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_ENUMERATION);
    first.signed_integer = 0;
    gf_value second = class_value(GF_VALUE_KIND_ENUMERATION);
    second.signed_integer = 1;
    class_run_case(kit, &table.header, "ButtonBase Control::on_right_to_left_changed", "right_to_left", &first, &second, "right_to_left_changed");
}

/* ButtonBase Control::on_tab_index_changed. */
static void class_case_10(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_tab_index_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_UNSIGNED);
    first.unsigned_integer = 1u;
    gf_value second = class_value(GF_VALUE_KIND_UNSIGNED);
    second.unsigned_integer = 2u;
    class_run_case(kit, &table.header, "ButtonBase Control::on_tab_index_changed", "tab_index", &first, &second, NULL);
}

/* ButtonBase Control::on_tab_stop_changed. */
static void class_case_11(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_tab_stop_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "ButtonBase Control::on_tab_stop_changed", "tab_stop", &first, &second, NULL);
}

/* ButtonBase Control::on_visible_changed. */
static void class_case_12(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_visible_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "ButtonBase Control::on_visible_changed", "visible", &first, &second, "visible_changed");
}

/* PictureBox PictureBox::on_size_mode_changed. */
static void class_case_13(gf_kit* kit) {
    gf_picture_box_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_PICTURE_BOX;
    segment.on_size_mode_changed = &class_entry;
    gf_picture_box_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_PICTURE_BOX;
    table.header.segment_count = 2u;
    table.header.release = &class_release;
    table.picture_box = &segment;
    gf_value first = class_value(GF_VALUE_KIND_ENUMERATION);
    first.signed_integer = 0;
    gf_value second = class_value(GF_VALUE_KIND_ENUMERATION);
    second.signed_integer = 1;
    class_run_case(kit, &table.header, "PictureBox PictureBox::on_size_mode_changed", "size_mode", &first, &second, NULL);
}

/* CheckBox CheckBox::on_checked_changed. */
static void class_case_14(gf_kit* kit) {
    gf_check_box_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CHECK_BOX;
    segment.on_checked_changed = &class_entry;
    gf_check_box_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_CHECK_BOX;
    table.header.segment_count = 2u;
    table.header.release = &class_release;
    table.check_box = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "CheckBox CheckBox::on_checked_changed", "checked", &first, &second, "checked_changed");
}

/* CheckBox CheckBox::on_check_state_changed. */
static void class_case_15(gf_kit* kit) {
    gf_check_box_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CHECK_BOX;
    segment.on_check_state_changed = &class_entry;
    gf_check_box_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_CHECK_BOX;
    table.header.segment_count = 2u;
    table.header.release = &class_release;
    table.check_box = &segment;
    gf_value first = class_value(GF_VALUE_KIND_ENUMERATION);
    first.signed_integer = 0;
    gf_value second = class_value(GF_VALUE_KIND_ENUMERATION);
    second.signed_integer = 1;
    class_run_case(kit, &table.header, "CheckBox CheckBox::on_check_state_changed", "check_state", &first, &second, "check_state_changed");
}

/* RadioButton RadioButton::on_checked_changed. */
static void class_case_16(gf_kit* kit) {
    gf_radio_button_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_RADIO_BUTTON;
    segment.on_checked_changed = &class_entry;
    gf_radio_button_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_RADIO_BUTTON;
    table.header.segment_count = 2u;
    table.header.release = &class_release;
    table.radio_button = &segment;
    gf_value first = class_value(GF_VALUE_KIND_BOOLEAN);
    first.boolean = 0u;
    gf_value second = class_value(GF_VALUE_KIND_BOOLEAN);
    second.boolean = 1u;
    class_run_case(kit, &table.header, "RadioButton RadioButton::on_checked_changed", "checked", &first, &second, "checked_changed");
}

/* RangeControl RangeControl::on_value_changed. */
static void class_case_17(gf_kit* kit) {
    gf_range_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_RANGE_CONTROL;
    segment.on_value_changed = &class_entry;
    gf_range_control_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_RANGE_CONTROL;
    table.header.segment_count = 2u;
    table.header.release = &class_release;
    table.range_control = &segment;
    gf_value first = class_value(GF_VALUE_KIND_NUMBER);
    first.number = 1.0;
    gf_value second = class_value(GF_VALUE_KIND_NUMBER);
    second.number = 2.0;
    class_run_case(kit, &table.header, "RangeControl RangeControl::on_value_changed", "value", &first, &second, "value_changed");
}

/* The refusals, base_raiser, a direct base call and base from another thread. */
static void class_refusals(gf_kit* kit) {
    gf_control_hooks segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = (uint32_t)sizeof(segment);
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_auto_size_changed = &class_entry;
    gf_button_base_class table;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1u;
    table.header.release = &class_release;
    table.control = &segment;

    class_refused(kit, NULL, GF_STATUS_INVALID_ARGUMENT, "classes.create refuses a null table");
    table.header.struct_size = 8u;
    class_refused(kit, &table.header, GF_STATUS_INVALID_ARGUMENT, "classes.create refuses a short header");
    table.header.struct_size = (uint32_t)sizeof(gf_class_header);
    table.header.abi_minor = GF_ABI_MINOR + 1u;
    class_refused(kit, &table.header, GF_STATUS_UNSUPPORTED_VERSION, "classes.create refuses a newer minor");
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.segment_count = 2u;
    class_refused(kit, &table.header, GF_STATUS_INVALID_ARGUMENT, "classes.create refuses too many segments");
    table.header.segment_count = 1u;
    segment.class_id = 4000000000u;
    class_refused(kit, &table.header, GF_STATUS_INVALID_ARGUMENT, "classes.create refuses a foreign segment");
    segment.class_id = GF_CLASS_CONTROL;
    segment.struct_size = 4u;
    class_refused(kit, &table.header, GF_STATUS_INVALID_ARGUMENT, "classes.create refuses a short segment");
    segment.struct_size = (uint32_t)sizeof(segment);
    table.header.class_id = 4000000000u;
    class_refused(kit, &table.header, GF_STATUS_NOT_FOUND, "classes.create refuses an unknown class");
    table.header.class_id = GF_CLASS_BUTTON_BASE;

    gf_base_raiser raiser = NULL;
    gf_kit_status(kit, (*kit).classes.base_raiser(GF_CLASS_CONTROL, 0u, &raiser), GF_STATUS_OK,
                  "classes.base_raiser gives the entry's base");
    gf_kit_check(kit, raiser != NULL, "classes.base_raiser writes the base");
    gf_base_raiser none = NULL;
    gf_kit_status(kit, (*kit).classes.base_raiser(GF_CLASS_CONTROL, 100000u, &none), GF_STATUS_NOT_FOUND,
                  "classes.base_raiser refuses an unknown entry");
    gf_kit_status(kit, (*kit).classes.base_raiser(4000000000u, 0u, &none), GF_STATUS_NOT_FOUND,
                  "classes.base_raiser refuses an unknown class");
    if (raiser == NULL) {
        return;
    }

    class_trace trace;
    memset(&trace, 0, sizeof(trace));
    trace.kit = kit;
    gf_object object = 0u;
    gf_kit_status(kit, (*kit).classes.create(&table.header, gf_kit_text("kit.classes.direct"), &trace, &object), GF_STATUS_OK,
                  "classes.create for a direct base");
    if (object == 0u) {
        return;
    }
    const gf_callback callback = gf_kit_callback(&class_subscriber, NULL, &trace);
    gf_subscription subscription = 0u;
    (void)(*kit).events.subscribe(object, gf_kit_text("auto_size_changed"), &callback, &subscription);
    gf_event event;
    memset(&event, 0, sizeof(event));
    event.struct_size = (uint32_t)sizeof(event);
    event.sender = object;
    event.name = gf_kit_text("auto_size");
    gf_kit_status(kit, raiser(object, &event), GF_STATUS_OK, "a base called outside an entry");
    gf_kit_check(kit, trace.entries == 0u, "a direct base runs no entry");
    gf_kit_check(kit, strcmp(trace.marks, "S") == 0, "a direct base runs the subscribers");
    class_thread_call call;
    memset(&call, 0, sizeof(call));
    call.raiser = raiser;
    call.object = object;
    pthread_t thread;
    if (pthread_create(&thread, NULL, &class_call_off_thread, &call) == 0) {
        (void)pthread_join(thread, NULL);
        gf_kit_status(kit, call.status, GF_STATUS_WRONG_THREAD, "base from another thread");
    }
    gf_kit_status(kit, (*kit).objects.dispose(object), GF_STATUS_OK, "dispose the direct object");
    gf_kit_status(kit, raiser(object, &event), GF_STATUS_STALE_HANDLE, "a direct base after disposal");
    gf_kit_status(kit, (*kit).objects.release(object), GF_STATUS_OK, "release the direct object");
    uint64_t invoked = 0u;
    (void)(*kit).dispatch.run_pending(&invoked);
    gf_kit_check(kit, trace.releases == 1u, "the direct object's release(instance) once");
}

void gf_kit_classes(gf_kit* kit) {
    class_case_0(kit);
    class_case_1(kit);
    class_case_2(kit);
    class_case_3(kit);
    class_case_4(kit);
    class_case_5(kit);
    class_case_6(kit);
    class_case_7(kit);
    class_case_8(kit);
    class_case_9(kit);
    class_case_10(kit);
    class_case_11(kit);
    class_case_12(kit);
    class_case_13(kit);
    class_case_14(kit);
    class_case_15(kit);
    class_case_16(kit);
    class_case_17(kit);
    class_refusals(kit);
}
