#pragma once

#include "gui_forms/prepared_text.hpp"
#include "../shaping/shaped_text_geometry.hpp"

#include <stxlib/optional.hpp>
#include <array>
#include <atomic>
#include <exception>
#include <optional>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms::detail {

class ProgramTypeface;
class PreparedFacePool;

// Private monotonic identity sources. Zero and the terminal maximum are never
// issued; refusal preserves output. The counter helper admits a local source
// for exhaustion fixtures, without resetting either process-wide source.
[[nodiscard]] PreparedTextStatus acquire_prepared_identity(std::atomic<std::uint64_t>& next,
    std::uint64_t& output) noexcept;
[[nodiscard]] PreparedTextStatus acquire_prepared_session_instance(std::uint64_t& output) noexcept;
[[nodiscard]] PreparedTextStatus acquire_prepared_controller_instance(std::uint64_t& output) noexcept;

enum class PreparedResource { font_bank, input, payload, mask };

struct PreparedLedger final {
    stxlib::Mutex mutex{};
    PreparedTextBudgetSnapshot usage{};
    std::uint64_t next_bank{1};
    bool session_claimed{};
    bool closing{};
};

// One shaping session per ledger, including source-private batch sessions.
// Acquire before starting a worker. The owner releases only after joining it
// or when worker creation fails; cancellation/exit alone do not release it.
// This owner is executor-confined; the ledger arbitrates competing owners.
class PreparedSessionClaim final {
public:
    PreparedSessionClaim() = default;
    ~PreparedSessionClaim();
    PreparedSessionClaim(const PreparedSessionClaim&) = delete;
    PreparedSessionClaim& operator=(const PreparedSessionClaim&) = delete;
    [[nodiscard]] PreparedTextStatus acquire(std::shared_ptr<PreparedLedger> ledger);
    void release() noexcept;
private:
    std::shared_ptr<PreparedLedger> ledger_{};
};

class PreparedReservation final {
public:
    PreparedReservation() = default;
    ~PreparedReservation();
    PreparedReservation(const PreparedReservation&) = delete;
    PreparedReservation& operator=(const PreparedReservation&) = delete;
    PreparedReservation(PreparedReservation&& other) noexcept;
    PreparedReservation& operator=(PreparedReservation&& other) noexcept;
    [[nodiscard]] PreparedTextStatus acquire(std::shared_ptr<PreparedLedger> ledger,
        const PreparedResource resource, const std::size_t bytes);
    void release() noexcept;
private:
    std::shared_ptr<PreparedLedger> ledger_{};
    PreparedResource resource_{PreparedResource::input};
    std::size_t bytes_{};
};

struct PreparedFontFace final {
    std::unique_ptr<std::byte[]> bytes{};
    std::size_t size{};
    stxlib::Optional<FontRole> role{};
    std::uint16_t weight{400};
    bool italic{};
    std::uint32_t index{};
    // A registered family's face (PreparedFontSource::family), or zero.
    FontFamilyId family{};
};

// The glyph faces' bytes of one font bank charged to its ledger
// (PreparedTextBudgetSnapshot::glyph_face_bytes and glyph_face_peak_bytes):
// STXlib's bytes_held of the bank's typefaces, recharged after each
// rasterization from them, and refunded when the bank goes. Any thread:
// every change is made under the ledger's mutex. A bank without a ledger (a
// text mask's) charges nothing.
class PreparedFaceCharge final {
public:
    PreparedFaceCharge() = default;
    ~PreparedFaceCharge();
    PreparedFaceCharge(const PreparedFaceCharge&) = delete;
    PreparedFaceCharge& operator=(const PreparedFaceCharge&) = delete;
    // Recharges this bank to `held` bytes and raises the ledger's peak to
    // `peak` (the typefaces' and the drawing faces' most, together). The
    // ledger is the bank's own, the same on every call; a different one is
    // charged after the first is refunded.
    void charge(const std::shared_ptr<PreparedLedger>& ledger, std::size_t held, std::size_t peak) noexcept;
    void release() noexcept;
private:
    std::shared_ptr<PreparedLedger> ledger_{};
    std::size_t bytes_{};
};

struct PreparedFontBank final {
    PreparedReservation reservation{};
    std::shared_ptr<PreparedLedger> ledger{};
    std::uint64_t identity{};
    std::uint64_t generation{1};
    std::array<PreparedFontFace, PreparedTextLimits::font_faces> faces{};
    std::size_t face_count{};
    std::size_t encoded_bytes{};
    // The STXlib typeface of each face (program_typefaces.hpp), opened over
    // `faces`' bytes by open_bank_typefaces before the bank is shared, then
    // read only. The bank is their only holder (shared pointers only so that
    // this header needs no STXlib type); every face over them borrows them
    // with no keeper, and the bank outlives those faces: its own pool's are
    // destroyed first, and a text mask's belong to a job that holds the bank.
    // Declared after `faces`, destroyed before them: the typefaces read those
    // bytes.
    std::array<std::shared_ptr<const ProgramTypeface>, PreparedTextLimits::font_faces> typefaces{};
    // The faces prepared rasterizations draw these typefaces with, opened on
    // first use and reused (prepared_face_pool.hpp); made with the
    // typefaces. Declared after them, destroyed before them.
    std::shared_ptr<PreparedFacePool> face_pool{};
    // Mutable: charging the typefaces' bytes is accounting, not a change to
    // the bank, and is made under the ledger's mutex.
    mutable PreparedFaceCharge face_charge{};
};

// Opens the STXlib typeface of each of the bank's faces
// (PreparedTextLimits::glyph_store_bytes each) into bank.typefaces, makes
// the bank's face pool, and checks what every rasterizer needs of a face:
// outlines STXlib reads, one to 1,000,000 glyphs and a Unicode character
// map. False, with bank.typefaces and the pool emptied, when a face fails
// any of them; std::bad_alloc, leaving them emptied, when memory runs out.
[[nodiscard]] bool open_bank_typefaces(PreparedFontBank& bank);

struct PreparedInputStorage final {
    PreparedReservation reservation{};
    std::shared_ptr<PreparedLedger> ledger{};
    PreparedTextKey key{};
    PreparedParagraphProof proof{};
    std::unique_ptr<char[]> text{};
    std::size_t text_bytes{};
    std::unique_ptr<DocumentMapSpan[]> mapping{};
    std::size_t mapping_count{};
    std::unique_ptr<PreparedSourceEndpoint[]> endpoints{};
    std::size_t endpoint_count{};
    std::size_t requested_bytes{};
};

struct PreparedAuthorityState final {
    stxlib::Mutex mutex{};
    stxlib::ThreadIdentity executor{};
    LayoutAuthority current{};
    std::optional<PreparedTextKey> key{};
    bool closing{};
};

struct PreparedTextStorage final {
    PreparedReservation reservation{};
    std::shared_ptr<PreparedLedger> ledger{};
    std::shared_ptr<PreparedAuthorityState> authority_state{};
    std::shared_ptr<const PreparedFontBank> fonts{};
    LayoutAuthority authority{};
    PreparedTextMetrics metrics{};
    std::unique_ptr<PreparedInputStorage> input{};
    std::unique_ptr<render::text::BoundedShapedText> geometry{};
    std::array<FontFaceId, PreparedTextLimits::font_faces> face_ids{};
    std::size_t requested_bytes{};
    std::size_t workspace_peak_bytes{};
};

struct PreparedMaskStorage final {
    PreparedReservation reservation{};
    PreparedTextMetrics metrics{};
    std::unique_ptr<std::uint8_t[]> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t left{};
    std::int32_t top{};
    std::size_t pixel_count{};
    // Stamps GrayTextMask::pixels(): the mask's release or replacement
    // destroys this storage and ends the views (borrow sanitizer, stage 3).
    // Masks are made on the session's worker and read elsewhere: any_thread.
    BorrowLender lender{BorrowThreading::any_thread};
};

struct PreparedServiceState final {
    std::shared_ptr<PreparedLedger> ledger{};
    std::weak_ptr<PreparedSessionState> session{};
    stxlib::ThreadIdentity executor{};
    std::uint64_t instance{};
};

struct PreparedSessionState final {
    PreparedSessionClaim claim{};
    std::shared_ptr<PreparedServiceState> service{};
    std::shared_ptr<const PreparedFontBank> fonts{};
    std::shared_ptr<PreparedAuthorityState> authority{};
    stxlib::ThreadIdentity executor{};
    stxlib::Mutex mutex{};
    stxlib::ConditionVariable condition{};
    stxlib::Thread worker{};
    PreparedTextWakeTarget* wake{};
    PreparedTextSessionSnapshot snapshot{};
    std::unique_ptr<PreparedTextStorage> job{};
    std::shared_ptr<const PreparedTextStorage> ready{};
    bool wake_pending{};
    // The worker's STXlib entry (`context` is the state) and its body.
    static void thread_main(const stxlib::StopSignal& stop, void* context) noexcept;
    void run() noexcept;
    // run's wait predicate, under the mutex: a queued job or closing.
    [[nodiscard]] static bool job_or_closing(const void* context) noexcept;
    void close();
    void join();
    ~PreparedSessionState();
};

// Private typed access for the provider and retained integration; no native
// handle, void payload or mutable prepared-storage alias crosses public headers.
struct PreparedTextAccess final {
    static std::shared_ptr<const PreparedFontBank>& fonts(EncodedFontLease& value) noexcept { return value.storage_; }
    static const std::shared_ptr<const PreparedFontBank>& fonts(const EncodedFontLease& value) noexcept { return value.storage_; }
    static std::unique_ptr<PreparedInputStorage>& input(PrepareInput& value) noexcept { return value.storage_; }
    static std::shared_ptr<const PreparedTextStorage>& layout(PreparedTextLayout& value) noexcept { return value.storage_; }
    static const std::shared_ptr<const PreparedTextStorage>& layout(const PreparedTextLayout& value) noexcept { return value.storage_; }
    static std::unique_ptr<PreparedMaskStorage>& mask(GrayTextMask& value) noexcept { return value.storage_; }
    static std::unique_ptr<PreparedTextSession> session(std::shared_ptr<PreparedSessionState> state);
    static PreparedTextLayout retained_layout(std::shared_ptr<const PreparedTextStorage> storage) {
        PreparedTextLayout layout{};
        layout.storage_ = std::move(storage);
        return layout;
    }
};

class PreparedTextPaintFailure final : public std::exception {
public:
    explicit PreparedTextPaintFailure(const PreparedTextStatus status) noexcept : status_(status) {}
    [[nodiscard]] PreparedTextStatus status() const noexcept { return status_; }
    [[nodiscard]] const char* what() const noexcept override { return "Prepared text paint refused"; }
private:
    PreparedTextStatus status_{};
};

[[nodiscard]] PreparedTextStatus validate_prepared_key(const PreparedTextKey& key) noexcept;
[[nodiscard]] PreparedTextStatus validate_prepared_input(const PreparedTextKey& key, const std::string_view text,
    const std::span<const DocumentMapSpan> mapping, const std::span<const PreparedSourceEndpoint> endpoints,
    const PreparedParagraphProof proof, std::size_t& bytes) noexcept;
[[nodiscard]] bool prepared_authority_current(const PreparedTextStorage& storage, const LayoutAuthority expected);
// Caller holds the mutex of storage.authority_state through the complete
// operation whose authority is being checked, including publication if any.
[[nodiscard]] bool prepared_authority_current_locked(const PreparedTextStorage& storage, const LayoutAuthority expected) noexcept;
[[nodiscard]] PreparedTextMetrics prepared_device_metrics(const FontSpec font, const double scale);
// Opens and checks the bank's typefaces (open_bank_typefaces):
// incompatible_font when a face fails, resource_failure when memory runs out.
[[nodiscard]] PreparedTextStatus validate_prepared_fonts(PreparedFontBank& bank);

} // namespace gui_forms::detail
