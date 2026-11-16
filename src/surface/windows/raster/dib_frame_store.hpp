#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>

namespace gui_forms::host::detail {

struct DibFrameKey final {
    std::uint64_t revision{};
    std::uint64_t epoch{};
    double scale{1.0};
};
[[nodiscard]] bool same_frame_key(const DibFrameKey& first, const DibFrameKey& second) noexcept;

enum class DibFrameStatus { success, empty, invalid, busy, allocation_failed,
                            flush_failed, present_failed, wrong_executor };
enum class DibFrameFault { none, allocation, begin_flush, commit_flush, present };

struct DibSurfaceView final {
    HDC dc{};
    int width{};
    int height{};
    std::span<std::uint32_t> pixels{};
};

struct DibFrontView final {
    int width{};
    int height{};
    std::span<const std::uint32_t> pixels{};
};

// Windows-only owner. Every call, borrow and destruction belongs to its
// construction thread. Views expire at begin/commit/abort/close. No GDI storage
// is shared with a worker. Fault points are instance-local test controls.
class DibFrameStore final {
public:
    static constexpr std::size_t pixel_limit = 16'777'216;
    DibFrameStore();
    ~DibFrameStore();
    DibFrameStore(const DibFrameStore&) = delete;
    DibFrameStore& operator=(const DibFrameStore&) = delete;

    [[nodiscard]] DibFrameStatus begin(int width, int height);
    [[nodiscard]] DibSurfaceView candidate() noexcept;
    [[nodiscard]] DibFrameStatus commit(HDC target, const RECT& damage, DibFrameKey key);
    // Partial present (stage DP): copies each rectangle of the candidate to
    // `target`, then makes the candidate the front. The swap stays
    // all-or-nothing: if any copy fails, the rectangles already copied get the
    // old front back (best effort, as for one rectangle), the front is
    // unchanged and the failure is returned. An empty set presents nothing
    // and still commits.
    [[nodiscard]] DibFrameStatus commit(HDC target, std::span<const RECT> damage, DibFrameKey key);
    void abort() noexcept;
    [[nodiscard]] DibFrameStatus expose(HDC target, const RECT& damage) const;
    [[nodiscard]] DibFrontView front() const noexcept;
    [[nodiscard]] DibFrameKey front_key() const noexcept;
    [[nodiscard]] bool has_front() const noexcept;
    void inject_failure(DibFrameFault fault) noexcept;
    void close() noexcept;

private:
    struct Surface final {
        HDC dc{};
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        std::uint32_t* pixels{};
        int width{};
        int height{};
        std::size_t count{};
        Surface() = default;
        ~Surface();
        Surface(const Surface&) = delete;
        Surface& operator=(const Surface&) = delete;
        void swap(Surface& other) noexcept;
        void release() noexcept;
        [[nodiscard]] bool allocate(int requested_width, int requested_height, std::size_t requested_count);
        [[nodiscard]] DibSurfaceView view() const noexcept;
    };
    [[nodiscard]] bool on_executor() const noexcept;
    [[nodiscard]] bool take_failure(DibFrameFault fault) noexcept;
    [[nodiscard]] static DibFrameStatus present_surface(const Surface& source, HDC target, const RECT& damage);
    Surface front_{};
    Surface candidate_{};
    DibFrameKey key_{};
    std::thread::id executor_{};
    DibFrameFault fault_{DibFrameFault::none};
    bool active_{};
    bool closed_{};
};
} // namespace gui_forms::host::detail
