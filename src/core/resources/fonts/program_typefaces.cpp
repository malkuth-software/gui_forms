// The program's typefaces and the executors' faces over them
// (program_typefaces.hpp).
//
// The registry is a table in static storage, never destroyed, under one
// mutex. It observes each typeface (std::weak_ptr) and is swept of the
// expired entries on every request, so it holds no more entries than there
// are typefaces held, plus those released since the last request. Opening
// a typeface reads only the font's table directory (under a microsecond,
// stxlib/glyph_face.hpp), so it is done under the mutex: two threads asking
// for the same face at once get the same typeface.

#include "program_typefaces.hpp"

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"

#include <stxlib/result.hpp>

#include <mutex>
#include <utility>
#include <vector>

namespace gui_forms::detail {
namespace {

// One face the registry has opened: its bytes' address and length, its
// index in a collection, and the typeface while some holder holds it.
struct TypefaceEntry final {
    const std::byte* data{};
    std::size_t size{};
    std::uint32_t face_index{};
    std::weak_ptr<const ProgramTypeface> typeface{};
};

struct TypefaceTable final {
    std::mutex mutex{};
    std::vector<TypefaceEntry> entries{};
};

[[nodiscard]] TypefaceTable& typeface_table() noexcept {
    // Never destroyed (rule 1: process termination releases none of our
    // state), so a typeface released during static destruction still finds
    // the table.
    static TypefaceTable* const table = new TypefaceTable{};
    return *table;
}

// Whether `entry` is no longer held by anyone.
struct ExpiredTypeface final {
    bool operator()(const TypefaceEntry& entry) const noexcept {
        const bool expired = entry.typeface.expired();
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

std::shared_ptr<const ProgramTypeface> program_typeface(const std::span<const std::byte> bytes,
                                                        std::shared_ptr<const void> owner,
                                                        const std::uint32_t face_index,
                                                        const TypefaceByteLimits& limits) {
    std::shared_ptr<const ProgramTypeface> result{};
    if (bytes.empty()) return result;
    TypefaceTable& table = typeface_table();
    const std::lock_guard<std::mutex> lock(table.mutex);
    std::erase_if(table.entries, ExpiredTypeface{});
    for (const TypefaceEntry& entry : table.entries) {
        if (entry.data != bytes.data() || entry.size != bytes.size() || entry.face_index != face_index) continue;
        result = entry.typeface.lock();
        if (result) return result;
    }
    stxlib::glyphs::TypefaceLimits typeface_limits{};
    typeface_limits.store.kept_bytes = static_cast<stxlib::usize>(limits.kept_glyph_bytes);
    typeface_limits.store.recent_bytes = static_cast<stxlib::usize>(limits.recent_glyph_bytes);
    stxlib::Result<stxlib::Shared<stxlib::glyphs::Typeface>> opened =
        stxlib::glyphs::open_typeface(platform::stxlib_bytes(bytes), face_index, typeface_limits);
    if (opened.failed()) return result;
    stxlib::Shared<stxlib::glyphs::Typeface> typeface = std::move(opened).take_value();
    // Room for the entry first, so that a failure leaves the table as it was.
    table.entries.reserve(table.entries.size() + 1U);
    std::shared_ptr<const ProgramTypeface> created = std::make_shared<const ProgramTypeface>(std::move(owner), std::move(typeface));
    (*created).account();
    TypefaceEntry entry{};
    entry.data = bytes.data();
    entry.size = bytes.size();
    entry.face_index = face_index;
    entry.typeface = created;
    table.entries.push_back(std::move(entry));
    result = std::move(created);
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

bool ExecutorFace::open(std::shared_ptr<const ProgramTypeface> typeface, const FaceByteLimits& limits) {
    release();
    stxlib::glyphs::FaceLimits face_limits{};
    face_limits.fitted_bytes = static_cast<stxlib::usize>(limits.fitted_bytes);
    face_limits.mask.width = limits.mask_width;
    face_limits.mask.height = limits.mask_height;
    const stxlib::Status opened = face_.open((*typeface).typeface(), face_limits);
    if (opened.failed()) {
        face_.release();
        return false;
    }
    typeface_ = std::move(typeface);
    account();
    return true;
}

bool ExecutorFace::is_open() const noexcept {
    const bool open = face_.font() != nullptr;
    return open;
}

const stxlib::glyphs::Font* ExecutorFace::font() const noexcept { return face_.font(); }

stxlib::Status ExecutorFace::measure_run(const stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                         const stxlib::glyphs::DrawStyle& style, stxlib::glyphs::RunBox& box) {
    const stxlib::Status measured = face_.measure_run(glyphs, style, box);
    account();
    return measured;
}

stxlib::Status ExecutorFace::draw_run(const stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                      const stxlib::glyphs::DrawStyle& style, stxlib::glyphs::RunCoverage& coverage) {
    const stxlib::Status drawn = face_.draw_run(glyphs, style, coverage);
    account();
    return drawn;
}

std::size_t ExecutorFace::bytes_held() const noexcept {
    std::size_t held = static_cast<std::size_t>(face_.bytes_held());
    if (typeface_) held += (*typeface_).bytes_held();
    return held;
}

std::size_t ExecutorFace::bytes_peak() const noexcept {
    std::size_t peak = static_cast<std::size_t>(face_.bytes_peak());
    if (typeface_) peak += (*typeface_).bytes_peak();
    return peak;
}

void ExecutorFace::release() noexcept {
    face_.release();
    count_held_change(counted_, 0U);
    counted_ = 0U;
    typeface_.reset();
}

void ExecutorFace::account() noexcept {
    const std::size_t held = static_cast<std::size_t>(face_.bytes_held());
    count_held_change(counted_, held);
    counted_ = held;
    if (typeface_) (*typeface_).account();
}

} // namespace gui_forms::detail
