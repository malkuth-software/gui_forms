/*
 * A C11 program built against the installed SDK's CAbi component: it
 * negotiates the runtime table, prints the versions, and reads back the
 * detail of a refused negotiation.
 */
#include <gui_forms/c/gf_abi.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    gf_runtime_table runtime;
    memset(&runtime, 0, sizeof(runtime));
    if (gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR, &runtime, sizeof(runtime)) != GF_STATUS_OK) {
        fputs("c_abi_consumer: the runtime table did not negotiate\n", stderr);
        return 1;
    }
    gf_abi_version_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    if (runtime.version(&info) != GF_STATUS_OK) {
        fputs("c_abi_consumer: version failed\n", stderr);
        return 1;
    }
    printf("GUI.Forms %u.%u.%u, C ABI %u.%u\n", info.library_major, info.library_minor, info.library_patch,
           info.abi_major, info.abi_minor);

    unsigned char unused[16];
    const gf_status refused = gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR + 1U, 0U, unused, sizeof(unused));
    gf_error_detail detail;
    memset(&detail, 0, sizeof(detail));
    detail.struct_size = sizeof(detail);
    char message[512];
    uint64_t message_size = 0U;
    if (refused != GF_STATUS_UNSUPPORTED_VERSION ||
        runtime.error_detail(&detail, message, sizeof(message), &message_size) != GF_STATUS_OK ||
        detail.status != GF_STATUS_UNSUPPORTED_VERSION || message_size == 0U) {
        fputs("c_abi_consumer: the refused negotiation was not reported\n", stderr);
        return 1;
    }
    printf("refused as expected: %.*s\n", (int)message_size, message);
    return 0;
}
