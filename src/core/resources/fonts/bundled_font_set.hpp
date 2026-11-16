#pragma once

#include "gui_forms/metrics/font_sources/bundled_font_sources.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/integrity/sha256/sha256.hpp"
#include "codepoint_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace stx_fontpack {
struct PackIndex;
} // namespace stx_fontpack

namespace gui_forms::detail {

class ProgramTypeface;

// The application's bundled font faces, verified before any parser sees them (owner decision,
// 2026-10-28). A shipped program reads them from the font pack compiled into the library
// (font pack stage 4, cmake/FontPack.cmake): a face's group is decoded the first time
// something asks for one of its faces (the eager groups the first time anything asks), every
// decoded face is checked against the CRC-32 the pack stores for it and against the size the
// manifest records, and only then is it copied into pages the set reserves, made read-only and
// handed out. A development build (GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY) may instead read a font
// directory, where each face is checked against its manifest size and SHA-256. Every window's
// text engine and raster borrows the same bytes through a shared owner, so a process holds one
// copy of each face.

// One face of assets/fonts/bundled-fonts.sha256, with the size the build measured.
struct BundledFaceRecord final {
    std::string_view file_name{};
    std::uint64_t size_bytes{};
    std::string_view sha256_hex{};
};

// The manifest compiled from assets/fonts/bundled-fonts.sha256, in its order. The records and
// their strings are static.
[[nodiscard]] std::span<const BundledFaceRecord> bundled_face_manifest() noexcept;

// The font pack compiled into the library (bundled_font_pack.cpp): static bytes, valid for the
// process. It holds every manifest face, less the fallback faces when the build leaves them out
// (GUI_FORMS_INSTALL_FALLBACK_FONTS).
[[nodiscard]] std::span<const std::byte> bundled_font_pack() noexcept;

// Whether this build reads a font directory the hosts find before the embedded pack
// (GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY, off for every shipped build).
[[nodiscard]] bool development_font_directories() noexcept;

// The checkout's assets/fonts that a development build reads after GUI_FORMS_FONT_DIR
// (BundledFontSource::source_tree; the build names it at configure time); empty in every
// shipped build. The hosts offer it as their second candidate.
[[nodiscard]] std::filesystem::path development_source_font_directory();

// How every host registers a bundled face (ADR-004): its manifest name, its role (none for a
// fallback face, which serves any role), weight and slant, and whether the window needs it. A
// window without a required face has no text; an optional face that fails is left out.
struct BundledFaceUse final {
    std::string_view file_name{};
    std::optional<FontRole> role{};
    std::uint16_t weight{400};
    bool italic{};
    bool required{};
    // The language a fallback face is drawn for first (FontSpec::language),
    // or none.
    TextLanguage language{};
};

// The bundled faces in registration order: the primaries, then the fallbacks in the order
// fallback selection tries them. The records and their strings are static.
[[nodiscard]] std::span<const BundledFaceUse> bundled_face_uses() noexcept;

// The characters a bundled fallback face covers: exactly the Unicode scalars the face's Unicode
// character map maps to a glyph (STXlib's glyph_of: the test that the text engine's fallback
// selection applies), generated from the face whose digest it names.
// bundled_font_coverage.inc holds the tables; `gui_forms_bundled_font_coverage_tests --write
// <that file>` regenerates them, and the same program without arguments checks them against
// the character maps.
struct BundledFaceCoverage final {
    std::string_view file_name{};
    std::string_view sha256_hex{};
    std::span<const CodepointRange> ranges{};
};

// Every generated coverage table.
[[nodiscard]] std::span<const BundledFaceCoverage> bundled_face_coverages() noexcept;

// The coverage of the manifest face `file_name`, when a table exists and was generated from the
// bytes the manifest names (its digest matches); otherwise none, and the face must be loaded to
// learn what it covers.
[[nodiscard]] std::optional<std::span<const CodepointRange>> bundled_face_coverage(
    std::string_view file_name) noexcept;

// The largest face the set reads (the raster's font limit as well).
inline constexpr std::uint64_t maximum_bundled_face_bytes = 64U * 1024U * 1024U;

// Page storage that can be made read-only: the capability a host gives the font set. The
// implementations live with the hosts: POSIX mmap and mprotect in src/host/posix/, Windows
// VirtualAlloc and VirtualProtect in src/host/windows/. None of them may call back into the set.
struct SealedPageFunctions final {
    // Returns `bytes` (more than zero) of zeroed, readable and writable pages, or null.
    std::byte* (*reserve)(std::size_t bytes) noexcept {};
    // Makes the pages `reserve` returned read-only. Returns false, leaving them writable, on
    // failure.
    bool (*seal)(std::byte* pages, std::size_t bytes) noexcept {};
    // Returns pages that `reserve` gave, sealed or not, with the byte count it was given.
    void (*release)(std::byte* pages, std::size_t bytes) noexcept {};
};

// The verified bytes of one face, in sealed pages this object owns, and the program's STXlib
// typeface over them (program_typefaces.hpp), opened when the face is sealed and registered for
// as long as this object lives, so every window and executor drawing the face draws the same
// typeface. Writing through `bytes()` faults. The typeface goes, then the pages, when the last
// shared owner goes.
class SealedFontFace final {
public:
    // Adopts `storage`, `size_bytes` long, from `pages.reserve`, already sealed.
    SealedFontFace(const SealedPageFunctions& pages, std::byte* storage,
                   std::size_t size_bytes) noexcept;
    ~SealedFontFace();
    SealedFontFace(const SealedFontFace&) = delete;
    SealedFontFace& operator=(const SealedFontFace&) = delete;
    SealedFontFace(SealedFontFace&&) = delete;
    SealedFontFace& operator=(SealedFontFace&&) = delete;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

    // Takes the typeface opened over bytes(); before the face is shared, once.
    void adopt_typeface(std::unique_ptr<ProgramTypeface> typeface) noexcept;
    // The typeface, or null when STXlib refused the face. Borrowed while this lives.
    [[nodiscard]] const ProgramTypeface* typeface() const noexcept;

private:
    void (*release_)(std::byte* pages, std::size_t bytes) noexcept;
    std::byte* storage_;
    std::size_t size_bytes_;
    // Reads the pages: released first, in the destructor's body, before them.
    std::unique_ptr<ProgramTypeface> typeface_{};
};

// Why a face is not available.
enum class FontSetError : std::uint8_t {
    unknown_face,    // the manifest does not list the name
    unreadable,      // missing, not a regular file, or the read stopped short
    size_mismatch,   // the file's size is not the manifest's
    digest_mismatch, // the bytes' SHA-256 is not the manifest's
    page_failure,    // the pages could not be reserved or made read-only
    pack_refused,    // the font pack, or the face's group in it, failed its checks
                     // (bundled_font_pack_refusal says how)
};

// A face request's result: the shared face, or why there is none.
class FontFaceLoad final {
public:
    // `face` must not be null (std::invalid_argument).
    [[nodiscard]] static FontFaceLoad loaded(std::shared_ptr<const SealedFontFace> face);
    [[nodiscard]] static FontFaceLoad failed(FontSetError error) noexcept;

    [[nodiscard]] bool accepted() const noexcept;
    // The face, or null when the request failed. Borrowed from this result.
    [[nodiscard]] const std::shared_ptr<const SealedFontFace>* face() const noexcept;
    // The failure, or none when the request succeeded.
    [[nodiscard]] std::optional<FontSetError> error() const noexcept;

private:
    using State = std::variant<std::shared_ptr<const SealedFontFace>, FontSetError>;
    explicit FontFaceLoad(State state) noexcept;
    State state_;
};

// The bundled faces of one application run, shared by all of its windows. Any thread: a
// fallback face is read when text first needs it, on whichever thread shapes that text.
// A face is read (from a directory) or its group decoded (from a pack) on its first request;
// the result, a failure included, is kept and returned to every later request, so a damaged
// install or pack is read once and reported the same way each time. A pack group is decoded
// on the core worker pool (gui_forms::AtomicThreadPool, one per decode on the requesting
// thread), or in turn on the requesting thread where no pool can be made (a single-threaded
// Emscripten build, one hardware thread, or a pool that fails to start). Faces handed out stay
// valid after the set is destroyed, for as long as a borrower keeps its owner.
class BundledFontSet final {
public:
    // The compiled manifest, read from `directory`.
    BundledFontSet(std::filesystem::path directory, const SealedPageFunctions& pages);
    // Another manifest, for tests. `manifest` and its strings must outlive the set. Throws
    // std::invalid_argument for a missing page function, a malformed digest, or a size of zero
    // or above maximum_bundled_face_bytes.
    BundledFontSet(std::filesystem::path directory, std::span<const BundledFaceRecord> manifest,
                   const SealedPageFunctions& pages);
    // The compiled manifest's faces, decoded from `pack` (bundled_font_pack(), or a test's
    // copy). `pack` must stay valid and unchanged for the set's life; the faces handed out are
    // copies and do not borrow it. A pack that does not open is not an exception: every face
    // is then refused with pack_refused and the refusal is recorded (bundled_font_pack_refusal).
    // A manifest face the pack does not hold is unreadable.
    BundledFontSet(std::span<const std::byte> pack, const SealedPageFunctions& pages);
    // Another manifest over `pack`, for tests; throws as the directory form does.
    BundledFontSet(std::span<const std::byte> pack, std::span<const BundledFaceRecord> manifest,
                   const SealedPageFunctions& pages);
    ~BundledFontSet();
    BundledFontSet(const BundledFontSet&) = delete;
    BundledFontSet& operator=(const BundledFontSet&) = delete;

    // The face the manifest lists under `file_name`.
    [[nodiscard]] FontFaceLoad face(std::string_view file_name);
    // Faces loaded so far and their bytes, for memory reports.
    [[nodiscard]] std::size_t loaded_face_count() const;
    [[nodiscard]] std::uint64_t loaded_bytes() const;
    // Pack groups decoded so far, refused ones included; 0 for a directory set.
    [[nodiscard]] std::size_t decoded_group_count() const;

private:
    [[nodiscard]] FontFaceLoad load(std::size_t index) const;
    // The pack forms of load; each needs mutex_ held.
    [[nodiscard]] FontFaceLoad load_from_pack(std::size_t index);
    void decode_eager_groups();
    void decode_group(std::uint32_t group);
    void refuse_group(std::uint32_t group, std::string_view reason);
    [[nodiscard]] FontFaceLoad seal_copy(std::size_t index, const std::uint8_t* data,
                                         std::size_t size_bytes) const;
    // The manifest position of the face called `file_name`, or none.
    [[nodiscard]] std::optional<std::size_t> manifest_position(std::string_view file_name) const noexcept;

    std::filesystem::path directory_;
    std::span<const BundledFaceRecord> manifest_;
    SealedPageFunctions pages_;
    // Parallel to manifest_.
    std::vector<Sha256Digest> digests_;
    // Guards results_ and the pack state below; held while a face is read or its group
    // decoded, so each face is read once and each group decoded once.
    mutable std::mutex mutex_;
    // Parallel to manifest_; empty until the face is first requested (or its group decoded).
    std::vector<std::optional<FontFaceLoad>> results_;
    // Pack sets only: the pack (borrowed), its index (which borrows the pack), whether it
    // opened, each manifest face's position among the pack's fonts (no_pack_font when the
    // pack lacks it), each group's state (1 once decoded or refused), and how many groups
    // have been decoded.
    static constexpr std::uint32_t no_pack_font = 0xffffffffU;
    bool pack_set_{};
    std::span<const std::byte> pack_{};
    std::unique_ptr<stx_fontpack::PackIndex> index_{};
    bool pack_opened_{};
    bool eager_decoded_{};
    std::vector<std::uint32_t> pack_fonts_{};
    std::vector<std::uint8_t> group_decoded_{};
    std::size_t decoded_groups_{};
};

// The process's bundled font set for `directory`. Every window that renders bundled text
// holds it, and so does an application run for its length, so all of them share one copy of
// each face; when the last holder lets go, the set and its pages go too, and the next request
// makes a new one. A request for another source makes a separate set, which then becomes
// the shared one. Any thread: the hosts ask on the UI thread, GUI.Drawing's executors on
// their own.
[[nodiscard]] std::shared_ptr<BundledFontSet> shared_bundled_font_set(
    const std::filesystem::path& directory, const SealedPageFunctions& pages);

// One directory a host offers to locate_bundled_fonts, and the source it stands for. An empty
// directory (the host could not work it out) is skipped.
struct BundledFontCandidate final {
    BundledFontSource source{BundledFontSource::none};
    std::filesystem::path directory{};
};

// Where locate_bundled_fonts found the fonts: the source and its directory (empty for the
// embedded pack), or none and an empty directory.
struct BundledFontLocation final {
    BundledFontSource source{BundledFontSource::none};
    std::filesystem::path directory{};
};

// The process's shared set for `location`: the embedded pack's for embedded_pack, the
// directory's for any other source, null for none. Shared as the directory form is. Any
// thread.
[[nodiscard]] std::shared_ptr<BundledFontSet> shared_bundled_font_set(
    const BundledFontLocation& location, const SealedPageFunctions& pages);

// What one lookup consults: the hosts' directories in their order, whether they are honoured
// (development_font_directories in the hosts' lookups; either way in tests), and the pack to
// fall back to.
struct BundledFontLookup final {
    std::span<const BundledFontCandidate> candidates{};
    bool directories_honoured{};
    std::span<const std::byte> pack{};
};

// Whether `directory` holds a regular file for every required face of bundled_face_uses. Only
// the names are checked; the set checks each face's size and digest when it reads it.
[[nodiscard]] bool directory_holds_required_faces(const std::filesystem::path& directory);

// Where the bundled faces come from. When directories are honoured, the first candidate, in
// order, whose directory holds the required faces; otherwise, or when none does, the pack, if
// it opens (embedded_pack, with a notice naming the directories tried when they were
// honoured). Records the lookup in bundled_font_diagnostics; when the pack does not open
// either, the answer is none, the refusal (bundled_font_refusal) says so and names every
// directory tried, and the process's first such lookup writes it to stderr. UI thread, as the
// hosts' window setup is.
[[nodiscard]] BundledFontLocation locate_bundled_fonts(const BundledFontLookup& lookup);

// The hosts' lookup: `candidates` honoured only when development_font_directories, then the
// embedded pack.
[[nodiscard]] BundledFontLocation locate_bundled_fonts(
    std::span<const BundledFontCandidate> candidates);

} // namespace gui_forms::detail
