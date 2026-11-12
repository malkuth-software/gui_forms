#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

// Private to the lab library: not an installed header (audit F10). The one
// production caller is CorrespondenceView (lab/src/correspondence_view.cpp);
// the sort lab and its test measure the candidates.
namespace gui_forms::detail {

// Stable insertion is the deliberately small candidate. It is useful when the
// collection is short or already close to order; it does no auxiliary heap
// allocation and moves only the displaced suffix.
template <typename Element, typename Less>
void stable_insertion_sort(std::span<Element> values, const Less& less) {
    for (std::size_t index = 1U; index < values.size(); ++index) {
        Element value = std::move(values[index]);
        std::size_t insertion = index;
        while (insertion != 0U && less(value, values[insertion - 1U])) {
            values[insertion] = std::move(values[insertion - 1U]);
            --insertion;
        }
        values[insertion] = std::move(value);
    }
}

template <typename Element>
struct SortAscending final {
    [[nodiscard]] constexpr bool operator()(const Element& left,
                                            const Element& right) const {
        return left < right;
    }
};

template <typename Element>
void stable_insertion_sort(std::span<Element> values) {
    stable_insertion_sort(values, SortAscending<Element>{});
}

namespace sort_implementation {

struct Run final {
    std::size_t first{};
    std::size_t last{};
};

template <typename Element>
void reverse_range(std::span<Element> values,
                   std::size_t first,
                   std::size_t last) {
    while (first < last) {
        --last;
        if (first >= last) return;
        using std::swap;
        swap(values[first], values[last]);
        ++first;
    }
}

template <typename Element, typename Less>
void merge_runs(std::span<Element> values,
                Run left,
                Run right,
                const Less& less,
                std::vector<Element>& scratch) {
    scratch.clear();
    std::size_t left_index = left.first;
    std::size_t right_index = right.first;
    while (left_index < left.last && right_index < right.last) {
        if (less(values[right_index], values[left_index])) {
            scratch.push_back(std::move(values[right_index]));
            ++right_index;
        } else {
            scratch.push_back(std::move(values[left_index]));
            ++left_index;
        }
    }
    while (left_index < left.last) {
        scratch.push_back(std::move(values[left_index]));
        ++left_index;
    }
    while (right_index < right.last) {
        scratch.push_back(std::move(values[right_index]));
        ++right_index;
    }
    for (std::size_t index = 0U; index < scratch.size(); ++index) {
        values[left.first + index] = std::move(scratch[index]);
    }
}

template <typename Element, typename Less>
std::size_t collect_run(std::span<Element> values,
                        std::size_t first,
                        const Less& less) {
    if (first + 1U >= values.size()) return values.size();
    std::size_t last = first + 2U;
    if (less(values[first + 1U], values[first])) {
        while (last < values.size() &&
               less(values[last], values[last - 1U])) {
            ++last;
        }
        // Descending runs are required to be strict. Reversing them therefore
        // cannot reorder equal keys and preserves the stable-sort contract.
        reverse_range(values, first, last);
    } else {
        while (last < values.size() &&
               !less(values[last], values[last - 1U])) {
            ++last;
        }
    }
    return last;
}

template <typename Element, typename Less>
void sift_down(std::span<Element> values,
               std::size_t first,
               std::size_t count,
               std::size_t root,
               const Less& less) {
    while (root < count / 2U) {
        std::size_t child = root * 2U + 1U;
        const std::size_t right = child + 1U;
        if (right < count &&
            less(values[first + child], values[first + right])) {
            child = right;
        }
        if (!less(values[first + root], values[first + child])) return;
        using std::swap;
        swap(values[first + root], values[first + child]);
        root = child;
    }
}

template <typename Element, typename Less>
void heap_sort_range(std::span<Element> values,
                     std::size_t first,
                     std::size_t last,
                     const Less& less) {
    const std::size_t count = last - first;
    for (std::size_t parent = count / 2U; parent != 0U; --parent) {
        sift_down(values, first, count, parent - 1U, less);
    }
    for (std::size_t remaining = count; remaining > 1U; --remaining) {
        using std::swap;
        swap(values[first], values[first + remaining - 1U]);
        sift_down(values, first, remaining - 1U, 0U, less);
    }
}

template <typename Element, typename Less>
std::size_t partition_range(std::span<Element> values,
                            std::size_t first,
                            std::size_t last,
                            const Less& less) {
    const std::size_t middle = first + (last - first) / 2U;
    const std::size_t final = last - 1U;
    using std::swap;
    if (less(values[middle], values[first])) {
        swap(values[middle], values[first]);
    }
    if (less(values[final], values[middle])) {
        swap(values[final], values[middle]);
    }
    if (less(values[middle], values[first])) {
        swap(values[middle], values[first]);
    }

    const Element pivot = values[middle];
    std::size_t left = first;
    std::size_t right = final;
    for (;;) {
        while (less(values[left], pivot)) ++left;
        while (less(pivot, values[right])) --right;
        if (left >= right) return left;
        swap(values[left], values[right]);
        ++left;
        --right;
    }
}

[[nodiscard]] constexpr std::size_t depth_limit(std::size_t count) {
    std::size_t depth = 0U;
    while (count > 1U) {
        count /= 2U;
        ++depth;
    }
    return depth * 2U;
}

struct Partition final {
    std::size_t first{};
    std::size_t last{};
    std::size_t depth{};
};

} // namespace sort_implementation

// Natural runs are retained and merged pairwise. Strict descending runs are
// reversed, short runs are extended with stable insertion, and one scratch
// allocation is reused for every merge. Already ordered input remains one run.
template <typename Element, typename Less>
void adaptive_stable_sort(std::span<Element> values, const Less& less) {
    constexpr std::size_t minimum_run = 24U;
    if (values.size() < 2U) return;

    std::vector<sort_implementation::Run> runs;
    runs.reserve(values.size() / minimum_run + 1U);
    std::size_t first = 0U;
    while (first < values.size()) {
        std::size_t last =
            sort_implementation::collect_run(values, first, less);
        const std::size_t extended =
            first + minimum_run < values.size()
                ? first + minimum_run
                : values.size();
        if (last < extended) {
            last = extended;
            stable_insertion_sort(values.subspan(first, last - first), less);
        }
        runs.push_back({first, last});
        first = last;
    }
    if (runs.size() == 1U) return;

    std::vector<Element> scratch;
    scratch.reserve(values.size());
    std::vector<sort_implementation::Run> merged;
    merged.reserve((runs.size() + 1U) / 2U);
    while (runs.size() > 1U) {
        merged.clear();
        std::size_t run_index = 0U;
        for (; run_index + 1U < runs.size(); run_index += 2U) {
            const sort_implementation::Run left = runs[run_index];
            const sort_implementation::Run right = runs[run_index + 1U];
            sort_implementation::merge_runs(
                values, left, right, less, scratch);
            merged.push_back({left.first, right.last});
        }
        if (run_index < runs.size()) merged.push_back(runs[run_index]);
        runs.swap(merged);
    }
}

template <typename Element>
void adaptive_stable_sort(std::span<Element> values) {
    adaptive_stable_sort(values, SortAscending<Element>{});
}

// The unstable candidate uses an explicit bounded partition stack. Median-of-
// three partitioning handles the common ordered cases; a depth limit falls back
// to a private heap implementation so adversarial input remains O(n log n).
template <typename Element, typename Less>
void explicit_stack_introsort(std::span<Element> values, const Less& less) {
    constexpr std::size_t insertion_threshold = 24U;
    constexpr std::size_t stack_capacity =
        std::numeric_limits<std::size_t>::digits * 2U + 1U;
    if (values.size() < 2U) return;

    std::array<sort_implementation::Partition, stack_capacity> stack{};
    std::size_t stack_size = 1U;
    stack[0] = {0U, values.size(),
                sort_implementation::depth_limit(values.size())};
    while (stack_size != 0U) {
        sort_implementation::Partition partition = stack[--stack_size];
        while (partition.last - partition.first > insertion_threshold) {
            if (partition.depth == 0U) {
                sort_implementation::heap_sort_range(
                    values, partition.first, partition.last, less);
                partition.first = partition.last;
                break;
            }
            --partition.depth;
            const std::size_t split = sort_implementation::partition_range(
                values, partition.first, partition.last, less);
            if (split == partition.first || split == partition.last) {
                sort_implementation::heap_sort_range(
                    values, partition.first, partition.last, less);
                partition.first = partition.last;
                break;
            }

            const sort_implementation::Partition left{
                partition.first, split, partition.depth};
            const sort_implementation::Partition right{
                split, partition.last, partition.depth};
            if (left.last - left.first < right.last - right.first) {
                stack[stack_size++] = right;
                partition = left;
            } else {
                stack[stack_size++] = left;
                partition = right;
            }
        }
        if (partition.last > partition.first + 1U) {
            stable_insertion_sort(
                values.subspan(partition.first,
                               partition.last - partition.first),
                less);
        }
    }
}

template <typename Element>
void explicit_stack_introsort(std::span<Element> values) {
    explicit_stack_introsort(values, SortAscending<Element>{});
}

} // namespace gui_forms::detail
