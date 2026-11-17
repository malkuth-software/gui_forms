// The program's typefaces and the executors' faces over them
// (program_typefaces.hpp).
//
// The table is in static storage, never destroyed (rule 1: termination
// releases none of our state; a holder released during static destruction
// still finds it), under one mutex. It owns no typeface: each entry observes
// the shared object that holds one (a registry's owner of the bytes, or an
// unregistered typeface itself) and is swept, on every request, once that
// object is gone. While the observed object lives its bytes stay at the
// entry's address, so no other font can be there; an entry whose object has
// gone is never matched. Opening a typeface reads only the font's table
// directory (under a microsecond, stxlib/glyph_face.hpp), so the
// unregistered path opens under the mutex: two executors asking for the same
// bytes at once get the same typeface.

#include "program_typefaces.hpp"

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"

#include <stxlib/error.hpp>
#include <stxlib/result.hpp>

#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace gui_forms::detail {
namespace {

// One typeface the table knows of: its bytes' address and length, its index
// in a collection, the identity of the registry owner that registered it
// (null for an unregistered typeface), the object that holds it, observed,
// and the typeface, valid while that object lives.
struct TypefaceEntry final {
    const std::byte* data{};
    std::size_t size{};
    std::uint32_t face_index{};
    const void* registrant{};
    std::weak_ptr<const void> keeper{};
    const ProgramTypeface* typeface{};
};

struct TypefaceTable final {
    std::mutex mutex{};
    std::vector<TypefaceEntry> entries{};
};

[[nodiscard]] TypefaceTable& typeface_table() noexcept {
    // Never destroyed (rule 1), so it has no static destructor.
    static TypefaceTable* const table = new TypefaceTable{};
    return *table;
}

// Whether the object holding `entry`'s typeface is gone.
struct ExpiredTypeface final {
    bool operator()(const TypefaceEntry& entry) const noexcept {
        const bool expired = entry.keeper.expired();
        return expired;
    }
};

// Counts a change from `before` to `after` bytes under text_layout.
void count_held_change(const std::size_t before, const std::size_t after) noexcept {
    if (after > before) {
        count_allocation(AllocationSubsystem::text_layout, after - before);
        return;
    }
    if (before > after) count_release(AllocationSubsystem::text_layout, before - after);
}

// Whether `entry` is face `face_index` of `bytes` as `registrant` registered
// it (null: unregistered).
[[nodiscard]] bool same_face(const TypefaceEntry& entry, const std::span<const std::byte> bytes,
                             const std::uint32_t face_index, const void* const registrant) noexcept {
    const bool same = entry.data == bytes.data() && entry.size == bytes.size() && entry.face_index == face_index &&
        entry.registrant == registrant;
    return same;
}

// The live entry for that face, as a borrow, or an empty borrow. Needs the
// table's mutex.
[[nodiscard]] TypefaceBorrow find_locked(const TypefaceTable& table, const std::span<const std::byte> bytes,
                                         const std::uint32_t face_index, const void* const registrant) {
    TypefaceBorrow result{};
    for (const TypefaceEntry& entry : table.entries) {
        if (!same_face(entry, bytes, face_index, registrant)) continue;
        std::shared_ptr<const void> keeper = entry.keeper.lock();
        if (!keeper) continue;
        result.keeper = std::move(keeper);
        result.typeface = entry.typeface;
        return result;
    }
    return result;
}

// STXlib's typeface of face `face_index` of `bytes`, or its failure.
[[nodiscard]] stxlib::OperationResult<stxlib::Shared<stxlib::glyphs::Typeface>> open_stxlib_typeface(
    const std::span<const std::byte> bytes, const std::uint32_t face_index, const TypefaceByteLimits& limits) noexcept {
    stxlib::glyphs::TypefaceLimits typeface_limits{};
    typeface_limits.store.kept_bytes = static_cast<stxlib::usize>(limits.kept_glyph_bytes);
    typeface_limits.store.recent_bytes = static_cast<stxlib::usize>(limits.recent_glyph_bytes);
    stxlib::OperationResult<stxlib::Shared<stxlib::glyphs::Typeface>> opened =
        stxlib::glyphs::open_typeface(platform::stxlib_bytes(bytes), face_index, typeface_limits);
    return opened;
}

[[nodiscard]] stxlib::OperationStatus disposed_face() noexcept {
    const stxlib::OperationStatus failure =
        stxlib::OperationStatus::failure(stxlib::ErrorRecord::make(stxlib::ErrorKind::disposed, "gf.text.executor_face.draw"));
    return failure;
}

[[nodiscard]] stxlib::OperationStatus retired_face() noexcept {
    const stxlib::OperationStatus failure =
        stxlib::OperationStatus::failure(stxlib::ErrorRecord::make(stxlib::ErrorKind::revoked, "gf.text.executor_face.draw"));
    return failure;
}

} // namespace

ProgramTypeface::ProgramTypeface(std::shared_ptr<const void> owner,
                                 stxlib::Shared<stxlib::glyphs::Typeface> typeface) noexcept
    : owner_(std::move(owner)), typeface_(std::move(typeface)) {}

ProgramTypeface::~ProgramTypeface() {
    const std::size_t counted = counted_.exchange(0U, std::memory_order_relaxed);
    count_held_change(counted, 0U);
}

const stxlib::Shared<stxlib::glyphs::Typeface>& ProgramTypeface::typeface() const noexcept { return typeface_; }

void ProgramTypeface::account() const noexcept {
    const std::size_t held = bytes_held();
    const std::size_t before = counted_.exchange(held, std::memory_order_relaxed);
    count_held_change(before, held);
}

std::size_t ProgramTypeface::bytes_held() const noexcept {
    const std::size_t held = static_cast<std::size_t>((*typeface_).bytes_held());
    return held;
}

std::size_t ProgramTypeface::bytes_peak() const noexcept {
    const std::size_t peak = static_cast<std::size_t>((*typeface_).bytes_peak());
    return peak;
}

void ProgramTypeface::retire() const noexcept { retired_.store(true, std::memory_order_release); }

bool ProgramTypeface::retired() const noexcept {
    const bool retired = retired_.load(std::memory_order_acquire);
    return retired;
}

std::unique_ptr<ProgramTypeface> open_program_typeface(const std::span<const std::byte> bytes,
                                                       const std::uint32_t face_index,
                                                       const TypefaceByteLimits& limits) {
    std::unique_ptr<ProgramTypeface> result{};
    if (bytes.empty()) return result;
    stxlib::OperationResult<stxlib::Shared<stxlib::glyphs::Typeface>> opened = open_stxlib_typeface(bytes, face_index, limits);
    if (opened.failed()) return result;
    stxlib::Shared<stxlib::glyphs::Typeface> typeface = std::move(opened).take_value();
    result = std::make_unique<ProgramTypeface>(std::shared_ptr<const void>{}, std::move(typeface));
    (*result).account();
    return result;
}

bool register_program_typeface(const std::span<const std::byte> bytes, const std::uint32_t face_index,
                               const std::shared_ptr<const void>& owner, const ProgramTypeface& typeface) noexcept {
    if (bytes.empty() || !owner) return false;
    TypefaceEntry entry{};
    entry.data = bytes.data();
    entry.size = bytes.size();
    entry.face_index = face_index;
    entry.registrant = owner.get();
    entry.keeper = owner;
    entry.typeface = &typeface;
    TypefaceTable& table = typeface_table();
    const std::lock_guard<std::mutex> lock(table.mutex);
    std::erase_if(table.entries, ExpiredTypeface{});
    try {
        table.entries.push_back(std::move(entry));
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

TypefaceBorrow program_typeface(const std::span<const std::byte> bytes, std::shared_ptr<const void> owner,
                                const std::uint32_t face_index, const TypefaceByteLimits& limits) {
    TypefaceBorrow result{};
    if (bytes.empty()) return result;
    TypefaceTable& table = typeface_table();
    const std::lock_guard<std::mutex> lock(table.mutex);
    std::erase_if(table.entries, ExpiredTypeface{});
    if (owner) {
        result = find_locked(table, bytes, face_index, owner.get());
        if (result.typeface != nullptr) return result;
    }
    result = find_locked(table, bytes, face_index, nullptr);
    if (result.typeface != nullptr) return result;
    stxlib::OperationResult<stxlib::Shared<stxlib::glyphs::Typeface>> opened = open_stxlib_typeface(bytes, face_index, limits);
    if (opened.failed()) return result;
    stxlib::Shared<stxlib::glyphs::Typeface> typeface = std::move(opened).take_value();
    // Room for the entry first, so that a failure leaves the table as it was.
    table.entries.reserve(table.entries.size() + 1U);
    // An unregistered typeface holds the bytes' owner itself and is shared by
    // the executors that draw it, so it is its own keeper.
    std::shared_ptr<const ProgramTypeface> created =
        std::make_shared<const ProgramTypeface>(std::move(owner), std::move(typeface));
    (*created).account();
    TypefaceEntry entry{};
    entry.data = bytes.data();
    entry.size = bytes.size();
    entry.face_index = face_index;
    entry.keeper = created;
    entry.typeface = created.get();
    table.entries.push_back(std::move(entry));
    result.typeface = created.get();
    result.keeper = std::move(created);
    return result;
}

std::size_t program_typeface_count() {
    TypefaceTable& table = typeface_table();
    const std::lock_guard<std::mutex> lock(table.mutex);
    std::erase_if(table.entries, ExpiredTypeface{});
    const std::size_t count = table.entries.size();
    return count;
}

ExecutorFace::~ExecutorFace() { release(); }

bool ExecutorFace::open(TypefaceBorrow borrow, const FaceByteLimits& limits) {
    release();
    if (borrow.typeface == nullptr || (*borrow.typeface).retired()) return false;
    stxlib::glyphs::FaceLimits face_limits{};
    face_limits.fitted_bytes = static_cast<stxlib::usize>(limits.fitted_bytes);
    face_limits.mask.width = limits.mask_width;
    face_limits.mask.height = limits.mask_height;
    const stxlib::OperationStatus opened = face_.open((*borrow.typeface).typeface(), face_limits);
    if (opened.failed()) {
        face_.release();
        return false;
    }
    keeper_ = std::move(borrow.keeper);
    typeface_ = borrow.typeface;
    account();
    return true;
}

bool ExecutorFace::is_open() const noexcept {
    const bool open = typeface_ != nullptr;
    return open;
}

const stxlib::glyphs::Font* ExecutorFace::font() const noexcept { return face_.font(); }

stxlib::OperationStatus ExecutorFace::admit() noexcept {
    if (typeface_ == nullptr) return disposed_face();
    if ((*typeface_).retired()) {
        release();
        return retired_face();
    }
    return stxlib::OperationStatus::success();
}

stxlib::OperationStatus ExecutorFace::measure_run(const stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                         const stxlib::glyphs::DrawStyle& style, stxlib::glyphs::RunBox& box) {
    const stxlib::OperationStatus admitted = admit();
    if (admitted.failed()) return admitted;
    const stxlib::OperationStatus measured = face_.measure_run(glyphs, style, box);
    account();
    return measured;
}

stxlib::OperationStatus ExecutorFace::draw_run(const stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                      const stxlib::glyphs::DrawStyle& style, stxlib::glyphs::RunCoverage& coverage) {
    const stxlib::OperationStatus admitted = admit();
    if (admitted.failed()) return admitted;
    const stxlib::OperationStatus drawn = face_.draw_run(glyphs, style, coverage);
    account();
    return drawn;
}

std::size_t ExecutorFace::bytes_held() const noexcept {
    std::size_t held = static_cast<std::size_t>(face_.bytes_held());
    if (typeface_ != nullptr) held += (*typeface_).bytes_held();
    return held;
}

std::size_t ExecutorFace::bytes_peak() const noexcept {
    std::size_t peak = static_cast<std::size_t>(face_.bytes_peak());
    if (typeface_ != nullptr) peak += (*typeface_).bytes_peak();
    return peak;
}

std::size_t ExecutorFace::face_bytes_held() const noexcept {
    const std::size_t held = static_cast<std::size_t>(face_.bytes_held());
    return held;
}

void ExecutorFace::release() noexcept {
    face_.release();
    count_held_change(counted_, 0U);
    counted_ = 0U;
    typeface_ = nullptr;
    keeper_.reset();
}

void ExecutorFace::account() noexcept {
    const std::size_t held = static_cast<std::size_t>(face_.bytes_held());
    count_held_change(counted_, held);
    counted_ = held;
    if (typeface_ != nullptr) (*typeface_).account();
}

} // namespace gui_forms::detail
