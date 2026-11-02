/* Kit part: the UI thread, any-thread release and begin_invoke. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <string.h>

/* The work one worker thread does, and what it saw. */
typedef struct worker_task {
    gf_kit* kit;
    gf_object object;
    const gf_callback* posted;
    gf_status create_status;
    gf_status state_status;
    gf_status retain_status;
    gf_status release_status;
    gf_status post_status;
    gf_object created;
} worker_task;

/* Checks that UI calls are refused here, then retains and releases. */
static void* refused_worker(void* argument) {
    worker_task* task = (worker_task*)argument;
    gf_object_state state = 0u;
    gf_object created = 0u;
    (*task).create_status = (*(*task).kit).objects.create(gf_kit_text("Panel"), gf_kit_text("kit.worker"), &created);
    (*task).state_status = (*(*task).kit).objects.state((*task).object, &state);
    (*task).retain_status = (*(*task).kit).objects.retain((*task).object);
    (*task).release_status = (*(*task).kit).objects.release((*task).object);
    return NULL;
}

/* Releases the last reference to the task's object. */
static void* releasing_worker(void* argument) {
    worker_task* task = (worker_task*)argument;
    (*task).release_status = (*(*task).kit).objects.release((*task).object);
    return NULL;
}

static void* posting_worker(void* argument) {
    worker_task* task = (worker_task*)argument;
    (*task).post_status = (*(*task).kit).dispatch.begin_invoke((*task).object, (*task).posted);
    return NULL;
}

/* Creates a control (becoming the UI thread of an idle library), then
 * releases it. */
static void* creating_worker(void* argument) {
    worker_task* task = (worker_task*)argument;
    (*task).create_status =
        (*(*task).kit).objects.create(gf_kit_text("Label"), gf_kit_text("kit.new-ui"), &(*task).created);
    (*task).release_status = (*(*task).kit).objects.release((*task).created);
    return NULL;
}

static int run_worker(void* (*body)(void*), worker_task* task) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, body, task) != 0) {
        return 0;
    }
    const int joined = pthread_join(thread, NULL) == 0;
    return joined;
}

static worker_task new_task(gf_kit* kit, gf_object object) {
    worker_task task;
    memset(&task, 0, sizeof(task));
    task.kit = kit;
    task.object = object;
    task.create_status = -1;
    task.state_status = -1;
    task.retain_status = -1;
    task.release_status = -1;
    task.post_status = -1;
    return task;
}

static void affinity(gf_kit* kit) {
    const gf_object panel = gf_kit_create(kit, "Panel", "kit.affinity");
    worker_task task = new_task(kit, panel);
    gf_kit_check(kit, run_worker(&refused_worker, &task), "start a worker");
    gf_kit_status(kit, task.create_status, GF_STATUS_WRONG_THREAD, "create off the UI thread is refused");
    gf_kit_status(kit, task.state_status, GF_STATUS_WRONG_THREAD, "state off the UI thread is refused");
    gf_kit_status(kit, task.retain_status, GF_STATUS_OK, "retain from any thread");
    gf_kit_status(kit, task.release_status, GF_STATUS_OK, "release from any thread");
    gf_kit_check(kit, gf_kit_state(kit, panel) == GF_OBJECT_STATE_ALIVE, "the UI thread's reference remains");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release on the UI thread");
}

static void deferred_release(gf_kit* kit) {
    /* A worker gives back the last reference; the label (and so its
     * subscription's release) ends on the UI thread, at its next call. */
    const gf_object label = gf_kit_create(kit, "Label", "kit.deferred");
    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(label, gf_kit_text("text_changed"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to the label");
    worker_task task = new_task(kit, label);
    gf_kit_check(kit, run_worker(&releasing_worker, &task), "start a releasing worker");
    gf_kit_status(kit, task.release_status, GF_STATUS_OK, "the last release off the UI thread");
    gf_kit_check(kit, recorder.released == 0u, "the object does not end on the worker");
    gf_kit_check(kit, gf_kit_state(kit, label) == GF_OBJECT_STATE_DISPOSED + 1u,
                 "its identity ended at the release");
    uint64_t invoked = 99u;
    gf_kit_status(kit, (*kit).dispatch.run_pending(&invoked), GF_STATUS_OK, "run_pending");
    gf_kit_check(kit, invoked == 0u, "nothing was posted");
    gf_kit_check(kit, recorder.released == 1u && recorder.off_thread_calls == 0u,
                 "the object ended on the UI thread");
}

static void posting(gf_kit* kit) {
    const gf_object target = gf_kit_create(kit, "Button", "kit.posting");
    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    worker_task task = new_task(kit, target);
    task.posted = &callback;
    gf_kit_check(kit, run_worker(&posting_worker, &task), "start a posting worker");
    gf_kit_status(kit, task.post_status, GF_STATUS_OK, "begin_invoke from any thread");
    gf_kit_check(kit, recorder.invoked == 0u, "a posted callback waits for the UI thread");
    gf_kit_status(kit, (*kit).objects.release(target), GF_STATUS_OK, "release the target while it is posted");
    uint64_t invoked = 0u;
    gf_kit_status(kit, (*kit).dispatch.run_pending(&invoked), GF_STATUS_OK, "run_pending");
    gf_kit_check(kit, invoked == 1u && recorder.invoked == 1u && recorder.released == 1u &&
                          recorder.last_sender == target && strcmp(recorder.last_name, "invoke") == 0 &&
                          recorder.off_thread_calls == 0u,
                 "the posted callback ran once on the UI thread, then released");
    gf_kit_check(kit, gf_kit_state(kit, target) == GF_OBJECT_STATE_DISPOSED + 1u,
                 "the posted reference ended with the callback");

    /* A target disposed before the drain only releases. */
    const gf_object disposed = gf_kit_create(kit, "Panel", "kit.posting.disposed");
    gf_kit_recorder skipped;
    memset(&skipped, 0, sizeof(skipped));
    skipped.kit = kit;
    const gf_callback skipped_callback = gf_kit_recording_callback(&skipped);
    gf_kit_status(kit, (*kit).dispatch.begin_invoke(disposed, &skipped_callback), GF_STATUS_OK, "post to a target");
    gf_kit_status(kit, (*kit).objects.dispose(disposed), GF_STATUS_OK, "dispose it");
    gf_kit_status(kit, (*kit).dispatch.run_pending(&invoked), GF_STATUS_OK, "run_pending");
    gf_kit_check(kit, invoked == 0u && skipped.invoked == 0u && skipped.released == 1u,
                 "a disposed target's callback only releases");
    gf_kit_status(kit, (*kit).objects.release(disposed), GF_STATUS_OK, "release the disposed target");

    /* The queue is bounded, and a refused post keeps the callback. */
    const gf_object busy = gf_kit_create(kit, "Panel", "kit.posting.busy");
    gf_kit_recorder many;
    memset(&many, 0, sizeof(many));
    many.kit = kit;
    const gf_callback many_callback = gf_kit_recording_callback(&many);
    uint32_t posted = 0u;
    uint32_t index = 0u;
    for (index = 0u; index < 4096u; ++index) {
        if ((*kit).dispatch.begin_invoke(busy, &many_callback) == GF_STATUS_OK) {
            ++posted;
        }
    }
    gf_kit_check(kit, posted == 4096u, "4096 callbacks may wait");
    gf_kit_status(kit, (*kit).dispatch.begin_invoke(busy, &many_callback), GF_STATUS_LIMIT_EXCEEDED,
                  "the 4097th is refused");
    gf_kit_status(kit, (*kit).dispatch.run_pending(&invoked), GF_STATUS_OK, "drain them");
    gf_kit_check(kit, invoked == 4096u && many.invoked == 4096u && many.released == 4096u,
                 "every accepted callback ran and released; the refused one did not");
    gf_kit_status(kit, (*kit).objects.release(busy), GF_STATUS_OK, "release the busy target");
    gf_kit_status(kit, (*kit).dispatch.begin_invoke(busy, &many_callback), GF_STATUS_STALE_HANDLE,
                  "posting to an ended target is refused");
}

static void handover(gf_kit* kit) {
    /* With nothing left, another thread may become the UI thread; once it
     * is done, this one may again. */
    worker_task task = new_task(kit, 0u);
    gf_kit_check(kit, run_worker(&creating_worker, &task), "start a creating worker");
    gf_kit_status(kit, task.create_status, GF_STATUS_OK, "an idle library binds a new UI thread");
    gf_kit_status(kit, task.release_status, GF_STATUS_OK, "that thread releases its object");
    const gf_object again = gf_kit_create(kit, "Panel", "kit.handover");
    gf_kit_check(kit, again != 0u, "the library is idle again afterwards");
    gf_kit_status(kit, (*kit).objects.release(again), GF_STATUS_OK, "release");
}

void gf_kit_threads(gf_kit* kit) {
    affinity(kit);
    deferred_release(kit);
    posting(kit);
    handover(kit);
}
