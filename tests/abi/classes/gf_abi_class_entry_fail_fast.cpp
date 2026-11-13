// A class table's entry is foreign code. A C entry cannot throw and the C#
// entries catch everything, but a C++ client's entry can: an exception
// leaving it is recorded and ends the process through the fail-fast report
// (owner's S2 ruling), naming the site "abi.class_entry", and never unwinds
// into the raiser (src/abi/stable/classes/abi_class_bridge.cpp).
//
// Modes:
//   plain           an entry that calls base and returns; prints
//                   "class entry: ok" and "survived".
//   throwing_entry  an entry that throws; must end the process with
//                   "GUI.Forms: fatal exception at abi.class_entry: injected
//                   class entry failure" and never print "survived"
//                   (cmake/expect_fail_fast.cmake).

#include "gui_forms/c/gf_abi.h"
#include "gui_forms/c/gf_abi_classes.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// A death test must not open the system's crash dialog, which would hold
// the test until someone dismisses it.
void quiet_crash_reporting() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER) || defined(_UCRT)
    _set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif
}

// The tables the program uses, loaded at the header's version.
struct Tables final {
    gf_objects_table objects{};
    gf_properties_table properties{};
    gf_classes_table classes{};
};

// The instance the entry receives: whether it throws, and what it saw.
struct EntryRecord final {
    bool throws{};
    std::uint32_t entries{};
    std::uint32_t releases{};
    gf_status base_status{GF_STATUS_OK};
};

gf_utf8_view text_view(std::string_view text) {
    gf_utf8_view view{};
    view.data = text.data();
    view.size = text.size();
    return view;
}

bool load(gf_table_id table_id, void* table, std::uint64_t size) {
    const gf_status loaded = gf_abi_get_table(table_id, GF_ABI_MAJOR, GF_ABI_MINOR, table, size);
    if (loaded != GF_STATUS_OK) {
        std::fprintf(stderr, "FAIL: table %u did not load (status %d)\n", static_cast<unsigned>(table_id),
                     static_cast<int>(loaded));
        return false;
    }
    return true;
}

// A C++ client's entry: unlike a C entry, it can throw.
void GF_CALL auto_size_entry(void* instance, gf_object self, gf_event* event, gf_base_raiser base) {
    EntryRecord& record = *static_cast<EntryRecord*>(instance);
    record.entries = record.entries + 1U;
    if (record.throws) {
        throw std::runtime_error("injected class entry failure");
    }
    record.base_status = base(self, event);
}

void GF_CALL release_record(void* instance) {
    EntryRecord& record = *static_cast<EntryRecord*>(instance);
    record.releases = record.releases + 1U;
}

gf_value boolean_value(bool value) {
    gf_value result{};
    result.struct_size = static_cast<std::uint32_t>(sizeof(result));
    result.kind = GF_VALUE_KIND_BOOLEAN;
    result.boolean = value ? 1U : 0U;
    return result;
}

// Creates a ButtonBase whose Control::on_auto_size_changed is
// auto_size_entry, writes auto_size false (a change or not, depending on the
// default), then true, which changes it and runs the entry once; `throws`
// applies from the second write. Returns the process status.
int run(bool throws) {
    Tables tables;
    if (!load(GF_TABLE_OBJECTS, &tables.objects, sizeof(tables.objects)) ||
        !load(GF_TABLE_PROPERTIES, &tables.properties, sizeof(tables.properties)) ||
        !load(GF_TABLE_CLASSES, &tables.classes, sizeof(tables.classes))) {
        return EXIT_FAILURE;
    }
    gf_control_hooks segment{};
    segment.struct_size = static_cast<std::uint32_t>(sizeof(segment));
    segment.class_id = GF_CLASS_CONTROL;
    segment.on_auto_size_changed = &auto_size_entry;
    gf_button_base_class table{};
    table.header.struct_size = static_cast<std::uint32_t>(sizeof(gf_class_header));
    table.header.abi_minor = GF_ABI_MINOR;
    table.header.class_id = GF_CLASS_BUTTON_BASE;
    table.header.segment_count = 1U;
    table.header.release = &release_record;
    table.control = &segment;
    EntryRecord record;
    gf_object object = 0U;
    const gf_status created =
        (*tables.classes.create)(&table.header, text_view("class-entry.button"), &record, &object);
    if (created != GF_STATUS_OK || object == 0U) {
        std::fprintf(stderr, "FAIL: create (status %d)\n", static_cast<int>(created));
        return EXIT_FAILURE;
    }
    const gf_value off = boolean_value(false);
    const gf_value on = boolean_value(true);
    const gf_status first = (*tables.properties.set)(object, text_view("auto_size"), &off);
    record.entries = 0U;
    record.throws = throws;
    const gf_status second = (*tables.properties.set)(object, text_view("auto_size"), &on);
    // Reached only when the entry returned.
    int status = EXIT_SUCCESS;
    if (first != GF_STATUS_OK || second != GF_STATUS_OK) {
        std::fprintf(stderr, "FAIL: the writes (status %d, %d)\n", static_cast<int>(first), static_cast<int>(second));
        status = EXIT_FAILURE;
    }
    if (record.entries != 1U || record.base_status != GF_STATUS_OK) {
        std::fprintf(stderr, "FAIL: one entry with base ok (entries %u, base %d)\n",
                     static_cast<unsigned>(record.entries), static_cast<int>(record.base_status));
        status = EXIT_FAILURE;
    }
    const gf_status released = (*tables.objects.release)(object);
    if (released != GF_STATUS_OK) {
        std::fprintf(stderr, "FAIL: release (status %d)\n", static_cast<int>(released));
        status = EXIT_FAILURE;
    }
    if (status == EXIT_SUCCESS) {
        std::printf("class entry: ok\n");
    }
    return status;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gui_forms_abi_class_entry_fail_fast plain|throwing_entry\n");
        return EXIT_FAILURE;
    }
    quiet_crash_reporting();
    const std::string_view mode = argv[1];
    int status = EXIT_FAILURE;
    if (mode == "plain") {
        status = run(false);
    } else if (mode == "throwing_entry") {
        status = run(true);
    } else {
        std::fprintf(stderr, "unknown mode\n");
        return EXIT_FAILURE;
    }
    std::printf("survived\n");
    return status;
}
