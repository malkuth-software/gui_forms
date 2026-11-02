/* The per-host capability matrix (stage C4 gate): the capability report a
 * program reads through GF_TABLE_HOST equals the row of
 * manifests/abi/host_capabilities.json for the host it runs on, by
 * platform name, protocol revision and capability set. Each host checks its
 * own row; CI runs this on Windows, macOS and Linux.
 *
 * Usage: gf_abi_capability_matrix <host_capabilities.json>
 *
 * The program consumes gf_abi.h and the C standard library only. The
 * matrix is read by a small scanner that knows this file's shape: a
 * "protocol_version" number and, per host, a "platform" string followed
 * by its "capabilities" array of strings. */

#include "gui_forms/c/gf_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One capability by its matrix name. */
typedef struct capability_name {
    const char* name;
    gf_host_capability bit;
} capability_name;

static const capability_name capability_names[] = {
    {"lifecycle", GF_HOST_CAPABILITY_LIFECYCLE},
    {"scale_notifications", GF_HOST_CAPABILITY_SCALE_NOTIFICATIONS},
    {"monitor_geometry", GF_HOST_CAPABILITY_MONITOR_GEOMETRY},
    {"occlusion", GF_HOST_CAPABILITY_OCCLUSION},
    {"scheduled_wake", GF_HOST_CAPABILITY_SCHEDULED_WAKE},
    {"pointer_input", GF_HOST_CAPABILITY_POINTER_INPUT},
    {"keyboard_input", GF_HOST_CAPABILITY_KEYBOARD_INPUT},
    {"text_composition", GF_HOST_CAPABILITY_TEXT_COMPOSITION},
    {"pointer_capture", GF_HOST_CAPABILITY_POINTER_CAPTURE},
    {"cursor", GF_HOST_CAPABILITY_CURSOR},
    {"clipboard", GF_HOST_CAPABILITY_CLIPBOARD},
    {"typed_drag_destination", GF_HOST_CAPABILITY_TYPED_DRAG_DESTINATION},
    {"dialogs", GF_HOST_CAPABILITY_DIALOGS},
    {"menus", GF_HOST_CAPABILITY_MENUS},
    {"font_discovery", GF_HOST_CAPABILITY_FONT_DISCOVERY},
    {"accessibility", GF_HOST_CAPABILITY_ACCESSIBILITY},
    {"typed_drag_source", GF_HOST_CAPABILITY_TYPED_DRAG_SOURCE},
    {"sound_cues", GF_HOST_CAPABILITY_SOUND_CUES},
    {"clipboard_images", GF_HOST_CAPABILITY_CLIPBOARD_IMAGES},
    {"frame_capture", GF_HOST_CAPABILITY_FRAME_CAPTURE},
    {"notification_area", GF_HOST_CAPABILITY_NOTIFICATION_AREA},
    {"notification_area_messages", GF_HOST_CAPABILITY_NOTIFICATION_AREA_MESSAGES},
    {"notification_area_menus", GF_HOST_CAPABILITY_NOTIFICATION_AREA_MENUS},
    {"runtime_windows", GF_HOST_CAPABILITY_RUNTIME_WINDOWS},
    {"window_frames", GF_HOST_CAPABILITY_WINDOW_FRAMES},
    {"screen_pointer", GF_HOST_CAPABILITY_SCREEN_POINTER},
    {"window_at_point", GF_HOST_CAPABILITY_WINDOW_AT_POINT},
    {"window_ordering", GF_HOST_CAPABILITY_WINDOW_ORDERING},
    {"transparent_windows", GF_HOST_CAPABILITY_TRANSPARENT_WINDOWS},
    {"window_backdrops", GF_HOST_CAPABILITY_WINDOW_BACKDROPS},
    {"compositor_notifications", GF_HOST_CAPABILITY_COMPOSITOR_NOTIFICATIONS},
    {"presentation_preferences", GF_HOST_CAPABILITY_PRESENTATION_PREFERENCES},
};

enum { capability_name_count = (int)(sizeof(capability_names) / sizeof(capability_names[0])) };

/* The matrix file, whole and terminated. The caller frees it. */
static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    char* text = NULL;
    long size = 0;
    if (fseek(file, 0, SEEK_END) == 0) {
        size = ftell(file);
    }
    if (size > 0 && size < 1048576 && fseek(file, 0, SEEK_SET) == 0) {
        text = (char*)malloc((size_t)size + 1u);
        if (text != NULL) {
            const size_t read = fread(text, 1u, (size_t)size, file);
            text[read] = '\0';
        }
    }
    fclose(file);
    return text;
}

/* The bit of a capability named by length bytes at name; 0 when unknown. */
static gf_host_capability bit_named(const char* name, size_t length) {
    for (int index = 0; index < capability_name_count; ++index) {
        const char* known = capability_names[index].name;
        if (strlen(known) == length && memcmp(known, name, length) == 0) {
            return capability_names[index].bit;
        }
    }
    return 0u;
}

/* Reads the capabilities array after the row of platform into set. Returns
 * 0 when the row or its array is missing, or a name is unknown. */
static int read_row(const char* text, const char* platform, gf_host_capability* set) {
    char key[160];
    snprintf(key, sizeof(key), "\"platform\": \"%s\"", platform);
    const char* row = strstr(text, key);
    if (row == NULL) {
        fprintf(stderr, "the matrix has no row for %s\n", platform);
        return 0;
    }
    const char* array = strstr(row, "\"capabilities\"");
    const char* open = array != NULL ? strchr(array, '[') : NULL;
    const char* close = open != NULL ? strchr(open, ']') : NULL;
    if (close == NULL) {
        fprintf(stderr, "the row for %s has no capabilities array\n", platform);
        return 0;
    }
    gf_host_capability found = 0u;
    const char* cursor = open + 1;
    while (cursor < close) {
        const char* start = strchr(cursor, '"');
        if (start == NULL || start >= close) {
            break;
        }
        const char* end = strchr(start + 1, '"');
        if (end == NULL || end >= close) {
            fprintf(stderr, "an unterminated name in the row for %s\n", platform);
            return 0;
        }
        const size_t length = (size_t)(end - start - 1);
        const gf_host_capability bit = bit_named(start + 1, length);
        if (bit == 0u) {
            fprintf(stderr, "the row for %s names an unknown capability %.*s\n", platform, (int)length, start + 1);
            return 0;
        }
        found |= bit;
        cursor = end + 1;
    }
    *set = found;
    return 1;
}

static void print_names(const char* label, gf_host_capability set) {
    for (int index = 0; index < capability_name_count; ++index) {
        if ((set & capability_names[index].bit) != 0u) {
            fprintf(stderr, "  %s: %s\n", label, capability_names[index].name);
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: gf_abi_capability_matrix <host_capabilities.json>\n");
        return 2;
    }
    gf_host_table host;
    memset(&host, 0, sizeof(host));
    if (gf_abi_get_table(GF_TABLE_HOST, GF_ABI_MAJOR, GF_ABI_MINOR, &host, sizeof(host)) != GF_STATUS_OK) {
        fprintf(stderr, "no host table\n");
        return 1;
    }
    char platform[128];
    uint64_t required = 0u;
    if (host.platform_name(platform, sizeof(platform) - 1u, &required) != GF_STATUS_OK) {
        fprintf(stderr, "platform_name failed\n");
        return 1;
    }
    platform[required] = '\0';
    /* A short buffer reports the size and writes nothing. */
    char short_buffer[1] = {'x'};
    uint64_t short_required = 0u;
    const gf_status short_status = host.platform_name(short_buffer, 0u, &short_required);
    gf_host_capabilities report;
    memset(&report, 0, sizeof(report));
    report.struct_size = (uint32_t)sizeof(report);
    if (host.capabilities(&report) != GF_STATUS_OK) {
        fprintf(stderr, "capabilities failed\n");
        return 1;
    }
    gf_host_capabilities too_small;
    memset(&too_small, 0, sizeof(too_small));
    too_small.struct_size = 8u;
    const gf_status small_status = host.capabilities(&too_small);

    char* text = read_file(argv[1]);
    if (text == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    int failures = 0;
    if (short_status != GF_STATUS_BUFFER_TOO_SMALL || short_required != required || short_buffer[0] != 'x') {
        fprintf(stderr, "a short platform_name buffer was not refused cleanly\n");
        ++failures;
    }
    if (small_status != GF_STATUS_INVALID_ARGUMENT) {
        fprintf(stderr, "a record smaller than its 1.0 size was accepted\n");
        ++failures;
    }
    const char* version = strstr(text, "\"protocol_version\":");
    const unsigned long expected_version = version != NULL ? strtoul(version + 19, NULL, 10) : 0ul;
    if (expected_version != report.protocol_version) {
        fprintf(stderr, "protocol %u, the matrix says %lu\n", report.protocol_version, expected_version);
        ++failures;
    }
    gf_host_capability expected = 0u;
    if (!read_row(text, platform, &expected)) {
        ++failures;
    } else if (expected != report.available) {
        fprintf(stderr, "%s reports another capability set than its row\n", platform);
        print_names("reported, not in the row", report.available & ~expected);
        print_names("in the row, not reported", expected & ~report.available);
        ++failures;
    }
    free(text);
    if (failures != 0) {
        return 1;
    }
    printf("gf_abi_capability_matrix: %s matches its row\n", platform);
    return 0;
}
