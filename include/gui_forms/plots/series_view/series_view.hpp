#pragma once

// Zero-copy series (IMGUI_IMPLOT_STUDY B.3, P1; owner decision 9: copy-in by
// default, leased borrowed views for zero copy). A SeriesView describes a
// caller's strided array (ImPlot's offset and stride, a struct-of-fields
// record array); a SeriesLease states how long the plot may read it.
//
// The lease: its owner (the application) keeps the bytes valid and
// unchanged until it calls release(), which returns only once no reader
// holds a pin, so the owner may free the bytes as soon as release()
// returns. A reader (the plot, once per frame) pins the lease before it
// reads and unpins after; a pin taken after release fails, and the plot
// draws the series as empty and counts the refusal. Pins and release may
// run on different threads.

#include "gui_forms/plots/plot_types/plot_types.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace gui_forms::plots {

// A borrowed strided array of `count` elements of Element: element `i`
// starts at `data + offset_bytes + i * stride_bytes`. The bytes are the
// owner's (see the file comment).
template <SeriesElement Element>
struct SeriesView final {
    const std::byte* data{};
    std::size_t count{};
    std::size_t stride_bytes{sizeof(Element)};
    std::size_t offset_bytes{};
};

// Why a view cannot be read; checked once, up front (validate_series_view).
enum class SeriesViewCheck : std::uint8_t {
    valid,
    // A non-empty view with null data.
    null_data,
    // A stride smaller than the element.
    stride_too_small,
    // The first element is not aligned for Element.
    misaligned,
    // The stride does not keep every element aligned.
    stride_misaligned,
    // offset + (count - 1) * stride + sizeof(Element) overflows.
    size_overflow,
};

template <SeriesElement Element>
[[nodiscard]] SeriesViewCheck validate_series_view(const SeriesView<Element>& view) noexcept {
    if (view.count == 0U) return SeriesViewCheck::valid;
    if (view.data == nullptr) return SeriesViewCheck::null_data;
    if (view.stride_bytes < sizeof(Element)) return SeriesViewCheck::stride_too_small;
    if (view.stride_bytes % alignof(Element) != 0U) return SeriesViewCheck::stride_misaligned;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(view.data);
    if ((address + view.offset_bytes) % alignof(Element) != 0U) return SeriesViewCheck::misaligned;
    const std::size_t maximum = static_cast<std::size_t>(-1);
    const std::size_t last = view.count - 1U;
    if (last != 0U && view.stride_bytes > maximum / last) return SeriesViewCheck::size_overflow;
    const std::size_t span_bytes = last * view.stride_bytes;
    if (span_bytes > maximum - view.offset_bytes - sizeof(Element)) return SeriesViewCheck::size_overflow;
    return SeriesViewCheck::valid;
}

// Element `index` (< count) of a validated view. The bytes are copied out,
// so a view over packed records reads correctly whatever its alignment
// guarantees beyond those validated.
template <SeriesElement Element>
[[nodiscard]] Element series_view_at(const SeriesView<Element>& view, ElementIndex index) noexcept {
    Element element{};
    const std::byte* const source = view.data + view.offset_bytes + index.value * view.stride_bytes;
    std::memcpy(&element, source, sizeof(Element));
    return element;
}

// x and y from two borrowed views, read as a decimation source
// (decimation.hpp's ColumnSamples for owned columns): read only under a
// pin (SeriesPin) of each view's lease.
template <SeriesElement XElement, SeriesElement YElement>
struct ViewSamples final {
    SeriesView<XElement> xs{};
    SeriesView<YElement> ys{};

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t result = xs.count < ys.count ? xs.count : ys.count;
        return result;
    }
    [[nodiscard]] double x(std::size_t index) const noexcept {
        return series_value(series_view_at(xs, ElementIndex{index}));
    }
    [[nodiscard]] double y(std::size_t index) const noexcept {
        return series_value(series_view_at(ys, ElementIndex{index}));
    }
};

// The lease's shared state, between the owner's SeriesLease and the
// readers' handles.
class SeriesLeaseState final {
public:
    // Takes a pin: true when the lease is still held (the reader may read
    // until unpin()); false after release, with no pin taken.
    [[nodiscard]] bool pin() noexcept;
    void unpin() noexcept;
    [[nodiscard]] bool released() const noexcept { return released_.load(std::memory_order_acquire); }
    // Pins refused because the lease was released.
    [[nodiscard]] std::uint64_t refused_pins() const noexcept {
        return refused_.load(std::memory_order_relaxed);
    }

private:
    friend class SeriesLease;
    // Marks the lease released and waits until no pin is held.
    void release() noexcept;

    std::atomic<bool> released_{};
    std::atomic<std::uint32_t> readers_{};
    std::atomic<std::uint64_t> refused_{};
};

// The owner's side. Move-only; destroying a held lease releases it.
class SeriesLease final {
public:
    // A held lease.
    SeriesLease();
    ~SeriesLease();
    SeriesLease(SeriesLease&& other) noexcept;
    SeriesLease& operator=(SeriesLease&& other) noexcept;
    SeriesLease(const SeriesLease&) = delete;
    SeriesLease& operator=(const SeriesLease&) = delete;

    // Ends the lease: once it returns, no reader is reading and none will,
    // and the owner may change or free the bytes. Releasing twice, or a
    // moved-from lease, does nothing.
    void release() noexcept;
    // The state a reader holds (BorrowedSeries); null once moved from.
    [[nodiscard]] std::shared_ptr<SeriesLeaseState> state() const noexcept { return state_; }

private:
    std::shared_ptr<SeriesLeaseState> state_{};
};

// A reader's borrowed series: a validated view and the lease it is read
// under. Copyable (each copy shares the lease).
template <SeriesElement Element>
class BorrowedSeries final {
public:
    BorrowedSeries() = default;
    // A view that does not validate is kept as empty, and check() says
    // why.
    BorrowedSeries(SeriesView<Element> view, const SeriesLease& lease)
        : view_(view), check_(validate_series_view(view)), lease_(lease.state()) {
        if (check_ != SeriesViewCheck::valid) view_ = SeriesView<Element>{};
    }

    [[nodiscard]] SeriesViewCheck check() const noexcept { return check_; }
    [[nodiscard]] const SeriesView<Element>& view() const noexcept { return view_; }
    [[nodiscard]] const std::shared_ptr<SeriesLeaseState>& lease() const noexcept { return lease_; }

private:
    SeriesView<Element> view_{};
    SeriesViewCheck check_{SeriesViewCheck::valid};
    std::shared_ptr<SeriesLeaseState> lease_{};
};

// A pin held for one read of a borrowed series (one frame): `held()` is
// false when the lease was released (read nothing). Unpins when destroyed.
class SeriesPin final {
public:
    explicit SeriesPin(const std::shared_ptr<SeriesLeaseState>& lease) noexcept;
    ~SeriesPin();
    SeriesPin(const SeriesPin&) = delete;
    SeriesPin& operator=(const SeriesPin&) = delete;

    [[nodiscard]] bool held() const noexcept { return held_; }

private:
    SeriesLeaseState* lease_{};
    bool held_{};
};

} // namespace gui_forms::plots
