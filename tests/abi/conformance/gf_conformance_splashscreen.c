/* Kit part: the splashscreen (GF_TABLE_RUNTIME, since 1.1). The refusals
 * show nothing and run in every kit run; a shown splashscreen runs only with
 * GF_CONFORMANCE_FLAGS_WINDOW, before the first window (which would hand it
 * over), and accepts a host without one (GF_STATUS_UNSUPPORTED_CAPABILITY).
 * The capability report's transient surfaces are read beside them. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* A 160 x 160 PNG of one colour, the least artwork admitted. */
static const uint8_t artwork_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0xa0, 0x08, 0x06, 0x00, 0x00, 0x00, 0x8b, 0xcf, 0x67,
    0x2d, 0x00, 0x00, 0x01, 0x39, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xd2, 0x31, 0x0d, 0x00,
    0x00, 0x08, 0xc0, 0x30, 0xe4, 0xa0, 0x09, 0x75, 0x38, 0x85, 0x0b, 0x0d, 0x3c, 0x3d, 0x6a, 0x60,
    0x59, 0x64, 0xf5, 0xc0, 0x97, 0x10, 0x01, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62,
    0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30,
    0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06,
    0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0xc4, 0x80,
    0x42, 0x60, 0x40, 0x0c, 0x08, 0x06, 0xc4, 0x80, 0x60, 0x40, 0x0c, 0x08, 0x06, 0xc4, 0x80, 0x60,
    0x40, 0x0c, 0x08, 0x06, 0xc4, 0x80, 0x60, 0x40, 0x0c, 0x08, 0x06, 0xc4, 0x80, 0x60, 0x40, 0x0c,
    0x08, 0x06, 0xc4, 0x80, 0x60, 0x40, 0x0c, 0x08, 0x06, 0xc4, 0x80, 0x60, 0x40, 0x0c, 0x08, 0x06,
    0xc4, 0x80, 0x60, 0x40, 0x0c, 0x08, 0x06, 0xc4, 0x80, 0x18, 0x10, 0x0c, 0x88, 0x01, 0xc1, 0x80,
    0x18, 0x10, 0x0c, 0x88, 0x01, 0xc1, 0x80, 0x18, 0x10, 0x0c, 0x88, 0x01, 0xc1, 0x80, 0x18, 0x10,
    0x0c, 0x88, 0x01, 0xc1, 0x80, 0x18, 0x10, 0x0c, 0x88, 0x01, 0xc1, 0x80, 0x18, 0x10, 0x0c, 0x88,
    0x01, 0xc1, 0x80, 0x18, 0x10, 0x0c, 0x88, 0x01, 0xc1, 0x80, 0x18, 0x10, 0x0c, 0x88, 0x01, 0x31,
    0xa0, 0x08, 0x18, 0x10, 0x03, 0x82, 0x01, 0x31, 0x20, 0x18, 0x10, 0x03, 0x82, 0x01, 0x31, 0x20,
    0x18, 0x10, 0x03, 0x82, 0x01, 0x31, 0x20, 0x18, 0x10, 0x03, 0x82, 0x01, 0x31, 0x20, 0x18, 0x10,
    0x03, 0x82, 0x01, 0x31, 0x20, 0x18, 0x10, 0x03, 0x82, 0x01, 0x31, 0x20, 0x18, 0x10, 0x03, 0x82,
    0x01, 0x31, 0x20, 0x18, 0x10, 0x03, 0x82, 0x01, 0x31, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30,
    0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06,
    0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03,
    0x62, 0x40, 0x30, 0x20, 0x06, 0x04, 0x03, 0x62, 0x40, 0x30, 0x20, 0x06, 0x84, 0xb3, 0x6a, 0xc0,
    0xf0, 0x6d, 0xbd, 0xae, 0x8e, 0x87, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
    0x60, 0x82
};

/* The 160-pixel artwork with status, no progress. */
static gf_splashscreen_options options_for(const char* status) {
    gf_splashscreen_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.artwork = artwork_png;
    options.artwork_size = sizeof(artwork_png);
    options.status = gf_kit_text(status);
    return options;
}

/* A caller compiled against the 1.0 header: the record ends after
 * available. tail is not part of it: the library must leave it alone. */
typedef struct gf_host_capabilities_1_0 {
    uint32_t struct_size;
    uint32_t protocol_version;
    gf_host_capability available;
    uint8_t tail[8];
} gf_host_capabilities_1_0;

typedef struct off_thread_status {
    gf_kit* kit;
    gf_splashscreen splashscreen;
    gf_status status;
} off_thread_status;

static void* set_status_off_thread(void* argument) {
    off_thread_status* call = (off_thread_status*)argument;
    (*call).status = (*(*call).kit).runtime.splashscreen_set_status((*call).splashscreen, gf_kit_text("Elsewhere"));
    return NULL;
}

void gf_kit_splashscreen_refusals(gf_kit* kit) {
    gf_host_capabilities report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    gf_kit_status(kit, (*kit).host.capabilities(&report), GF_STATUS_OK, "read the transient surfaces");
    const int has_surfaces = (report.available & GF_HOST_CAPABILITY_TRANSIENT_SURFACES) != 0u;
    gf_kit_check(kit, has_surfaces ? report.transient_surfaces != 0u : report.transient_surfaces == 0u,
                 "transient surface kinds are reported with the capability only");
    gf_kit_check(kit, (report.transient_surfaces & ~31u) == 0u, "only known transient surface bits are reported");
    gf_kit_check(kit, report.reserved == 0u, "the report's reserved field is zero");
    gf_host_capabilities_1_0 short_report;
    memset(&short_report, 0xab, sizeof(short_report));
    short_report.struct_size = 16u;
    gf_kit_status(kit, (*kit).host.capabilities((gf_host_capabilities*)&short_report), GF_STATUS_OK,
                  "a 1.0 capability record is accepted");
    gf_kit_check(kit, short_report.tail[0] == 0xabu && short_report.tail[7] == 0xabu,
                 "a 1.0 capability record receives the 1.0 fields only");

    gf_splashscreen splashscreen = 0u;
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(NULL, &splashscreen), GF_STATUS_INVALID_ARGUMENT,
                  "splashscreen_show needs its options");
    gf_splashscreen_options options = options_for("Starting");
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "splashscreen_show needs its handle");
    options.struct_size = 8u;
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, &splashscreen), GF_STATUS_INVALID_ARGUMENT,
                  "a record smaller than the 1.1 size is refused");
    options = options_for("Starting");
    options.artwork_size = 16u;
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, &splashscreen), GF_STATUS_INVALID_ARGUMENT,
                  "artwork that is not a whole PNG file is refused");
    options = options_for("Starting");
    options.artwork = NULL;
    options.artwork_size = 0u;
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, &splashscreen), GF_STATUS_INVALID_ARGUMENT,
                  "a splashscreen without artwork is refused");
    options = options_for("Starting");
    options.show_progress = 1u;
    options.progress = 1.5;
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, &splashscreen), GF_STATUS_INVALID_ARGUMENT,
                  "a progress outside [0, 1] is refused");
    options = options_for("\xff\xfe");
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, &splashscreen), GF_STATUS_INVALID_ARGUMENT,
                  "a status that is not UTF-8 is refused");
    gf_kit_check(kit, splashscreen == 0u, "a refused show writes no handle");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_status(0u, gf_kit_text("None")), GF_STATUS_STALE_HANDLE,
                  "set_status needs an open splashscreen");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_progress(0u, 1u, 0.5), GF_STATUS_STALE_HANDLE,
                  "set_progress needs an open splashscreen");
    gf_kit_status(kit, (*kit).runtime.splashscreen_close(0u), GF_STATUS_STALE_HANDLE,
                  "close needs an open splashscreen");
}

void gf_kit_splashscreen(gf_kit* kit) {
    gf_splashscreen_options options = options_for("Starting");
    gf_splashscreen splashscreen = 0u;
    const gf_status shown = (*kit).runtime.splashscreen_show(&options, &splashscreen);
    gf_kit_check(kit, shown == GF_STATUS_OK || shown == GF_STATUS_UNSUPPORTED_CAPABILITY,
                 "a valid splashscreen shows, or the host has none");
    if (shown != GF_STATUS_OK) {
        return;
    }
    gf_kit_check(kit, splashscreen != 0u, "a shown splashscreen has a handle");
    gf_splashscreen second = 0u;
    gf_kit_status(kit, (*kit).runtime.splashscreen_show(&options, &second), GF_STATUS_INVALID_STATE,
                  "one splashscreen at a time");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_status(splashscreen, gf_kit_text("Loading")), GF_STATUS_OK,
                  "set the splashscreen's status");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_progress(splashscreen, 1u, 0.5), GF_STATUS_OK,
                  "show the splashscreen's progress");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_progress(splashscreen, 1u, -0.5), GF_STATUS_INVALID_ARGUMENT,
                  "a progress outside [0, 1] is refused while shown");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_progress(splashscreen, 0u, -0.5), GF_STATUS_OK,
                  "hide the splashscreen's progress");
    off_thread_status call;
    memset(&call, 0, sizeof(call));
    call.kit = kit;
    call.splashscreen = splashscreen;
    call.status = -1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, &set_status_off_thread, &call) == 0) {
        (void)pthread_join(thread, NULL);
    }
    gf_kit_status(kit, call.status, GF_STATUS_WRONG_THREAD, "another thread is refused");
    gf_kit_status(kit, (*kit).runtime.splashscreen_close(splashscreen), GF_STATUS_OK, "close the splashscreen");
    gf_kit_status(kit, (*kit).runtime.splashscreen_set_status(splashscreen, gf_kit_text("Gone")),
                  GF_STATUS_STALE_HANDLE, "a closed splashscreen's handle is stale");
    gf_kit_status(kit, (*kit).runtime.splashscreen_close(splashscreen), GF_STATUS_STALE_HANDLE,
                  "a closed splashscreen closes once");
}
