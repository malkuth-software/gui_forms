/* Kit part: identities, references, the tree, disposal and failures. */

#include "gf_conformance_internal.h"

#include <string.h>

/* Whether the last failure on this thread names table and slot. */
static int last_failure_is(gf_kit* kit, gf_status status, gf_table_id table_id, uint32_t slot_index) {
    gf_error_detail detail;
    char message[512];
    uint64_t required = 0u;
    memset(&detail, 0, sizeof(detail));
    detail.struct_size = (uint32_t)sizeof(detail);
    if ((*kit).runtime.error_detail(&detail, message, sizeof(message), &required) != GF_STATUS_OK) {
        return 0;
    }
    /* A failure has a message; no failure has none. */
    const int message_matches = status == GF_STATUS_OK ? required == 0u : required != 0u;
    const int matches = detail.status == status && detail.table_id == table_id && detail.slot_index == slot_index &&
                        message_matches;
    return matches;
}

/* Copies an object's type id or stable id and compares it with expected. */
static int text_is(gf_kit* kit, gf_object object, int stable_id, const char* expected) {
    char buffer[64];
    uint64_t required = 0u;
    gf_status copied = GF_STATUS_OK;
    if (stable_id) {
        copied = (*kit).objects.stable_id(object, buffer, sizeof(buffer), &required);
    } else {
        copied = (*kit).objects.type_id(object, buffer, sizeof(buffer), &required);
    }
    if (copied != GF_STATUS_OK || required != (uint64_t)strlen(expected)) {
        return 0;
    }
    const int equal = memcmp(buffer, expected, (size_t)required) == 0;
    return equal;
}

static void identities(gf_kit* kit) {
    gf_abi_version_info version;
    memset(&version, 0, sizeof(version));
    version.struct_size = (uint32_t)sizeof(version);
    gf_kit_status(kit, (*kit).runtime.version(&version), GF_STATUS_OK, "version");
    gf_kit_check(kit, version.abi_major == GF_ABI_MAJOR && version.abi_minor >= GF_ABI_MINOR,
                 "the library implements the header's ABI");

    const gf_object panel = gf_kit_create(kit, "Panel", "kit.panel");
    gf_kit_check(kit, panel != 0u, "create gives an identity");
    gf_kit_check(kit, gf_kit_state(kit, panel) == GF_OBJECT_STATE_ALIVE, "a new object is alive");
    gf_kit_check(kit, text_is(kit, panel, 0, "panel"), "type_id reports the schema type");
    gf_kit_check(kit, text_is(kit, panel, 1, "kit.panel"), "stable_id reports the stable id");

    char small[2];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).objects.stable_id(panel, small, sizeof(small), &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "a short buffer is refused");
    gf_kit_check(kit, required == 9u, "a short buffer still reports the size");

    gf_object untouched = 77u;
    gf_kit_status(kit, (*kit).objects.create(gf_kit_text("NoSuchType"), gf_kit_text("kit.none"), &untouched),
                  GF_STATUS_NOT_FOUND, "an unknown type is refused");
    gf_kit_check(kit, untouched == 77u, "a refused create leaves its output");
    gf_kit_check(kit, last_failure_is(kit, GF_STATUS_NOT_FOUND, GF_TABLE_OBJECTS, 0u),
                 "the error detail names objects.create");
    gf_kit_status(kit, (*kit).runtime.clear_error(), GF_STATUS_OK, "clear_error");
    gf_kit_check(kit, last_failure_is(kit, GF_STATUS_OK, 0u, 0u), "clear_error forgets the failure");
    gf_kit_status(kit, (*kit).objects.create(gf_kit_text("Panel"), gf_kit_text(""), &untouched),
                  GF_STATUS_INVALID_ARGUMENT, "an empty stable id is refused");
    gf_kit_status(kit, (*kit).objects.retain(0u), GF_STATUS_INVALID_ARGUMENT, "zero is no object");

    gf_kit_status(kit, (*kit).objects.retain(panel), GF_STATUS_OK, "retain");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release one of two references");
    gf_kit_check(kit, gf_kit_state(kit, panel) == GF_OBJECT_STATE_ALIVE, "one reference keeps the object");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the last reference");
    gf_object_state state = 99u;
    gf_kit_status(kit, (*kit).objects.state(panel, &state), GF_STATUS_STALE_HANDLE,
                  "an ended identity is stale");
    gf_kit_check(kit, state == 99u, "a stale call leaves its output");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_STALE_HANDLE, "a second release is stale");
}

static void tree(gf_kit* kit) {
    const gf_object panel = gf_kit_create(kit, "Panel", "kit.tree");
    const gf_object first = gf_kit_create(kit, "Button", "kit.tree.first");
    const gf_object second = gf_kit_create(kit, "Label", "kit.tree.second");
    gf_kit_status(kit, (*kit).objects.insert_child(panel, first, 0u), GF_STATUS_OK, "insert a child");
    gf_kit_status(kit, (*kit).objects.insert_child(panel, second, 0u), GF_STATUS_OK, "insert before it");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).objects.child_count(panel, &count), GF_STATUS_OK, "child_count");
    gf_kit_check(kit, count == 2u, "two children");
    gf_object at = 0u;
    gf_kit_status(kit, (*kit).objects.child_at(panel, 0u, &at), GF_STATUS_OK, "child_at");
    gf_kit_check(kit, at == second, "child_at follows the insertion index and reuses the identity");
    gf_kit_status(kit, (*kit).objects.release(at), GF_STATUS_OK, "release child_at's reference");
    gf_object parent = 0u;
    gf_kit_status(kit, (*kit).objects.parent(first, &parent), GF_STATUS_OK, "parent");
    gf_kit_check(kit, parent == panel, "parent names the parent");
    gf_kit_status(kit, (*kit).objects.release(parent), GF_STATUS_OK, "release parent's reference");
    gf_kit_status(kit, (*kit).objects.parent(panel, &parent), GF_STATUS_OK, "parent of a root");
    gf_kit_check(kit, parent == 0u, "a root has no parent");

    gf_kit_status(kit, (*kit).objects.insert_child(panel, first, 0u), GF_STATUS_INVALID_STATE,
                  "a child with a parent is refused");
    gf_kit_status(kit, (*kit).objects.insert_child(first, panel, 0u), GF_STATUS_INVALID_ARGUMENT,
                  "an ancestor cannot become a child");
    const gf_object third = gf_kit_create(kit, "CheckBox", "kit.tree.third");
    gf_kit_status(kit, (*kit).objects.insert_child(panel, third, 3u), GF_STATUS_INVALID_ARGUMENT,
                  "an index past the count is refused");
    gf_kit_status(kit, (*kit).objects.child_at(panel, 2u, &at), GF_STATUS_INVALID_ARGUMENT,
                  "child_at past the last child is refused");

    /* The parent keeps a child whose last reference goes; its identity ends. */
    gf_kit_status(kit, (*kit).objects.release(first), GF_STATUS_OK, "release a child's only reference");
    gf_kit_check(kit, gf_kit_state(kit, first) == GF_OBJECT_STATE_DISPOSED + 1u, "the child's identity ended");
    gf_kit_status(kit, (*kit).objects.child_count(panel, &count), GF_STATUS_OK, "child_count after release");
    gf_kit_check(kit, count == 2u, "the parent kept the child");
    gf_kit_status(kit, (*kit).objects.child_at(panel, 1u, &at), GF_STATUS_OK, "child_at for the kept child");
    gf_kit_check(kit, at != 0u && at != first, "the kept child gets a new identity");

    /* A removed child lives on the caller's reference. */
    gf_kit_status(kit, (*kit).objects.remove_child(panel, at), GF_STATUS_OK, "remove_child");
    gf_kit_status(kit, (*kit).objects.child_count(panel, &count), GF_STATUS_OK, "child_count after remove");
    gf_kit_check(kit, count == 1u, "one child left");
    gf_kit_check(kit, gf_kit_state(kit, at) == GF_OBJECT_STATE_ALIVE, "the removed child is alive");
    gf_kit_status(kit, (*kit).objects.remove_child(panel, at), GF_STATUS_INVALID_ARGUMENT,
                  "removing a non-child is refused");
    gf_kit_status(kit, (*kit).objects.release(at), GF_STATUS_OK, "release the removed child");

    /* Disposal reaches the subtree and keeps the identities. */
    gf_kit_status(kit, (*kit).objects.dispose(panel), GF_STATUS_OK, "dispose");
    gf_kit_check(kit, gf_kit_state(kit, panel) == GF_OBJECT_STATE_DISPOSED, "the root is disposed");
    gf_kit_check(kit, gf_kit_state(kit, second) == GF_OBJECT_STATE_DISPOSED, "its child is disposed");
    gf_kit_status(kit, gf_kit_set_text(kit, second, "text", "late"), GF_STATUS_DISPOSED,
                  "a disposed object refuses properties");
    gf_kit_status(kit, (*kit).objects.insert_child(panel, third, 0u), GF_STATUS_DISPOSED,
                  "a disposed parent refuses children");
    gf_kit_status(kit, (*kit).objects.release(second), GF_STATUS_OK, "release a disposed child");
    gf_kit_status(kit, (*kit).objects.release(third), GF_STATUS_OK, "release a detached object");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release a disposed root");
}

void gf_kit_ownership(gf_kit* kit) {
    identities(kit);
    tree(kit);
}
