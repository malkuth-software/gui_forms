/* Kit part: the visible-area guarantee (since 1.7; stage VA): controls.
 * set_viewport_fit and viewport_fit on a loose control, and windows.
 * overflow_records, unanswered_overflow_count and safe_insets on a window
 * from windows.create, before any run. A window that has not laid out
 * knows of no overflow, and no host has reported safe insets. */

#include "gf_conformance_internal.h"

#include <stddef.h>
#include <string.h>

/* The table sizes that carry the 1.7 slots. */
static int tables_have_viewport(const gf_kit* kit) {
    const int controls = (*kit).controls.struct_size >= offsetof(gf_controls_table, viewport_fit) + sizeof(void*) &&
                         (*kit).controls.set_viewport_fit != NULL && (*kit).controls.viewport_fit != NULL;
    const int windows = (*kit).windows.struct_size >= offsetof(gf_windows_table, safe_insets) + sizeof(void*) &&
                        (*kit).windows.overflow_records != NULL &&
                        (*kit).windows.unanswered_overflow_count != NULL && (*kit).windows.safe_insets != NULL;
    const int present = controls && windows;
    return present;
}

static gf_viewport_fit viewport_fit_record(gf_overflow_answer answer, double minimum, double maximum) {
    gf_viewport_fit fit;
    memset(&fit, 0, sizeof(fit));
    fit.struct_size = (uint32_t)sizeof(fit);
    fit.answer = answer;
    fit.minimum_scale = minimum;
    fit.maximum_scale = maximum;
    fit.below_minimum = GF_OVERFLOW_ANSWER_SCROLL;
    fit.alignment = GF_CONTENT_ALIGNMENT_MIDDLE_CENTER;
    return fit;
}

/* The default, a write read back, and each malformed record refused as
 * INVALID_ARGUMENT with the control unchanged. */
static void viewport_fit_values(gf_kit* kit, gf_object control) {
    gf_viewport_fit read = viewport_fit_record(GF_OVERFLOW_ANSWER_SCROLL, 9.0, 9.0);
    gf_kit_status(kit, (*kit).controls.viewport_fit(control, &read), GF_STATUS_OK, "read the default viewport fit");
    gf_kit_check(kit,
                 read.answer == GF_OVERFLOW_ANSWER_CONTAIN && read.minimum_scale == 0.0 && read.maximum_scale == 1.0 &&
                     read.below_minimum == GF_OVERFLOW_ANSWER_SCROLL &&
                     read.alignment == GF_CONTENT_ALIGNMENT_MIDDLE_CENTER,
                 "a panel's default viewport fit is contain, 0 to 1, scroll below, centred");

    gf_viewport_fit written = viewport_fit_record(GF_OVERFLOW_ANSWER_COVER, 0.5, 2.0);
    written.alignment = GF_CONTENT_ALIGNMENT_TOP_LEFT;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &written), GF_STATUS_OK, "set a viewport fit");
    read = viewport_fit_record(GF_OVERFLOW_ANSWER_SCROLL, 9.0, 9.0);
    gf_kit_status(kit, (*kit).controls.viewport_fit(control, &read), GF_STATUS_OK, "read it back");
    gf_kit_check(kit,
                 read.answer == GF_OVERFLOW_ANSWER_COVER && read.minimum_scale == 0.5 && read.maximum_scale == 2.0 &&
                     read.alignment == GF_CONTENT_ALIGNMENT_TOP_LEFT,
                 "the viewport fit reads as written");

    gf_viewport_fit bad = written;
    bad.struct_size = 8u;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &bad), GF_STATUS_INVALID_ARGUMENT,
                  "a record smaller than its size is refused");
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "a null record is refused");
    bad = written;
    bad.answer = 4u;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &bad), GF_STATUS_INVALID_ARGUMENT,
                  "an answer outside gf_overflow_answer is refused");
    bad = written;
    bad.below_minimum = GF_OVERFLOW_ANSWER_CONTAIN;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &bad), GF_STATUS_INVALID_ARGUMENT,
                  "below_minimum other than scroll or clip is refused");
    bad = written;
    bad.minimum_scale = 3.0;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &bad), GF_STATUS_INVALID_ARGUMENT,
                  "a maximum below the minimum is refused");
    bad = written;
    bad.maximum_scale = 2048.0;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &bad), GF_STATUS_INVALID_ARGUMENT,
                  "a maximum above 1024 is refused");
    bad = written;
    bad.alignment = 9u;
    gf_kit_status(kit, (*kit).controls.set_viewport_fit(control, &bad), GF_STATUS_INVALID_ARGUMENT,
                  "an alignment outside gf_content_alignment is refused");
    read = viewport_fit_record(GF_OVERFLOW_ANSWER_SCROLL, 9.0, 9.0);
    gf_kit_status(kit, (*kit).controls.viewport_fit(control, &read), GF_STATUS_OK, "read after the refusals");
    gf_kit_check(kit, read.answer == GF_OVERFLOW_ANSWER_COVER && read.maximum_scale == 2.0,
                 "a refused record changes nothing");
    gf_kit_status(kit, (*kit).controls.viewport_fit(control, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "viewport_fit needs an output");
}

/* The window's overflow records, unanswered count and safe insets before
 * it has laid out, and the refusals. */
static void window_overflows(gf_kit* kit, gf_object window, gf_object loose) {
    uint64_t required = 99u;
    uint64_t text_required = 99u;
    gf_kit_status(kit, (*kit).windows.overflow_records(window, NULL, 0u, &required, NULL, 0u, &text_required),
                  GF_STATUS_OK, "count the overflow records");
    gf_kit_check(kit, required == 0u && text_required == 0u, "a window that has not laid out knows of no overflow");
    gf_kit_status(kit, (*kit).windows.overflow_records(window, NULL, 0u, NULL, NULL, 0u, &text_required),
                  GF_STATUS_INVALID_ARGUMENT, "overflow_records needs the record count output");
    gf_kit_status(kit, (*kit).windows.overflow_records(window, NULL, 0u, &required, NULL, 4u, &text_required),
                  GF_STATUS_INVALID_ARGUMENT, "a text capacity needs a buffer");
    gf_overflow_record records[2];
    memset(records, 0, sizeof(records));
    records[0].struct_size = (uint32_t)sizeof(gf_overflow_record);
    records[1].struct_size = 16u;
    gf_kit_status(kit, (*kit).windows.overflow_records(window, records, 2u, &required, NULL, 0u, &text_required),
                  GF_STATUS_INVALID_ARGUMENT, "every record of the array repeats the first's struct_size");

    uint64_t unanswered = 99u;
    gf_kit_status(kit, (*kit).windows.unanswered_overflow_count(window, &unanswered), GF_STATUS_OK,
                  "count the unanswered overflows");
    gf_kit_check(kit, unanswered == 0u, "no overflow is unanswered before layout");
    gf_kit_status(kit, (*kit).windows.unanswered_overflow_count(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "unanswered_overflow_count needs an output");

    gf_logical_insets insets;
    insets.left = 9.0;
    insets.top = 9.0;
    insets.right = 9.0;
    insets.bottom = 9.0;
    gf_kit_status(kit, (*kit).windows.safe_insets(window, &insets), GF_STATUS_OK, "read the safe insets");
    gf_kit_check(kit, insets.left == 0.0 && insets.top == 0.0 && insets.right == 0.0 && insets.bottom == 0.0,
                 "no host has reported safe insets");
    gf_kit_status(kit, (*kit).windows.safe_insets(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "safe_insets needs an output");
    gf_kit_status(kit, (*kit).windows.safe_insets(loose, &insets), GF_STATUS_INVALID_STATE,
                  "a control not in a window has no safe insets");
}

void gf_kit_viewport(gf_kit* kit) {
    if (!tables_have_viewport(kit)) {
        return;
    }
    /* A panel: Windows Forms panels contain by default (decision 4); a
     * plain control clips. */
    const gf_object loose = gf_kit_create(kit, "Panel", "kit.viewport.loose");
    viewport_fit_values(kit, loose);

    const gf_object root = gf_kit_create(kit, "Panel", "kit.viewport");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for the viewport");
    window_overflows(kit, window, loose);

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the viewport window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the viewport root");
    gf_kit_status(kit, (*kit).objects.release(loose), GF_STATUS_OK, "release the loose viewport control");
}
