#pragma once

#include "gui_forms/text_mask.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms::detail {

enum class MaskResource : std::size_t {
    utf8, metadata, coverage, shaping, workspace, fonts,
    masks, keys, cache, slots, banks, count
};

struct MaskUsage final {
    std::size_t live{0};
    std::size_t reserved{0};
    std::size_t peak{0};
    std::size_t limit{0};
};

struct MaskBudgetFailure final : std::exception {
    explicit MaskBudgetFailure(const MaskResource resource) noexcept : resource(resource) {}
    MaskResource resource{};
};

struct MaskLimitFailure final : std::exception {
    explicit MaskLimitFailure(const TextMaskLimit limit) noexcept : limit(limit) {}
    TextMaskLimit limit{TextMaskLimit::none};
};

struct MaskLedger final {
    stxlib::Mutex mutex{};
    std::array<MaskUsage, static_cast<std::size_t>(MaskResource::count)> usage{};
    MaskLedger();
    void reserve(const MaskResource resource, const std::size_t amount);
    void commit(const MaskResource resource, const std::size_t amount) noexcept;
    void abandon(const MaskResource resource, const std::size_t amount) noexcept;
    void release(const MaskResource resource, const std::size_t amount) noexcept;
    [[nodiscard]] TextMaskBudgetSnapshot snapshot();
};

// One private allocator boundary measures the STL's actual allocation request,
// including allocate_shared's rebound control-block type. No allocator header
// or vendor allocation is reported as first-party requested storage.
template<class T> class MaskAllocator {
public:
    using value_type = T;
    std::shared_ptr<MaskLedger> ledger{};
    MaskResource resource{MaskResource::metadata};
    MaskAllocator() = default;
    explicit MaskAllocator(std::shared_ptr<MaskLedger> owner,
        const MaskResource kind = MaskResource::metadata) noexcept
        : ledger(std::move(owner)), resource(kind) {}
    template<class U> MaskAllocator(const MaskAllocator<U>& other) noexcept
        : ledger(other.ledger), resource(other.resource) {}
    [[nodiscard]] T* allocate(const std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) throw std::bad_alloc{};
        const std::size_t bytes = count * sizeof(T);
        (*ledger).reserve(resource, bytes);
        T* storage = nullptr;
        try { storage = std::allocator<T>{}.allocate(count); }
        catch (...) { (*ledger).abandon(resource, bytes); throw; }
        (*ledger).commit(resource, bytes);
        return storage;
    }
    void deallocate(T* const storage, const std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(storage, count);
        (*ledger).release(resource, count * sizeof(T));
    }
    template<class U> [[nodiscard]] bool operator==(const MaskAllocator<U>& other) const noexcept {
        const bool equal = ledger == other.ledger && resource == other.resource;
        return equal;
    }
};

// Bootstrap has no ledger yet. Its allocator checks the fixed metadata bound
// before allocating and reports the exact control-block request to the factory.
// The borrowed counter is used only by allocate, never by later deallocation.
template<class T> class MaskBootstrapAllocator {
public:
    using value_type = T;
    std::size_t* bytes{};
    explicit MaskBootstrapAllocator(std::size_t& output) noexcept : bytes(&output) {}
    template<class U> MaskBootstrapAllocator(const MaskBootstrapAllocator<U>& other) noexcept : bytes(other.bytes) {}
    [[nodiscard]] T* allocate(const std::size_t count) {
        if (count > (8U * 1024U * 1024U) / sizeof(T)) throw std::bad_alloc{};
        T* storage = std::allocator<T>{}.allocate(count);
        *bytes = count * sizeof(T);
        return storage;
    }
    void deallocate(T* const storage, const std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(storage, count);
    }
    template<class U> [[nodiscard]] bool operator==(const MaskBootstrapAllocator<U>& other) const noexcept {
        const bool equal = bytes == other.bytes;
        return equal;
    }
};

class MaskCharge final {
public:
    MaskCharge() = default;
    ~MaskCharge() { reset(); }
    MaskCharge(const MaskCharge&) = delete;
    MaskCharge& operator=(const MaskCharge&) = delete;
    MaskCharge(MaskCharge&& other) noexcept;
    MaskCharge& operator=(MaskCharge&& other) noexcept;
    void reserve(std::shared_ptr<MaskLedger> ledger, const MaskResource resource, const std::size_t amount);
    void commit() noexcept;
    void acquire(std::shared_ptr<MaskLedger> ledger, const MaskResource resource, const std::size_t amount);
    void reset() noexcept;
private:
    std::shared_ptr<MaskLedger> ledger_{};
    MaskResource resource_{MaskResource::metadata};
    std::size_t amount_{0};
    bool committed_{false};
};

[[nodiscard]] std::shared_ptr<MaskLedger> make_mask_ledger();
[[nodiscard]] TextMaskResult mask_budget_result(const MaskResource resource) noexcept;

} // namespace gui_forms::detail
