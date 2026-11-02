/*
 * GUI.Forms C ABI 1: version negotiation and the error model, from a C11
 * consumer. Each failure check also confirms that the outputs the failing
 * call was given are left as they were (sentinel bytes).
 */
#include "gui_forms/c/gf_abi.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "gf_abi_runtime_c11_tests: %s\n", message);
        exit(1);
    }
}

static gf_runtime_table negotiate_runtime(void) {
    gf_runtime_table table;
    memset(&table, 0, sizeof(table));
    const gf_status status = gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR, &table, sizeof(table));
    require(status == GF_STATUS_OK, "the runtime table negotiates at the header's version");
    return table;
}

/* Reads the calling thread's detail; message receives at most 600 bytes. */
static gf_error_detail read_detail(const gf_runtime_table* runtime, char* message, uint64_t* message_size) {
    gf_error_detail detail;
    memset(&detail, 0, sizeof(detail));
    detail.struct_size = sizeof(detail);
    const gf_status status = (*runtime).error_detail(&detail, message, 600U, message_size);
    require(status == GF_STATUS_OK, "error_detail reads into a large enough buffer");
    return detail;
}

static void test_negotiation(void) {
    const gf_runtime_table table = negotiate_runtime();
    require(table.struct_size == sizeof(gf_runtime_table), "struct_size reports the bytes written");
    require(table.abi_minor == GF_ABI_MINOR, "abi_minor reports the library's minor");
    require(table.version != NULL && table.error_detail != NULL && table.clear_error != NULL,
            "every runtime slot is present");

    gf_abi_version_info info;
    memset(&info, 0xab, sizeof(info));
    info.struct_size = sizeof(info);
    require(table.version(&info) == GF_STATUS_OK, "version succeeds");
    require(info.struct_size == sizeof(info), "version keeps the caller's struct_size");
    require(info.abi_major == GF_ABI_MAJOR && info.abi_minor == GF_ABI_MINOR, "version reports the ABI version");
    require(info.library_major == 0U, "the library is GUI.Forms 0.x");

    /* A caller compiled before the version slot existed asks for less. */
    unsigned char partial[sizeof(gf_runtime_table)];
    memset(partial, 0xcd, sizeof(partial));
    require(gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR, 0U, partial, 16U) == GF_STATUS_OK,
            "a table of header and one slot negotiates");
    uint32_t written = 0U;
    memcpy(&written, partial, sizeof(written));
    require(written == 16U, "struct_size reports the 16 bytes written");
    for (size_t index = 16U; index < sizeof(partial); ++index) {
        require(partial[index] == 0xcdU, "nothing past the caller's capacity is written");
    }
}

static void expect_entry_failure(const gf_runtime_table* runtime, gf_table_id table_id, uint32_t major,
                                 uint32_t minor, uint64_t capacity, int null_table, gf_status expected,
                                 const char* message) {
    unsigned char table[64];
    memset(table, 0x5a, sizeof(table));
    const gf_status status = gf_abi_get_table(table_id, major, minor, null_table ? NULL : table, capacity);
    require(status == expected, message);
    for (size_t index = 0U; index < sizeof(table); ++index) {
        require(table[index] == 0x5aU, "a failed negotiation writes nothing");
    }
    char text[600];
    uint64_t text_size = 0U;
    const gf_error_detail detail = read_detail(runtime, text, &text_size);
    require(detail.status == expected, "the detail records the failure's status");
    require(detail.table_id == table_id, "the detail records the requested table");
    require(detail.slot_index == GF_ABI_ENTRY_SLOT, "the detail names gf_abi_get_table");
    require(detail.message_size == text_size && text_size > 0U && text_size <= 512U,
            "the detail carries a bounded message");
}

static void test_negotiation_failures(void) {
    const gf_runtime_table runtime = negotiate_runtime();
    expect_entry_failure(&runtime, GF_TABLE_RUNTIME, GF_ABI_MAJOR + 1U, 0U, 32U, 0,
                         GF_STATUS_UNSUPPORTED_VERSION, "another major is refused");
    expect_entry_failure(&runtime, GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR + 1U, 32U, 0,
                         GF_STATUS_UNSUPPORTED_VERSION, "a newer minor is refused");
    expect_entry_failure(&runtime, 999U, GF_ABI_MAJOR, GF_ABI_MINOR, 32U, 0,
                         GF_STATUS_NOT_FOUND, "an unknown table is not found");
    expect_entry_failure(&runtime, GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR, 32U, 1,
                         GF_STATUS_INVALID_ARGUMENT, "a null table is refused");
    expect_entry_failure(&runtime, GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR, 4U, 0,
                         GF_STATUS_INVALID_ARGUMENT, "a table smaller than its header is refused");
    expect_entry_failure(&runtime, GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR, 12U, 0,
                         GF_STATUS_INVALID_ARGUMENT, "a capacity that is not a multiple of 8 is refused");
}

static void test_detail_reading(void) {
    const gf_runtime_table runtime = negotiate_runtime();
    unsigned char unused[16];
    require(gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR + 1U, 0U, unused, sizeof(unused)) ==
                GF_STATUS_UNSUPPORTED_VERSION, "a failure to read back");
    char text[600];
    uint64_t text_size = 0U;
    const gf_error_detail first = read_detail(&runtime, text, &text_size);

    /* Too small a buffer: only the required size is written, and reading the
     * detail does not replace it. */
    gf_error_detail untouched;
    memset(&untouched, 0x77, sizeof(untouched));
    untouched.struct_size = sizeof(untouched);
    char small[4];
    memset(small, 0x11, sizeof(small));
    uint64_t required = 0U;
    require(runtime.error_detail(&untouched, small, sizeof(small), &required) == GF_STATUS_BUFFER_TOO_SMALL,
            "a short buffer is reported");
    require(required == text_size, "the required size is the message size");
    require(untouched.status == 0x77777777 && small[0] == 0x11, "nothing else is written");
    const gf_error_detail again = read_detail(&runtime, text, &text_size);
    require(again.status == first.status && again.message_size == first.message_size,
            "reading the detail does not replace it");

    /* Refused arguments to error_detail are not recorded either. */
    require(runtime.error_detail(NULL, text, sizeof(text), &required) == GF_STATUS_INVALID_ARGUMENT,
            "a null detail is refused");
    gf_error_detail short_record;
    memset(&short_record, 0, sizeof(short_record));
    short_record.struct_size = 8U;
    require(runtime.error_detail(&short_record, text, sizeof(text), &required) == GF_STATUS_INVALID_ARGUMENT,
            "a record smaller than ABI 1.0's is refused");
    require(runtime.error_detail(&short_record, NULL, 8U, &required) == GF_STATUS_INVALID_ARGUMENT,
            "a null buffer with a capacity is refused");
    const gf_error_detail still = read_detail(&runtime, text, &text_size);
    require(still.status == first.status, "refused reads leave the detail");

    /* A caller compiled against a later minor declares a larger record; the
     * library writes only the part it knows. */
    struct later_detail {
        gf_error_detail known;
        uint64_t later_field;
    } later;
    memset(&later, 0, sizeof(later));
    later.known.struct_size = sizeof(later);
    later.later_field = 0x0123456789abcdefULL;
    require(runtime.error_detail(&later.known, text, sizeof(text), &required) == GF_STATUS_OK,
            "a larger record is accepted");
    require(later.known.struct_size == sizeof(later), "the caller's struct_size is kept");
    require(later.later_field == 0x0123456789abcdefULL, "fields the library does not know are not written");

    require(runtime.clear_error() == GF_STATUS_OK, "clear_error succeeds");
    const gf_error_detail cleared = read_detail(&runtime, text, &text_size);
    require(cleared.status == GF_STATUS_OK && cleared.message_size == 0U && text_size == 0U,
            "after clear_error the thread has no failure");
}

struct thread_result {
    gf_status seen_before;
    gf_status seen_after;
};

static void* other_thread(void* opaque) {
    struct thread_result* result = (struct thread_result*)opaque;
    const gf_runtime_table runtime = negotiate_runtime();
    char text[600];
    uint64_t text_size = 0U;
    (*result).seen_before = read_detail(&runtime, text, &text_size).status;
    unsigned char unused[16];
    require(gf_abi_get_table(998U, GF_ABI_MAJOR, GF_ABI_MINOR, unused, sizeof(unused)) == GF_STATUS_NOT_FOUND,
            "the other thread fails on its own");
    (*result).seen_after = read_detail(&runtime, text, &text_size).status;
    return NULL;
}

static void test_detail_is_per_thread(void) {
    const gf_runtime_table runtime = negotiate_runtime();
    unsigned char unused[16];
    require(gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR + 1U, 0U, unused, sizeof(unused)) ==
                GF_STATUS_UNSUPPORTED_VERSION, "this thread fails");
    struct thread_result result = {GF_STATUS_INTERNAL, GF_STATUS_INTERNAL};
    pthread_t thread;
    require(pthread_create(&thread, NULL, other_thread, &result) == 0, "the second thread starts");
    require(pthread_join(thread, NULL) == 0, "the second thread ends");
    require(result.seen_before == GF_STATUS_OK, "a new thread has no failure");
    require(result.seen_after == GF_STATUS_NOT_FOUND, "a thread reads its own failure");
    char text[600];
    uint64_t text_size = 0U;
    require(read_detail(&runtime, text, &text_size).status == GF_STATUS_UNSUPPORTED_VERSION,
            "another thread's failure does not replace this thread's");
}

int main(void) {
    test_negotiation();
    test_negotiation_failures();
    test_detail_reading();
    test_detail_is_per_thread();
    puts("gf_abi_runtime_c11_tests: passed");
    return 0;
}
