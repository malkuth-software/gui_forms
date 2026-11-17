#include "dib_frame_store.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

namespace gui_forms::host::detail {

static_assert(sizeof(std::uint32_t) == 4);
static_assert(DibFrameStore::pixel_limit <= std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t));
static_assert(DibFrameStore::pixel_limit <= static_cast<std::size_t>(std::numeric_limits<LONG>::max()));
// Spell the SDK's sentinel through its signed pointer-width representation;
// MinGW's HGDI_ERROR macro narrows an unsigned 32-bit literal through LONG.
const HGDIOBJ invalid_gdi_object = reinterpret_cast<HGDIOBJ>(static_cast<INT_PTR>(-1));

bool same_frame_key(const DibFrameKey& first, const DibFrameKey& second) noexcept {
    const bool same = first.revision == second.revision && first.epoch == second.epoch &&
        first.scale == second.scale;
    return same;
}

DibFrameStore::Surface::~Surface() { release(); }
void DibFrameStore::Surface::swap(Surface& other) noexcept {
    std::swap(dc, other.dc);
    std::swap(bitmap, other.bitmap);
    std::swap(previous, other.previous);
    std::swap(pixels, other.pixels);
    std::swap(width, other.width);
    std::swap(height, other.height);
    std::swap(count, other.count);
}
void DibFrameStore::Surface::release() noexcept {
    // Flush drains the owner thread's pending batch even when it reports an
    // operation failure. No views may survive release; deselect before delete.
    if (dc != nullptr) {
        static_cast<void>(GdiFlush());
        if (previous != nullptr && previous != invalid_gdi_object) { SelectObject(dc, previous); }
    }
    if (bitmap != nullptr) { DeleteObject(bitmap); }
    if (dc != nullptr) { DeleteDC(dc); }
    dc = nullptr;
    bitmap = nullptr;
    previous = nullptr;
    pixels = nullptr;
    width = 0;
    height = 0;
    count = 0;
}
bool DibFrameStore::Surface::allocate(int requested_width, int requested_height, std::size_t requested_count) {
    dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) { return false; }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = requested_width;
    info.bmiHeader.biHeight = -requested_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* storage = nullptr;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
    if (bitmap == nullptr || storage == nullptr) { return false; }
    previous = SelectObject(dc, bitmap);
    if (previous == nullptr || previous == invalid_gdi_object) { return false; }
    pixels = static_cast<std::uint32_t*>(storage);
    width = requested_width;
    height = requested_height;
    count = requested_count;
    return true;
}
DibSurfaceView DibFrameStore::Surface::view() const noexcept {
    const DibSurfaceView result{dc, width, height, std::span<std::uint32_t>(pixels, count)};
    return result;
}

DibFrameStore::DibFrameStore() : executor_(std::this_thread::get_id()) {}
DibFrameStore::~DibFrameStore() { close(); }
bool DibFrameStore::on_executor() const noexcept {
    const bool current = std::this_thread::get_id() == executor_;
    return current;
}
bool DibFrameStore::take_failure(DibFrameFault fault) noexcept {
    if (fault_ != fault) { return false; }
    fault_ = DibFrameFault::none;
    return true;
}

DibFrameStatus DibFrameStore::begin(int width, int height) {
    if (!on_executor()) { return DibFrameStatus::wrong_executor; }
    if (closed_) { return DibFrameStatus::invalid; }
    if (active_) { return DibFrameStatus::busy; }
    if (width < 0 || height < 0) { return DibFrameStatus::invalid; }
    if (width == 0 || height == 0) { return DibFrameStatus::empty; }
    const std::size_t columns = static_cast<std::size_t>(width);
    const std::size_t rows = static_cast<std::size_t>(height);
    if (columns > pixel_limit / rows) { return DibFrameStatus::invalid; }
    const std::size_t count = columns * rows;
    const BOOL flushed = GdiFlush();
    if (take_failure(DibFrameFault::begin_flush) || flushed == FALSE) { return DibFrameStatus::flush_failed; }
    if (candidate_.width != width || candidate_.height != height) {
        if (take_failure(DibFrameFault::allocation)) { return DibFrameStatus::allocation_failed; }
        Surface replacement{};
        const bool allocated = replacement.allocate(width, height, count);
        if (!allocated) { return DibFrameStatus::allocation_failed; }
        candidate_.swap(replacement);
    }
    // Preserve committed pixels for partial damage. New resize extents are
    // initialized, never stretched/relabelled as the old frame's scale/key.
    std::fill_n(candidate_.pixels, count, 0U);
    if (front_.pixels != nullptr) {
        const int copied_width = std::min(width, front_.width);
        const int copied_height = std::min(height, front_.height);
        const std::size_t copy_bytes = static_cast<std::size_t>(copied_width) * sizeof(std::uint32_t);
        const std::size_t front_stride = static_cast<std::size_t>(front_.width);
        for (int row = 0; row < copied_height; ++row) {
            const std::size_t source = static_cast<std::size_t>(row) * front_stride;
            const std::size_t destination = static_cast<std::size_t>(row) * columns;
            std::memcpy(candidate_.pixels + destination, front_.pixels + source, copy_bytes);
        }
    }
    active_ = true;
    return DibFrameStatus::success;
}

DibSurfaceView DibFrameStore::candidate() noexcept {
    if (!on_executor() || !active_) { return {}; }
    const DibSurfaceView result = candidate_.view();
    return result;
}
DibFrameStatus DibFrameStore::present_surface(const Surface& source, HDC target, const RECT& damage) {
    if (source.dc == nullptr || target == nullptr) { return DibFrameStatus::present_failed; }
    const LONG left = std::clamp<LONG>(damage.left, 0, source.width);
    const LONG top = std::clamp<LONG>(damage.top, 0, source.height);
    const LONG right = std::clamp<LONG>(damage.right, left, source.width);
    const LONG bottom = std::clamp<LONG>(damage.bottom, top, source.height);
    if (right == left || bottom == top) { return DibFrameStatus::success; }
    const int width = static_cast<int>(right - left);
    const int height = static_cast<int>(bottom - top);
    const BOOL copied = BitBlt(target, left, top, width, height, source.dc, left, top, SRCCOPY);
    const BOOL flushed = GdiFlush();
    if (copied == FALSE || flushed == FALSE) { return DibFrameStatus::present_failed; }
    return DibFrameStatus::success;
}

DibFrameStatus DibFrameStore::commit(HDC target, const RECT& damage, DibFrameKey key) {
    const std::span<const RECT> single(&damage, 1U);
    return commit(target, single, key);
}

DibFrameStatus DibFrameStore::commit(HDC target, std::span<const RECT> damage, DibFrameKey key) {
    if (!on_executor()) { return DibFrameStatus::wrong_executor; }
    if (!active_ || key.revision == 0 || key.epoch == 0 || !std::isfinite(key.scale) || key.scale <= 0) {
        return DibFrameStatus::invalid;
    }
    const BOOL flushed = GdiFlush();
    if (take_failure(DibFrameFault::commit_flush) || flushed == FALSE) {
        active_ = false;
        return DibFrameStatus::flush_failed;
    }
    DibFrameStatus presented = DibFrameStatus::success;
    std::size_t copied = 0;
    if (take_failure(DibFrameFault::present)) {
        presented = DibFrameStatus::present_failed;
    } else if (target == nullptr) {
        presented = DibFrameStatus::present_failed;
    }
    while (presented == DibFrameStatus::success && copied < damage.size()) {
        presented = present_surface(candidate_, target, damage[copied]);
        ++copied;
    }
    active_ = false;
    if (presented != DibFrameStatus::success) {
        // Best-effort recovery of visible pixels over every rectangle this
        // commit touched; never claim OS presentation failure left the screen
        // untouched. The committed backing stays exact.
        const std::size_t touched = copied == 0 ? damage.size() : copied;
        for (std::size_t index = 0; index < touched; ++index) {
            static_cast<void>(present_surface(front_, target, damage[index]));
        }
        return presented;
    }
    front_.swap(candidate_);
    key_ = key;
    return DibFrameStatus::success;
}
void DibFrameStore::abort() noexcept {
    if (!on_executor()) { return; }
    static_cast<void>(GdiFlush());
    active_ = false;
}
DibFrameStatus DibFrameStore::expose(HDC target, const RECT& damage) const {
    if (!on_executor()) { return DibFrameStatus::wrong_executor; }
    const DibFrameStatus status = present_surface(front_, target, damage);
    return status;
}
DibFrontView DibFrameStore::front() const noexcept {
    if (!on_executor()) { return {}; }
    const DibFrontView result{front_.width, front_.height,
        std::span<const std::uint32_t>(front_.pixels, front_.count)};
    return result;
}
DibFrameKey DibFrameStore::front_key() const noexcept { return key_; }
bool DibFrameStore::has_front() const noexcept { return front_.pixels != nullptr; }
void DibFrameStore::inject_failure(DibFrameFault fault) noexcept { fault_ = fault; }
void DibFrameStore::close() noexcept {
    if (!on_executor()) { std::terminate(); }
    abort();
    candidate_.release();
    front_.release();
    key_ = {};
    closed_ = true;
}
} // namespace gui_forms::host::detail
