// The one exported function of the GUI.Forms C ABI (major 1) and the runtime
// table it hands out. See include/gui_forms/c/gf_abi.h for the contract.

#include "gui_forms/c/gf_abi.h"

#include "../failure/abi_failure.hpp"
#include "../tables/abi_tables.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if !defined(GF_ABI_LIBRARY_VERSION_MAJOR) || !defined(GF_ABI_LIBRARY_VERSION_MINOR) || \
    !defined(GF_ABI_LIBRARY_VERSION_PATCH)
#error "The build defines GF_ABI_LIBRARY_VERSION_MAJOR, _MINOR and _PATCH from the project version"
#endif

namespace {

using gui_forms::abi1::FailureRecord;
using gui_forms::abi1::SlotIdentity;

// The sizes of the output records at ABI 1.0. A caller compiled against 1.0
// passes at least these; a later minor may grow a record at its tail.
constexpr std::uint32_t version_info_size_1_0 = 24U;
constexpr std::uint32_t error_detail_size_1_0 = 24U;
static_assert(sizeof(gf_abi_version_info) == version_info_size_1_0);
static_assert(sizeof(gf_error_detail) == error_detail_size_1_0);

// A table starts with struct_size and abi_minor.
constexpr std::uint64_t table_header_size = 8U;

// Writes the bytes of a complete output record after its struct_size field,
// up to the size the caller declared. The caller's struct_size is kept.
template <typename Record>
void write_output_record(const Record& complete, Record* output) noexcept {
    const std::uint32_t declared = (*output).struct_size;
    const std::size_t writable = std::min<std::size_t>(declared, sizeof(Record));
    const std::size_t skipped = sizeof(std::uint32_t);
    unsigned char* destination = reinterpret_cast<unsigned char*>(output);
    const unsigned char* source = reinterpret_cast<const unsigned char*>(&complete);
    std::memcpy(destination + skipped, source + skipped, writable - skipped);
}

// GF_TABLE_RUNTIME. These functions return failures without recording them,
// so reading the detail never replaces it.

gf_status GF_CALL runtime_version(gf_abi_version_info* info) noexcept {
    if (info == nullptr || (*info).struct_size < version_info_size_1_0) {
        return GF_STATUS_INVALID_ARGUMENT;
    }
    gf_abi_version_info complete{};
    complete.struct_size = sizeof(gf_abi_version_info);
    complete.abi_major = GF_ABI_MAJOR;
    complete.abi_minor = GF_ABI_MINOR;
    complete.library_major = GF_ABI_LIBRARY_VERSION_MAJOR;
    complete.library_minor = GF_ABI_LIBRARY_VERSION_MINOR;
    complete.library_patch = GF_ABI_LIBRARY_VERSION_PATCH;
    write_output_record(complete, info);
    return GF_STATUS_OK;
}

gf_status GF_CALL runtime_error_detail(gf_error_detail* detail,
                                       char* message_buffer,
                                       std::uint64_t message_capacity,
                                       std::uint64_t* message_required) noexcept {
    if (detail == nullptr || (*detail).struct_size < error_detail_size_1_0 ||
        message_required == nullptr || (message_buffer == nullptr && message_capacity != 0U)) {
        return GF_STATUS_INVALID_ARGUMENT;
    }
    const FailureRecord& record = gui_forms::abi1::thread_failure();
    const std::uint64_t message_size = record.message_size;
    if (message_capacity < message_size) {
        *message_required = message_size;
        return GF_STATUS_BUFFER_TOO_SMALL;
    }
    gf_error_detail complete{};
    complete.struct_size = sizeof(gf_error_detail);
    complete.status = record.status;
    complete.table_id = record.slot.table_id;
    complete.slot_index = record.slot.slot_index;
    complete.message_size = message_size;
    write_output_record(complete, detail);
    if (message_size != 0U) {
        std::memcpy(message_buffer, record.message.data(), static_cast<std::size_t>(message_size));
    }
    *message_required = message_size;
    return GF_STATUS_OK;
}

gf_status GF_CALL runtime_clear_error() noexcept {
    gui_forms::abi1::clear_failure();
    return GF_STATUS_OK;
}

constexpr gf_runtime_table runtime_table{
    .struct_size = sizeof(gf_runtime_table),
    .abi_minor = GF_ABI_MINOR,
    .version = &runtime_version,
    .error_detail = &runtime_error_detail,
    .clear_error = &runtime_clear_error,
};

// The complete table for an id, or no table. Every table is a constant with
// static storage, so the result never dangles.
struct TableBytes {
    const void* data = nullptr;
    std::uint64_t size = 0U;
};

TableBytes find_table(gf_table_id table_id) noexcept {
    TableBytes found{};
    switch (table_id) {
    case GF_TABLE_RUNTIME:
        found.data = &runtime_table;
        found.size = sizeof(gf_runtime_table);
        break;
    case GF_TABLE_OBJECTS:
        found.data = &gui_forms::abi1::objects_table();
        found.size = sizeof(gf_objects_table);
        break;
    case GF_TABLE_PROPERTIES:
        found.data = &gui_forms::abi1::properties_table();
        found.size = sizeof(gf_properties_table);
        break;
    case GF_TABLE_EVENTS:
        found.data = &gui_forms::abi1::events_table();
        found.size = sizeof(gf_events_table);
        break;
    case GF_TABLE_DISPATCH:
        found.data = &gui_forms::abi1::dispatch_table();
        found.size = sizeof(gf_dispatch_table);
        break;
    case GF_TABLE_WINDOWS:
        found.data = &gui_forms::abi1::windows_table();
        found.size = sizeof(gf_windows_table);
        break;
    case GF_TABLE_SCHEMA:
        found.data = &gui_forms::abi1::schema_table();
        found.size = sizeof(gf_schema_table);
        break;
    case GF_TABLE_THEMES:
        found.data = &gui_forms::abi1::themes_table();
        found.size = sizeof(gf_themes_table);
        break;
    case GF_TABLE_HOST:
        found.data = &gui_forms::abi1::host_table();
        found.size = sizeof(gf_host_table);
        break;
    case GF_TABLE_DIAGNOSTICS:
        found.data = &gui_forms::abi1::diagnostics_table();
        found.size = sizeof(gf_diagnostics_table);
        break;
    case GF_TABLE_CONTROLS:
        found.data = &gui_forms::abi1::controls_table();
        found.size = sizeof(gf_controls_table);
        break;
    case GF_TABLE_TEXT_EDITING:
        found.data = &gui_forms::abi1::text_editing_table();
        found.size = sizeof(gf_text_editing_table);
        break;
    case GF_TABLE_POPUPS:
        found.data = &gui_forms::abi1::popups_table();
        found.size = sizeof(gf_popups_table);
        break;
    case GF_TABLE_PROPERTY_GRID:
        found.data = &gui_forms::abi1::property_grid_table();
        found.size = sizeof(gf_property_grid_table);
        break;
    case GF_TABLE_CUSTOM_CONTROLS:
        found.data = &gui_forms::abi1::custom_controls_table();
        found.size = sizeof(gf_custom_controls_table);
        break;
    case GF_TABLE_BINDINGS:
        found.data = &gui_forms::abi1::bindings_table();
        found.size = sizeof(gf_bindings_table);
        break;
#if defined(GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT)
    case GF_TABLE_WINDOWS_PAINT_ENDPOINT:
        found.data = &gui_forms::abi1::windows_paint_endpoint_table();
        found.size = sizeof(gf_windows_paint_endpoint_table);
        break;
#endif
    default:
        // An id this library does not know has no table: the caller gets
        // GF_STATUS_NOT_FOUND.
        break;
    }
    return found;
}

} // namespace

extern "C" GF_ABI_EXPORT gf_status GF_CALL gf_abi_get_table(gf_table_id table_id,
                                                            std::uint32_t abi_major,
                                                            std::uint32_t abi_minor,
                                                            void* table,
                                                            std::uint64_t table_capacity) {
    const SlotIdentity entry{.table_id = table_id, .slot_index = GF_ABI_ENTRY_SLOT};
    if (table == nullptr || table_capacity < table_header_size || table_capacity % 8U != 0U) {
        return gui_forms::abi1::fail(GF_STATUS_INVALID_ARGUMENT, entry,
                                     "gf_abi_get_table needs a table of at least 8 bytes, "
                                     "a multiple of 8");
    }
    if (abi_major != GF_ABI_MAJOR) {
        return gui_forms::abi1::fail(GF_STATUS_UNSUPPORTED_VERSION, entry,
                                     "this library implements GUI.Forms C ABI major 1 only");
    }
    if (abi_minor > GF_ABI_MINOR) {
        return gui_forms::abi1::fail(GF_STATUS_UNSUPPORTED_VERSION, entry,
                                     "the requested GUI.Forms C ABI minor is newer than this library");
    }
    const TableBytes found = find_table(table_id);
    if (found.data == nullptr) {
        return gui_forms::abi1::fail(GF_STATUS_NOT_FOUND, entry,
                                     "this library has no GUI.Forms C ABI table with that id");
    }
    const std::uint64_t written = std::min(table_capacity, found.size);
    std::memcpy(table, found.data, static_cast<std::size_t>(written));
    // struct_size reports what was written, so the caller knows which slots
    // are present; abi_minor is already the library's own.
    const std::uint32_t written_size = static_cast<std::uint32_t>(written);
    std::memcpy(table, &written_size, sizeof(written_size));
    return GF_STATUS_OK;
}
