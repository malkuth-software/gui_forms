/* Kit part: the host's services (GF_TABLE_HOST). The capability report is
 * read from the UI thread and from a worker; the running window's services
 * are refused outside a run and checked during one (gf_kit_host_running,
 * called from the window part's ready callback). The kit never writes the
 * clipboard or shows a dialog: each such request it makes is refused before
 * it reaches the host, so a run leaves the desktop as it found it. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <string.h>

typedef struct capability_read {
    gf_kit* kit;
    gf_status status;
    gf_host_capabilities report;
} capability_read;

static void* read_capabilities(void* argument) {
    capability_read* read = (capability_read*)argument;
    (*read).report.struct_size = (uint32_t)sizeof((*read).report);
    (*read).status = (*(*read).kit).host.capabilities(&(*read).report);
    return NULL;
}

/* A valid message dialog, refused only for want of a running window. */
static gf_dialog_request message_request(void) {
    gf_dialog_request request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.kind = GF_DIALOG_KIND_MESSAGE;
    request.title = gf_kit_text("Conformance");
    request.message = gf_kit_text("Never shown");
    request.buttons = GF_MESSAGE_BUTTONS_OK_CANCEL;
    request.default_choice = GF_DIALOG_CHOICE_OK;
    return request;
}

static gf_dialog_result empty_result(void) {
    gf_dialog_result result;
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    return result;
}

void gf_kit_host(gf_kit* kit) {
    gf_host_capabilities report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    gf_kit_status(kit, (*kit).host.capabilities(&report), GF_STATUS_OK, "read the host's capabilities");
    gf_kit_check(kit, report.protocol_version != 0u, "the report names a protocol revision");
    capability_read worker;
    memset(&worker, 0, sizeof(worker));
    worker.kit = kit;
    worker.status = -1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, &read_capabilities, &worker) == 0) {
        (void)pthread_join(thread, NULL);
    }
    gf_kit_status(kit, worker.status, GF_STATUS_OK, "read the capabilities from a worker");
    gf_kit_check(kit, worker.report.available == report.available, "every thread reads one report");
    char name[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).host.platform_name(name, sizeof(name), &required), GF_STATUS_OK,
                  "read the host's name");
    gf_kit_check(kit, required > 0u, "the host has a name");
    gf_kit_status(kit, (*kit).host.capabilities(NULL), GF_STATUS_INVALID_ARGUMENT, "capabilities needs its record");

    gf_kit_status(kit, (*kit).host.set_cursor(GF_POINTER_CURSOR_HAND), GF_STATUS_INVALID_STATE,
                  "set_cursor needs a run");
    gf_kit_status(kit, (*kit).host.write_clipboard_text(gf_kit_text("kit")), GF_STATUS_INVALID_STATE,
                  "write_clipboard_text needs a run");
    gf_kit_status(kit, (*kit).host.read_clipboard_text(NULL, 0u, &required), GF_STATUS_INVALID_STATE,
                  "read_clipboard_text needs a run");
    const gf_dialog_request request = message_request();
    gf_dialog_result result = empty_result();
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_STATE,
                  "show_dialog needs a run");
    gf_kit_status(kit, (*kit).host.dialog_path_at(0u, NULL, 0u, &required), GF_STATUS_INVALID_ARGUMENT,
                  "no dialog has chosen a path");
}

void gf_kit_host_running(gf_kit* kit) {
    gf_kit_status(kit, (*kit).host.set_cursor(GF_POINTER_CURSOR_ARROW), GF_STATUS_OK, "set the cursor in a run");
    gf_kit_status(kit, (*kit).host.set_cursor(GF_POINTER_CURSOR_RESIZE_DIAGONAL_UP + 1u), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown cursor is refused");
    /* The clipboard is read, never written: its state is the desktop's. */
    uint64_t required = 0u;
    const gf_status read = (*kit).host.read_clipboard_text(NULL, 0u, &required);
    gf_kit_check(kit, read == GF_STATUS_OK || read == GF_STATUS_BUFFER_TOO_SMALL || read == GF_STATUS_NOT_FOUND,
                 "read the clipboard in a run");
    const char invalid_utf8[] = {(char)0xC3, (char)0x28};
    gf_utf8_view invalid;
    invalid.data = invalid_utf8;
    invalid.size = sizeof(invalid_utf8);
    gf_kit_status(kit, (*kit).host.write_clipboard_text(invalid), GF_STATUS_INVALID_ARGUMENT,
                  "text that is not UTF-8 is refused before the clipboard");

    gf_dialog_request request = message_request();
    gf_dialog_result result = empty_result();
    request.kind = GF_DIALOG_KIND_COLOR + 1u;
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown dialog kind is refused");
    request = message_request();
    request.flags = GF_DIALOG_FLAGS_ALLOW_MULTIPLE;
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_ARGUMENT,
                  "a flag the kind does not read is refused");
    request = message_request();
    request.kind = GF_DIALOG_KIND_FOLDER;
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_ARGUMENT,
                  "a field the kind does not read is refused");
    request = message_request();
    request.reserved = 1u;
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_ARGUMENT,
                  "reserved must be zero");
    gf_utf8_view extension = gf_kit_text("png");
    gf_file_filter filter;
    memset(&filter, 0, sizeof(filter));
    filter.label = gf_kit_text("Images");
    filter.extensions = &extension;
    filter.extension_count = 1u;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.kind = GF_DIALOG_KIND_OPEN_FILE;
    request.filters = &filter;
    request.filter_count = 1u;
    request.initial_filter = 1u;
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_ARGUMENT,
                  "an initial filter past the filters is refused");
    request.initial_filter = 0u;
    request.filters = NULL;
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &result), GF_STATUS_INVALID_ARGUMENT,
                  "filters are needed for a count");
    gf_dialog_result small = empty_result();
    small.struct_size = 8u;
    request = message_request();
    gf_kit_status(kit, (*kit).host.show_dialog(&request, &small), GF_STATUS_INVALID_ARGUMENT,
                  "a short result record is refused");
    /* A value published during the run reaches the running window. */
    gf_kit_status(kit, (*kit).themes.publish_value(gf_kit_text("kit-running"), 2.0), GF_STATUS_OK,
                  "publish a value during a run");
}
