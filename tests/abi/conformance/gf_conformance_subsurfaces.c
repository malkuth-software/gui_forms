/* Kit part: a threaded sub-surface (GF_TABLE_SUBSURFACES, since 1.1). A
 * window runs a sub_surface_host whose content a build callback makes on
 * the sub-surface's own thread. The checks: the build and the callbacks
 * posted to the content's objects run on that thread; objects belong to the
 * thread that made them; the tables a sub-surface may not call refuse;
 * stopping the sub-surface ends its objects' identities as stale and
 * releases the build callback on its thread; a build that hands over no
 * root fails with a reason. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

/* How many times the UI thread looks for the sub-surface to reach a state,
 * one millisecond apart. */
enum { poll_limit = 5000 };

/* What the run's phases wait for, in turn. */
enum {
    phase_running,
    phase_posted,
    phase_failed,
    phase_done
};

/* The run's state. The UI thread writes the phase fields; the sub-surface's
 * thread writes the build_ and inside_ fields before the step the UI thread
 * waits for (its state becoming running, inside_runs growing), and the UI
 * thread reads them only after that step. */
typedef struct subsurface_run {
    gf_kit* kit;
    gf_object root;
    gf_object host;
    uint32_t phase;
    uint32_t polls;
    int timed_out;
    gf_callback poll_callback;
    gf_callback inside_callback;
    /* The build callback (the sub-surface's thread). */
    uint32_t builds;
    int build_off_ui;
    gf_object content_root;
    gf_status build_create;
    gf_status build_set_root;
    gf_status build_ui_object;
    uint32_t build_releases;
    int build_release_off_ui;
    /* The callback posted to the content's root (the sub-surface's thread). */
    atomic_uint inside_runs;
    int inside_off_ui;
    gf_status inside_set_text;
    gf_status inside_create;
    gf_status inside_window_call;
    uint32_t inside_releases;
    /* What the UI thread saw. */
    gf_subsurface_state running_state;
    uint64_t running_epoch;
    gf_status ui_on_content;
    gf_status ui_set_root;
    gf_status stop;
    gf_subsurface_state stopped_state;
    gf_status stale_state;
    gf_status stale_post;
    gf_status release_stale;
    /* The content that hands over no root. */
    uint32_t empty_builds;
    gf_subsurface_state failed_state;
    uint64_t failure_size;
    char failure[128];
} subsurface_run;

static int off_ui(const subsurface_run* run) {
    const int off = pthread_equal(pthread_self(), (*(*run).kit).ui_thread) == 0;
    return off;
}

static void GF_CALL build_content(void* context, gf_event* event) {
    subsurface_run* run = (subsurface_run*)context;
    gf_kit* kit = (*run).kit;
    (void)event;
    ++(*run).builds;
    (*run).build_off_ui = off_ui(run);
    gf_object_state host_state = 0u;
    /* The host is the UI thread's. */
    (*run).build_ui_object = (*kit).objects.state((*run).host, &host_state);
    (*run).build_create =
        (*kit).objects.create(gf_kit_text("Label"), gf_kit_text("kit.subsurface.content"), &(*run).content_root);
    if ((*run).build_create == GF_STATUS_OK) {
        (*run).build_set_root = (*kit).subsurfaces.set_root((*run).content_root);
    }
}

static void GF_CALL release_build(void* context) {
    subsurface_run* run = (subsurface_run*)context;
    ++(*run).build_releases;
    (*run).build_release_off_ui = off_ui(run);
}

static void GF_CALL build_nothing(void* context, gf_event* event) {
    subsurface_run* run = (subsurface_run*)context;
    (void)event;
    ++(*run).empty_builds;
}

static void GF_CALL inside_content(void* context, gf_event* event) {
    subsurface_run* run = (subsurface_run*)context;
    gf_kit* kit = (*run).kit;
    (*run).inside_off_ui = off_ui(run);
    (*run).inside_set_text = gf_kit_set_text(kit, (*event).sender, "text", "set on the sub-surface");
    gf_object made = 0u;
    (*run).inside_create = (*kit).objects.create(gf_kit_text("Label"), gf_kit_text("kit.subsurface.made"), &made);
    if ((*run).inside_create == GF_STATUS_OK) {
        (void)(*kit).objects.release(made);
    }
    /* Windows stay on the UI thread. */
    (*run).inside_window_call = (*kit).windows.request_close((*run).root);
    atomic_fetch_add_explicit(&(*run).inside_runs, 1u, memory_order_release);
}

static void GF_CALL release_inside(void* context) {
    ++(*(subsurface_run*)context).inside_releases;
}

static gf_subsurface_content content_of(subsurface_run* run, void (GF_CALL* build)(void*, gf_event*)) {
    gf_subsurface_content content;
    memset(&content, 0, sizeof(content));
    content.struct_size = (uint32_t)sizeof(content);
    content.opaque = 0u;
    content.build = gf_kit_callback(build, &release_build, run);
    return content;
}

/* Polls again in a millisecond, or gives up and closes the window. */
static void poll_again(subsurface_run* run) {
    gf_kit* kit = (*run).kit;
    ++(*run).polls;
    if ((*run).polls > (uint32_t)poll_limit) {
        (*run).timed_out = 1;
        (void)(*kit).windows.request_close((*run).root);
        return;
    }
    usleep(1000);
    (void)(*kit).dispatch.begin_invoke((*run).root, &(*run).poll_callback);
}

/* The sub-surface runs: what the UI thread may and may not do with it. */
static void on_running(subsurface_run* run) {
    gf_kit* kit = (*run).kit;
    (void)(*kit).subsurfaces.content_epoch((*run).host, &(*run).running_epoch);
    gf_object_state content_state = 0u;
    (*run).ui_on_content = (*kit).objects.state((*run).content_root, &content_state);
    (*run).ui_set_root = (*kit).subsurfaces.set_root((*run).content_root);
    (void)(*kit).dispatch.begin_invoke((*run).content_root, &(*run).inside_callback);
}

/* The posted callback ran: stop the sub-surface, then start one that fails. */
static void on_posted(subsurface_run* run) {
    gf_kit* kit = (*run).kit;
    (*run).stop = (*kit).subsurfaces.set_content((*run).host, NULL);
    (void)(*kit).subsurfaces.state((*run).host, &(*run).stopped_state);
    gf_object_state stale = 0u;
    (*run).stale_state = (*kit).objects.state((*run).content_root, &stale);
    (*run).stale_post = (*kit).dispatch.begin_invoke((*run).content_root, &(*run).inside_callback);
    (*run).release_stale = (*kit).objects.release((*run).content_root);
    const gf_subsurface_content empty = content_of(run, &build_nothing);
    (void)(*kit).subsurfaces.set_content((*run).host, &empty);
}

static void on_failed(subsurface_run* run) {
    gf_kit* kit = (*run).kit;
    (void)(*kit).subsurfaces.failure((*run).host, (*run).failure, sizeof((*run).failure) - 1u,
                                     &(*run).failure_size);
    (void)(*kit).subsurfaces.set_content((*run).host, NULL);
    (void)(*kit).windows.request_close((*run).root);
}

static void GF_CALL poll(void* context, gf_event* event) {
    subsurface_run* run = (subsurface_run*)context;
    gf_kit* kit = (*run).kit;
    (void)event;
    gf_subsurface_state state = GF_SUBSURFACE_STATE_STOPPED;
    (void)(*kit).subsurfaces.state((*run).host, &state);
    if ((*run).phase == phase_running) {
        if (state == GF_SUBSURFACE_STATE_RUNNING) {
            (*run).running_state = state;
            (*run).phase = phase_posted;
            (*run).polls = 0u;
            on_running(run);
        }
        poll_again(run);
    } else if ((*run).phase == phase_posted) {
        if (atomic_load_explicit(&(*run).inside_runs, memory_order_acquire) != 0u) {
            (*run).phase = phase_failed;
            (*run).polls = 0u;
            on_posted(run);
        }
        poll_again(run);
    } else if ((*run).phase == phase_failed) {
        if (state == GF_SUBSURFACE_STATE_FAILED) {
            (*run).failed_state = state;
            (*run).phase = phase_done;
            on_failed(run);
            return;
        }
        poll_again(run);
    } else {
        /* Done: the window is closing. */
    }
}

static void GF_CALL on_ready(void* context, gf_event* event) {
    subsurface_run* run = (subsurface_run*)context;
    (void)event;
    (void)(*(*run).kit).dispatch.begin_invoke((*run).root, &(*run).poll_callback);
}

static void refusals(gf_kit* kit, subsurface_run* run) {
    gf_subsurface_content content = content_of(run, &build_content);
    gf_kit_status(kit, (*kit).subsurfaces.set_content((*run).root, &content), GF_STATUS_WRONG_HANDLE_KIND,
                  "set_content on a control that is not a sub_surface_host is refused");
    content.struct_size = 8u;
    gf_kit_status(kit, (*kit).subsurfaces.set_content((*run).host, &content), GF_STATUS_INVALID_ARGUMENT,
                  "a short gf_subsurface_content is refused");
    gf_kit_check(kit, (*run).build_releases == 0u, "a refused set_content does not take the build callback");
    gf_kit_status(kit, (*kit).subsurfaces.set_root((*run).root), GF_STATUS_INVALID_STATE,
                  "set_root outside a build callback is refused");
    gf_subsurface_state state = GF_SUBSURFACE_STATE_FAILED;
    gf_kit_status(kit, (*kit).subsurfaces.state((*run).host, &state), GF_STATUS_OK, "read a host's state");
    gf_kit_check(kit, state == GF_SUBSURFACE_STATE_STOPPED, "a host without content has no thread");
    uint64_t epoch = 99u;
    gf_kit_status(kit, (*kit).subsurfaces.content_epoch((*run).host, &epoch), GF_STATUS_OK,
                  "read a host's content epoch");
    gf_kit_check(kit, epoch == 0u, "no configuration before the host is laid out");
    uint64_t size = 99u;
    gf_kit_status(kit, (*kit).subsurfaces.failure((*run).host, NULL, 0u, &size), GF_STATUS_OK,
                  "read a host's failure");
    gf_kit_check(kit, size == 0u, "no failure while stopped");
}

void gf_kit_subsurfaces(gf_kit* kit) {
    subsurface_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    atomic_init(&run.inside_runs, 0u);
    run.root = gf_kit_create(kit, "Panel", "kit.subsurface.window");
    run.host = gf_kit_create(kit, "SubSurfaceHost", "kit.subsurface.host");
    gf_kit_status(kit, (*kit).objects.insert_child(run.root, run.host, 0u), GF_STATUS_OK,
                  "put a sub_surface_host in the window's tree");
    gf_value bounds;
    memset(&bounds, 0, sizeof(bounds));
    bounds.struct_size = (uint32_t)sizeof(bounds);
    bounds.kind = GF_VALUE_KIND_RECTANGLE;
    bounds.rectangle.x = 10.0;
    bounds.rectangle.y = 10.0;
    bounds.rectangle.width = 160.0;
    bounds.rectangle.height = 90.0;
    gf_kit_status(kit, (*kit).properties.set(run.host, gf_kit_text("bounds"), &bounds), GF_STATUS_OK,
                  "give the host a region");
    refusals(kit, &run);

    const gf_subsurface_content content = content_of(&run, &build_content);
    gf_kit_status(kit, (*kit).subsurfaces.set_content(run.host, &content), GF_STATUS_OK, "give the host content");
    run.poll_callback = gf_kit_callback(&poll, NULL, &run);
    run.inside_callback = gf_kit_callback(&inside_content, &release_inside, &run);
    const gf_callback ready = gf_kit_callback(&on_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("GUI.Forms C ABI sub-surface");
    options.width = 240.0;
    options.height = 160.0;
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(run.root, &options), GF_STATUS_OK, "run a window with a sub-surface");

    gf_kit_check(kit, run.timed_out == 0, "the sub-surface reached each state in time");
    gf_kit_check(kit, run.builds == 1u && run.build_off_ui, "the build ran once, on the sub-surface's thread");
    gf_kit_status(kit, run.build_ui_object, GF_STATUS_WRONG_THREAD,
                  "the UI thread's object is refused on the sub-surface's thread");
    gf_kit_status(kit, run.build_create, GF_STATUS_OK, "the build creates a control");
    gf_kit_status(kit, run.build_set_root, GF_STATUS_OK, "the build hands over its root");
    gf_kit_check(kit, run.running_state == GF_SUBSURFACE_STATE_RUNNING, "the sub-surface runs");
    gf_kit_check(kit, run.running_epoch != 0u, "a configuration was sent");
    gf_kit_status(kit, run.ui_on_content, GF_STATUS_WRONG_THREAD,
                  "the sub-surface's object is refused on the UI thread");
    gf_kit_status(kit, run.ui_set_root, GF_STATUS_INVALID_STATE, "set_root on the UI thread is refused");
    gf_kit_check(kit, atomic_load(&run.inside_runs) == 1u && run.inside_off_ui,
                 "a callback posted to the content runs on the sub-surface's thread");
    gf_kit_status(kit, run.inside_set_text, GF_STATUS_OK, "the sub-surface's thread sets its object's property");
    gf_kit_status(kit, run.inside_create, GF_STATUS_OK, "the sub-surface's thread creates and releases a control");
    gf_kit_status(kit, run.inside_window_call, GF_STATUS_WRONG_THREAD,
                  "the windows table is refused on the sub-surface's thread");
    gf_kit_status(kit, run.stop, GF_STATUS_OK, "set_content with no content stops the sub-surface");
    gf_kit_check(kit, run.stopped_state == GF_SUBSURFACE_STATE_STOPPED, "a stopped host has no thread");
    gf_kit_status(kit, run.stale_state, GF_STATUS_STALE_HANDLE, "the stopped sub-surface's objects are stale");
    gf_kit_status(kit, run.stale_post, GF_STATUS_STALE_HANDLE, "nothing is posted to a stale object");
    gf_kit_status(kit, run.release_stale, GF_STATUS_OK, "a stale identity is released");
    gf_kit_check(kit, run.inside_releases == 1u, "the posted callback is released once");
    gf_kit_check(kit, run.empty_builds == 1u && run.failed_state == GF_SUBSURFACE_STATE_FAILED,
                 "a build that hands over no root fails");
    gf_kit_check(kit, run.failure_size != 0u, "a failed sub-surface says why");
    gf_kit_check(kit, run.build_releases == 2u && run.build_release_off_ui,
                 "each build callback is released once, on its sub-surface's thread");
    gf_kit_status(kit, (*kit).objects.release(run.host), GF_STATUS_OK, "release the host");
    gf_kit_status(kit, (*kit).objects.release(run.root), GF_STATUS_OK, "release the window's root");
}
