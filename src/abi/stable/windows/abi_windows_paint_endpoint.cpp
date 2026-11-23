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
#include "../ic/abi_windows_paint_endpoint_ic_checks.hpp"

#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/platform/windows_compatibility_paint_endpoint/windows_compatibility_paint_endpoint.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <stxlib/threads.hpp>
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
    stxlib::ThreadIdentity thread;
};

// The live endpoints and writes, by token. Any thread reaches them (the
// device-context functions run on the shim's), so one mutex guards the
// maps and the counters; no endpoint function runs while it is held.
// tokens_by_handle and tokens_by_device_context index the live endpoints by
// their compatibility handle and device context: both are made with the
// endpoint (acquire configures it) and stay fixed until its release, so the
// indexes are written at acquire and erased at release only (N22).
struct EndpointRegistry final {
    stxlib::Mutex mutex;
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
    const stxlib::ScopedLock lock(endpoints.mutex);
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

// The index's candidate is confirmed against the endpoint's current value
// outside the lock (no endpoint function runs under it): an endpoint the
// host released on its own (its control disposed) reports zero, and GDI
// recycles device-context values, so a stale entry never claims a handle.
std::shared_ptr<Endpoint> endpoint_for_handle(std::uint64_t handle) {
    std::shared_ptr<Endpoint> endpoint;
    {
        EndpointRegistry& endpoints = endpoint_registry();
        const stxlib::ScopedLock lock(endpoints.mutex);
        endpoint = indexed_endpoint_locked(endpoints.tokens_by_handle, handle);
    }
    if (endpoint == nullptr || static_cast<std::uint64_t>((*endpoint).compatibility_handle()) != handle) {
        return nullptr;
    }
    return endpoint;
}

std::shared_ptr<Endpoint> endpoint_for_dc(std::uint64_t device_context) {
    std::shared_ptr<Endpoint> endpoint;
    {
        EndpointRegistry& endpoints = endpoint_registry();
        const stxlib::ScopedLock lock(endpoints.mutex);
        endpoint = indexed_endpoint_locked(endpoints.tokens_by_device_context, device_context);
    }
    if (endpoint == nullptr || static_cast<std::uint64_t>((*endpoint).device_context()) != device_context) {
        return nullptr;
    }
    return endpoint;
}

bool valid_extent(std::uint32_t width, std::uint32_t height) noexcept {
    return width != 0U && height != 0U && width <= GF_PAINT_ENDPOINT_MAXIMUM_SIDE &&
           height <= GF_PAINT_ENDPOINT_MAXIMUM_SIDE &&
           static_cast<std::uint64_t>(width) * height <= maximum_pixels;
}

gf_status endpoint_acquire_body(gf_object control, std::uint32_t width, std::uint32_t height,
                                gf_paint_endpoint* endpoint, std::uint64_t* compatibility_handle) {
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
    const std::uint64_t handle = static_cast<std::uint64_t>((*made).compatibility_handle());
    const std::uint64_t device_context = static_cast<std::uint64_t>((*made).device_context());
    // Every allocation before the surface is bound (A-gf-abi-03-F6): the
    // three map nodes are made in scratch maps, keyed once the token is
    // known, and each map's buckets are reserved, so nothing after the bind
    // can fail. reserve() alone makes buckets, not nodes.
    std::unordered_map<std::uint64_t, EndpointBinding> endpoint_scratch;
    std::unordered_map<std::uint64_t, std::uint64_t> handle_scratch;
    std::unordered_map<std::uint64_t, std::uint64_t> device_context_scratch;
    endpoint_scratch.emplace(0U, EndpointBinding{made, std::weak_ptr<Control>(target), surface});
    handle_scratch.emplace(handle, 0U);
    device_context_scratch.emplace(device_context, 0U);
    using EndpointNode = std::unordered_map<std::uint64_t, EndpointBinding>::node_type;
    using TokenNode = std::unordered_map<std::uint64_t, std::uint64_t>::node_type;
    EndpointNode endpoint_node = endpoint_scratch.extract(endpoint_scratch.begin());
    TokenNode handle_node = handle_scratch.extract(handle_scratch.begin());
    TokenNode device_context_node = device_context_scratch.extract(device_context_scratch.begin());
    EndpointRegistry& endpoints = endpoint_registry();
    {
        const stxlib::ScopedLock lock(endpoints.mutex);
        endpoints.endpoints.reserve(endpoints.endpoints.size() + 1U);
        endpoints.tokens_by_handle.reserve(endpoints.tokens_by_handle.size() + 1U);
        endpoints.tokens_by_device_context.reserve(endpoints.tokens_by_device_context.size() + 1U);
    }
    if (!bind_custom_surface(*target, surface)) {
        (*made).release();
        return fail(GF_STATUS_WRONG_HANDLE_KIND, acquire_slot, "the control is not a custom control");
    }
    const stxlib::ScopedLock lock(endpoints.mutex);
    const std::uint64_t token = endpoints.next_endpoint;
    endpoints.next_endpoint = token + 1U;
    endpoint_node.key() = token;
    static_cast<void>(endpoints.endpoints.insert(std::move(endpoint_node)));
    handle_node.mapped() = token;
    const std::unordered_map<std::uint64_t, std::uint64_t>::insert_return_type by_handle =
        endpoints.tokens_by_handle.insert(std::move(handle_node));
    if (!by_handle.inserted) {
        (*by_handle.position).second = token;
    }
    if (device_context != 0U) {
        device_context_node.mapped() = token;
        const std::unordered_map<std::uint64_t, std::uint64_t>::insert_return_type by_device_context =
            endpoints.tokens_by_device_context.insert(std::move(device_context_node));
        if (!by_device_context.inserted) {
            (*by_device_context.position).second = token;
        }
    }
    *endpoint = token;
    *compatibility_handle = handle;
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_acquire(gf_object control, std::uint32_t width, std::uint32_t height,
                                   gf_paint_endpoint* endpoint, std::uint64_t* compatibility_handle) noexcept {
    return call_slot(acquire_slot, endpoint_acquire_body, control, width, height, endpoint, compatibility_handle);
}

gf_status endpoint_configure_body(gf_paint_endpoint token, std::uint32_t width, std::uint32_t height) {
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
}

gf_status GF_CALL endpoint_configure(gf_paint_endpoint token, std::uint32_t width, std::uint32_t height) noexcept {
    return call_slot(configure_slot, endpoint_configure_body, token, width, height);
}

gf_status endpoint_touch_body(gf_paint_endpoint token, std::uint32_t explicit_boundary) {
    if (explicit_boundary > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, touch_slot, "explicit_boundary is 0 or 1");
    }
    const std::shared_ptr<Endpoint> endpoint = find_endpoint(token);
    if (!endpoint) {
        return fail(GF_STATUS_STALE_HANDLE, touch_slot, stale_endpoint);
    }
    (*endpoint).touch(explicit_boundary == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_touch(gf_paint_endpoint token, std::uint32_t explicit_boundary) noexcept {
    return call_slot(touch_slot, endpoint_touch_body, token, explicit_boundary);
}

gf_status endpoint_drain_body(gf_paint_endpoint token) {
    const std::shared_ptr<Endpoint> endpoint = find_endpoint(token);
    if (!endpoint) {
        return fail(GF_STATUS_STALE_HANDLE, drain_slot, stale_endpoint);
    }
    if (!(*endpoint).drain_now()) {
        return fail(GF_STATUS_WRONG_THREAD, drain_slot, "drain runs on the thread that acquired");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_drain(gf_paint_endpoint token) noexcept {
    return call_slot(drain_slot, endpoint_drain_body, token);
}

gf_status endpoint_snapshot_body(gf_paint_endpoint token, char* buffer, std::uint64_t capacity,
                                 std::uint64_t* required) {
    if (!ic_windows_paint_endpoint::admits_snapshot_buffer(required, buffer, capacity)) {
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
}

gf_status GF_CALL endpoint_snapshot(gf_paint_endpoint token, char* buffer, std::uint64_t capacity,
                                    std::uint64_t* required) noexcept {
    return call_slot(snapshot_slot, endpoint_snapshot_body, token, buffer, capacity, required);
}

gf_status endpoint_submit_bgra_body(std::uint64_t compatibility_handle, std::uint32_t width,
                                    std::uint32_t height, std::uint64_t row_bytes,
                                    const std::uint8_t* pixels) {
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
}

gf_status GF_CALL endpoint_submit_bgra(std::uint64_t compatibility_handle, std::uint32_t width,
                                       std::uint32_t height, std::uint64_t row_bytes,
                                       const std::uint8_t* pixels) noexcept {
    return call_slot(submit_bgra_slot, endpoint_submit_bgra_body,
                     compatibility_handle, width, height, row_bytes, pixels);
}

gf_status endpoint_release_body(gf_paint_endpoint token) {
    EndpointBinding binding;
    {
        EndpointRegistry& endpoints = endpoint_registry();
        const stxlib::ScopedLock lock(endpoints.mutex);
        const std::unordered_map<std::uint64_t, EndpointBinding>::iterator found =
            endpoints.endpoints.find(token);
        if (found == endpoints.endpoints.end()) {
            return fail(GF_STATUS_STALE_HANDLE, release_slot, stale_endpoint);
        }
        binding = std::move((*found).second);
        endpoints.endpoints.erase(found);
    }
    // The endpoint's handle and device context are read outside the
    // registry's lock (no endpoint function runs under it, so release
    // never holds it while waiting on the endpoint's own lock, which a
    // write in flight holds), then their index entries go if they still
    // name this token.
    const std::uint64_t handle = static_cast<std::uint64_t>((*binding.endpoint).compatibility_handle());
    const std::uint64_t device_context = static_cast<std::uint64_t>((*binding.endpoint).device_context());
    {
        EndpointRegistry& endpoints = endpoint_registry();
        const stxlib::ScopedLock lock(endpoints.mutex);
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
    // tested first, without recording a failure: release succeeds from
    // any thread, so it leaves no WRONG_THREAD behind for error_detail
    // (N23); on the UI thread enter_ui then completes deferred releases.
    const Control::Ptr control = binding.control.lock();
    if (control && registry().calling_ui_thread() && registry().enter_ui(release_slot, false) == GF_STATUS_OK &&
        (*control).is_alive()) {
        static_cast<void>(unbind_custom_surface(*control, binding.surface));
    }
    (*binding.endpoint).release();
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_release(gf_paint_endpoint token) noexcept {
    return call_slot(release_slot, endpoint_release_body, token);
}

gf_status endpoint_get_dc_body(std::uint64_t compatibility_handle, std::uint64_t* device_context) {
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
}

gf_status GF_CALL endpoint_get_dc(std::uint64_t compatibility_handle, std::uint64_t* device_context) noexcept {
    return call_slot(get_dc_slot, endpoint_get_dc_body, compatibility_handle, device_context);
}

gf_status endpoint_release_dc_body(std::uint64_t compatibility_handle, std::uint64_t device_context) {
    if (compatibility_handle == 0U || device_context == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, release_dc_slot, "release_dc needs a handle and a device context");
    }
    const std::shared_ptr<Endpoint> endpoint = endpoint_for_handle(compatibility_handle);
    if (!endpoint || static_cast<std::uint64_t>((*endpoint).device_context()) != device_context) {
        return fail(GF_STATUS_STALE_HANDLE, release_dc_slot, not_endpoint_dc);
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_release_dc(std::uint64_t compatibility_handle, std::uint64_t device_context) noexcept {
    return call_slot(release_dc_slot, endpoint_release_dc_body, compatibility_handle, device_context);
}

gf_status endpoint_publish_dc_body(std::uint64_t device_context) {
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
}

gf_status GF_CALL endpoint_publish_dc(std::uint64_t device_context) noexcept {
    return call_slot(publish_dc_slot, endpoint_publish_dc_body, device_context);
}

gf_status endpoint_begin_write_body(std::uint64_t device_context, gf_paint_write_lease* lease) {
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
        const stxlib::ScopedLock lock(endpoints.mutex);
        token = endpoints.next_write;
        endpoints.next_write = token + 1U;
        endpoints.writes.emplace(token, WriteBinding{endpoint, static_cast<std::uintptr_t>(device_context),
                                                   stxlib::current_thread()});
    }
    if (!(*endpoint).begin_device_context_write(static_cast<std::uintptr_t>(device_context))) {
        const stxlib::ScopedLock lock(endpoints.mutex);
        endpoints.writes.erase(token);
        return fail(GF_STATUS_STALE_HANDLE, begin_write_slot, "the endpoint was released before the write began");
    }
    *lease = token;
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_begin_write(std::uint64_t device_context, gf_paint_write_lease* lease) noexcept {
    return call_slot(begin_write_slot, endpoint_begin_write_body, device_context, lease);
}

gf_status endpoint_end_write_body(gf_paint_write_lease lease, std::uint32_t publish) {
    if (lease == 0U || publish > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, end_write_slot, "end_write needs a lease and publish 0 or 1");
    }
    WriteBinding write;
    {
        EndpointRegistry& endpoints = endpoint_registry();
        const stxlib::ScopedLock lock(endpoints.mutex);
        const std::unordered_map<std::uint64_t, WriteBinding>::iterator found = endpoints.writes.find(lease);
        if (found == endpoints.writes.end()) {
            return fail(GF_STATUS_STALE_HANDLE, end_write_slot, "the write lease is not active");
        }
        if ((*found).second.thread != stxlib::current_thread()) {
            return fail(GF_STATUS_WRONG_THREAD, end_write_slot, "a write ends on the thread that began it");
        }
        write = std::move((*found).second);
        endpoints.writes.erase(found);
    }
    if (!(*write.endpoint).end_device_context_write(write.device_context, publish == 1U)) {
        return fail(GF_STATUS_INTERNAL, end_write_slot, "the write could not finish or publish");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL endpoint_end_write(gf_paint_write_lease lease, std::uint32_t publish) noexcept {
    return call_slot(end_write_slot, endpoint_end_write_body, lease, publish);
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
