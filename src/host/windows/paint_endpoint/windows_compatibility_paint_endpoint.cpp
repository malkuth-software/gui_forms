#include "gui_forms/platform/windows_compatibility_paint_endpoint/windows_compatibility_paint_endpoint.hpp"

#include "gui_forms/live_surface.hpp"

#include "windows_compatibility_paint_endpoint_metrics.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gui_forms::host {
namespace {

struct CompatibilityPaintMetrics final {
    std::atomic<std::uint64_t> active{};
    std::atomic<std::uint64_t> requests{};
    std::atomic<std::uint64_t> commits{};
    std::atomic<std::uint64_t> coalesced{};
    std::atomic<std::uint64_t> drops{};
    std::atomic<std::uint64_t> copy_duration_nanoseconds{};
    std::atomic<std::uint64_t> worst_copy_duration_nanoseconds{};
};

CompatibilityPaintMetrics compatibility_paint_metrics;

void record_compatibility_copy_duration(std::uint64_t duration) noexcept {
    compatibility_paint_metrics.copy_duration_nanoseconds.fetch_add(
        duration, std::memory_order_relaxed);
    std::uint64_t worst =
        compatibility_paint_metrics.worst_copy_duration_nanoseconds.load(
            std::memory_order_relaxed);
    while (duration > worst &&
           !compatibility_paint_metrics.worst_copy_duration_nanoseconds.
               compare_exchange_weak(worst, duration,
                                     std::memory_order_relaxed)) {}
}

std::string compatibility_paint_metrics_snapshot_json() {
    std::ostringstream result;
    result << "{\"active\":"
           << compatibility_paint_metrics.active.load(std::memory_order_relaxed)
           << ",\"requests\":"
           << compatibility_paint_metrics.requests.load(std::memory_order_relaxed)
           << ",\"commits\":"
           << compatibility_paint_metrics.commits.load(std::memory_order_relaxed)
           << ",\"coalesced\":"
           << compatibility_paint_metrics.coalesced.load(std::memory_order_relaxed)
           << ",\"drops\":"
           << compatibility_paint_metrics.drops.load(std::memory_order_relaxed)
           << ",\"copy_duration_nanoseconds\":"
           << compatibility_paint_metrics.copy_duration_nanoseconds.load(
                  std::memory_order_relaxed)
           << ",\"worst_copy_duration_nanoseconds\":"
           << compatibility_paint_metrics.worst_copy_duration_nanoseconds.load(
                  std::memory_order_relaxed)
           << '}';
    return result.str();
}


} // namespace

std::string detail::compatibility_paint_metrics_json() {
    return compatibility_paint_metrics_snapshot_json();
}

struct WindowsCompatibilityPaintEndpoint::Implementation final {
    ~Implementation() { release(); }

    static std::uintptr_t allocate_compatibility_handle() noexcept {
        static std::atomic<std::uint64_t> next{1U};
        for (std::size_t attempt = 0; attempt < 1024U; ++attempt) {
            const std::uint64_t sequence =
                next.fetch_add(1U, std::memory_order_relaxed);
            std::uintptr_t candidate{};
            if constexpr (sizeof(std::uintptr_t) >= sizeof(std::uint64_t)) {
                candidate = static_cast<std::uintptr_t>(
                    0x47460000e7000000ULL |
                    ((sequence & 0x000000ffff000000ULL) << 16U) |
                    (sequence & 0x0000000000ffffffULL));
            } else {
                candidate = static_cast<std::uintptr_t>(
                    0xe7000000UL | (sequence & 0x00ffffffUL));
            }
            // Wine treats many HWND operations as 32-bit USER-handle lookups,
            // so a high 64-bit tag alone is insufficient. Reject any candidate
            // the active host happens to recognize as a native window.
            if (candidate != 0U &&
                IsWindow(reinterpret_cast<HWND>(candidate)) == FALSE) {
                return candidate;
            }
        }
        return 0U;
    }

    // The endpoint is an opaque WinForms Control surface: every published
    // pixel has alpha 255 (copy_opaque forces it), so the presenter copies
    // the frame instead of blending it over what lies beneath.
    static LiveSurfaceDescription opaque_description(std::uint32_t surface_width,
                                                     std::uint32_t surface_height) noexcept {
        LiveSurfaceDescription description{};
        description.width = surface_width;
        description.height = surface_height;
        description.opaque = true;
        return description;
    }

    // Copies the DIB into a write lease of the same size, forcing alpha to
    // 255 in the same pass: conventional GDI drawing does not preserve the
    // alpha byte. Caller holds state_mutex (the DIB is coherent) and
    // surface_publish_mutex (the lease is the only writer). False when the
    // lease's size is not the DIB's.
    bool copy_opaque(LiveSurfaceWriteLease& lease) const noexcept {
        std::span<std::byte> destination = lease.pixels();
        const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
        if (destination.size() != pixel_count * 4U) return false;
        std::uint32_t* target = reinterpret_cast<std::uint32_t*>(destination.data());
        const std::uint32_t* source = pixels;
        for (std::size_t index = 0; index < pixel_count; ++index) {
            target[index] = source[index] | 0xff000000U;
        }
        return true;
    }

    bool create(std::uint32_t requested_width,
                std::uint32_t requested_height) noexcept {
        owner_thread = GetCurrentThreadId();
        width = std::max<std::uint32_t>(1U, requested_width);
        height = std::max<std::uint32_t>(1U, requested_height);
        LiveSurfaceDescription description = opaque_description(width, height);
        description.origin = LiveSurfaceOrigin::compatibility_paint;
        surface = LiveSurface::create(description);
        if (!surface || !create_backing(width, height)) return false;
        compatibility_handle = allocate_compatibility_handle();
        if (compatibility_handle == 0U) return false;
        try {
            publisher_thread =
                std::thread(&Implementation::publisher_loop, this);
        } catch (...) {
            return false;
        }
        metrics_registered = true;
        compatibility_paint_metrics.active.fetch_add(
            1U, std::memory_order_relaxed);
        return true;
    }

    bool create_backing(std::uint32_t requested_width,
                        std::uint32_t requested_height) noexcept {
        if (memory_dc == nullptr) memory_dc = CreateCompatibleDC(nullptr);
        if (memory_dc == nullptr) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(requested_width);
        info.bmiHeader.biHeight = -static_cast<LONG>(requested_height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* next_pixels{};
        HBITMAP next_bitmap = CreateDIBSection(
            memory_dc, &info, DIB_RGB_COLORS, &next_pixels, nullptr, 0);
        if (next_bitmap == nullptr || next_pixels == nullptr) return false;
        HGDIOBJ replaced = SelectObject(memory_dc, next_bitmap);
        if (replaced == nullptr || replaced == HGDI_ERROR) {
            DeleteObject(next_bitmap);
            return false;
        }
        if (bitmap != nullptr) DeleteObject(bitmap);
        else stock_bitmap = replaced;
        bitmap = next_bitmap;
        pixels = static_cast<std::uint32_t*>(next_pixels);
        std::memset(pixels, 0,
                    static_cast<std::size_t>(requested_width) *
                        requested_height * 4U);
        return true;
    }

    bool configure(std::uint32_t requested_width,
                   std::uint32_t requested_height) noexcept {
        if (GetCurrentThreadId() != owner_thread) return false;
        const unsigned int next_width = std::max<std::uint32_t>(1U, requested_width);
        const unsigned int next_height = std::max<std::uint32_t>(1U, requested_height);
        std::scoped_lock lock(state_mutex);
        if (released || compatibility_handle == 0U) return false;
        if (width == next_width && height == next_height) return true;
        std::scoped_lock publish_lock(surface_publish_mutex);
        if (!(*surface).reconfigure(opaque_description(next_width, next_height)) ||
            !create_backing(next_width, next_height)) {
            return false;
        }
        width = next_width;
        height = next_height;
        ++epoch;
        return true;
    }

    bool publish_locked() noexcept {
        if (released || pixels == nullptr || !surface) return false;
        const std::chrono::steady_clock::time_point started =
            std::chrono::steady_clock::now();
        std::scoped_lock publish_lock(surface_publish_mutex);
        LiveSurfaceWriteLease lease = (*surface).try_acquire_write(false);
        if (!lease) return false;
        if (!copy_opaque(lease)) return false;
        const bool result = lease.publish() != 0U;
        record_compatibility_copy_duration(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count()));
        return result;
    }

    bool request_publish_locked(bool explicit_boundary = false) noexcept {
        if (released || pixels == nullptr || !surface) return false;
        ++content_revision;
        ++publish_requests;
        compatibility_paint_metrics.requests.fetch_add(
            1U, std::memory_order_relaxed);
        if (explicit_boundary) explicit_present = true;
        if (publish_requested) {
            ++publish_coalesced;
            compatibility_paint_metrics.coalesced.fetch_add(
                1U, std::memory_order_relaxed);
        }
        publish_requested = true;
        publish_wake.notify_one();
        return true;
    }

    void publisher_loop() noexcept {
        std::unique_lock lock(state_mutex);
        for (;;) {
            while (!publisher_stop && !publish_requested) {
                publish_wake.wait(lock);
            }
            if (publisher_stop) return;

            publish_requested = false;
            const std::chrono::steady_clock::time_point started =
                std::chrono::steady_clock::now();
            // One copy: the DIB straight into the live surface's write lease
            // (alpha forced in the same pass), under state_mutex so the DIB
            // is coherent, then publication after GDI writes may resume. The
            // epoch and revision are read with the copy.
            const std::uint64_t copied_epoch = epoch;
            const std::uint64_t copied_revision = content_revision;
            bool published = false;
            if (!released && pixels != nullptr && surface) {
                std::unique_lock publish_lock(surface_publish_mutex);
                LiveSurfaceWriteLease lease = (*surface).try_acquire_write(false);
                const bool copied = lease && copy_opaque(lease);
                lock.unlock();
                published = copied && lease.publish() != 0U;
            } else {
                lock.unlock();
            }
            const std::uint64_t duration = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count());
            record_compatibility_copy_duration(duration);
            lock.lock();

            if (published && !released && copied_epoch == epoch) {
                published_revision = (std::max)(
                    published_revision, copied_revision);
                ++publish_commits;
                compatibility_paint_metrics.commits.fetch_add(
                    1U, std::memory_order_relaxed);
            } else {
                ++publish_drops;
                compatibility_paint_metrics.drops.fetch_add(
                    1U, std::memory_order_relaxed);
            }
        }
    }

    bool drain() noexcept {
        std::scoped_lock lock(state_mutex);
        if (released) return false;
        publish_requested = false;
        const bool result = publish_locked();
        if (result) {
            published_revision = content_revision;
            ++publish_commits;
            compatibility_paint_metrics.commits.fetch_add(
                1U, std::memory_order_relaxed);
        } else {
            ++publish_drops;
            compatibility_paint_metrics.drops.fetch_add(
                1U, std::memory_order_relaxed);
        }
        return result;
    }

    bool begin_write(std::uintptr_t requested_dc) noexcept {
        if (requested_dc == 0U) return false;
        state_mutex.lock();
        if (released || memory_dc == nullptr ||
            requested_dc != reinterpret_cast<std::uintptr_t>(memory_dc)) {
            state_mutex.unlock();
            return false;
        }
        write_thread = GetCurrentThreadId();
        write_active = true;
        return true;
    }

    bool end_write(std::uintptr_t requested_dc, bool should_publish) noexcept {
        // begin_write and end_write are one synchronous shim call on the same
        // thread. Do not attempt to acquire state_mutex here: this thread owns
        // it until the destination GDI operation and publication are complete.
        if (!write_active || write_thread != GetCurrentThreadId() ||
            requested_dc != reinterpret_cast<std::uintptr_t>(memory_dc)) {
            return false;
        }
        const bool result = !should_publish || request_publish_locked();
        write_active = false;
        write_thread = 0;
        state_mutex.unlock();
        return result;
    }

    bool submit(std::uint32_t submitted_width,
                std::uint32_t submitted_height,
                std::uint64_t submitted_row_bytes,
                std::span<const std::byte> submitted) noexcept {
        if (submitted_width == 0U || submitted_height == 0U ||
            submitted_row_bytes < static_cast<std::uint64_t>(submitted_width) * 4U ||
            submitted.size() < submitted_row_bytes * submitted_height) {
            return false;
        }
        std::scoped_lock lock(state_mutex);
        if (released || pixels == nullptr || submitted_width != width ||
            submitted_height != height) return false;
        std::scoped_lock publish_lock(surface_publish_mutex);
        LiveSurfaceWriteLease lease = (*surface).try_acquire_write(false);
        if (!lease) return false;
        const std::size_t packed_row = static_cast<std::size_t>(width) * 4U;
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(reinterpret_cast<std::byte*>(pixels) + row * packed_row,
                        submitted.data() + row * submitted_row_bytes, packed_row);
            std::memcpy(lease.pixels().data() + row * packed_row,
                        submitted.data() + row * submitted_row_bytes, packed_row);
        }
        ++content_revision;
        ++publish_requests;
        compatibility_paint_metrics.requests.fetch_add(
            1U, std::memory_order_relaxed);
        explicit_present = true;
        const bool result = lease.publish() != 0U;
        if (result) {
            published_revision = content_revision;
            ++publish_commits;
            compatibility_paint_metrics.commits.fetch_add(
                1U, std::memory_order_relaxed);
        } else {
            ++publish_drops;
            compatibility_paint_metrics.drops.fetch_add(
                1U, std::memory_order_relaxed);
        }
        return result;
    }

    std::string snapshot() const {
        std::scoped_lock lock(state_mutex);
        const char* state = released || compatibility_handle == 0U
            ? "retired" : "live";
        std::ostringstream result;
        result << "state:" << state
               << "|content:" << content_revision
               << "|published:" << published_revision
               << "|publish-requests:" << publish_requests
               << "|publish-commits:" << publish_commits
               << "|publish-coalesced:" << publish_coalesced
               << "|publish-drops:" << publish_drops
               << "|epoch:" << epoch
               << "|size:" << width << 'x' << height
               << "|transport:virtual-handle-memory-dc"
               << "|explicit:" << (explicit_present ? 1 : 0);
        return result.str();
    }

    void release() noexcept {
        {
            std::scoped_lock lock(state_mutex);
            if (released) return;
            released = true;
            compatibility_handle = 0U;
            publisher_stop = true;
            publish_wake.notify_all();
        }
        if (publisher_thread.joinable() &&
            publisher_thread.get_id() != std::this_thread::get_id()) {
            publisher_thread.join();
        }
        std::scoped_lock lock(state_mutex);
        if (metrics_registered) {
            metrics_registered = false;
            compatibility_paint_metrics.active.fetch_sub(
                1U, std::memory_order_relaxed);
        }
        if (memory_dc != nullptr && stock_bitmap != nullptr) {
            SelectObject(memory_dc, stock_bitmap);
        }
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memory_dc != nullptr) DeleteDC(memory_dc);
        bitmap = nullptr;
        memory_dc = nullptr;
        stock_bitmap = nullptr;
        pixels = nullptr;
        surface.reset();
    }

    mutable std::mutex state_mutex;
    std::mutex surface_publish_mutex;
    std::condition_variable publish_wake;
    std::thread publisher_thread;
    std::uintptr_t compatibility_handle{};
    HDC memory_dc{};
    HBITMAP bitmap{};
    HGDIOBJ stock_bitmap{};
    std::uint32_t* pixels{};
    std::shared_ptr<LiveSurface> surface;
    DWORD owner_thread{};
    std::uint32_t width{1};
    std::uint32_t height{1};
    std::uint64_t epoch{1};
    std::uint64_t content_revision{};
    std::uint64_t published_revision{};
    std::uint64_t publish_requests{};
    std::uint64_t publish_commits{};
    std::uint64_t publish_coalesced{};
    std::uint64_t publish_drops{};
    bool explicit_present{};
    bool publish_requested{};
    bool publisher_stop{};
    bool metrics_registered{};
    bool released{};
    bool write_active{};
    DWORD write_thread{};
};

WindowsCompatibilityPaintEndpoint::WindowsCompatibilityPaintEndpoint(
    std::unique_ptr<Implementation> implementation) noexcept
    : implementation_(std::move(implementation)) {}

WindowsCompatibilityPaintEndpoint::~WindowsCompatibilityPaintEndpoint() {
    release();
}

std::shared_ptr<WindowsCompatibilityPaintEndpoint>
WindowsCompatibilityPaintEndpoint::acquire(
    std::uint32_t width, std::uint32_t height) {
    std::unique_ptr<Implementation> implementation =
        std::make_unique<Implementation>();
    if (!(*implementation).create(width, height)) return {};
    return std::shared_ptr<WindowsCompatibilityPaintEndpoint>(
        new WindowsCompatibilityPaintEndpoint(std::move(implementation)));
}

std::uintptr_t WindowsCompatibilityPaintEndpoint::compatibility_handle() const noexcept {
    if (!implementation_) return 0;
    std::scoped_lock lock((*implementation_).state_mutex);
    return (*implementation_).compatibility_handle;
}

std::uintptr_t WindowsCompatibilityPaintEndpoint::device_context() const noexcept {
    if (!implementation_) return 0;
    std::scoped_lock lock((*implementation_).state_mutex);
    return reinterpret_cast<std::uintptr_t>((*implementation_).memory_dc);
}

std::shared_ptr<LiveSurface>
WindowsCompatibilityPaintEndpoint::live_surface() const noexcept {
    if (!implementation_) return {};
    std::scoped_lock lock((*implementation_).state_mutex);
    return (*implementation_).surface;
}

bool WindowsCompatibilityPaintEndpoint::publish_device_context(
    std::uintptr_t device_context) noexcept {
    if (!implementation_ || device_context == 0U) return false;
    std::scoped_lock lock((*implementation_).state_mutex);
    if (device_context !=
        reinterpret_cast<std::uintptr_t>((*implementation_).memory_dc)) {
        return false;
    }
    (*implementation_).explicit_present = true;
    ++(*implementation_).content_revision;
    ++(*implementation_).publish_requests;
    compatibility_paint_metrics.requests.fetch_add(
        1U, std::memory_order_relaxed);
    (*implementation_).publish_requested = false;
    const bool result = (*implementation_).publish_locked();
    if (result) {
        (*implementation_).published_revision =
            (*implementation_).content_revision;
        ++(*implementation_).publish_commits;
        compatibility_paint_metrics.commits.fetch_add(
            1U, std::memory_order_relaxed);
    } else {
        ++(*implementation_).publish_drops;
        compatibility_paint_metrics.drops.fetch_add(
            1U, std::memory_order_relaxed);
    }
    return result;
}

bool WindowsCompatibilityPaintEndpoint::begin_device_context_write(
    std::uintptr_t device_context) noexcept {
    return implementation_ && (*implementation_).begin_write(device_context);
}

bool WindowsCompatibilityPaintEndpoint::end_device_context_write(
    std::uintptr_t device_context, bool publish) noexcept {
    return implementation_ &&
        (*implementation_).end_write(device_context, publish);
}

bool WindowsCompatibilityPaintEndpoint::configure(
    std::uint32_t width, std::uint32_t height) noexcept {
    return implementation_ && (*implementation_).configure(width, height);
}

bool WindowsCompatibilityPaintEndpoint::submit_bgra32_premultiplied(
    std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
    std::span<const std::byte> pixels) noexcept {
    return implementation_ && (*implementation_).submit(
        width, height, row_bytes, pixels);
}

void WindowsCompatibilityPaintEndpoint::touch(bool explicit_boundary) noexcept {
    if (implementation_) {
        std::scoped_lock lock((*implementation_).state_mutex);
        static_cast<void>(
            (*implementation_).request_publish_locked(explicit_boundary));
    }
}

bool WindowsCompatibilityPaintEndpoint::drain_now() noexcept {
    if (!implementation_ ||
        GetCurrentThreadId() != (*implementation_).owner_thread) return false;
    return (*implementation_).drain();
}

std::string WindowsCompatibilityPaintEndpoint::snapshot() const {
    return implementation_ ? (*implementation_).snapshot() : "state:retired";
}

void WindowsCompatibilityPaintEndpoint::release() noexcept {
    if (implementation_) (*implementation_).release();
}


} // namespace gui_forms::host
