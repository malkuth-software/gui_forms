/* Kit part: the diagnostics table's shadow frames (stage C5). Callbacks never
 * nest, so the stack stays shallow here; its capacity and overflow are tested
 * in tests/shadow_frames_tests.cpp. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_shadow_stack_info stack_info(void) {
    gf_shadow_stack_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    return info;
}

static int text_is(const uint8_t* field, uint32_t size, const char* text) {
    const size_t length = strlen(text);
    return size == length && memcmp(field, text, length) == 0;
}

/* What the callbacks below saw of the stack. */
typedef struct frames_probe {
    gf_kit* kit;
    gf_object check;
    uint32_t invoked;
    gf_status status;
    gf_status small_status;
    uint64_t required;
    uint64_t small_required;
    gf_shadow_stack_info info;
    gf_shadow_frame frames[GF_SHADOW_STACK_CAPACITY];
} frames_probe;

static void read_frames(frames_probe* probe) {
    gf_kit* kit = (*probe).kit;
    gf_shadow_frame one;
    gf_shadow_stack_info info = stack_info();
    (*probe).small_status = (*kit).diagnostics.shadow_frames(&one, 1u, &(*probe).small_required, &info);
    (*probe).info = stack_info();
    (*probe).status = (*kit).diagnostics.shadow_frames((*probe).frames, GF_SHADOW_STACK_CAPACITY,
                                                       &(*probe).required, &(*probe).info);
}

static void GF_CALL check_invoke(void* context, gf_event* event) {
    (void)event;
    frames_probe* probe = (frames_probe*)context;
    ++(*probe).invoked;
    read_frames(probe);
}

static void GF_CALL posted_invoke(void* context, gf_event* event) {
    (void)event;
    frames_probe* probe = (frames_probe*)context;
    ++(*probe).invoked;
    read_frames(probe);
    /* A failing call inside a callback closes its frame too. */
    gf_object none = 0u;
    (void)(*(*probe).kit).objects.create(gf_kit_text(""), gf_kit_text("kit.diag.bad"), &none);
}

static void outside(gf_kit* kit) {
    uint64_t required = 9u;
    gf_shadow_stack_info info = stack_info();
    gf_kit_status(kit, (*kit).diagnostics.shadow_frames(NULL, 0u, &required, &info), GF_STATUS_OK,
                  "shadow_frames with no buffer outside any call");
    gf_kit_check(kit, required == 0u && info.depth == 0u && info.dropped == 0u,
                 "no frames are open between calls");
    gf_kit_status(kit, (*kit).diagnostics.shadow_frames(NULL, 0u, NULL, &info), GF_STATUS_INVALID_ARGUMENT,
                  "shadow_frames needs required");
    gf_kit_status(kit, (*kit).diagnostics.shadow_frames(NULL, 1u, &required, &info), GF_STATUS_INVALID_ARGUMENT,
                  "shadow_frames needs frames when capacity is not zero");
    gf_shadow_stack_info small;
    memset(&small, 0, sizeof(small));
    small.struct_size = 8u;
    gf_kit_status(kit, (*kit).diagnostics.shadow_frames(NULL, 0u, &required, &small), GF_STATUS_INVALID_ARGUMENT,
                  "shadow_frames needs an info record of the 1.0 size");
}

static void in_event(gf_kit* kit) {
    frames_probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.kit = kit;
    probe.check = gf_kit_create(kit, "check_box", "kit.diag.check");
    const gf_callback callback = gf_kit_callback(&check_invoke, NULL, &probe);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(probe.check, gf_kit_text("check_state_changed"), &callback,
                                               &subscription),
                  GF_STATUS_OK, "subscribe for frames");
    gf_value checked;
    memset(&checked, 0, sizeof(checked));
    checked.struct_size = (uint32_t)sizeof(checked);
    checked.kind = GF_VALUE_KIND_BOOLEAN;
    checked.boolean = 1u;
    gf_kit_status(kit, (*kit).properties.set(probe.check, gf_kit_text("checked"), &checked), GF_STATUS_OK,
                  "check the check box for frames");
    gf_kit_status(kit, probe.status, GF_STATUS_OK, "shadow_frames inside an event callback");
    gf_kit_check(kit, probe.required >= 2u && probe.required == probe.info.depth, "the callback sees its frames");
    gf_kit_check(kit, probe.small_status == GF_STATUS_BUFFER_TOO_SMALL && probe.small_required == probe.required,
                 "a small buffer reports the frames needed");
    const gf_shadow_frame* outer = &probe.frames[0];
    gf_kit_check(kit, (*outer).kind == GF_SHADOW_FRAME_KIND_LIBRARY_CALL && (*outer).table_id == GF_TABLE_PROPERTIES &&
                          (*outer).slot_index == 1u && (*outer).subject_size == 0u,
                 "the outermost frame is the properties.set call");
    const gf_shadow_frame* inner = &probe.frames[probe.required - 1u];
    gf_kit_check(kit, (*inner).kind == GF_SHADOW_FRAME_KIND_EVENT_CALLBACK &&
                          text_is((*inner).subject, (*inner).subject_size, "kit.diag.check") &&
                          text_is((*inner).name, (*inner).name_size, "check_state_changed") && (*inner).flags == 0u,
                 "the innermost frame is the event, with its sender and name");
    gf_kit_check(kit, (*inner).subject[(*inner).subject_size] == 0u, "the bytes after a frame's text are zero");

    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "disconnect the frames probe");
    gf_kit_status(kit, (*kit).objects.release(probe.check), GF_STATUS_OK, "release the frames check box");
}

static void in_posted(gf_kit* kit) {
    frames_probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.kit = kit;
    const gf_object label = gf_kit_create(kit, "label", "kit.diag.label");
    const gf_callback callback = gf_kit_callback(&posted_invoke, NULL, &probe);
    gf_kit_status(kit, (*kit).dispatch.begin_invoke(label, &callback), GF_STATUS_OK, "post for frames");
    uint64_t invoked = 0u;
    gf_kit_status(kit, (*kit).dispatch.run_pending(&invoked), GF_STATUS_OK, "run the posted frames probe");
    gf_kit_check(kit, probe.status == GF_STATUS_OK && probe.required == 2u, "a posted callback sees two frames");
    gf_kit_check(kit, probe.frames[0].kind == GF_SHADOW_FRAME_KIND_LIBRARY_CALL &&
                          probe.frames[0].table_id == GF_TABLE_DISPATCH && probe.frames[0].slot_index == 1u,
                 "the outer frame is dispatch.run_pending");
    gf_kit_check(kit, probe.frames[1].kind == GF_SHADOW_FRAME_KIND_POSTED_CALLBACK &&
                          text_is(probe.frames[1].subject, probe.frames[1].subject_size, "kit.diag.label"),
                 "the inner frame is the posted callback on its target");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the frames label");
}

/* The invalidation overlay's process switch: set, read back, refused
 * values, then restored. */
static void invalidation_overlay(gf_kit* kit) {
    if ((*kit).diagnostics.struct_size < 32u) return;
    uint32_t before = 7u;
    gf_kit_status(kit, (*kit).diagnostics.invalidation_overlay(&before), GF_STATUS_OK, "read the overlay switch");
    gf_kit_check(kit, before <= 1u, "the overlay switch reads 0 or 1");
    gf_kit_status(kit, (*kit).diagnostics.set_invalidation_overlay(1u), GF_STATUS_OK, "turn the overlay on");
    uint32_t on = 0u;
    gf_kit_status(kit, (*kit).diagnostics.invalidation_overlay(&on), GF_STATUS_OK, "read the overlay on");
    gf_kit_check(kit, on == 1u, "the overlay reads on");
    gf_kit_status(kit, (*kit).diagnostics.set_invalidation_overlay(2u), GF_STATUS_INVALID_ARGUMENT,
                  "an overlay switch other than 0 or 1 is refused");
    gf_kit_status(kit, (*kit).diagnostics.invalidation_overlay(NULL), GF_STATUS_INVALID_ARGUMENT,
                  "reading the overlay needs an output");
    gf_kit_status(kit, (*kit).diagnostics.set_invalidation_overlay(before), GF_STATUS_OK, "restore the overlay");
}

void gf_kit_diagnostics(gf_kit* kit) {
    outside(kit);
    invalidation_overlay(kit);
    in_event(kit);
    in_posted(kit);
    uint64_t required = 9u;
    gf_shadow_stack_info info = stack_info();
    gf_kit_status(kit, (*kit).diagnostics.shadow_frames(NULL, 0u, &required, &info), GF_STATUS_OK,
                  "shadow_frames after the callbacks");
    gf_kit_check(kit, required == 0u && info.depth == 0u, "every frame closed, on failing calls too");
}
