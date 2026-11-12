#include "../src/core/algorithm/sort.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct StableRecord final {
    int key{};
    std::size_t sequence{};

    bool operator==(const StableRecord& other) const noexcept {
        return key == other.key && sequence == other.sequence;
    }
};

struct StableRecordLess final {
    bool operator()(const StableRecord& left,
                    const StableRecord& right) const noexcept {
        return left.key < right.key;
    }
};

class DeterministicGenerator final {
public:
    explicit DeterministicGenerator(std::uint32_t state) noexcept
        : state_(state) {}

    std::uint32_t next() noexcept {
        state_ = state_ * 1664525U + 1013904223U;
        return state_;
    }

private:
    std::uint32_t state_{};
};

std::vector<int> make_values(std::size_t size, std::size_t pattern) {
    std::vector<int> values;
    values.reserve(size);
    DeterministicGenerator generator(
        static_cast<std::uint32_t>(size * 31U + pattern * 101U + 7U));
    for (std::size_t index = 0U; index < size; ++index) {
        int value{};
        if (pattern == 0U) {
            value = static_cast<int>(index);
        } else if (pattern == 1U) {
            value = static_cast<int>(size - index);
        } else if (pattern == 2U) {
            value = static_cast<int>(index % 7U);
        } else if (pattern == 3U) {
            value = static_cast<int>(generator.next() % 29U);
        } else {
            value = static_cast<int>(generator.next());
        }
        values.push_back(value);
    }
    if (pattern == 0U && size > 8U) {
        using std::swap;
        swap(values[size / 3U], values[size / 3U + 1U]);
        swap(values[size * 2U / 3U], values[size * 2U / 3U - 1U]);
    }
    return values;
}

void test_unstable_equivalence() {
    for (std::size_t size = 0U; size <= 513U; ++size) {
        for (std::size_t pattern = 0U; pattern < 5U; ++pattern) {
            const std::vector<int> source = make_values(size, pattern);
            std::vector<int> expected = source;
            std::vector<int> insertion = source;
            std::vector<int> introspective = source;
            std::sort(expected.begin(), expected.end());
            gui_forms::detail::stable_insertion_sort(
                std::span<int>(insertion));
            gui_forms::detail::explicit_stack_introsort(
                std::span<int>(introspective));
            require(insertion == expected,
                    "stable insertion must match std::sort values");
            require(introspective == expected,
                    "explicit-stack introsort must match std::sort values");
        }
    }
}

void test_adaptive_stability() {
    for (std::size_t size = 0U; size <= 513U; ++size) {
        std::vector<StableRecord> source;
        source.reserve(size);
        for (std::size_t index = 0U; index < size; ++index) {
            source.push_back(
                {static_cast<int>((index * 17U + size) % 11U), index});
        }
        std::vector<StableRecord> expected = source;
        std::vector<StableRecord> insertion = source;
        std::vector<StableRecord> adaptive = source;
        std::stable_sort(expected.begin(), expected.end(), StableRecordLess{});
        gui_forms::detail::stable_insertion_sort(
            std::span<StableRecord>(insertion), StableRecordLess{});
        gui_forms::detail::adaptive_stable_sort(
            std::span<StableRecord>(adaptive), StableRecordLess{});
        require(insertion == expected,
                "stable insertion must preserve equal-key order");
        require(adaptive == expected,
                "adaptive sort must preserve equal-key order");
    }
}

void test_descending_equal_key_boundary() {
    std::vector<StableRecord> values{
        {5, 0}, {4, 1}, {4, 2}, {3, 3}, {2, 4}, {2, 5}, {1, 6}};
    std::vector<StableRecord> expected = values;
    std::stable_sort(expected.begin(), expected.end(), StableRecordLess{});
    gui_forms::detail::adaptive_stable_sort(
        std::span<StableRecord>(values), StableRecordLess{});
    require(values == expected,
            "descending run reversal must not reverse equal keys");
}

} // namespace

int main() {
    try {
        test_unstable_equivalence();
        test_adaptive_stability();
        test_descending_equal_key_boundary();
        std::cout << "sort algorithm tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sort algorithm test failure: " << error.what() << '\n';
        return 1;
    }
}
