#pragma once

// The program's typefaces, part of the font family registry: one STXlib
// Typeface per registered face for the whole program (owner, 2026-11-16:
// "the store should be program, not window"), and one Face per executor over
// it (an ExecutorFace), since a Face is used by one thread at a time
// (stxlib/glyph_face.hpp). The four text rasterizers draw through them:
// the glyph-run rasterizer (interface text and the C drawing API), prepared
// text, text masks and the paint probe.
//
// Who holds a typeface. A typeface reads its font's bytes in place, so it is
// held by the object that holds those bytes, beside them, and lives exactly
// as long as they do; nothing else owns one.
// - A registered face. The registry opens the typeface when it first holds
//   the face's bytes and keeps it with them: the bundled font set in each
//   sealed face (SealedFontFace, bundled_font_set.hpp), the application font
//   registry in each loaded family's byte block (app_font_registry.cpp). It
//   then registers the pair here, so every executor and window that draws
//   those bytes finds the same typeface. The bundled set is the process's,
//   shared by every window and executor and held by the application run for
//   its length, so its typefaces are the program's; nothing is released for
//   termination (rule 1) and the table below has no static destructor.
// - A face no registry holds (a font a GUI.Drawing executor registers under
//   its own name, a test's bytes, the paint probe's sources): the first
//   executor to draw it opens a typeface that the executors drawing it share
//   and that goes with the last of them (program_typeface).
// - A prepared font bank's face: the bank owns its bytes and a typeface for
//   each (prepared_storage.hpp); no other object can hold those bytes.
//
// What an executor gets is a borrow (TypefaceBorrow): the typeface and a
// keeper, the shared owner that holds it and its bytes, so a Face opened over
// it can never outlive either.
//
// Unloading. An application family can be unloaded and loaded again
// (app_fonts.hpp). Unloading retires each of its typefaces at once: a face
// still open over one is rejected on its next draw (released, and the draw
// fails as revoked) and none opens over it again. Its bytes go, as before,
// with the last engine or face that keeps them. A reload is a new family with
// new typefaces.
//
// Memory, in bytes (owner, 2026-11-16: text memory limits are in bytes). A
// typeface's glyph store is bounded by TypefaceByteLimits and a face's
// fitted outlines by its fitted-byte limit. What each holds, as STXlib
// reports it (bytes_held), is counted under AllocationSubsystem::text_layout
// as it changes: a growth as one allocation of the difference, a fall as one
// release, and what is left as a release when the object goes. bytes_peak
// is each one's most since it was opened or its peak was last reset.
//
// Threads. Everything on ProgramTypeface and the free functions: any thread.
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
    // `owner` keeps the bytes `typeface` was opened from alive; it is empty
    // when the object that owns this typeface owns the bytes too (a sealed
    // face, an application family's block, a prepared font bank).
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

    // Marks the typeface's family unloaded: faces over it are rejected from
    // now on. Const and any thread: retiring changes no glyph, only whether
    // executors may draw them.
    void retire() const noexcept;
    [[nodiscard]] bool retired() const noexcept;

private:
    // Declared first, destroyed last: the typeface borrows the bytes.
    std::shared_ptr<const void> owner_;
    stxlib::Shared<stxlib::glyphs::Typeface> typeface_;
    // The bytes counted under text_layout so far. Mutable: counting is not a
    // change to the typeface, and any thread may count.
    mutable std::atomic<std::size_t> counted_{0U};
    mutable std::atomic<bool> retired_{false};
};

// A typeface as an executor borrows it: `typeface` stays valid, with the
// bytes it reads, while `keeper` is held. `keeper` is empty only for a
// prepared font bank's typeface, which the bank keeps and which outlives
// every face over it (prepared_storage.hpp). Null `typeface`: none.
struct TypefaceBorrow final {
    std::shared_ptr<const void> keeper{};
    const ProgramTypeface* typeface{};
};

// Opens face `face_index` of the font in `bytes` as a typeface the caller
// owns beside the bytes (a registry or a prepared font bank). Null when
// STXlib refuses the face (not a font it reads, a needed table missing or
// cut short) or its storage runs out; std::bad_alloc when ours does.
[[nodiscard]] std::unique_ptr<ProgramTypeface> open_program_typeface(std::span<const std::byte> bytes,
                                                                     std::uint32_t face_index,
                                                                     const TypefaceByteLimits& limits = TypefaceByteLimits{});

// Registers `typeface`, opened over face `face_index` of `bytes`, as the
// program's typeface of those bytes for as long as `owner` lives; `owner`
// holds both (the sealed face or the family's byte block). Executors find it
// through program_typeface with the same owner. False when the table's
// storage runs out: executors then open their own typeface of those bytes
// (program_typeface's unregistered path), which draws the same.
[[nodiscard]] bool register_program_typeface(std::span<const std::byte> bytes, std::uint32_t face_index,
                                             const std::shared_ptr<const void>& owner,
                                             const ProgramTypeface& typeface) noexcept;

// The typeface an executor draws face `face_index` of `bytes` with. `owner`
// keeps the bytes alive (as an engine's face source gives it). The
// typeface that owner registered for these bytes when there is one, retired
// or not (ExecutorFace refuses a retired one); else the shared unregistered
// typeface of these bytes, opened now with `limits` if no executor holds
// one. An empty borrow when STXlib refuses the face or its storage runs out;
// std::bad_alloc when the table's does.
[[nodiscard]] TypefaceBorrow program_typeface(std::span<const std::byte> bytes, std::shared_ptr<const void> owner,
                                              std::uint32_t face_index,
                                              const TypefaceByteLimits& limits = TypefaceByteLimits{});

// How many typefaces the table knows of whose holders still hold them,
// registered and unregistered. For tests.
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

    // Opens over `borrow`, keeping its keeper. False, with the face left
    // closed, for an empty borrow or a retired typeface, or when STXlib
    // refuses the limits or memory runs out. A face already open is released
    // first.
    [[nodiscard]] bool open(TypefaceBorrow borrow, const FaceByteLimits& limits);

    [[nodiscard]] bool is_open() const noexcept;

    // The font opened, or null for a face not open.
    [[nodiscard]] const stxlib::glyphs::Font* font() const noexcept;

    // stxlib::glyphs::Face's measure_run and draw_run, then the counts. A
    // face not open fails as disposed; a face whose typeface was retired
    // since it opened is released and fails as revoked.
    [[nodiscard]] stxlib::OperationStatus measure_run(stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                             const stxlib::glyphs::DrawStyle& style, stxlib::glyphs::RunBox& box);
    [[nodiscard]] stxlib::OperationStatus draw_run(stxlib::View<stxlib::glyphs::RunGlyph> glyphs,
                                          const stxlib::glyphs::DrawStyle& style,
                                          stxlib::glyphs::RunCoverage& coverage);

    // The face's bytes and its typeface's together: held now, and the most
    // each has held since it was opened.
    [[nodiscard]] std::size_t bytes_held() const noexcept;
    [[nodiscard]] std::size_t bytes_peak() const noexcept;

    // The face's own bytes held (its fitted outlines), without its
    // typeface's.
    [[nodiscard]] std::size_t face_bytes_held() const noexcept;

    // Frees the face and uncounts its bytes; the face is as before open.
    void release() noexcept;

private:
    // Fails as disposed, or as revoked after releasing a retired face, or
    // succeeds.
    [[nodiscard]] stxlib::OperationStatus admit() noexcept;
    void account() noexcept;

    // Declared first, destroyed last: it keeps the typeface and its bytes.
    std::shared_ptr<const void> keeper_{};
    const ProgramTypeface* typeface_{};
    stxlib::glyphs::Face face_{};
    // The face's bytes counted under text_layout so far.
    std::size_t counted_{0U};
};

} // namespace gui_forms::detail
