// GF_TABLE_WINDOWS_PAINT_ENDPOINT: the Windows compatibility paint endpoint
// (host::WindowsCompatibilityPaintEndpoint) bound to a custom control. See
// gf_abi.h. Built only with the Windows host
// (GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT); elsewhere the table does not exist.

#if defined(GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT)

#include "../custom/abi_custom_control.hpp"
#include "../failure/abi_failure.hpp"
#include "../registry/abi_registry.hpp"
#include "../tables/abi_control_entry.hpp"
#include "../tables/abi_tables.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/platform/windows_compatibility_paint_endpoint/windows_compatibility_paint_endpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

using Endpoint = host::WindowsCompatibilityPaintEndpoint;

constexpr SlotIdentity acquire_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 0U};
constexpr SlotIdentity configure_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 1U};
constexpr SlotIdentity touch_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 2U};
constexpr SlotIdentity drain_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 3U};
constexpr SlotIdentity snapshot_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 4U};
constexpr SlotIdentity submit_bgra_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 5U};
constexpr SlotIdentity release_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 6U};
constexpr SlotIdentity get_dc_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 7U};
constexpr SlotIdentity release_dc_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 8U};
constexpr SlotIdentity publish_dc_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 9U};
constexpr SlotIdentity begin_write_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 10U};
constexpr SlotIdentity end_write_slot{GF_TABLE_WINDOWS_PAINT_ENDPOINT, 11U};

// At most 2^28 pixels (1 GiB of BGRA) in one endpoint.
constexpr std::uint64_t maximum_pixels = 268435456U;

constexpr const char* stale_endpoint = "the paint endpoint is not active";
constexpr const char* not_endpoint_handle = "the handle is not a paint endpoint's";
constexpr const char* not_endpoint_dc = "the device context is not a paint endpoint's";

// One live endpoint: the endpoint, the custom control it is bound to (which
// it does not keep alive) and the surface it gave the control, so a release
// unbinds that surface and no other.
struct EndpointBinding final {
    std::shared_ptr<Endpoint> endpoint;
    std::weak_ptr<Control> control;
    std::shared_ptr<LiveSurface> surface;
};

// One open write: the endpoint it serializes and the thread that must end it.
struct WriteBinding final {
    std::shared_ptr<Endpoint> endpoint;
    std::uintptr_t device_context{};
    std::thread::id thread;
};

// The live endpoints and writes, by token. Any thread reaches them (the
// device-context functions run on the shim's), so one mutex guards the
// maps and the counters; no endpoint function runs while it is held.
// tokens_by_handle and tokens_by_device_context index the live endpoints by
// their compatibility handle and device context: both are made with the
// endpoint (acquire configures it) and stay fixed until its release, so the
// indexes are written at acquire and erased at release only (N22).
struct EndpointRegistry final {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, EndpointBinding> endpoints;
    std::unordered_map<std::uint64_t, std::uint64_t> tokens_by_handle;
    std::unordered_map<std::uint64_t, std::uint64_t> tokens_by_device_context;
    std::unordered_map<std::uint64_t, WriteBinding> writes;
    std::uint64_t next_endpoint{1U};
    std::uint64_t next_write{1U};
};

// Leaked on purpose: endpoints a process never released are not torn down
// after the host that backs them during static destruction.
EndpointRegistry& endpoint_registry() {
    static EndpointRegistry* registry = new EndpointRegistry();
    return *registry;
}

std::shared_ptr<Endpoint> find_endpoint(gf_paint_endpoint token) {
    EndpointRegistry& endpoints = endpoint_registry();
    const std::scoped_lock lock(endpoints.mutex);
    const std::unordered_map<std::uint64_t, EndpointBinding>::const_iterator found = endpoints.endpoints.find(token);
    if (found == endpoints.endpoints.end()) {
        return nullptr;
    }
    return (*found).second.endpoint;
}

// The live endpoint an index names by key, or null: one lookup in the
// index and one in the endpoints. The caller holds the registry's lock.
std::shared_ptr<Endpoint> indexed_endpoint_locked(const std::unordered_map<std::uint64_t, std::uint64_t>& index,
                                                  std::uint64_t key) {
    EndpointRegistry& endpoints = endpoint_registry();
    const std::unordered_map<std::uint64_t, std::uint64_t>::const_iterator token = index.find(key);
    if (token == index.end()) {
        return nullptr;
    }
    const std::unordered_map<std::uint64_t, EndpointBinding>::const_iterator found =
        endpoints.endpoints.find((*token).second);
    if (found == endpoints.endpoints.end()) {
        return nullptr;
    }
    return (*found).second.endpoint;
}

std::shared_ptr<Endpoint> endpoint_for_handle(std::uint64_t handle) {
    EndpointRegistry& endpoints = endpoint_registry();
    const std::scoped_lock lock(endpoints.mutex);
    std::shared_ptr<Endpoint> endpoint = indexed_endpoint_locked(endpoints.tokens_by_handle, handle);
    return endpoint;
}

std::shared_ptr<Endpoint> endpoint_for_dc(std::uint64_t device_context) {
    EndpointRegistry& endpoints = endpoint_registry();
    const std::scoped_lock lock(endpoints.mutex);
    std::shared_ptr<Endpoint> endpoint = indexed_endpoint_locked(endpoints.tokens_by_device_context, device_context);
    return endpoint;
}

bool valid_extent(std::uint32_t width, std::uint32_t height) noexcept {
    return width != 0U && height != 0U && width <= GF_PAINT_ENDPOINT_MAXIMUM_SIDE &&
           height <= GF_PAINT_ENDPOINT_MAXIMUM_SIDE &&
           static_cast<std::uint64_t>(width) * height <= maximum_pixels;
}

gf_status GF_CALL endpoint_acquire(gf_object control, std::uint32_t width, std::uint32_t height,
                                   gf_paint_endpoint* endpoint, std::uint64_t* compatibility_handle) noexcept {
    try {
        const ShadowFrameScope shadow_frame(acquire_slot.table_id, acquire_slot.slot_index);
        if (endpoint == nullptr || compatibility_handle == nullptr || !valid_extent(width, height)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, acquire_slot,
                        "acquire needs outputs and 1 to 32768 pixels a side, at most 2^28 in all");
        }
        Control::Ptr target;
        const gf_status entered = enter_live_control(control, acquire_slot, target);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::shared_ptr<Endpoint> made = Endpoint::acquire(width, height);
        if (!made || (*made).compatibility_handle() == 0U) {
            return fail(GF_STATUS_INTERNAL, acquire_slot, "the paint endpoint could not be made");
        }
        const std::shared_ptr<LiveSurface> surface = (*made).live_surface();
        if (!bind_custom_surface(*target, surface)) {
            (*made).release();
            return fail(GF_STATUS_WRONG_HANDLE_KIND, acquire_slot, "the control is not a custom control");
        }
        const std::uint64_t handle = static_cast<std::uint64_t>((*made).compatibility_handle());
        const std::uint64_t device_context = static_cast<std::uint64_t>((*made).device_context());
        EndpointRegistry& endpoints = endpoint_registry();
        const std::scoped_lock lock(endpoints.mutex);
        const std::uint64_t token = endpoints.next_endpoint;
        // Room for every node first, so a failed allocation leaves the maps
        // as they were.
        endpoints.endpoints.reserve(endpoints.endpoints.size() + 1U);
        endpoints.tokens_by_handle.reserve(endpoints.tokens_by_handle.size() + 1U);
        endpoints.tokens_by_device_context.reserve(endpoints.tokens_by_device_context.size() + 1U);
        endpoints.next_endpoint = token + 1U;
        endpoints.endpoints.emplace(token, EndpointBinding{made, std::weak_ptr<Control>(target), surface});
        endpoints.tokens_by_handle.insert_or_assign(handle, token);
        if (device_context != 0U) {
            endpoints.tokens_by_device_context.insert_or_assign(device_context, token);
        }
        *endpoint = token;
        *compatibility_handle = handle;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(acquire_slot);
    }
}

gf_status GF_CALL endpoint_configure(gf_paint_endpoint token, std::uint32_t width, std::uint32_t height) noexcept {
    try {
        const ShadowFrameScope shadow_frame(configure_slot.table_id, configure_slot.slot_index);
        if (!valid_extent(width, height)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, configure_slot,
                        "configure needs 1 to 32768 pixels a side, at most 2^28 in all");
        }
        const std::shared_ptr<Endpoint> endpoint = find_endpoint(token);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, configure_slot, stale_endpoint);
        }
        if (!(*endpoint).configure(width, height)) {
            return fail(GF_STATUS_WRONG_THREAD, configure_slot, "configure runs on the thread that acquired");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(configure_slot);
    }
}

gf_status GF_CALL endpoint_touch(gf_paint_endpoint token, std::uint32_t explicit_boundary) noexcept {
    try {
        const ShadowFrameScope shadow_frame(touch_slot.table_id, touch_slot.slot_index);
        if (explicit_boundary > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, touch_slot, "explicit_boundary is 0 or 1");
        }
        const std::shared_ptr<Endpoint> endpoint = find_endpoint(token);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, touch_slot, stale_endpoint);
        }
        (*endpoint).touch(explicit_boundary == 1U);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(touch_slot);
    }
}

gf_status GF_CALL endpoint_drain(gf_paint_endpoint token) noexcept {
    try {
        const ShadowFrameScope shadow_frame(drain_slot.table_id, drain_slot.slot_index);
        const std::shared_ptr<Endpoint> endpoint = find_endpoint(token);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, drain_slot, stale_endpoint);
        }
        if (!(*endpoint).drain_now()) {
            return fail(GF_STATUS_WRONG_THREAD, drain_slot, "drain runs on the thread that acquired");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(drain_slot);
    }
}

gf_status GF_CALL endpoint_snapshot(gf_paint_endpoint token, char* buffer, std::uint64_t capacity,
                                    std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(snapshot_slot.table_id, snapshot_slot.slot_index);
        if (required == nullptr || (buffer == nullptr && capacity != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, snapshot_slot,
                        "snapshot needs a size output, and a buffer when the capacity is not zero");
        }
        const std::shared_ptr<Endpoint> endpoint = find_endpoint(token);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, snapshot_slot, stale_endpoint);
        }
        const std::string text = (*endpoint).snapshot();
        *required = text.size();
        if (capacity < text.size()) {
            return fail(GF_STATUS_BUFFER_TOO_SMALL, snapshot_slot, "the buffer is smaller than the snapshot");
        }
        if (!text.empty()) {
            std::memcpy(buffer, text.data(), text.size());
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(snapshot_slot);
    }
}

gf_status GF_CALL endpoint_submit_bgra(std::uint64_t compatibility_handle, std::uint32_t width,
                                       std::uint32_t height, std::uint64_t row_bytes,
                                       const std::uint8_t* pixels) noexcept {
    try {
        const ShadowFrameScope shadow_frame(submit_bgra_slot.table_id, submit_bgra_slot.slot_index);
        if (compatibility_handle == 0U || pixels == nullptr || !valid_extent(width, height) ||
            row_bytes < static_cast<std::uint64_t>(width) * 4U ||
            height > std::numeric_limits<std::size_t>::max() / row_bytes) {
            return fail(GF_STATUS_INVALID_ARGUMENT, submit_bgra_slot,
                        "submit_bgra needs a handle and bounded pixels with rows of at least width * 4 bytes");
        }
        const std::shared_ptr<Endpoint> endpoint = endpoint_for_handle(compatibility_handle);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, submit_bgra_slot, not_endpoint_handle);
        }
        const std::size_t size = static_cast<std::size_t>(row_bytes) * height;
        const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pixels), size);
        if (!(*endpoint).submit_bgra32_premultiplied(width, height, row_bytes, bytes)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, submit_bgra_slot, "the endpoint refused the frame");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(submit_bgra_slot);
    }
}

gf_status GF_CALL endpoint_release(gf_paint_endpoint token) noexcept {
    try {
        const ShadowFrameScope shadow_frame(release_slot.table_id, release_slot.slot_index);
        EndpointBinding binding;
        {
            EndpointRegistry& endpoints = endpoint_registry();
            const std::scoped_lock lock(endpoints.mutex);
            const std::unordered_map<std::uint64_t, EndpointBinding>::iterator found =
                endpoints.endpoints.find(token);
            if (found == endpoints.endpoints.end()) {
                return fail(GF_STATUS_STALE_HANDLE, release_slot, stale_endpoint);
            }
            binding = std::move((*found).second);
            endpoints.endpoints.erase(found);
            const std::uint64_t handle = static_cast<std::uint64_t>((*binding.endpoint).compatibility_handle());
            const std::uint64_t device_context = static_cast<std::uint64_t>((*binding.endpoint).device_context());
            const std::unordered_map<std::uint64_t, std::uint64_t>::const_iterator by_handle =
                endpoints.tokens_by_handle.find(handle);
            if (by_handle != endpoints.tokens_by_handle.end() && (*by_handle).second == token) {
                endpoints.tokens_by_handle.erase(by_handle);
            }
            const std::unordered_map<std::uint64_t, std::uint64_t>::const_iterator by_device_context =
                endpoints.tokens_by_device_context.find(device_context);
            if (by_device_context != endpoints.tokens_by_device_context.end() &&
                (*by_device_context).second == token) {
                endpoints.tokens_by_device_context.erase(by_device_context);
            }
        }
        // On the UI thread the control lets the surface go now; from another
        // thread (a finalizer) it keeps the last frame until it is disposed,
        // since retained state changes only on the UI thread. The thread is
        // tested without recording a failure: release succeeds from any
        // thread, so it leaves no WRONG_THREAD behind for error_detail (N23).
        const Control::Ptr control = binding.control.lock();
        if (control && registry().calling_ui_thread() && (*control).is_alive()) {
            static_cast<void>(unbind_custom_surface(*control, binding.surface));
        }
        (*binding.endpoint).release();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(release_slot);
    }
}

gf_status GF_CALL endpoint_get_dc(std::uint64_t compatibility_handle, std::uint64_t* device_context) noexcept {
    try {
        const ShadowFrameScope shadow_frame(get_dc_slot.table_id, get_dc_slot.slot_index);
        if (compatibility_handle == 0U || device_context == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, get_dc_slot, "get_dc needs a handle and an output");
        }
        const std::shared_ptr<Endpoint> endpoint = endpoint_for_handle(compatibility_handle);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, get_dc_slot, not_endpoint_handle);
        }
        const std::uint64_t context = static_cast<std::uint64_t>((*endpoint).device_context());
        if (context == 0U) {
            return fail(GF_STATUS_STALE_HANDLE, get_dc_slot, "the endpoint has no device context now");
        }
        *device_context = context;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(get_dc_slot);
    }
}

gf_status GF_CALL endpoint_release_dc(std::uint64_t compatibility_handle, std::uint64_t device_context) noexcept {
    try {
        const ShadowFrameScope shadow_frame(release_dc_slot.table_id, release_dc_slot.slot_index);
        if (compatibility_handle == 0U || device_context == 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, release_dc_slot, "release_dc needs a handle and a device context");
        }
        const std::shared_ptr<Endpoint> endpoint = endpoint_for_handle(compatibility_handle);
        if (!endpoint || static_cast<std::uint64_t>((*endpoint).device_context()) != device_context) {
            return fail(GF_STATUS_STALE_HANDLE, release_dc_slot, not_endpoint_dc);
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(release_dc_slot);
    }
}

gf_status GF_CALL endpoint_publish_dc(std::uint64_t device_context) noexcept {
    try {
        const ShadowFrameScope shadow_frame(publish_dc_slot.table_id, publish_dc_slot.slot_index);
        if (device_context == 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, publish_dc_slot, "publish_dc needs a device context");
        }
        const std::shared_ptr<Endpoint> endpoint = endpoint_for_dc(device_context);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, publish_dc_slot, not_endpoint_dc);
        }
        if (!(*endpoint).publish_device_context(static_cast<std::uintptr_t>(device_context))) {
            return fail(GF_STATUS_INTERNAL, publish_dc_slot, "the endpoint's frame could not be published");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(publish_dc_slot);
    }
}

gf_status GF_CALL endpoint_begin_write(std::uint64_t device_context, gf_paint_write_lease* lease) noexcept {
    try {
        const ShadowFrameScope shadow_frame(begin_write_slot.table_id, begin_write_slot.slot_index);
        if (device_context == 0U || lease == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, begin_write_slot,
                        "begin_write needs a device context and an output");
        }
        const std::shared_ptr<Endpoint> endpoint = endpoint_for_dc(device_context);
        if (!endpoint) {
            return fail(GF_STATUS_STALE_HANDLE, begin_write_slot, not_endpoint_dc);
        }
        EndpointRegistry& endpoints = endpoint_registry();
        std::uint64_t token = 0U;
        {
            const std::scoped_lock lock(endpoints.mutex);
            token = endpoints.next_write;
            endpoints.next_write = token + 1U;
            endpoints.writes.emplace(token, WriteBinding{endpoint, static_cast<std::uintptr_t>(device_context),
                                                       std::this_thread::get_id()});
        }
        if (!(*endpoint).begin_device_context_write(static_cast<std::uintptr_t>(device_context))) {
            const std::scoped_lock lock(endpoints.mutex);
            endpoints.writes.erase(token);
            return fail(GF_STATUS_STALE_HANDLE, begin_write_slot, "the endpoint was released before the write began");
        }
        *lease = token;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(begin_write_slot);
    }
}

gf_status GF_CALL endpoint_end_write(gf_paint_write_lease lease, std::uint32_t publish) noexcept {
    try {
        const ShadowFrameScope shadow_frame(end_write_slot.table_id, end_write_slot.slot_index);
        if (lease == 0U || publish > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, end_write_slot, "end_write needs a lease and publish 0 or 1");
        }
        WriteBinding write;
        {
            EndpointRegistry& endpoints = endpoint_registry();
            const std::scoped_lock lock(endpoints.mutex);
            const std::unordered_map<std::uint64_t, WriteBinding>::iterator found = endpoints.writes.find(lease);
            if (found == endpoints.writes.end()) {
                return fail(GF_STATUS_STALE_HANDLE, end_write_slot, "the write lease is not active");
            }
            if ((*found).second.thread != std::this_thread::get_id()) {
                return fail(GF_STATUS_WRONG_THREAD, end_write_slot, "a write ends on the thread that began it");
            }
            write = std::move((*found).second);
            endpoints.writes.erase(found);
        }
        if (!(*write.endpoint).end_device_context_write(write.device_context, publish == 1U)) {
            return fail(GF_STATUS_INTERNAL, end_write_slot, "the write could not finish or publish");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(end_write_slot);
    }
}

constexpr gf_windows_paint_endpoint_table table{
    .struct_size = sizeof(gf_windows_paint_endpoint_table),
    .abi_minor = GF_ABI_MINOR,
    .acquire = &endpoint_acquire,
    .configure = &endpoint_configure,
    .touch = &endpoint_touch,
    .drain = &endpoint_drain,
    .snapshot = &endpoint_snapshot,
    .submit_bgra = &endpoint_submit_bgra,
    .release = &endpoint_release,
    .get_dc = &endpoint_get_dc,
    .release_dc = &endpoint_release_dc,
    .publish_dc = &endpoint_publish_dc,
    .begin_write = &endpoint_begin_write,
    .end_write = &endpoint_end_write,
};

} // namespace

const gf_windows_paint_endpoint_table& windows_paint_endpoint_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1

#endif
