#pragma once

// The program's typefaces, part of the font family registry: one STXlib
// Typeface per registered face for the whole program (owner, 2026-11-16:
// "the store should be program, not window"), opened from the font bytes the
// text engine already holds, and one Face per executor over it (an
// ExecutorFace), since a Face is used by one thread at a time
// (stxlib/glyph_face.hpp). The four text rasterizers draw through them:
// the glyph-run rasterizer (interface text and the C drawing API), prepared
// text, text masks and the paint probe.
//
// What is kept. The registry observes each typeface and the faces own it: a
// typeface lives while an executor's face, a prepared font bank or a text
// mask's fonts hold it, and every holder of the same face's bytes finds the
// same typeface (the glyphs one executor caused to be read are there for
// the others). When the last holder lets go it is released, as the bundled
// font set's pages are (bundled_font_set.hpp), and the next request opens a
// new one. A face is identified by the address and length of its bytes and
// its index in a collection: while a typeface lives its holders keep those
// bytes alive, so no other font can be at that address.
//
// Memory, in bytes (owner, 2026-11-16: text memory limits are in bytes). A
// typeface's glyph store is bounded by TypefaceByteLimits and a face's
// fitted outlines by its fitted-byte limit. What each holds, as STXlib
// reports it (bytes_held), is counted under AllocationSubsystem::text_layout
// as it changes: a growth as one allocation of the difference, a fall as one
// release, and what is left as a release when the object goes. bytes_peak
// is each one's most since it was opened or its peak was last reset.
//
// Threads. program_typeface and everything on ProgramTypeface: any thread.
// An ExecutorFace: one thread at a time (its executor's).

#include "../../../platform/stxlib_bridge.hpp"

#include <stxlib/glyph_face.hpp>
#include <stxlib/shared.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms::detail {

// A typeface's glyph store, in bytes (stxlib::glyphs::StoreLimits): the
// glyphs named in advance and those in recent use. Storage is taken as it
// is needed; these are where it stops.
struct TypefaceByteLimits final {
    std::size_t kept_glyph_bytes{1048576};
    std::size_t recent_glyph_bytes{4194304};
};

// A face's fitted outlines, in bytes (stxlib::glyphs::FaceLimits), and the
// widest and tallest run mask it makes, in pixels.
struct FaceByteLimits final {
    std::size_t fitted_bytes{4194304};
    std::uint32_t mask_width{16384};
    std::uint32_t mask_height{16384};
};

// One face of a font as the program holds it.
class ProgramTypeface final {
public:
    // `owner` keeps the bytes `typeface` was opened from alive; it may be
    // empty when the caller's own object owns both (a prepared font bank).
    ProgramTypeface(std::shared_ptr<const void> owner, stxlib::Shared<stxlib::glyphs::Typeface> typeface) noexcept;
    ~ProgramTypeface();
    ProgramTypeface(const ProgramTypeface&) = delete;
    ProgramTypeface& operator=(const ProgramTypeface&) = delete;

    // The typeface a Face opens over; borrowed while this lives.
    [[nodiscard]] const stxlib::Shared<stxlib::glyphs::Typeface>& typeface() const noexcept;

    // Counts the change in the typeface's bytes_held since the last count
    // under AllocationSubsystem::text_layout. Any thread; each change is
    // counted once (the last count is exchanged atomically).
    void account() const noexcept;

    [[nodiscard]] std::size_t bytes_held() const noexcept;
    [[nodiscard]] std::size_t bytes_peak() const noexcept;

private:
    // Declared first, destroyed last: the typeface borrows the bytes.
    std::shared_ptr<const void> owner_;
    stxlib::Shared<stxlib::glyphs::Typeface> typeface_;
    // The bytes counted under text_layout so far. Mutable: counting is not a
    // change to the typeface, and any thread may count.
    mutable std::atomic<std::size_t> counted_{0U};
};

// The program's typeface for face `face_index` of the font in `bytes`,
// opened now if no holder holds one; `owner` keeps `bytes` alive (it may be
// empty when the caller's object owns the bytes and outlives every holder
// of the result). `limits` apply when the typeface is opened here; a
// typeface already held keeps the limits it was opened with. Null when
// STXlib refuses the face (not a font it reads, a needed table missing or
// cut short) or its storage runs out; std::bad_alloc when the registry's
// own does. Nothing is registered on either.
[[nodiscard]] std::shared_ptr<const ProgramTypeface> program_typeface(std::span<const std::byte> bytes,
                                                                      std::shared_ptr<const void> owner,
                                                                      std::uint32_t face_index,
                                                                      const TypefaceByteLimits& limits = TypefaceByteLimits{});

// How many typefaces the registry observes that are still held. For tests.
[[nodiscard]] std::size_t program_typeface_count();

// One executor's face over a program typeface: STXlib's Face, which keeps
// the outlines it has fitted, with its bytes counted as the typeface's are.
// Every draw goes through measure_run and draw_run, which count afterwards.
// Not copyable or movable (the counts belong to this object).
class ExecutorFace final {
public:
    ExecutorFace() noexcept = default;
    ~ExecutorFace();
    ExecutorFace(const ExecutorFace&) = delete;
    ExecutorFace& operator=(const ExecutorFace&) = delete;

    // Opens over `typeface` (not null). False, with the face left closed,
    // when STXlib refuses the limits or memory runs out. A face already open
    // is released first.
    [[nodiscard]] bool open(std::shared_ptr<const ProgramTypeface> typeface, const FaceByteLimits& limits);

    [[nodiscard]] bool is_open() const noexcept;

    // The font opened, or null for a face not opened.
    [[nodiscard]] const stxlib::glyphs::Font* font() const noexcept;

    // stxlib::glyphs::Face's measure_run and draw_run, then the counts.
    [[nodiscard]] stxlib::Status measure_run(stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                             const stxlib::glyphs::DrawStyle& style, stxlib::glyphs::RunBox& box);
    [[nodiscard]] stxlib::Status draw_run(stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                          const stxlib::glyphs::DrawStyle& style,
                                          stxlib::glyphs::RunCoverage& coverage);

    // The face's bytes and its typeface's together: held now, and the most
    // each has held since it was opened.
    [[nodiscard]] std::size_t bytes_held() const noexcept;
    [[nodiscard]] std::size_t bytes_peak() const noexcept;

    // Frees the face and uncounts its bytes; the face is as before open.
    void release() noexcept;

private:
    void account() noexcept;

    // Declared before face_, destroyed after it: the face holds the
    // typeface, whose bytes the program typeface keeps.
    std::shared_ptr<const ProgramTypeface> typeface_{};
    stxlib::glyphs::Face face_{};
    // The face's bytes counted under text_layout so far.
    std::size_t counted_{0U};
};

} // namespace gui_forms::detail
