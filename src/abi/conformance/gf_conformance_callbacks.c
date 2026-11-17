/* Kit part: subscriptions, emission order, disconnect and release. */

#include "gf_conformance_internal.h"

#include <stdio.h>
#include <string.h>

/* A callback that, on its first call, subscribes a late recorder to the same
 * source, or disconnects a subscription (another one or its own). */
typedef struct reentrant_context {
    gf_kit* kit;
    gf_object source;
    gf_kit_recorder* late;
    gf_subscription late_subscription;
    gf_subscription disconnect;
    uint32_t invoked;
    uint32_t released;
    /* this callback's releases seen inside its own invoke */
    uint32_t released_inside;
    gf_status status;
} reentrant_context;

static void GF_CALL reentrant_invoke(void* context, gf_event* event) {
    reentrant_context* state = (reentrant_context*)context;
    (void)event;
    ++(*state).invoked;
    if ((*state).invoked != 1u) {
        return;
    }
    if ((*state).late != NULL) {
        const gf_callback late = gf_kit_recording_callback((*state).late);
        (*state).status = (*(*state).kit).events.subscribe((*state).source, gf_kit_text("text_changed"), &late,
                                                           &(*state).late_subscription);
    }
    if ((*state).disconnect != 0u) {
        (*state).status = (*(*state).kit).events.disconnect((*state).disconnect);
        (*state).released_inside = (*state).released;
    }
}

static void GF_CALL reentrant_release(void* context) {
    reentrant_context* state = (reentrant_context*)context;
    ++(*state).released;
}

static gf_kit_recorder new_recorder(gf_kit* kit) {
    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    return recorder;
}

static void payloads(gf_kit* kit, gf_object button) {
    gf_kit_recorder text = new_recorder(kit);
    gf_kit_recorder property = new_recorder(kit);
    gf_kit_recorder enabled = new_recorder(kit);
    const gf_callback text_callback = gf_kit_recording_callback(&text);
    const gf_callback property_callback = gf_kit_recording_callback(&property);
    const gf_callback enabled_callback = gf_kit_recording_callback(&enabled);
    gf_subscription text_subscription = 0u;
    gf_subscription property_subscription = 0u;
    gf_subscription enabled_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(button, gf_kit_text("text_changed"), &text_callback, &text_subscription),
                  GF_STATUS_OK, "subscribe to an interaction");
    gf_kit_status(kit, (*kit).events.subscribe_property(button, gf_kit_text("text"), &property_callback,
                                                        &property_subscription),
                  GF_STATUS_OK, "subscribe to a property");
    gf_kit_status(kit, (*kit).events.subscribe(button, gf_kit_text("enabled_changed"), &enabled_callback,
                                               &enabled_subscription),
                  GF_STATUS_OK, "subscribe to a boolean interaction");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "Changed"), GF_STATUS_OK, "change the text");
    gf_kit_check(kit, text.invoked == 1u && text.last_sender == button &&
                          strcmp(text.last_name, "text_changed") == 0 &&
                          text.last_payload_kind == GF_VALUE_KIND_TEXT && strcmp(text.last_text, "Changed") == 0,
                 "an interaction carries its sender, name and payload");
    gf_kit_check(kit, property.invoked == 1u && strcmp(property.last_name, "text") == 0 &&
                          property.last_payload_kind == GF_VALUE_KIND_NULL,
                 "a property change carries the property's name and no payload");
    gf_value disabled;
    memset(&disabled, 0, sizeof(disabled));
    disabled.struct_size = (uint32_t)sizeof(disabled);
    disabled.kind = GF_VALUE_KIND_BOOLEAN;
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("enabled"), &disabled), GF_STATUS_OK, "disable");
    gf_kit_check(kit, enabled.invoked == 1u && enabled.last_payload_kind == GF_VALUE_KIND_BOOLEAN,
                 "a boolean interaction carries a boolean");
    gf_kit_status(kit, (*kit).events.disconnect(text_subscription), GF_STATUS_OK, "disconnect");
    gf_kit_check(kit, text.released == 1u, "disconnect releases before it returns");
    gf_kit_status(kit, (*kit).events.disconnect(text_subscription), GF_STATUS_STALE_HANDLE,
                  "a second disconnect is stale");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "Again"), GF_STATUS_OK, "change the text again");
    gf_kit_check(kit, text.invoked == 1u, "a disconnected subscription is not called");
    gf_kit_status(kit, (*kit).events.disconnect(property_subscription), GF_STATUS_OK, "disconnect the property");
    gf_kit_status(kit, (*kit).events.disconnect(enabled_subscription), GF_STATUS_OK, "disconnect the boolean");
    gf_kit_check(kit, text.off_thread_calls == 0u && property.off_thread_calls == 0u,
                 "callbacks run on the UI thread");
}

static void emission_boundaries(gf_kit* kit, gf_object button) {
    /* A subscription made during an emission waits for the next one. */
    gf_kit_recorder late = new_recorder(kit);
    reentrant_context adder;
    memset(&adder, 0, sizeof(adder));
    adder.kit = kit;
    adder.source = button;
    adder.late = &late;
    const gf_callback adder_callback = gf_kit_callback(&reentrant_invoke, &reentrant_release, &adder);
    gf_subscription adder_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(button, gf_kit_text("text_changed"), &adder_callback,
                                               &adder_subscription),
                  GF_STATUS_OK, "subscribe a callback that subscribes");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "one"), GF_STATUS_OK, "first emission");
    gf_kit_check(kit, adder.status == GF_STATUS_OK && late.invoked == 0u,
                 "a subscription made during an emission is not called by it");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "two"), GF_STATUS_OK, "second emission");
    gf_kit_check(kit, late.invoked == 1u, "it is called by the next emission");
    gf_kit_status(kit, (*kit).events.disconnect(adder.late_subscription), GF_STATUS_OK, "disconnect the late one");
    gf_kit_status(kit, (*kit).events.disconnect(adder_subscription), GF_STATUS_OK, "disconnect the adder");
    gf_kit_check(kit, adder.released == 1u && late.released == 1u, "each releases once");

    /* One ended during an emission is not called later in it. */
    reentrant_context first;
    memset(&first, 0, sizeof(first));
    first.kit = kit;
    gf_kit_recorder victim = new_recorder(kit);
    const gf_callback first_callback = gf_kit_callback(&reentrant_invoke, &reentrant_release, &first);
    const gf_callback victim_callback = gf_kit_recording_callback(&victim);
    gf_subscription first_subscription = 0u;
    gf_subscription victim_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(button, gf_kit_text("text_changed"), &first_callback,
                                               &first_subscription),
                  GF_STATUS_OK, "subscribe a callback that disconnects");
    gf_kit_status(kit, (*kit).events.subscribe(button, gf_kit_text("text_changed"), &victim_callback,
                                               &victim_subscription),
                  GF_STATUS_OK, "subscribe the one it disconnects");
    first.disconnect = victim_subscription;
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "three"), GF_STATUS_OK, "emission that disconnects");
    gf_kit_check(kit, first.status == GF_STATUS_OK && victim.invoked == 0u,
                 "a subscription ended earlier in an emission is skipped");
    gf_kit_check(kit, victim.released == 1u, "and released");

    /* Disconnecting itself: the release follows the invoke. */
    reentrant_context self;
    memset(&self, 0, sizeof(self));
    self.kit = kit;
    const gf_callback self_callback = gf_kit_callback(&reentrant_invoke, &reentrant_release, &self);
    gf_subscription self_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(button, gf_kit_text("text_changed"), &self_callback,
                                               &self_subscription),
                  GF_STATUS_OK, "subscribe a callback that disconnects itself");
    self.disconnect = self_subscription;
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "four"), GF_STATUS_OK, "emission that self-disconnects");
    gf_kit_check(kit, self.status == GF_STATUS_OK && self.invoked == 1u && self.released_inside == 0u &&
                          self.released == 1u,
                 "a self-disconnect releases once, after its invoke");
    gf_kit_status(kit, (*kit).events.disconnect(first_subscription), GF_STATUS_OK, "disconnect the disconnecter");
}

/* Releases its own reference to the sender from inside the invoke. */
static void GF_CALL release_sender_invoke(void* context, gf_event* event) {
    gf_kit* kit = (gf_kit*)context;
    (void)(*kit).objects.release((*event).sender);
}

static void endings(gf_kit* kit) {
    /* The source ending ends the subscription. */
    const gf_object label = gf_kit_create(kit, "Label", "kit.ending");
    gf_kit_recorder recorder = new_recorder(kit);
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(label, gf_kit_text("text_changed"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to a detached label");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
    gf_kit_check(kit, recorder.released == 1u, "the source's end releases the callback");
    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_STALE_HANDLE,
                  "the ended subscription is stale");

    /* Disposal ends the subscription before dispose returns. */
    const gf_object box = gf_kit_create(kit, "TextBox", "kit.disposal");
    gf_kit_recorder disposed = new_recorder(kit);
    const gf_callback disposed_callback = gf_kit_recording_callback(&disposed);
    gf_kit_status(kit, (*kit).events.subscribe(box, gf_kit_text("text_changed"), &disposed_callback, &subscription),
                  GF_STATUS_OK, "subscribe to a text box");
    gf_kit_status(kit, (*kit).objects.dispose(box), GF_STATUS_OK, "dispose the text box");
    gf_kit_check(kit, disposed.released == 1u, "disposal releases the callback");
    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_STALE_HANDLE,
                  "the disposed source's subscription is stale");
    gf_kit_status(kit, (*kit).objects.release(box), GF_STATUS_OK, "release the text box");

    /* Disposing a parent ends the subscriptions of its subtree, and only
     * those: a control outside it keeps its own (N8). */
    const gf_object holder = gf_kit_create(kit, "Panel", "kit.disposal.holder");
    const gf_object inner = gf_kit_create(kit, "TextBox", "kit.disposal.inner");
    const gf_object outside = gf_kit_create(kit, "TextBox", "kit.disposal.outside");
    gf_kit_status(kit, (*kit).objects.insert_child(holder, inner, 0u), GF_STATUS_OK, "put the inner box in the holder");
    gf_kit_recorder inner_recorder = new_recorder(kit);
    gf_kit_recorder outside_recorder = new_recorder(kit);
    const gf_callback inner_callback = gf_kit_recording_callback(&inner_recorder);
    const gf_callback outside_callback = gf_kit_recording_callback(&outside_recorder);
    gf_subscription inner_subscription = 0u;
    gf_subscription outside_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(inner, gf_kit_text("text_changed"), &inner_callback,
                                               &inner_subscription),
                  GF_STATUS_OK, "subscribe to the inner box");
    gf_kit_status(kit, (*kit).events.subscribe(outside, gf_kit_text("text_changed"), &outside_callback,
                                               &outside_subscription),
                  GF_STATUS_OK, "subscribe to the outside box");
    gf_kit_status(kit, (*kit).objects.dispose(holder), GF_STATUS_OK, "dispose the holder");
    gf_kit_check(kit, inner_recorder.released == 1u, "disposing the parent releases the child's callback");
    gf_kit_check(kit, outside_recorder.released == 0u, "a control outside the subtree keeps its callback");
    gf_kit_status(kit, (*kit).events.disconnect(inner_subscription), GF_STATUS_STALE_HANDLE,
                  "the disposed child's subscription is stale");
    gf_kit_status(kit, (*kit).events.disconnect(outside_subscription), GF_STATUS_OK,
                  "the outside subscription is still live");
    gf_kit_check(kit, outside_recorder.released == 1u, "disconnect releases the outside callback");
    gf_kit_status(kit, (*kit).objects.release(inner), GF_STATUS_OK, "release the inner box");
    gf_kit_status(kit, (*kit).objects.release(holder), GF_STATUS_OK, "release the holder");
    gf_kit_status(kit, (*kit).objects.release(outside), GF_STATUS_OK, "release the outside box");

    /* A callback may end its own sender. */
    const gf_object check = gf_kit_create(kit, "CheckBox", "kit.reentrant");
    gf_kit_status(kit, (*kit).objects.retain(check), GF_STATUS_OK, "a reference for the callback");
    const gf_callback ender = gf_kit_callback(&release_sender_invoke, NULL, kit);
    gf_kit_status(kit, (*kit).events.subscribe(check, gf_kit_text("checked_changed"), &ender, &subscription),
                  GF_STATUS_OK, "subscribe a callback that releases its sender");
    gf_value checked;
    memset(&checked, 0, sizeof(checked));
    checked.struct_size = (uint32_t)sizeof(checked);
    checked.kind = GF_VALUE_KIND_BOOLEAN;
    checked.boolean = 1u;
    gf_kit_status(kit, (*kit).properties.set(check, gf_kit_text("checked"), &checked), GF_STATUS_OK, "check it");
    gf_kit_check(kit, gf_kit_state(kit, check) == GF_OBJECT_STATE_ALIVE, "the caller's reference remains");
    gf_kit_status(kit, (*kit).objects.release(check), GF_STATUS_OK, "release the check box");
}

static void refusals(gf_kit* kit) {
    const gf_object label = gf_kit_create(kit, "Label", "kit.refusals");
    gf_kit_recorder recorder = new_recorder(kit);
    gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_subscription subscription = 55u;
    gf_kit_status(kit, (*kit).events.subscribe(label, gf_kit_text("no_such_interaction"), &callback, &subscription),
                  GF_STATUS_NOT_FOUND, "an unknown interaction is refused");
    gf_kit_status(kit, (*kit).events.subscribe(label, gf_kit_text("clicked"), &callback, &subscription),
                  GF_STATUS_NOT_FOUND, "an interaction the type does not offer is refused");
    gf_kit_status(kit, (*kit).events.subscribe_property(label, gf_kit_text("no_such_property"), &callback,
                                                        &subscription),
                  GF_STATUS_NOT_FOUND, "an unknown property is refused");
    callback.reserved = 1u;
    gf_kit_status(kit, (*kit).events.subscribe(label, gf_kit_text("text_changed"), &callback, &subscription),
                  GF_STATUS_INVALID_ARGUMENT, "a callback with reserved set is refused");
    callback.reserved = 0u;
    callback.invoke = NULL;
    gf_kit_status(kit, (*kit).events.subscribe(label, gf_kit_text("text_changed"), &callback, &subscription),
                  GF_STATUS_INVALID_ARGUMENT, "a callback without invoke is refused");
    gf_kit_check(kit, subscription == 55u && recorder.released == 0u,
                 "a refused subscribe neither writes nor takes the callback");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release");
    gf_kit_check(kit, recorder.released == 0u, "a refused callback is never released");
}

/* One rule for every slot that takes a callback (N20, N34, N71): a call that
 * fails has not taken it, never calls it and never calls its release, also
 * after the posted work has run. Counted per failing slot, so a binding
 * that frees its handle on failure never sees a second free. */
static void expect_untaken(gf_kit* kit, const gf_kit_recorder* recorder, gf_status status, const char* slot) {
    char name[160];
    uint64_t invoked = 0u;
    (void)(*kit).dispatch.run_pending(&invoked);
    snprintf(name, sizeof(name), "%s fails", slot);
    gf_kit_check(kit, status != GF_STATUS_OK, name);
    snprintf(name, sizeof(name), "a failing %s neither calls nor releases its callback", slot);
    gf_kit_check(kit, (*recorder).invoked == 0u && (*recorder).released == 0u, name);
}

static void failing_slot_releases(gf_kit* kit) {
    const gf_object label = gf_kit_create(kit, "Label", "kit.callbacks.untaken");

    gf_kit_recorder posted = new_recorder(kit);
    const gf_callback posted_callback = gf_kit_recording_callback(&posted);
    expect_untaken(kit, &posted, (*kit).dispatch.begin_invoke(0u, &posted_callback), "dispatch.begin_invoke(0)");
    const gf_object ended = gf_kit_create(kit, "Label", "kit.callbacks.untaken.ended");
    gf_kit_status(kit, (*kit).objects.release(ended), GF_STATUS_OK, "end the stale target");
    expect_untaken(kit, &posted, (*kit).dispatch.begin_invoke(ended, &posted_callback),
                   "dispatch.begin_invoke(stale)");

    gf_kit_recorder content = new_recorder(kit);
    gf_subsurface_content made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.build = gf_kit_recording_callback(&content);
    expect_untaken(kit, &content, (*kit).subsurfaces.set_content(label, &made),
                   "subsurfaces.set_content(not a host)");
    expect_untaken(kit, &content, (*kit).subsurfaces.set_content(0u, &made), "subsurfaces.set_content(0)");

    gf_kit_recorder subscribed = new_recorder(kit);
    const gf_callback subscribed_callback = gf_kit_recording_callback(&subscribed);
    gf_subscription subscription = 0u;
    expect_untaken(kit, &subscribed,
                   (*kit).events.subscribe(label, gf_kit_text("clicked"), &subscribed_callback, &subscription),
                   "events.subscribe(unknown interaction)");

    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the untaken label");
    uint64_t invoked = 0u;
    (void)(*kit).dispatch.run_pending(&invoked);
    gf_kit_check(kit, posted.released == 0u && content.released == 0u && subscribed.released == 0u,
                 "no failing slot releases its callback later");
}

void gf_kit_callbacks(gf_kit* kit) {
    const gf_object button = gf_kit_create(kit, "Button", "kit.callbacks");
    payloads(kit, button);
    emission_boundaries(kit, button);
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release the button");
    endings(kit);
    refusals(kit);
    failing_slot_releases(kit);
}
