/* Kit part: one window's run, close requests and posted work. The window
 * is on screen briefly: a primary window cannot start hidden. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <string.h>

/* The run's state, shared by its callbacks. order records them in turn:
 * R ready, Q a posted close request, C closing, X a cancelled closing,
 * D closed. */
typedef struct window_run {
    gf_kit* kit;
    gf_object root;
    char order[16];
    uint32_t order_length;
    uint32_t releases[4];
    gf_status nested_run;
    gf_status worker_post;
    gf_status requests[2];
    uint32_t request_count;
    int worker_started;
    pthread_t worker;
    gf_callback request_callback;
} window_run;

enum { release_ready, release_closing, release_closed, release_request };

static void note(window_run* run, char mark) {
    if ((*run).order_length + 1u < sizeof((*run).order)) {
        (*run).order[(*run).order_length] = mark;
        ++(*run).order_length;
        (*run).order[(*run).order_length] = '\0';
    }
}

static void* posting_worker(void* argument) {
    window_run* run = (window_run*)argument;
    (*run).worker_post = (*(*run).kit).dispatch.begin_invoke((*run).root, &(*run).request_callback);
    return NULL;
}

static void GF_CALL on_request(void* context, gf_event* event) {
    window_run* run = (window_run*)context;
    note(run, 'Q');
    const gf_status requested = (*(*run).kit).windows.request_close((*event).sender);
    if ((*run).request_count < 2u) {
        (*run).requests[(*run).request_count] = requested;
    }
    ++(*run).request_count;
}

static void GF_CALL on_ready(void* context, gf_event* event) {
    window_run* run = (window_run*)context;
    note(run, 'R');
    gf_kit_host_running((*run).kit);
    gf_run_options nested;
    memset(&nested, 0, sizeof(nested));
    nested.struct_size = (uint32_t)sizeof(nested);
    (*run).nested_run = (*(*run).kit).windows.run((*event).sender, &nested);
    /* The first close request comes from another thread, through the run's
     * event loop. */
    (*run).worker_started = pthread_create(&(*run).worker, NULL, &posting_worker, run) == 0;
    if (!(*run).worker_started) {
        (void)(*(*run).kit).windows.request_close((*event).sender);
    }
}

static void GF_CALL on_closing(void* context, gf_event* event) {
    window_run* run = (window_run*)context;
    note(run, 'C');
    if ((*run).request_count == 1u) {
        /* Keep the window once, and ask again from the UI thread. */
        (*event).cancel = 1u;
        note(run, 'X');
        (void)(*(*run).kit).dispatch.begin_invoke((*event).sender, &(*run).request_callback);
    }
}

static void GF_CALL on_closed(void* context, gf_event* event) {
    window_run* run = (window_run*)context;
    (void)event;
    note(run, 'D');
}

static void GF_CALL release_ready_callback(void* context) {
    ++(*(window_run*)context).releases[release_ready];
}
static void GF_CALL release_closing_callback(void* context) {
    ++(*(window_run*)context).releases[release_closing];
}
static void GF_CALL release_closed_callback(void* context) {
    ++(*(window_run*)context).releases[release_closed];
}
static void GF_CALL release_request_callback(void* context) {
    ++(*(window_run*)context).releases[release_request];
}

void gf_kit_window(gf_kit* kit) {
    window_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.nested_run = -1;
    run.worker_post = -1;
    run.root = gf_kit_create(kit, "Panel", "kit.window");
    const gf_object button = gf_kit_create(kit, "Button", "kit.window.button");
    gf_kit_status(kit, (*kit).objects.insert_child(run.root, button, 0u), GF_STATUS_OK, "build the window's tree");
    run.request_callback = gf_kit_callback(&on_request, &release_request_callback, &run);
    const gf_callback ready = gf_kit_callback(&on_ready, &release_ready_callback, &run);
    const gf_callback closing = gf_kit_callback(&on_closing, &release_closing_callback, &run);
    const gf_callback closed = gf_kit_callback(&on_closed, &release_closed_callback, &run);

    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.flags = GF_RUN_FLAGS_NONE;
    options.title = gf_kit_text("GUI.Forms C ABI conformance");
    options.width = 320.0;
    options.height = 200.0;
    options.ready = &ready;
    options.closing = &closing;
    options.closed = &closed;

    gf_kit_status(kit, (*kit).windows.request_close(run.root), GF_STATUS_INVALID_STATE,
                  "request_close before the run is refused");
    gf_kit_status(kit, (*kit).windows.run(button, &options), GF_STATUS_INVALID_STATE,
                  "a root with a parent is refused");
    gf_kit_check(kit, run.releases[release_ready] == 0u, "a refused run does not take its callbacks");
    options.flags = 1u;
    gf_kit_status(kit, (*kit).windows.run(run.root, &options), GF_STATUS_INVALID_ARGUMENT,
                  "unknown run flags are refused");
    options.flags = GF_RUN_FLAGS_NONE;
    /* The minimum client size (since 1.1): a negative side is refused before
     * the run takes the callbacks; a minimum under the client size runs. */
    options.minimum_width = -1.0;
    gf_kit_status(kit, (*kit).windows.run(run.root, &options), GF_STATUS_INVALID_ARGUMENT,
                  "a negative minimum size is refused");
    gf_kit_check(kit, run.releases[release_ready] == 0u, "a refused minimum does not take the callbacks");
    options.minimum_width = 100.0;
    options.minimum_height = 80.0;

    gf_kit_status(kit, (*kit).windows.run(run.root, &options), GF_STATUS_OK, "run a window");
    if (run.worker_started) {
        (void)pthread_join(run.worker, NULL);
    }
    gf_kit_status(kit, run.nested_run, GF_STATUS_INVALID_STATE, "a second run during a run is refused");
    gf_kit_status(kit, run.worker_post, GF_STATUS_OK, "a worker posts to the running window");
    gf_kit_check(kit, strcmp(run.order, "RQCXQCD") == 0,
                 "ready, a posted close, a cancelled closing, a second close, closing, closed");
    gf_kit_status(kit, run.requests[0], GF_STATUS_OK, "the first close request");
    gf_kit_status(kit, run.requests[1], GF_STATUS_OK, "the second close request");
    gf_kit_check(kit, run.releases[release_ready] == 1u && run.releases[release_closing] == 1u &&
                          run.releases[release_closed] == 1u,
                 "each run callback is released once");
    gf_kit_check(kit, run.releases[release_request] == 2u, "each posted callback is released once");
    gf_kit_status(kit, (*kit).windows.request_close(run.root), GF_STATUS_INVALID_STATE,
                  "request_close after the run is refused");
    gf_kit_check(kit, gf_kit_state(kit, button) == GF_OBJECT_STATE_ALIVE, "the tree outlives the window");
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release the button");
    gf_kit_status(kit, (*kit).objects.release(run.root), GF_STATUS_OK, "release the root");
}

/* A window from create: what its ready callback saw. */
typedef struct handle_run {
    gf_kit* kit;
    gf_object tips;
    gf_status made_during_run;
    gf_status close;
} handle_run;

static void GF_CALL on_handle_ready(void* context, gf_event* event) {
    handle_run* run = (handle_run*)context;
    gf_kit* kit = (*run).kit;
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = (*event).sender;
    (*run).made_during_run = (*kit).objects.create_with(gf_kit_text("tool_tip"), gf_kit_text("kit.handle.tips"),
                                                        &arguments, &(*run).tips);
    (*run).close = (*kit).windows.request_close((*event).sender);
}

/* A window made by create, components bound to it before and during the
 * run, and the window's end. */
void gf_kit_window_handle(gf_kit* kit) {
    handle_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.made_during_run = -1;
    run.close = -1;
    const gf_object root = gf_kit_create(kit, "Panel", "kit.handle");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window");
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = window;
    gf_object timer = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("timer"), gf_kit_text("kit.handle.timer"), &arguments,
                                                  &timer),
                  GF_STATUS_OK, "a timer bound to the window before its run");
    const gf_callback ready = gf_kit_callback(&on_handle_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("GUI.Forms C ABI window handle");
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(window, &options), GF_STATUS_OK, "run a window from create");
    gf_kit_status(kit, run.made_during_run, GF_STATUS_OK, "a component bound to the running window");
    gf_kit_status(kit, run.close, GF_STATUS_OK, "request_close takes the window");
    gf_kit_check(kit, gf_kit_state(kit, window) == GF_OBJECT_STATE_DISPOSED, "the run ends the window");
    gf_kit_check(kit, gf_kit_state(kit, timer) == GF_OBJECT_STATE_ALIVE,
                 "a bound component outlives its window, unbound");
    gf_kit_status(kit, (*kit).windows.run(window, &options), GF_STATUS_DISPOSED, "a window runs once");
    gf_kit_status(kit, (*kit).objects.release(run.tips), GF_STATUS_OK, "release the run's tool tip");
    gf_kit_status(kit, (*kit).objects.release(timer), GF_STATUS_OK, "release the timer");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the window's root");
}
