#include "../lab/src/sort.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

struct IntegerLess final {
    bool operator()(int left, int right) const noexcept { return left < right; }
};

struct StableRecord final {
    int key{};
    std::size_t sequence{};
};

struct StableRecordLess final {
    bool operator()(const StableRecord& left,
                    const StableRecord& right) const noexcept {
        return left.key < right.key;
    }
};

struct StringLess final {
    bool operator()(const std::string& left,
                    const std::string& right) const noexcept {
        return left < right;
    }
};

class StandardUnstableSort final {
public:
    template <typename Element, typename Less>
    void operator()(std::span<Element> values, const Less& less) const {
        std::sort(values.begin(), values.end(), less);
    }
};

class StandardStableSort final {
public:
    template <typename Element, typename Less>
    void operator()(std::span<Element> values, const Less& less) const {
        std::stable_sort(values.begin(), values.end(), less);
    }
};

class HouseInsertionSort final {
public:
    template <typename Element, typename Less>
    void operator()(std::span<Element> values, const Less& less) const {
        gui_forms::detail::stable_insertion_sort(values, less);
    }
};

class HouseAdaptiveSort final {
public:
    template <typename Element, typename Less>
    void operator()(std::span<Element> values, const Less& less) const {
        gui_forms::detail::adaptive_stable_sort(values, less);
    }
};

class HouseIntrosort final {
public:
    template <typename Element, typename Less>
    void operator()(std::span<Element> values, const Less& less) const {
        gui_forms::detail::explicit_stack_introsort(values, less);
    }
};

struct IntegerChecksum final {
    std::uint64_t operator()(const std::vector<int>& values) const noexcept {
        std::uint64_t result = 1469598103934665603ULL;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            result ^= static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(values[index]));
            result *= 1099511628211ULL;
        }
        return result;
    }
};

struct RecordChecksum final {
    std::uint64_t operator()(
        const std::vector<StableRecord>& values) const noexcept {
        std::uint64_t result = 1469598103934665603ULL;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            result ^= static_cast<std::uint64_t>(values[index].key) +
                      values[index].sequence * 257U;
            result *= 1099511628211ULL;
        }
        return result;
    }
};

struct StringChecksum final {
    std::uint64_t operator()(
        const std::vector<std::string>& values) const noexcept {
        std::uint64_t result = 1469598103934665603ULL;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            const std::string& value = values[index];
            for (std::size_t byte = 0U; byte < value.size(); ++byte) {
                result ^= static_cast<unsigned char>(value[byte]);
                result *= 1099511628211ULL;
            }
        }
        return result;
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

template <typename Element,
          typename Less,
          typename Sorter,
          typename Checksum>
void measure(const char* case_name,
             const char* stability,
             const char* algorithm,
             const std::vector<Element>& source,
             std::size_t iterations,
             const Less& less,
             const Sorter& sorter,
             const Checksum& checksum) {
    constexpr std::size_t sample_count = 5U;
    std::chrono::nanoseconds best = std::chrono::nanoseconds::max();
    std::uint64_t final_checksum{};
    for (std::size_t sample = 0U; sample < sample_count; ++sample) {
        std::vector<std::vector<Element>> batches(iterations, source);
        const std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
            sorter(std::span<Element>(batches[iteration]), less);
        }
        const std::chrono::steady_clock::time_point finish =
            std::chrono::steady_clock::now();
        const std::chrono::nanoseconds elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start);
        if (elapsed < best) best = elapsed;
        final_checksum ^= checksum(batches[sample % iterations]);
    }
    const double per_sort =
        static_cast<double>(best.count()) / static_cast<double>(iterations);
    std::cout << case_name << ',' << source.size() << ',' << stability << ','
              << algorithm << ',' << best.count() << ',' << iterations << ','
              << std::fixed << std::setprecision(2) << per_sort << ','
              << final_checksum << '\n';
}

std::vector<int> make_integer_case(std::size_t size,
                                   std::uint32_t modulus,
                                   std::size_t disorder_stride) {
    std::vector<int> values;
    values.reserve(size);
    DeterministicGenerator generator(
        static_cast<std::uint32_t>(size * 73U + modulus));
    for (std::size_t index = 0U; index < size; ++index) {
        values.push_back(static_cast<int>(generator.next() % modulus));
    }
    std::sort(values.begin(), values.end());
    if (disorder_stride != 0U) {
        for (std::size_t index = disorder_stride;
             index + 1U < values.size();
             index += disorder_stride) {
            using std::swap;
            swap(values[index], values[index + 1U]);
        }
    }
    return values;
}

std::vector<int> make_random_integer_case(std::size_t size,
                                          std::uint32_t modulus) {
    std::vector<int> values;
    values.reserve(size);
    DeterministicGenerator generator(
        static_cast<std::uint32_t>(size * 193U + modulus));
    for (std::size_t index = 0U; index < size; ++index) {
        values.push_back(static_cast<int>(generator.next() % modulus));
    }
    return values;
}

std::vector<StableRecord> make_stable_case(std::size_t size,
                                           std::size_t key_count,
                                           std::size_t run_length) {
    std::vector<StableRecord> values;
    values.reserve(size);
    for (std::size_t index = 0U; index < size; ++index) {
        const std::size_t run = index / run_length;
        const std::size_t in_run = index % run_length;
        values.push_back({static_cast<int>((in_run + run * 3U) % key_count),
                          index});
    }
    return values;
}

std::vector<std::string> make_string_case(std::size_t size) {
    std::vector<std::string> values;
    values.reserve(size);
    DeterministicGenerator generator(static_cast<std::uint32_t>(size * 997U));
    for (std::size_t index = 0U; index < size; ++index) {
        const std::uint32_t value = generator.next();
        values.push_back("control." + std::to_string(value % 41U) + "." +
                         std::to_string(value));
    }
    return values;
}

template <typename Element, typename Less, typename Checksum>
void measure_unstable_family(const char* case_name,
                             const std::vector<Element>& source,
                             std::size_t iterations,
                             const Less& less,
                             const Checksum& checksum) {
    measure(case_name, "unstable", "std_sort", source, iterations, less,
            StandardUnstableSort{}, checksum);
    measure(case_name, "unstable", "house_insertion", source, iterations,
            less, HouseInsertionSort{}, checksum);
    measure(case_name, "unstable", "house_introsort", source, iterations,
            less, HouseIntrosort{}, checksum);
}

template <typename Element, typename Less, typename Checksum>
void measure_stable_family(const char* case_name,
                           const std::vector<Element>& source,
                           std::size_t iterations,
                           const Less& less,
                           const Checksum& checksum) {
    measure(case_name, "stable", "std_stable_sort", source, iterations, less,
            StandardStableSort{}, checksum);
    measure(case_name, "stable", "house_insertion", source, iterations, less,
            HouseInsertionSort{}, checksum);
    measure(case_name, "stable", "house_adaptive", source, iterations, less,
            HouseAdaptiveSort{}, checksum);
}

} // namespace

int main() {
    std::cout << "case,size,stability,algorithm,best_total_ns,iterations,"
                 "ns_per_sort,checksum\n";
    measure_unstable_family("correspondence-three",
                            make_integer_case(3U, 17U, 0U),
                            120000U, IntegerLess{}, IntegerChecksum{});
    measure_unstable_family("selection-nearly-ordered",
                            make_integer_case(16U, 29U, 5U),
                            50000U, IntegerLess{}, IntegerChecksum{});
    measure_unstable_family("selection-moderate",
                            make_integer_case(64U, 97U, 7U),
                            16000U, IntegerLess{}, IntegerChecksum{});
    measure_unstable_family("selection-random",
                            make_random_integer_case(64U, 97U),
                            16000U, IntegerLess{}, IntegerChecksum{});
    measure_unstable_family("damage-duplicate-edges",
                            make_integer_case(128U, 31U, 0U),
                            7000U, IntegerLess{}, IntegerChecksum{});
    measure_unstable_family("damage-random-edges",
                            make_random_integer_case(512U, 65521U),
                            1200U, IntegerLess{}, IntegerChecksum{});
    measure_unstable_family("stable-id-strings",
                            make_string_case(32U),
                            10000U, StringLess{}, StringChecksum{});

    measure_stable_family("font-face-tiers",
                          make_stable_case(12U, 3U, 4U),
                          70000U, StableRecordLess{}, RecordChecksum{});
    measure_stable_family("tab-order-small",
                          make_stable_case(32U, 12U, 8U),
                          22000U, StableRecordLess{}, RecordChecksum{});
    measure_stable_family("tab-order-moderate",
                          make_stable_case(256U, 48U, 32U),
                          2500U, StableRecordLess{}, RecordChecksum{});
    return 0;
}
