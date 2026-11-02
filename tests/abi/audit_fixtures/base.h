#ifndef GUI_FORMS_C_GF_ABI_H
#define GUI_FORMS_C_GF_ABI_H

/* The audit fixture: a small header in the gf_abi.h dialect. Each seeded
 * break in tests/abi/abi_audit_tests.py edits one line of it. */

#include <stdint.h>

#if UINTPTR_MAX != UINT64_MAX
#error "64-bit targets only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* gf:platform-begin */
#define GF_ABI_EXPORT
#define GF_CALL
/* gf:platform-end */

#define GF_ABI_MAJOR 1u
#define GF_ABI_MINOR 1u

typedef int32_t gf_status;
#define GF_OK 0
#define GF_ERROR_INVALID_ARGUMENT 1

typedef uint32_t gf_table_id;
#define GF_TABLE_SAMPLE 1u

typedef uint64_t gf_object;

typedef struct gf_point {
    double x;
    double y;
} gf_point;

typedef struct gf_sample_record {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t count;
} gf_sample_record;

typedef void (GF_CALL* gf_sample_callback)(void* context, uint32_t value);

typedef struct gf_sample_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    gf_status (GF_CALL* first)(gf_object object, gf_point point);
    gf_status (GF_CALL* second)(gf_object object, gf_sample_record* record);
} gf_sample_table;

GF_ABI_EXPORT gf_status GF_CALL gf_abi_get_table(gf_table_id table_id,
                                                 uint32_t abi_major,
                                                 uint32_t abi_minor,
                                                 void* table,
                                                 uint64_t table_capacity);

#ifdef __cplusplus
}
#endif

#endif
