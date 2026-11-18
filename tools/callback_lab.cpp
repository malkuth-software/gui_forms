#include "gui_forms/event.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>

namespace allocation_probe {

std::size_t allocations{};
bool enabled{};

void begin() noexcept {
    allocations = 0U;
    enabled = true;
}

std::size_t finish() noexcept {
    enabled = false;
    return allocations;
}

} // namespace allocation_probe

void* operator new(std::size_t size) {
    if (allocation_probe::enabled) ++allocation_probe::allocations;
    void* memory = std::malloc(size);
    if (memory == nullptr) throw std::bad_alloc();
    return memory;
}

void* operator new[](std::size_t size) {
    if (allocation_probe::enabled) ++allocation_probe::allocations;
    void* memory = std::malloc(size);
    if (memory == nullptr) throw std::bad_alloc();
    return memory;
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

class CallbackTarget final {
public:
    void add(int value) noexcept {
        total_.fetch_add(static_cast<std::uint64_t>(value),
                         std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t total() const noexcept {
        return total_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> total_{};
};

class SmallOwningCallback final {
public:
    explicit SmallOwningCallback(CallbackTarget& target) noexcept
        : target_(&target) {}

    void operator()(int value) const noexcept { (*target_).add(value); }

private:
    CallbackTarget* target_{};
};

class LargeOwningCallback final {
public:
    explicit LargeOwningCallback(CallbackTarget& target) noexcept
        : target_(&target) {}

    void operator()(int value) const noexcept { (*target_).add(value); }

private:
    CallbackTarget* target_{};
    // Padding that makes the callable too large for small-buffer storage;
    // never read.
    [[maybe_unused]] std::array<std::byte, 192U> retained_state_{};
};

template <typename Callback>
std::chrono::nanoseconds measure_invocation(Callback& callback,
                                            std::size_t iterations) {
    const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        callback(1);
    }
    const std::chrono::steady_clock::time_point finish =
        std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start);
}

class DelegateInvoker final {
public:
    explicit DelegateInvoker(gui_forms::Delegate<int> delegate) noexcept
        : delegate_(delegate) {}

    void operator()(int value) const { delegate_(value); }

private:
    gui_forms::Delegate<int> delegate_;
};

void print_invocation(const char* kind,
                      std::chrono::nanoseconds elapsed,
                      std::size_t iterations,
                      std::uint64_t total) {
    const double per_call = static_cast<double>(elapsed.count()) /
                            static_cast<double>(iterations);
    std::cout << "invoke," << kind << ',' << elapsed.count() << ','
              << iterations << ',' << per_call << ',' << total << '\n';
}

} // namespace

int main() {
    constexpr std::size_t iterations = 10000000U;
    CallbackTarget delegate_target;
    allocation_probe::begin();
    const gui_forms::Delegate<int> delegate =
        gui_forms::Delegate<int>::bind<CallbackTarget, &CallbackTarget::add>(
            delegate_target);
    const std::size_t delegate_binding_allocations = allocation_probe::finish();

    CallbackTarget small_target;
    allocation_probe::begin();
    std::function<void(int)> small_callback = SmallOwningCallback(small_target);
    const std::size_t small_binding_allocations = allocation_probe::finish();

    CallbackTarget large_target;
    allocation_probe::begin();
    std::function<void(int)> large_callback = LargeOwningCallback(large_target);
    const std::size_t large_binding_allocations = allocation_probe::finish();

    gui_forms::Event<int> delegate_event;
    allocation_probe::begin();
    const gui_forms::SubscriptionToken delegate_token =
        delegate_event.subscribe(delegate);
    const std::size_t delegate_subscription_allocations =
        allocation_probe::finish();
    allocation_probe::begin();
    delegate_event.emit(1);
    const std::size_t delegate_emission_allocations = allocation_probe::finish();

    gui_forms::Event<int> large_event;
    allocation_probe::begin();
    const gui_forms::SubscriptionToken large_token =
        large_event.subscribe(LargeOwningCallback(large_target));
    const std::size_t large_subscription_allocations = allocation_probe::finish();
    allocation_probe::begin();
    large_event.emit(1);
    const std::size_t large_emission_allocations = allocation_probe::finish();

    std::cout << "operation,kind,allocations,object_bytes\n";
    std::cout << "bind,delegate," << delegate_binding_allocations << ','
              << sizeof(delegate) << '\n';
    std::cout << "bind,std_function_small," << small_binding_allocations << ','
              << sizeof(small_callback) << '\n';
    std::cout << "bind,std_function_large," << large_binding_allocations << ','
              << sizeof(large_callback) << '\n';
    std::cout << "subscribe,delegate," << delegate_subscription_allocations
              << ",0\n";
    std::cout << "emit,delegate," << delegate_emission_allocations << ",0\n";
    std::cout << "subscribe,std_function_large,"
              << large_subscription_allocations << ",0\n";
    std::cout << "emit,std_function_large," << large_emission_allocations
              << ",0\n";
    std::cout << "operation,kind,total_ns,iterations,ns_per_call,total\n";

    DelegateInvoker delegate_invoker(delegate);
    const std::chrono::nanoseconds delegate_time =
        measure_invocation(delegate_invoker, iterations);
    const std::chrono::nanoseconds small_time =
        measure_invocation(small_callback, iterations);
    const std::chrono::nanoseconds large_time =
        measure_invocation(large_callback, iterations);
    print_invocation("delegate", delegate_time, iterations,
                     delegate_target.total());
    print_invocation("std_function_small", small_time, iterations,
                     small_target.total());
    print_invocation("std_function_large", large_time, iterations,
                     large_target.total());
    return delegate_token.connected() && large_token.connected() ? 0 : 1;
}
