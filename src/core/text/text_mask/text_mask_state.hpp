#pragma once

#include "text_mask_budget.hpp"
#include "../prepared/prepared_storage.hpp"
#include <stxlib/threads.hpp>
#include <vector>

namespace gui_forms::detail {

struct MaskFontOwner final {
    MaskCharge count{};
    MaskCharge bytes{};
    PreparedFontBank bank{};
};

struct MaskOptions final {
    std::uint32_t primary_face{0};
    std::int32_t size_64{0};
    std::int32_t width_64{0};
    std::int32_t gap_64{0};
    double scale{1.0};
    TextMaskRaster raster{TextMaskRaster::outline_gray};
};

struct MaskKey final {
    MaskCharge count{};
    std::shared_ptr<const MaskFontOwner> fonts{};
    MaskOptions options{};
    std::vector<char, MaskAllocator<char>> text;
    explicit MaskKey(const std::shared_ptr<MaskLedger>& ledger, MaskCharge&& reservation)
        : count(std::move(reservation)), text(MaskAllocator<char>(ledger, MaskResource::utf8)) {
        count.commit();
    }
};

struct TextMaskStorage final {
    MaskCharge count{};
    std::shared_ptr<const MaskKey> key{};
    TextMaskMetrics metrics{};
    std::vector<TextMaskLine, MaskAllocator<TextMaskLine>> lines;
    std::vector<std::uint8_t, MaskAllocator<std::uint8_t>> pixels;
    explicit TextMaskStorage(const std::shared_ptr<MaskLedger>& ledger, MaskCharge&& reservation)
        : count(std::move(reservation)), lines(MaskAllocator<TextMaskLine>(ledger)),
          pixels(MaskAllocator<std::uint8_t>(ledger, MaskResource::coverage)) {
        count.commit();
    }
};

// Worker-only boundary. Test backends are private, never installed. The caller
// publishes only success with a complete initialized candidate. Native Stage 2
// will fill the same counted storage. Backend and wake borrows survive join.
class MaskBackend {
public:
    virtual ~MaskBackend() = default;
    [[nodiscard]] virtual TextMaskResult execute(const std::shared_ptr<MaskLedger>& ledger,
        const std::shared_ptr<const MaskKey>& key,
        std::shared_ptr<const TextMaskStorage>& output) = 0;
};

struct MaskSlot final {
    MaskCharge charge{};
    TextMaskRequestId id{};
    TextMaskSlot state{TextMaskSlot::empty};
    TextMaskResult completion{};
    std::shared_ptr<const MaskKey> key{};
    std::shared_ptr<const TextMaskStorage> result{};
    void retire() noexcept;
};
struct MaskCacheEntry final {
    MaskCharge charge{};
    std::shared_ptr<const TextMaskStorage> value{};
    // Rank is bounded to [0,699]; touching increments only newer entries.
    std::size_t rank{0};
};

struct TextMaskSessionState final {
    MaskCharge handle_charge{};
    std::shared_ptr<MaskLedger> ledger{};
    stxlib::ThreadIdentity executor{};
    std::uint64_t identity{0};
    std::uint64_t next_serial{1};
    mutable stxlib::Mutex mutex{};
    stxlib::ConditionVariable condition{};
    stxlib::Thread worker{};
    PreparedTextWakeTarget* wake{};
    MaskBackend* backend{};
    std::array<MaskSlot, 9> slots{};
    std::array<MaskCacheEntry, 700> cache{};
    std::array<std::weak_ptr<const MaskFontOwner>, 2> banks{};
    bool closing{false};
    bool joined{false};
    bool wake_pending{false};
    bool wake_requested{false};
    // The worker's STXlib entry (`context` is the state) and its body.
    static void thread_main(const stxlib::StopSignal& stop, void* context) noexcept;
    void run() noexcept;
    // run's wait predicate, under the mutex: closing, a wake or a slot to
    // run.
    [[nodiscard]] static bool work_waiting(const void* context) noexcept;
    void close();
    void join();
    void clear_cache_locked() noexcept;
    void cache_insert_locked(const std::shared_ptr<const TextMaskStorage>& value);
    [[nodiscard]] std::shared_ptr<const TextMaskStorage> cache_lookup_locked(
        const MaskFontOwner& fonts, const MaskOptions& options, const std::string_view text);
    [[nodiscard]] std::shared_ptr<const MaskFontOwner> find_bank(const EncodedFontLease& fonts) const;
    ~TextMaskSessionState();
};

struct TextMaskServiceState final {
    std::shared_ptr<MaskLedger> ledger{};
    stxlib::ThreadIdentity executor{};
    std::weak_ptr<TextMaskSessionState> session{};
    std::array<std::weak_ptr<const MaskFontOwner>, 2> banks{};
    bool closing{false};
    MaskBackend* backend{};
};

struct TextMaskAccess final {
    static std::shared_ptr<const TextMaskStorage>& storage(TextMaskLease& lease) noexcept { return lease.storage_; }
    static std::shared_ptr<TextMaskServiceState>& state(TextMaskService& service) noexcept { return service.state_; }
    static std::shared_ptr<TextMaskSessionState>& state(TextMaskSession& session) noexcept { return session.state_; }
    [[nodiscard]] static std::unique_ptr<TextMaskSession> session(std::shared_ptr<TextMaskSessionState> state);
};

[[nodiscard]] TextMaskResult normalize_mask_request(const TextMaskRequest& request, MaskOptions& output) noexcept;
[[nodiscard]] std::shared_ptr<TextMaskStorage> allocate_mask_storage(
    const std::shared_ptr<MaskLedger>& ledger, const std::shared_ptr<const MaskKey>& key,
    const TextMaskMetrics& metrics, const std::size_t line_count);
[[nodiscard]] std::uint64_t next_mask_identity();
void require_mask_executor(const stxlib::ThreadIdentity executor);
[[nodiscard]] MaskBackend& native_mask_backend() noexcept;

} // namespace gui_forms::detail
