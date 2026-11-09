/* Kit part: modal and owned windows over the running window
 * (windows.show_modal, windows.show_owned) and anchored overlays
 * (overlays.open_anchored, overlays.close_anchored), all since 1.1. The
 * refusals need no window; the window part opens each kind briefly during
 * one run, driven by posted work so it needs no input. */

#include "gf_conformance_internal.h"

#include <stddef.h>
#include <string.h>

/* The table sizes that carry the 1.1 slots. */
static int windows_have_modal(const gf_kit* kit) {
    const int present = (*kit).windows.struct_size >= offsetof(gf_windows_table, show_owned) + sizeof(void*) &&
                        (*kit).windows.show_modal != NULL && (*kit).windows.show_owned != NULL;
    return present;
}

static int overlays_have_anchored(const gf_kit* kit) {
    const int present =
        (*kit).overlays.struct_size >= offsetof(gf_overlays_table, close_anchored) + sizeof(void*) &&
        (*kit).overlays.open_anchored != NULL && (*kit).overlays.close_anchored != NULL;
    return present;
}

static gf_owned_window_options owned_options(const char* title) {
    gf_owned_window_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.flags = GF_OWNED_WINDOW_FLAGS_NONE;
    options.title = gf_kit_text(title);
    options.width = 240.0;
    options.height = 160.0;
    return options;
}

static gf_anchored_overlay_options anchored_options(gf_anchored_overlay_flags flags, const gf_callback* events) {
    gf_anchored_overlay_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.flags = flags;
    options.width = 120.0;
    options.height = 80.0;
    options.gap = 2.0;
    options.events = events;
    return options;
}

/* Refusals before any window runs: options, flags and the missing run. A
 * refused call does not take its callbacks. */
void gf_kit_modal_refusals(gf_kit* kit) {
    gf_kit_check(kit, windows_have_modal(kit), "the windows table has show_modal and show_owned");
    gf_kit_check(kit, overlays_have_anchored(kit), "the overlays table has open_anchored and close_anchored");
    if (!windows_have_modal(kit) || !overlays_have_anchored(kit)) {
        return;
    }
    const gf_object root = gf_kit_create(kit, "Panel", "kit.modal.refused");
    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback recording = gf_kit_recording_callback(&recorder);
    gf_dialog_result_code result = 99u;

    gf_kit_status(kit, (*kit).windows.show_modal(root, NULL, &result), GF_STATUS_INVALID_ARGUMENT,
                  "show_modal without options is refused");
    gf_owned_window_options options = owned_options("refused");
    options.struct_size = 8u;
    gf_kit_status(kit, (*kit).windows.show_modal(root, &options, &result), GF_STATUS_INVALID_ARGUMENT,
                  "show_modal options below their 1.1 size are refused");
    options = owned_options("refused");
    options.flags = 2u;
    gf_kit_status(kit, (*kit).windows.show_owned(root, &options), GF_STATUS_INVALID_ARGUMENT,
                  "unknown owned window flags are refused");
    options = owned_options("refused");
    options.width = -1.0;
    gf_kit_status(kit, (*kit).windows.show_owned(root, &options), GF_STATUS_INVALID_ARGUMENT,
                  "a negative owned window size is refused");
    options = owned_options("refused");
    options.ready = &recording;
    options.closed = &recording;
    gf_kit_status(kit, (*kit).windows.show_modal(root, &options, &result), GF_STATUS_INVALID_STATE,
                  "show_modal outside a run is refused");
    gf_kit_status(kit, (*kit).windows.show_owned(root, &options), GF_STATUS_INVALID_STATE,
                  "show_owned outside a run is refused");
    gf_kit_check(kit, result == 99u, "a refused show_modal leaves its result");
    gf_kit_check(kit, recorder.invoked == 0u && recorder.released == 0u,
                 "a refused show_modal or show_owned does not take its callbacks");

    const gf_object anchor = gf_kit_create(kit, "Button", "kit.modal.refused.anchor");
    const gf_object content = gf_kit_create(kit, "Panel", "kit.modal.refused.content");
    gf_kit_status(kit, (*kit).overlays.open_anchored(anchor, content, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "open_anchored without options is refused");
    gf_anchored_overlay_options anchored = anchored_options(64u, &recording);
    gf_kit_status(kit, (*kit).overlays.open_anchored(anchor, content, &anchored), GF_STATUS_INVALID_ARGUMENT,
                  "unknown anchored overlay flags are refused");
    anchored = anchored_options(GF_ANCHORED_OVERLAY_FLAGS_NONE, &recording);
    anchored.gap = -1.0;
    gf_kit_status(kit, (*kit).overlays.open_anchored(anchor, content, &anchored), GF_STATUS_INVALID_ARGUMENT,
                  "a negative anchored overlay gap is refused");
    anchored = anchored_options(GF_ANCHORED_OVERLAY_FLAGS_NONE, &recording);
    gf_kit_status(kit, (*kit).overlays.open_anchored(anchor, content, &anchored), GF_STATUS_INVALID_STATE,
                  "an anchor outside a window is refused");
    gf_kit_status(kit, (*kit).overlays.close_anchored(content), GF_STATUS_NOT_FOUND,
                  "close_anchored of content that is not open");
    gf_kit_check(kit, recorder.invoked == 0u && recorder.released == 0u,
                 "a refused open_anchored does not take its callback");

    gf_kit_status(kit, (*kit).objects.release(content), GF_STATUS_OK, "release the refused content");
    gf_kit_status(kit, (*kit).objects.release(anchor), GF_STATUS_OK, "release the refused anchor");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the refused root");
}

/* The window part's state, shared by its callbacks. order records the
 * modal sequence: M the modal window ready, T its posted step, N the inner
 * modal window ready, Q the inner window's posted close, n the inner
 * window closed, m the modal window closed, A show_modal returned. */
typedef struct modal_run {
    gf_kit* kit;
    gf_object main_root;
    gf_object anchor;
    gf_object content;
    gf_object owned_root;
    gf_object modal_root;
    gf_object inner_root;
    char order[16];
    uint32_t order_length;
    gf_kit_recorder anchored_events;
    uint32_t owned_ready;
    uint32_t owned_closed;
    uint32_t releases;
    gf_status anchored_open;
    gf_status anchored_again;
    gf_status anchored_close;
    gf_status anchored_close_again;
    gf_status owned_open;
    gf_status owned_again;
    gf_status owned_close;
    gf_status modal_again;
    gf_status inner_status;
    gf_dialog_result_code inner_result;
    gf_status modal_status;
    gf_dialog_result_code modal_result;
    gf_callback step;
    gf_callback modal_step;
    gf_callback close_inner;
} modal_run;

static void note(modal_run* run, char mark) {
    if ((*run).order_length + 1u < sizeof((*run).order)) {
        (*run).order[(*run).order_length] = mark;
        ++(*run).order_length;
        (*run).order[(*run).order_length] = '\0';
    }
}

static void GF_CALL count_release(void* context) {
    ++(*(modal_run*)context).releases;
}

static void GF_CALL on_owned_ready(void* context, gf_event* event) {
    (void)event;
    ++(*(modal_run*)context).owned_ready;
}

static void GF_CALL on_owned_closed(void* context, gf_event* event) {
    (void)event;
    ++(*(modal_run*)context).owned_closed;
}

static void GF_CALL on_inner_ready(void* context, gf_event* event) {
    modal_run* run = (modal_run*)context;
    note(run, 'N');
    /* Posted work runs inside the nested loop. */
    (void)(*(*run).kit).dispatch.begin_invoke((*event).sender, &(*run).close_inner);
}

static void GF_CALL on_close_inner(void* context, gf_event* event) {
    modal_run* run = (modal_run*)context;
    note(run, 'Q');
    (void)(*(*run).kit).windows.request_close((*event).sender);
}

static void GF_CALL on_inner_closed(void* context, gf_event* event) {
    (void)event;
    note((modal_run*)context, 'n');
}

static void GF_CALL on_modal_ready(void* context, gf_event* event) {
    modal_run* run = (modal_run*)context;
    note(run, 'M');
    (void)(*(*run).kit).dispatch.begin_invoke((*event).sender, &(*run).modal_step);
}

/* Inside the modal loop: a second show_modal of the same root is refused;
 * a modal window over the modal window runs its own loop; then the modal
 * window is asked to close. */
static void GF_CALL on_modal_step(void* context, gf_event* event) {
    modal_run* run = (modal_run*)context;
    gf_kit* kit = (*run).kit;
    note(run, 'T');
    gf_owned_window_options again = owned_options("again");
    gf_dialog_result_code ignored = 0u;
    (*run).modal_again = (*kit).windows.show_modal((*run).modal_root, &again, &ignored);
    const gf_callback ready = gf_kit_callback(&on_inner_ready, &count_release, run);
    const gf_callback closed = gf_kit_callback(&on_inner_closed, &count_release, run);
    gf_owned_window_options inner = owned_options("GUI.Forms C ABI inner modal");
    inner.owner = (*run).modal_root;
    inner.ready = &ready;
    inner.closed = &closed;
    (*run).inner_status = (*kit).windows.show_modal((*run).inner_root, &inner, &(*run).inner_result);
    (void)(*kit).windows.request_close((*event).sender);
}

static void GF_CALL on_modal_closed(void* context, gf_event* event) {
    (void)event;
    note((modal_run*)context, 'm');
}

/* The main window's posted step: an anchored overlay, an owned window and
 * a modal window, then the main window's close. */
static void GF_CALL on_step(void* context, gf_event* event) {
    modal_run* run = (modal_run*)context;
    gf_kit* kit = (*run).kit;

    const gf_callback events = gf_kit_recording_callback(&(*run).anchored_events);
    gf_anchored_overlay_options anchored = anchored_options(GF_ANCHORED_OVERLAY_TAKE_FOCUS, &events);
    (*run).anchored_open = (*kit).overlays.open_anchored((*run).anchor, (*run).content, &anchored);
    (*run).anchored_again = (*kit).overlays.open_anchored((*run).anchor, (*run).content, &anchored);
    (*run).anchored_close = (*kit).overlays.close_anchored((*run).content);
    (*run).anchored_close_again = (*kit).overlays.close_anchored((*run).content);

    const gf_callback owned_ready = gf_kit_callback(&on_owned_ready, &count_release, run);
    const gf_callback owned_closed = gf_kit_callback(&on_owned_closed, &count_release, run);
    gf_owned_window_options owned = owned_options("GUI.Forms C ABI owned window");
    owned.flags = GF_OWNED_WINDOW_TOOL;
    owned.ready = &owned_ready;
    owned.closed = &owned_closed;
    (*run).owned_open = (*kit).windows.show_owned((*run).owned_root, &owned);
    gf_owned_window_options duplicate = owned_options("duplicate");
    (*run).owned_again = (*kit).windows.show_owned((*run).owned_root, &duplicate);
    (*run).owned_close = (*kit).windows.request_close((*run).owned_root);

    const gf_callback modal_ready = gf_kit_callback(&on_modal_ready, &count_release, run);
    const gf_callback modal_closed = gf_kit_callback(&on_modal_closed, &count_release, run);
    gf_owned_window_options modal = owned_options("GUI.Forms C ABI modal window");
    modal.ready = &modal_ready;
    modal.closed = &modal_closed;
    (*run).modal_status = (*kit).windows.show_modal((*run).modal_root, &modal, &(*run).modal_result);
    note(run, 'A');
    (void)(*kit).windows.request_close((*event).sender);
}

static void GF_CALL on_main_ready(void* context, gf_event* event) {
    modal_run* run = (modal_run*)context;
    (void)(*(*run).kit).dispatch.begin_invoke((*event).sender, &(*run).step);
}

void gf_kit_modal(gf_kit* kit) {
    if (!windows_have_modal(kit) || !overlays_have_anchored(kit)) {
        return;
    }
    modal_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.anchored_events.kit = kit;
    run.anchored_open = -1;
    run.owned_open = -1;
    run.modal_status = -1;
    run.inner_status = -1;
    run.modal_result = 99u;
    run.inner_result = 99u;
    run.main_root = gf_kit_create(kit, "Panel", "kit.modal");
    run.anchor = gf_kit_create(kit, "Button", "kit.modal.anchor");
    gf_kit_status(kit, (*kit).objects.insert_child(run.main_root, run.anchor, 0u), GF_STATUS_OK,
                  "build the modal part's window");
    run.content = gf_kit_create(kit, "Button", "kit.modal.content");
    run.owned_root = gf_kit_create(kit, "Panel", "kit.modal.owned");
    run.modal_root = gf_kit_create(kit, "Panel", "kit.modal.dialog");
    run.inner_root = gf_kit_create(kit, "Panel", "kit.modal.inner");
    run.step = gf_kit_callback(&on_step, NULL, &run);
    run.modal_step = gf_kit_callback(&on_modal_step, NULL, &run);
    run.close_inner = gf_kit_callback(&on_close_inner, NULL, &run);

    const gf_callback ready = gf_kit_callback(&on_main_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("GUI.Forms C ABI modal windows");
    options.width = 400.0;
    options.height = 300.0;
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(run.main_root, &options), GF_STATUS_OK, "run the modal part's window");

    gf_kit_status(kit, run.anchored_open, GF_STATUS_OK, "open an anchored overlay");
    gf_kit_status(kit, run.anchored_again, GF_STATUS_INVALID_STATE, "open content that is already open");
    gf_kit_status(kit, run.anchored_close, GF_STATUS_OK, "close the anchored overlay");
    gf_kit_status(kit, run.anchored_close_again, GF_STATUS_NOT_FOUND, "close it a second time");
    gf_kit_check(kit, run.anchored_events.invoked == 1u && strcmp(run.anchored_events.last_name, "closed") == 0,
                 "the anchored overlay reports closed once");
    gf_kit_check(kit, run.anchored_events.released == 1u, "the anchored overlay's callback is released once");

    gf_kit_status(kit, run.owned_open, GF_STATUS_OK, "show an owned window");
    gf_kit_status(kit, run.owned_again, GF_STATUS_INVALID_STATE, "show the same root twice");
    gf_kit_status(kit, run.owned_close, GF_STATUS_OK, "request_close closes an owned window");
    gf_kit_check(kit, run.owned_ready == 1u && run.owned_closed == 1u, "the owned window was ready and closed");

    gf_kit_status(kit, run.modal_status, GF_STATUS_OK, "show a modal window");
    gf_kit_check(kit, run.modal_result == GF_DIALOG_RESULT_CANCEL, "a modal window closed without a result gives cancel");
    gf_kit_status(kit, run.modal_again, GF_STATUS_INVALID_STATE, "show_modal of an open root is refused");
    gf_kit_status(kit, run.inner_status, GF_STATUS_OK, "a modal window over a modal window");
    gf_kit_check(kit, run.inner_result == GF_DIALOG_RESULT_CANCEL, "the inner modal window gives cancel");
    gf_kit_check(kit, strcmp(run.order, "MTNQnmA") == 0,
                 "nested loops: posted work runs in each, and show_modal returns after closed");
    /* Two callbacks each for the owned, modal and inner windows. */
    gf_kit_check(kit, run.releases == 6u, "each owned and modal window callback is released once");
    gf_kit_status(kit, (*kit).windows.request_close(run.modal_root), GF_STATUS_INVALID_STATE,
                  "request_close of a closed modal window is refused");

    gf_kit_status(kit, (*kit).objects.release(run.inner_root), GF_STATUS_OK, "release the inner root");
    gf_kit_status(kit, (*kit).objects.release(run.modal_root), GF_STATUS_OK, "release the modal root");
    gf_kit_status(kit, (*kit).objects.release(run.owned_root), GF_STATUS_OK, "release the owned root");
    gf_kit_status(kit, (*kit).objects.release(run.content), GF_STATUS_OK, "release the content");
    gf_kit_status(kit, (*kit).objects.release(run.anchor), GF_STATUS_OK, "release the anchor");
    gf_kit_status(kit, (*kit).objects.release(run.main_root), GF_STATUS_OK, "release the modal part's root");
}
