#pragma once

// SeriesColumn: one bulk coordinate stream of a plot (IMGUI_IMPLOT_STUDY
// B.3, P1). It is a bulk sibling of CollectionCell, which records an edit
// per element and would cost too much at a million points: a column's edits
// (assign, append, write_range, push_overwrite) each record one dirty range
// of sequence numbers and bump one revision.
//
// Shape: `capacity` is reserved once and stated; `length` is the live
// count. A growable column grows only by an explicit reserve(), called
// outside a frame; an edit that would pass the capacity is refused whole. A
// ring never reallocates: push_overwrite drops the oldest elements.
//
// Ownership: the column owns its elements (copy-in). Zero-copy reading of
// a caller's array is a SeriesView borrowed under a SeriesLease
// (series_view.hpp).
//
// Order: a column declared XOrder::monotone verifies, on every edit, that
// its finite values never decrease, and refuses an edit that would break
// it (nothing changes). NaN is a gap: never plotted, and it breaks a line.
//
// Threads: a column belongs to one thread. A live plot's producer writes
// through a SeriesRingChannel (series_ring_channel.hpp), never into a
// column another thread reads.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/plot_types/plot_types.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace gui_forms::plots {

// The element types a column may hold. Kernels are instantiated per type,
// chosen before their loops, and convert to double in one place.
template <typename Element>
concept SeriesElement = std::same_as<Element, double> || std::same_as<Element, float> ||
                        std::same_as<Element, std::int32_t> || std::same_as<Element, std::int64_t>;

// An element as the kernels compute with it. NaN stays NaN; integers
// convert exactly up to 2^53 in magnitude.
template <SeriesElement Element>
[[nodiscard]] constexpr double series_value(Element element) noexcept {
    const double result = static_cast<double>(element);
    return result;
}

// Whether an element is a gap. Integer elements never are.
template <SeriesElement Element>
[[nodiscard]] bool series_gap(Element element) noexcept {
    if constexpr (std::is_floating_point_v<Element>) {
        const bool result = std::isnan(element);
        return result;
    } else {
        static_cast<void>(element);
        return false;
    }
}

// A column's live elements, oldest first: one span, or two for a wrapped
// ring (`first` then `second`). Borrowed: valid until the column's next
// edit or reserve.
template <SeriesElement Element>
struct SeriesSpans final {
    std::span<const Element> first{};
    std::span<const Element> second{};

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t result = first.size() + second.size();
        return result;
    }
    [[nodiscard]] bool empty() const noexcept { return first.empty() && second.empty(); }
    // The element at `index` (< size()).
    [[nodiscard]] Element at(ElementIndex index) const noexcept {
        if (index.value < first.size()) return first[index.value];
        const Element result = second[index.value - first.size()];
        return result;
    }
};

enum class SeriesMode : std::uint8_t { growable, ring };

// What an edit did. A refused edit changes nothing.
enum class SeriesEditOutcome : std::uint8_t {
    applied,
    // The elements would pass a growable column's capacity, or a ring's
    // push is larger than the ring.
    refused_capacity,
    // A monotone column's finite values would decrease.
    refused_order,
    // write_range's first element is past the live length, or the range
    // passes it.
    refused_range,
    // The verb does not apply to the column's mode (push_overwrite on a
    // growable column, append past capacity on a ring).
    refused_mode,
};

// The sequence numbers an edit touched, half open: [first, end).
struct GUI_FORMS_API SeriesDirtyRange final {
    ElementSequence first{};
    ElementSequence end{};

    [[nodiscard]] bool empty() const noexcept { return !(first < end); }
    friend bool operator==(const SeriesDirtyRange& left, const SeriesDirtyRange& right) noexcept {
        return left.first == right.first && left.end == right.end;
    }
};

template <SeriesElement Element>
class SeriesColumn final {
public:
    // An empty growable column of capacity 0, declared unordered.
    SeriesColumn() = default;
    // Reserves `capacity` elements now (std::bad_alloc propagates, nothing
    // made).
    SeriesColumn(SeriesMode mode, std::size_t capacity, XOrder order);

    [[nodiscard]] SeriesMode mode() const noexcept { return mode_; }
    [[nodiscard]] XOrder order() const noexcept { return order_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    // Bumped by every applied edit and by reserve().
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Elements a ring dropped from its front, in total: the sequence number
    // of the oldest live element.
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }
    // Elements ever written: the sequence number the next appended element
    // takes.
    [[nodiscard]] std::uint64_t written() const noexcept { return dropped_ + length_; }
    // Live gap elements (NaN), counted on every edit.
    [[nodiscard]] std::size_t gap_count() const noexcept { return gaps_; }
    // The union of the sequence ranges edited since the last take_dirty().
    [[nodiscard]] SeriesDirtyRange dirty() const noexcept { return dirty_; }
    // Returns dirty() and clears it.
    SeriesDirtyRange take_dirty() noexcept;

    // Growable only: makes room for `capacity` elements, keeping the live
    // ones. Allocates; call outside a frame. A smaller capacity than the
    // live length is refused_capacity; a ring is refused_mode.
    SeriesEditOutcome reserve(std::size_t capacity);
    // Replaces every element with `elements`. A ring keeps the newest
    // `capacity` of them.
    SeriesEditOutcome assign(std::span<const Element> elements);
    // Adds `elements` after the newest. A ring refuses (refused_mode) a
    // block that would overflow it; it takes push_overwrite.
    SeriesEditOutcome append(std::span<const Element> elements);
    // Overwrites the live elements from `first`.
    SeriesEditOutcome write_range(ElementIndex first, std::span<const Element> elements);
    // Ring only: adds `elements`, dropping the oldest as needed; a block
    // larger than the ring keeps its newest `capacity` elements.
    SeriesEditOutcome push_overwrite(std::span<const Element> elements);
    // Removes every element (a ring forgets its dropped count too).
    void clear() noexcept;

    // The live elements, oldest first.
    [[nodiscard]] SeriesSpans<Element> read() const noexcept;

private:
    // Whether `elements`, placed after `previous` (the newest live finite
    // value, NaN for none), keep a monotone column monotone.
    [[nodiscard]] bool keeps_order(double previous, std::span<const Element> elements) const noexcept;
    // The newest live finite value, NaN when there is none.
    [[nodiscard]] double newest_finite() const noexcept;
    // The oldest live finite value at or after `index`, NaN when none.
    [[nodiscard]] double finite_from(std::size_t index) const noexcept;
    [[nodiscard]] double finite_before(std::size_t index) const noexcept;
    [[nodiscard]] static std::size_t count_gaps(std::span<const Element> elements) noexcept;
    // Storage slot of live element `index`.
    [[nodiscard]] std::size_t slot(std::size_t index) const noexcept;
    void mark(std::uint64_t first, std::uint64_t end) noexcept;

    SeriesMode mode_{SeriesMode::growable};
    XOrder order_{XOrder::unordered};
    // Capacity slots; a ring's live elements start at head_ and wrap.
    std::vector<Element> storage_{};
    std::size_t head_{};
    std::size_t length_{};
    std::size_t gaps_{};
    std::uint64_t dropped_{};
    std::uint64_t revision_{};
    SeriesDirtyRange dirty_{};
};

// -- definitions ---------------------------------------------------------------

template <SeriesElement Element>
SeriesColumn<Element>::SeriesColumn(SeriesMode mode, std::size_t capacity, XOrder order)
    : mode_(mode), order_(order), storage_(capacity) {}

template <SeriesElement Element>
SeriesDirtyRange SeriesColumn<Element>::take_dirty() noexcept {
    const SeriesDirtyRange taken = dirty_;
    dirty_ = SeriesDirtyRange{};
    return taken;
}

template <SeriesElement Element>
std::size_t SeriesColumn<Element>::slot(std::size_t index) const noexcept {
    const std::size_t position = head_ + index;
    if (position < storage_.size()) return position;
    const std::size_t wrapped = position - storage_.size();
    return wrapped;
}

template <SeriesElement Element>
void SeriesColumn<Element>::mark(std::uint64_t first, std::uint64_t end) noexcept {
    revision_ = revision_ + 1U;
    if (!(first < end)) return;
    if (dirty_.empty()) {
        dirty_ = SeriesDirtyRange{ElementSequence{first}, ElementSequence{end}};
        return;
    }
    dirty_.first.value = std::min(dirty_.first.value, first);
    dirty_.end.value = std::max(dirty_.end.value, end);
}

template <SeriesElement Element>
std::size_t SeriesColumn<Element>::count_gaps(std::span<const Element> elements) noexcept {
    std::size_t count = 0U;
    if constexpr (std::is_floating_point_v<Element>) {
        for (const Element element : elements) {
            if (std::isnan(element)) count = count + 1U;
        }
    }
    return count;
}

template <SeriesElement Element>
double SeriesColumn<Element>::finite_before(std::size_t index) const noexcept {
    std::size_t position = index;
    while (position > 0U) {
        position = position - 1U;
        const Element element = storage_[slot(position)];
        if (!series_gap(element)) return series_value(element);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

template <SeriesElement Element>
double SeriesColumn<Element>::newest_finite() const noexcept {
    const double result = finite_before(length_);
    return result;
}

template <SeriesElement Element>
double SeriesColumn<Element>::finite_from(std::size_t index) const noexcept {
    for (std::size_t position = index; position < length_; ++position) {
        const Element element = storage_[slot(position)];
        if (!series_gap(element)) return series_value(element);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

template <SeriesElement Element>
bool SeriesColumn<Element>::keeps_order(double previous, std::span<const Element> elements) const noexcept {
    if (order_ != XOrder::monotone) return true;
    double last = previous;
    for (const Element element : elements) {
        if (series_gap(element)) continue;
        const double value = series_value(element);
        if (!std::isnan(last) && value < last) return false;
        last = value;
    }
    return true;
}

template <SeriesElement Element>
SeriesEditOutcome SeriesColumn<Element>::reserve(std::size_t capacity) {
    if (mode_ != SeriesMode::growable) return SeriesEditOutcome::refused_mode;
    if (capacity < length_) return SeriesEditOutcome::refused_capacity;
    if (capacity == storage_.size()) return SeriesEditOutcome::applied;
    // A growable column's live elements always start at slot 0.
    std::vector<Element> grown(capacity);
    std::copy(storage_.begin(), storage_.begin() + static_cast<std::ptrdiff_t>(length_), grown.begin());
    storage_.swap(grown);
    revision_ = revision_ + 1U;
    return SeriesEditOutcome::applied;
}

template <SeriesElement Element>
SeriesEditOutcome SeriesColumn<Element>::assign(std::span<const Element> elements) {
    const std::size_t capacity = storage_.size();
    std::span<const Element> kept = elements;
    if (kept.size() > capacity) {
        if (mode_ == SeriesMode::growable) return SeriesEditOutcome::refused_capacity;
        kept = kept.subspan(kept.size() - capacity);
    }
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (!keeps_order(none, kept)) return SeriesEditOutcome::refused_order;
    const std::size_t previous_length = length_;
    std::copy(kept.begin(), kept.end(), storage_.begin());
    head_ = 0U;
    // A growable column's sequence numbers are its indices, so they start
    // again at 0. A ring's continue: its old elements, and those of the
    // block it did not keep, count as dropped.
    if (mode_ == SeriesMode::ring) dropped_ = dropped_ + length_ + (elements.size() - kept.size());
    const std::uint64_t first = mode_ == SeriesMode::ring ? dropped_ : 0U;
    length_ = kept.size();
    gaps_ = count_gaps(kept);
    const std::uint64_t end = mode_ == SeriesMode::ring
        ? dropped_ + length_
        : static_cast<std::uint64_t>(std::max(previous_length, length_));
    mark(first, end);
    return SeriesEditOutcome::applied;
}

template <SeriesElement Element>
SeriesEditOutcome SeriesColumn<Element>::append(std::span<const Element> elements) {
    if (elements.size() > storage_.size() - length_) {
        if (mode_ == SeriesMode::ring) return SeriesEditOutcome::refused_mode;
        return SeriesEditOutcome::refused_capacity;
    }
    if (!keeps_order(newest_finite(), elements)) return SeriesEditOutcome::refused_order;
    const std::uint64_t first = written();
    for (std::size_t index = 0U; index < elements.size(); ++index) {
        storage_[slot(length_ + index)] = elements[index];
    }
    length_ = length_ + elements.size();
    gaps_ = gaps_ + count_gaps(elements);
    mark(first, written());
    return SeriesEditOutcome::applied;
}

template <SeriesElement Element>
SeriesEditOutcome SeriesColumn<Element>::write_range(ElementIndex first, std::span<const Element> elements) {
    if (first.value > length_ || elements.size() > length_ - first.value) return SeriesEditOutcome::refused_range;
    // Order holds when the block fits between its finite neighbours.
    if (order_ == XOrder::monotone) {
        if (!keeps_order(finite_before(first.value), elements)) return SeriesEditOutcome::refused_order;
        const double after = finite_from(first.value + elements.size());
        double block_last = std::numeric_limits<double>::quiet_NaN();
        for (const Element element : elements) {
            if (!series_gap(element)) block_last = series_value(element);
        }
        if (std::isnan(block_last)) block_last = finite_before(first.value);
        if (!std::isnan(after) && !std::isnan(block_last) && after < block_last) {
            return SeriesEditOutcome::refused_order;
        }
    }
    std::size_t removed_gaps = 0U;
    for (std::size_t index = 0U; index < elements.size(); ++index) {
        Element& stored = storage_[slot(first.value + index)];
        if (series_gap(stored)) removed_gaps = removed_gaps + 1U;
        stored = elements[index];
    }
    gaps_ = gaps_ - removed_gaps + count_gaps(elements);
    const std::uint64_t begin = dropped_ + first.value;
    mark(begin, begin + elements.size());
    return SeriesEditOutcome::applied;
}

template <SeriesElement Element>
SeriesEditOutcome SeriesColumn<Element>::push_overwrite(std::span<const Element> elements) {
    if (mode_ != SeriesMode::ring) return SeriesEditOutcome::refused_mode;
    const std::size_t capacity = storage_.size();
    if (capacity == 0U) return SeriesEditOutcome::refused_capacity;
    std::span<const Element> kept = elements;
    if (kept.size() > capacity) kept = kept.subspan(kept.size() - capacity);
    // Order is checked against the newest live element, even one this push
    // drops: a monotone ring's values never go back (a time axis).
    if (!keeps_order(newest_finite(), kept)) return SeriesEditOutcome::refused_order;
    const std::uint64_t first = written() + (elements.size() - kept.size());
    const std::size_t overflow = length_ + kept.size() > capacity ? length_ + kept.size() - capacity : 0U;
    // Drop the oldest `overflow` elements, counting the gaps they held.
    std::size_t dropped_gaps = 0U;
    for (std::size_t index = 0U; index < overflow; ++index) {
        if (series_gap(storage_[slot(index)])) dropped_gaps = dropped_gaps + 1U;
    }
    head_ = slot(overflow);
    length_ = length_ - overflow;
    dropped_ = dropped_ + overflow + (elements.size() - kept.size());
    for (std::size_t index = 0U; index < kept.size(); ++index) {
        storage_[slot(length_ + index)] = kept[index];
    }
    length_ = length_ + kept.size();
    gaps_ = gaps_ - dropped_gaps + count_gaps(kept);
    mark(first, written());
    return SeriesEditOutcome::applied;
}

template <SeriesElement Element>
void SeriesColumn<Element>::clear() noexcept {
    const std::uint64_t first = dropped_;
    const std::uint64_t end = written();
    head_ = 0U;
    length_ = 0U;
    gaps_ = 0U;
    dropped_ = 0U;
    mark(first, end);
}

template <SeriesElement Element>
SeriesSpans<Element> SeriesColumn<Element>::read() const noexcept {
    SeriesSpans<Element> spans{};
    if (length_ == 0U) return spans;
    const std::span<const Element> all(storage_.data(), storage_.size());
    const std::size_t to_end = storage_.size() - head_;
    if (length_ <= to_end) {
        spans.first = all.subspan(head_, length_);
        return spans;
    }
    spans.first = all.subspan(head_, to_end);
    spans.second = all.subspan(0U, length_ - to_end);
    return spans;
}

} // namespace gui_forms::plots
