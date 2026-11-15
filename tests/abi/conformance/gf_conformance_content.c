/* The content table (GF_TABLE_CONTENT, since 1.4): a content surface's
 * channel driven from the caller's own code, a content program run on the
 * library's thread with its sink and its failure, the hosted-content
 * refusals, the content text editor's layout handshake and every refusal
 * that needs no window. Frames shown in a running window, the watchdog and
 * hosted programs are left to the C++ content tests
 * (tests/content_surface_tests.cpp, tests/hosted_content_tests.cpp). */

/* glibc and musl declare clock_gettime and CLOCK_REALTIME only with POSIX
 * 2008 visible under strict C11; macOS declares them either way. */
#define _POSIX_C_SOURCE 200809L

#include "gf_conformance_internal.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

/* What the content program saw on its own thread, read by the UI thread
 * once the library released the program's callback. The kit's checks are
 * not made from the program's thread. */
typedef struct content_program_state {
    gf_kit* kit;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    uint32_t ran;
    uint32_t released;
    gf_status sink_status;
    gf_status publish_status;
    gf_content_publish_result publish_result;
    gf_status semantics_status;
    gf_content_semantics_result semantics_result;
    gf_status wait_status;
    gf_status state_status;
    uint32_t revoked;
    gf_status statistics_status;
    uint64_t frames_published;
    gf_status release_status;
    gf_status fail_status;
} content_program_state;

static uint8_t content_pixels[2u * 2u * 4u];

/* A 2 by 2 frame over content_pixels with the whole frame as damage. */
static gf_content_frame content_frame(void) {
    gf_content_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.struct_size = (uint32_t)sizeof(gf_content_frame);
    frame.pixel_format = GF_CONTENT_PIXEL_FORMAT_BGRA32_PREMULTIPLIED_SRGB;
    frame.width = 2u;
    frame.height = 2u;
    frame.row_bytes = 8u;
    frame.opaque = 1u;
    frame.damage.width = 2u;
    frame.damage.height = 2u;
    frame.pixels.data = content_pixels;
    frame.pixels.size = sizeof(content_pixels);
    return frame;
}

static gf_content_semantics content_semantics(const char* name, const char* text) {
    gf_content_semantics semantics;
    memset(&semantics, 0, sizeof(semantics));
    semantics.struct_size = (uint32_t)sizeof(gf_content_semantics);
    semantics.name = gf_kit_text(name);
    semantics.text = gf_kit_text(text);
    semantics.description = gf_kit_text("");
    return semantics;
}

/* The program's run: everything a content thread may call, recorded. */
static void GF_CALL content_program_run(void* context, gf_event* event) {
    content_program_state* state = (content_program_state*)context;
    gf_kit* kit = (*state).kit;
    (void)event;
    gf_content_sink sink = 0u;
    (*state).sink_status = (*kit).content.program_sink(&sink);
    gf_content_request request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(gf_content_request);
    const gf_content_frame frame = content_frame();
    (*state).publish_status = (*kit).content.sink_publish(sink, &frame, &(*state).publish_result);
    const gf_content_semantics semantics = content_semantics("Kit program", "drawn on its own thread");
    (*state).semantics_status =
        (*kit).content.sink_publish_semantics(sink, &semantics, &(*state).semantics_result);
    /* A zero timeout returns the newest request at once. */
    (*state).wait_status = (*kit).content.sink_wait_for_request(sink, 0u, 0u, &request);
    uint32_t stop_requested = 0u;
    (*state).state_status = (*kit).content.sink_state(sink, &(*state).revoked, &stop_requested);
    gf_content_statistics statistics;
    memset(&statistics, 0, sizeof(statistics));
    statistics.struct_size = (uint32_t)sizeof(gf_content_statistics);
    (*state).statistics_status = (*kit).content.sink_statistics(sink, &statistics);
    (*state).frames_published = statistics.frames_published;
    (*state).release_status = (*kit).content.sink_release(sink);
    (*state).fail_status = (*kit).content.program_fail(gf_kit_text("the kit's program failed on purpose"));
    pthread_mutex_lock(&(*state).lock);
    (*state).ran = 1u;
    pthread_mutex_unlock(&(*state).lock);
}

static void GF_CALL content_program_release(void* context) {
    content_program_state* state = (content_program_state*)context;
    pthread_mutex_lock(&(*state).lock);
    (*state).released = 1u;
    pthread_cond_broadcast(&(*state).changed);
    pthread_mutex_unlock(&(*state).lock);
}

/* Waits up to ten seconds for the library to release the program; whether
 * it did. */
static int content_wait_for_release(content_program_state* state) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 10;
    pthread_mutex_lock(&(*state).lock);
    int waited = 0;
    while ((*state).released == 0u && waited != ETIMEDOUT) {
        waited = pthread_cond_timedwait(&(*state).changed, &(*state).lock, &deadline);
    }
    const int released = (*state).released != 0u;
    pthread_mutex_unlock(&(*state).lock);
    return released;
}

/* The caller's own channel: frames and semantics from this thread, the
 * refusals as results, termination and the stale handle. */
static void content_channel(gf_kit* kit, gf_object surface) {
    gf_content_sink sink = 0u;
    gf_kit_status(kit, (*kit).content.open_content(surface, 2u, &sink), GF_STATUS_INVALID_ARGUMENT,
                  "opaque is 0 or 1");
    gf_kit_status(kit, (*kit).content.open_content(surface, 1u, &sink), GF_STATUS_OK, "open the surface's content");
    gf_kit_check(kit, sink != 0u, "open_content gives a sink");
    gf_content_status status = GF_CONTENT_STATUS_FAILED;
    gf_kit_status(kit, (*kit).content.status(surface, &status), GF_STATUS_OK, "read the status");
    gf_kit_check(kit, status == GF_CONTENT_STATUS_WAITING, "an open surface waits for its first frame");

    gf_content_frame frame = content_frame();
    gf_content_publish_result result = GF_CONTENT_PUBLISH_REVOKED;
    gf_kit_status(kit, (*kit).content.sink_publish(sink, &frame, &result), GF_STATUS_OK, "publish a frame");
    gf_kit_check(kit, result == GF_CONTENT_PUBLISH_PUBLISHED, "a valid frame is published");
    frame.width = 0u;
    gf_kit_status(kit, (*kit).content.sink_publish(sink, &frame, &result), GF_STATUS_OK,
                  "publish an empty frame");
    gf_kit_check(kit, result == GF_CONTENT_PUBLISH_REFUSED_EMPTY, "an empty frame is a refusal, not a failed call");
    frame = content_frame();
    frame.pixel_format = 300u;
    gf_kit_status(kit, (*kit).content.sink_publish(sink, &frame, &result), GF_STATUS_OK,
                  "publish an unknown format");
    gf_kit_check(kit, result == GF_CONTENT_PUBLISH_REFUSED_FORMAT, "an unknown pixel format is refused");
    frame = content_frame();
    frame.reserved = 1u;
    gf_kit_status(kit, (*kit).content.sink_publish(sink, &frame, &result), GF_STATUS_INVALID_ARGUMENT,
                  "a frame's reserved field is zero");
    gf_kit_status(kit, (*kit).content.sink_heartbeat(sink), GF_STATUS_OK, "send a heartbeat");

    gf_content_statistics statistics;
    memset(&statistics, 0, sizeof(statistics));
    statistics.struct_size = (uint32_t)sizeof(gf_content_statistics);
    gf_kit_status(kit, (*kit).content.statistics(surface, &statistics), GF_STATUS_OK, "read the statistics");
    gf_kit_check(kit, statistics.frames_published == 1u && statistics.frames_refused == 2u &&
                          statistics.heartbeats == 1u &&
                          statistics.last_refusal == GF_CONTENT_PUBLISH_REFUSED_FORMAT,
                 "the statistics count the frames, refusals and heartbeat");
    memset(&statistics, 0, sizeof(statistics));
    statistics.struct_size = (uint32_t)sizeof(gf_content_statistics);
    gf_kit_status(kit, (*kit).content.sink_statistics(sink, &statistics), GF_STATUS_OK,
                  "read the statistics through the sink");
    gf_kit_check(kit, statistics.frames_published == 1u, "the sink reads the same statistics");
    statistics.struct_size = 8u;
    gf_kit_status(kit, (*kit).content.statistics(surface, &statistics), GF_STATUS_INVALID_ARGUMENT,
                  "a short statistics record is refused");

    gf_content_request request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(gf_content_request);
    gf_kit_status(kit, (*kit).content.request(surface, &request), GF_STATUS_OK, "read the surface's request");
    gf_kit_check(kit, request.scale > 0.0, "a request has a display scale");
    gf_kit_status(kit, (*kit).content.sink_request(sink, &request), GF_STATUS_OK, "read the request through the sink");

    gf_content_semantics semantics = content_semantics("Kit content", "two by two");
    gf_content_semantics_result semantics_result = GF_CONTENT_SEMANTICS_REVOKED;
    gf_kit_status(kit, (*kit).content.sink_publish_semantics(sink, &semantics, &semantics_result), GF_STATUS_OK,
                  "publish semantic text");
    gf_kit_check(kit, semantics_result == GF_CONTENT_SEMANTICS_PUBLISHED, "valid semantic text is published");
    semantics.text = gf_kit_text("\xff");
    gf_kit_status(kit, (*kit).content.sink_publish_semantics(sink, &semantics, &semantics_result), GF_STATUS_OK,
                  "publish invalid semantic text");
    gf_kit_check(kit, semantics_result == GF_CONTENT_SEMANTICS_REFUSED_INVALID_UTF8,
                 "invalid UTF-8 from the content is a refusal");
    gf_kit_status(kit, (*kit).content.set_semantics(surface, &semantics), GF_STATUS_INVALID_ARGUMENT,
                  "invalid UTF-8 from the caller is a failed call");
    semantics = content_semantics("Kit content", "set by the program");
    gf_kit_status(kit, (*kit).content.set_semantics(surface, &semantics), GF_STATUS_OK, "set the semantic text");

    gf_kit_status(kit, (*kit).content.set_status_caption(surface, GF_CONTENT_STATUS_STALE, gf_kit_text("Paused")),
                  GF_STATUS_OK, "set the stale caption");
    gf_kit_status(kit, (*kit).content.set_status_caption(surface, (gf_content_status)99u, gf_kit_text("x")),
                  GF_STATUS_INVALID_ARGUMENT, "an unknown status has no caption");
    gf_kit_status(kit, (*kit).content.set_stale_interval(surface, 0u), GF_STATUS_OK, "turn the watchdog off");
    gf_kit_status(kit, (*kit).content.set_stale_interval(surface, GF_CONTENT_MAXIMUM_WAIT_MS + 1u),
                  GF_STATUS_INVALID_ARGUMENT, "an interval over the maximum wait is refused");
    gf_kit_status(kit, (*kit).content.set_termination_grace(surface, 50u), GF_STATUS_OK,
                  "set the termination grace");

    uint32_t running = 1u;
    gf_kit_status(kit, (*kit).content.program_running(surface, &running), GF_STATUS_OK, "ask for a program");
    gf_kit_check(kit, running == 0u, "the caller's own channel runs no program");

    /* Termination of the caller's own threads ends the channel at once. */
    gf_kit_status(kit, (*kit).content.terminate(surface), GF_STATUS_OK, "terminate the content");
    gf_kit_status(kit, (*kit).content.status(surface, &status), GF_STATUS_OK, "read the status after terminate");
    gf_kit_check(kit, status == GF_CONTENT_STATUS_ENDED, "terminated content has ended");
    uint32_t revoked = 0u;
    uint32_t stop_requested = 0u;
    gf_kit_status(kit, (*kit).content.sink_state(sink, &revoked, &stop_requested), GF_STATUS_OK,
                  "read the sink's state");
    gf_kit_check(kit, revoked == 1u && stop_requested == 1u, "an ended channel is revoked and asked to stop");
    frame = content_frame();
    gf_kit_status(kit, (*kit).content.sink_publish(sink, &frame, &result), GF_STATUS_OK,
                  "publish after the end");
    gf_kit_check(kit, result == GF_CONTENT_PUBLISH_REVOKED, "a revoked channel shows nothing");

    char text[64];
    uint64_t required = 99u;
    gf_kit_status(kit, (*kit).content.failure(surface, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the failure of content that ended");
    gf_kit_check(kit, required == 0u, "content that ended has no failure");

    gf_kit_status(kit, (*kit).content.sink_release(sink), GF_STATUS_OK, "release the sink");
    gf_kit_status(kit, (*kit).content.sink_release(sink), GF_STATUS_STALE_HANDLE, "a released sink is stale");
    gf_kit_status(kit, (*kit).content.sink_heartbeat(sink), GF_STATUS_STALE_HANDLE,
                  "a released sink takes no calls");
    gf_kit_status(kit, (*kit).content.close_content(surface), GF_STATUS_OK, "close the content");
    gf_kit_status(kit, (*kit).content.status(surface, &status), GF_STATUS_OK, "read the status after close");
    gf_kit_check(kit, status == GF_CONTENT_STATUS_DISCONNECTED, "closed content is disconnected");
}

/* A program on the library's thread: its sink, its frame and its failure. */
static void content_program(gf_kit* kit, gf_object surface) {
    gf_content_sink outside = 0u;
    gf_kit_status(kit, (*kit).content.program_sink(&outside), GF_STATUS_INVALID_STATE,
                  "program_sink is for a running program only");
    gf_kit_status(kit, (*kit).content.program_fail(gf_kit_text("no")), GF_STATUS_INVALID_STATE,
                  "program_fail is for a running program only");

    content_program_state state;
    memset(&state, 0, sizeof(state));
    state.kit = kit;
    pthread_mutex_init(&state.lock, NULL);
    pthread_cond_init(&state.changed, NULL);
    gf_content_program program;
    memset(&program, 0, sizeof(program));
    program.struct_size = (uint32_t)sizeof(gf_content_program);
    program.opaque = 2u;
    program.run = gf_kit_callback(&content_program_run, &content_program_release, &state);
    gf_kit_status(kit, (*kit).content.start_program(surface, &program), GF_STATUS_INVALID_ARGUMENT,
                  "a program's opaque is 0 or 1");
    gf_kit_check(kit, state.released == 0u, "a refused program keeps its callback");
    program.opaque = 1u;
    gf_kit_status(kit, (*kit).content.start_program(surface, &program), GF_STATUS_OK, "start a content program");
    const int released = content_wait_for_release(&state);
    gf_kit_check(kit, released, "the library releases the program's callback after run");
    if (released) {
        gf_kit_check(kit, state.ran == 1u, "the program ran");
        gf_kit_check(kit, state.sink_status == GF_STATUS_OK, "a running program has a sink");
        gf_kit_check(kit, state.publish_status == GF_STATUS_OK && state.publish_result == GF_CONTENT_PUBLISH_PUBLISHED,
                     "the program publishes from its own thread");
        gf_kit_check(kit, state.semantics_status == GF_STATUS_OK &&
                              state.semantics_result == GF_CONTENT_SEMANTICS_PUBLISHED,
                     "the program publishes its semantic text");
        gf_kit_check(kit, state.wait_status == GF_STATUS_OK, "a zero-timeout wait returns");
        gf_kit_check(kit, state.state_status == GF_STATUS_OK && state.revoked == 0u,
                     "the program's channel is live while it runs");
        gf_kit_check(kit, state.statistics_status == GF_STATUS_OK && state.frames_published == 1u,
                     "the program reads its statistics");
        gf_kit_check(kit, state.release_status == GF_STATUS_OK, "the program releases its sink");
        gf_kit_check(kit, state.fail_status == GF_STATUS_OK, "the program names its failure");
    }
    /* The worker marks itself done just after the release; terminate reaps
     * a done program at once and otherwise waits for its window. */
    gf_kit_status(kit, (*kit).content.terminate(surface), GF_STATUS_OK, "reap the program");
    gf_content_status status = GF_CONTENT_STATUS_DISCONNECTED;
    gf_kit_status(kit, (*kit).content.status(surface, &status), GF_STATUS_OK, "read the program's status");
    gf_kit_check(kit, status == GF_CONTENT_STATUS_FAILED || status == GF_CONTENT_STATUS_TERMINATING,
                 "a program that names a failure fails");
    if (status == GF_CONTENT_STATUS_FAILED) {
        char text[128];
        uint64_t required = 0u;
        gf_kit_status(kit, (*kit).content.failure(surface, text, sizeof(text) - 1u, &required), GF_STATUS_OK,
                      "read the program's failure");
        text[required < sizeof(text) ? required : 0u] = '\0';
        gf_kit_check(kit, strstr(text, "failed on purpose") != NULL, "the failure is the program's message");
    }
    gf_kit_status(kit, (*kit).content.close_content(surface), GF_STATUS_OK, "close the program's content");
    pthread_cond_destroy(&state.changed);
    pthread_mutex_destroy(&state.lock);
}

/* Hosted content: the refusals that start no process. */
static void content_hosted(gf_kit* kit, gf_object surface) {
    gf_hosted_support support = GF_HOSTED_SUPPORT_NOT_REPORTED;
    gf_kit_status(kit, (*kit).content.hosted_support(&support), GF_STATUS_OK, "ask for hosted content");
    gf_hosted_options options;
    memset(&options, 0, sizeof(options));
    gf_kit_status(kit, (*kit).content.start_hosted(surface, &options), GF_STATUS_INVALID_ARGUMENT,
                  "hosted options need their size");
    options.struct_size = (uint32_t)sizeof(gf_hosted_options);
    options.executable = gf_kit_text("/nonexistent/kit-hosted-program");
    if (support == GF_HOSTED_SUPPORT_AVAILABLE) {
        const gf_utf8_view argument = gf_kit_text("x");
        options.arguments = &argument;
        options.argument_count = GF_HOSTED_MAXIMUM_ARGUMENTS + 1u;
        gf_kit_status(kit, (*kit).content.start_hosted(surface, &options), GF_STATUS_LIMIT_EXCEEDED,
                      "too many arguments are refused before any is read");
    } else {
        gf_kit_status(kit, (*kit).content.start_hosted(surface, &options), GF_STATUS_UNSUPPORTED_CAPABILITY,
                      "hosted content is refused where it is not reported");
    }
    gf_hosted_input_report report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(gf_hosted_input_report);
    report.connected = 1u;
    gf_kit_status(kit, (*kit).content.hosted_input(surface, &report), GF_STATUS_OK, "read the hosted input");
    gf_kit_check(kit, report.connected == 0u && report.dropped == 0u, "a surface without hosted content forwards nothing");
}

/* The editor's text, layout handshake and the queries over the layout. */
static void content_editor(gf_kit* kit, gf_object editor, gf_object surface) {
    gf_kit_status(kit, (*kit).content.editor_set_text(editor, gf_kit_text("hello")), GF_STATUS_OK,
                  "set the editor's text");
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).content.editor_text(editor, text, sizeof(text) - 1u, &required), GF_STATUS_OK,
                  "read the editor's text");
    text[required < sizeof(text) ? required : 0u] = '\0';
    gf_kit_check(kit, strcmp(text, "hello") == 0, "the editor's text reads back");
    uint64_t revision = 0u;
    gf_kit_status(kit, (*kit).content.editor_text_revision(editor, &revision), GF_STATUS_OK, "read the revision");

    /* One line of five clusters, eight units each. */
    gf_content_text_line line;
    memset(&line, 0, sizeof(line));
    line.start = 0u;
    line.end = 5u;
    line.bounds.width = 40.0;
    line.bounds.height = 16.0;
    gf_content_text_cluster clusters[5];
    memset(clusters, 0, sizeof(clusters));
    uint32_t index = 0u;
    for (index = 0u; index < 5u; ++index) {
        clusters[index].start = index;
        clusters[index].end = index + 1u;
        clusters[index].bounds.x = 8.0 * (double)index;
        clusters[index].bounds.width = 8.0;
        clusters[index].bounds.height = 16.0;
    }
    gf_content_text_layout layout;
    memset(&layout, 0, sizeof(layout));
    layout.struct_size = (uint32_t)sizeof(gf_content_text_layout);
    layout.text_revision = revision + 1u;
    layout.lines = &line;
    layout.line_count = 1u;
    layout.clusters = clusters;
    layout.cluster_count = 5u;
    gf_content_layout_result result = GF_CONTENT_LAYOUT_ACCEPTED;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_OK,
                  "submit a layout for another revision");
    gf_kit_check(kit, result == GF_CONTENT_LAYOUT_STALE_REVISION, "a layout for another revision is stale");
    layout.text_revision = revision;
    clusters[2].start = 3u;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_OK,
                  "submit a layout with a gap");
    gf_kit_check(kit, result == GF_CONTENT_LAYOUT_REFUSED_CLUSTER, "clusters with a gap are refused");
    clusters[2].start = 2u;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_OK,
                  "submit the layout");
    gf_kit_check(kit, result == GF_CONTENT_LAYOUT_ACCEPTED, "a well-formed layout is accepted");
    uint32_t current = 0u;
    gf_kit_status(kit, (*kit).content.editor_layout_current(editor, &current), GF_STATUS_OK,
                  "ask whether the layout is current");
    gf_kit_check(kit, current == 1u, "the accepted layout is current");

    uint32_t present = 0u;
    gf_logical_rect caret;
    memset(&caret, 0, sizeof(caret));
    gf_kit_status(kit, (*kit).content.editor_caret_rectangle(editor, &present, &caret), GF_STATUS_OK,
                  "read the caret");
    gf_kit_check(kit, present == 1u && caret.height > 0.0, "a current layout places the caret");
    gf_logical_rect boxes[4];
    required = 99u;
    gf_kit_status(kit, (*kit).content.editor_selection_rectangles(editor, boxes, 4u, &required), GF_STATUS_OK,
                  "read the selection boxes");
    gf_kit_check(kit, required <= 4u, "the selection boxes fit");
    gf_kit_status(kit, (*kit).content.editor_selection_rectangles(editor, NULL, 4u, &required),
                  GF_STATUS_INVALID_ARGUMENT, "a nonzero capacity needs a buffer");
    gf_logical_point point;
    point.x = 17.0;
    point.y = 5.0;
    uint64_t offset = 99u;
    gf_kit_status(kit, (*kit).content.editor_position_at(editor, point, &offset), GF_STATUS_OK,
                  "map a point to a text position");
    gf_kit_check(kit, offset == 2u, "the nearest cluster edge is the position");

    /* Provisional additions: the composition, quads and the layout's
     * transform. No composition is open. */
    uint32_t composing = 9u;
    uint64_t marked_start = 7u;
    uint64_t marked_end = 7u;
    gf_kit_status(kit, (*kit).content.editor_composition_range(editor, &composing, &marked_start, &marked_end),
                  GF_STATUS_OK, "read the composition range");
    gf_kit_check(kit, composing == 0u && marked_start == 7u && marked_end == 7u,
                 "without a composition the range is absent and its outputs untouched");
    gf_content_quad quad;
    memset(&quad, 0, sizeof(quad));
    present = 0u;
    gf_kit_status(kit, (*kit).content.editor_caret_quad(editor, &present, &quad), GF_STATUS_OK, "read the caret quad");
    gf_kit_check(kit, present == 1u && quad.top_left.x == caret.x && quad.bottom_left.y == caret.y + caret.height,
                 "without a transform the caret quad is its rectangle's corners");
    gf_content_quad quads[4];
    required = 99u;
    gf_kit_status(kit, (*kit).content.editor_selection_quads(editor, quads, 4u, &required), GF_STATUS_OK,
                  "read the selection quads");
    gf_kit_check(kit, required <= 4u, "the selection quads fit");
    gf_kit_status(kit, (*kit).content.editor_selection_quads(editor, NULL, 4u, &required),
                  GF_STATUS_INVALID_ARGUMENT, "a nonzero capacity needs a buffer for quads");
    required = 99u;
    gf_kit_status(kit, (*kit).content.editor_composition_quads(editor, quads, 4u, &required), GF_STATUS_OK,
                  "read the composition quads");
    gf_kit_check(kit, required == 0u, "without a composition there are no composition quads");

    layout.transformed = 1u;
    layout.transform.xx = 1.0;
    layout.transform.yy = 1.0;
    layout.transform.dx = 100.0;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_OK,
                  "submit a moved layout");
    gf_kit_check(kit, result == GF_CONTENT_LAYOUT_ACCEPTED, "a transformed layout is accepted");
    gf_kit_status(kit, (*kit).content.editor_caret_quad(editor, &present, &quad), GF_STATUS_OK,
                  "read the moved caret quad");
    gf_kit_check(kit, present == 1u && quad.top_left.x == caret.x + 100.0, "the caret quad follows the transform");
    point.x = 117.0;
    gf_kit_status(kit, (*kit).content.editor_position_at(editor, point, &offset), GF_STATUS_OK,
                  "map a point through the transform");
    gf_kit_check(kit, offset == 2u, "hit testing maps the point back through the transform");
    layout.transform.xx = 0.0;
    layout.transform.yy = 0.0;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_OK,
                  "submit a flattening transform");
    gf_kit_check(kit, result == GF_CONTENT_LAYOUT_REFUSED_GEOMETRY, "a flattening transform is refused");
    layout.transformed = 2u;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_INVALID_ARGUMENT,
                  "transformed is 0 or 1");
    layout.transformed = 0u;
    gf_kit_status(kit, (*kit).content.editor_submit_layout(editor, &layout, &result), GF_STATUS_OK,
                  "submit the plain layout again");
    gf_kit_check(kit, result == GF_CONTENT_LAYOUT_ACCEPTED, "the plain layout is accepted again");

    gf_kit_status(kit, (*kit).content.editor_set_accepts_tab(editor, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "accepts_tab is 0 or 1");
    gf_kit_status(kit, (*kit).content.editor_set_accepts_tab(editor, 1u), GF_STATUS_OK, "let the editor take Tab");
    uint32_t applied = 9u;
    gf_kit_status(kit, (*kit).content.editor_delete_selection(editor, &applied), GF_STATUS_OK,
                  "delete an empty selection");
    gf_kit_check(kit, applied == 0u, "an empty selection deletes nothing");
    const char not_utf8[1] = {(char)0xFF};
    gf_utf8_view broken;
    broken.data = not_utf8;
    broken.size = 1u;
    gf_kit_status(kit, (*kit).content.editor_replace_selection(editor, broken, &applied), GF_STATUS_INVALID_ARGUMENT,
                  "a replacement that is not UTF-8 is refused");
    gf_kit_status(kit, (*kit).content.editor_replace_selection(editor, gf_kit_text("!"), &applied), GF_STATUS_OK,
                  "insert at the caret");
    gf_kit_status(kit, (*kit).content.editor_text(editor, text, sizeof(text) - 1u, &required), GF_STATUS_OK,
                  "read the edited text");
    text[required < sizeof(text) ? required : 0u] = '\0';
    gf_kit_check(kit, applied == 1u && strcmp(text, "hello!") == 0, "the replacement is inserted at the caret");

    gf_kit_status(kit, (*kit).content.editor_set_draws_selection(editor, 1u), GF_STATUS_OK,
                  "let the editor draw the selection");
    gf_kit_status(kit, (*kit).content.editor_set_multiline(editor, 1u), GF_STATUS_OK, "make the editor multiline");
    gf_kit_status(kit, (*kit).content.editor_set_read_only(editor, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "read_only is 0 or 1");
    gf_kit_status(kit, (*kit).content.editor_set_read_only(editor, 0u), GF_STATUS_OK, "keep the editor editable");

    gf_content_sink sink = 0u;
    gf_kit_status(kit, (*kit).content.editor_open_frames(editor, 0u, &sink), GF_STATUS_OK,
                  "open the editor's frames");
    const gf_content_frame frame = content_frame();
    gf_content_publish_result published = GF_CONTENT_PUBLISH_REVOKED;
    gf_kit_status(kit, (*kit).content.sink_publish(sink, &frame, &published), GF_STATUS_OK,
                  "publish the editor's frame");
    gf_kit_check(kit, published == GF_CONTENT_PUBLISH_PUBLISHED, "the editor shows the consumer's frame");
    gf_kit_status(kit, (*kit).content.sink_release(sink), GF_STATUS_OK, "release the editor's sink");

    /* Each family's slots refuse the other type. */
    gf_kit_status(kit, (*kit).content.editor_layout_current(surface, &current), GF_STATUS_WRONG_HANDLE_KIND,
                  "a content surface is not an editor");
    gf_kit_status(kit, (*kit).content.open_content(editor, 0u, &sink), GF_STATUS_WRONG_HANDLE_KIND,
                  "an editor is not a content surface");
}

void gf_kit_content(gf_kit* kit) {
    const gf_object surface = gf_kit_create(kit, "content_surface", "kit.content.surface");
    const gf_object editor = gf_kit_create(kit, "content_text_editor", "kit.content.editor");
    if (surface == 0u || editor == 0u) {
        return;
    }
    content_channel(kit, surface);
    content_program(kit, surface);
    content_hosted(kit, surface);
    content_editor(kit, editor, surface);
    gf_kit_status(kit, (*kit).objects.release(editor), GF_STATUS_OK, "release the editor");
    gf_kit_status(kit, (*kit).objects.release(surface), GF_STATUS_OK, "release the content surface");
}
