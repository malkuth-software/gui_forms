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

namespace gui_forms::detail {

// The application's bundled font faces, verified before any parser sees them (owner decision,
// 2026-10-28): each face is read once from the install's font directory into pages the set
// reserves, its size and SHA-256 are checked against the manifest compiled into the library,
// and only then are the pages made read-only and handed out. Every window's text engine and
// raster borrows the same bytes through a shared owner, so a process holds one copy of each
// face, and a face is read only when something first asks for it.

// One face of assets/fonts/bundled-fonts.sha256, with the size the build measured.
struct BundledFaceRecord final {
    std::string_view file_name{};
    std::uint64_t size_bytes{};
    std::string_view sha256_hex{};
};

// The manifest compiled from assets/fonts/bundled-fonts.sha256, in its order. The records and
// their strings are static.
[[nodiscard]] std::span<const BundledFaceRecord> bundled_face_manifest() noexcept;

// How every host registers a bundled face (ADR-004): its manifest name, its role (none for a
// fallback face, which serves any role), weight and slant, and whether the window needs it. A
// window without a required face has no text; an optional face that fails is left out.
struct BundledFaceUse final {
    std::string_view file_name{};
    std::optional<FontRole> role{};
    std::uint16_t weight{400};
    bool italic{};
    bool required{};
};

// The bundled faces in registration order: the primaries, then the fallbacks in the order
// fallback selection tries them. The records and their strings are static.
[[nodiscard]] std::span<const BundledFaceUse> bundled_face_uses() noexcept;

// The characters a bundled fallback face covers: exactly the Unicode scalars FreeType maps to a
// glyph in the face's Unicode charmap (the test that the text engine's fallback selection
// applies), generated from the face whose digest it names. bundled_font_coverage.inc holds the
// tables; `gui_forms_bundled_font_coverage_tests --write <that file>` regenerates them, and the
// same program without arguments checks them against FreeType.
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

// The verified bytes of one face, in sealed pages this object owns. Writing through `bytes()`
// faults. The pages are released when the last shared owner goes.
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

private:
    void (*release_)(std::byte* pages, std::size_t bytes) noexcept;
    std::byte* storage_;
    std::size_t size_bytes_;
};

// Why a face is not available.
enum class FontSetError : std::uint8_t {
    unknown_face,    // the manifest does not list the name
    unreadable,      // missing, not a regular file, or the read stopped short
    size_mismatch,   // the file's size is not the manifest's
    digest_mismatch, // the bytes' SHA-256 is not the manifest's
    page_failure,    // the pages could not be reserved or made read-only
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
// A face is read on its first request; the result, a failure included, is kept and returned
// to every later request, so a damaged install is read once and reported the same way each
// time. Faces handed out stay valid after the set is destroyed, for as long as a borrower
// keeps its owner.
class BundledFontSet final {
public:
    // The compiled manifest, read from `directory`.
    BundledFontSet(std::filesystem::path directory, const SealedPageFunctions& pages);
    // Another manifest, for tests. `manifest` and its strings must outlive the set. Throws
    // std::invalid_argument for a missing page function, a malformed digest, or a size of zero
    // or above maximum_bundled_face_bytes.
    BundledFontSet(std::filesystem::path directory, std::span<const BundledFaceRecord> manifest,
                   const SealedPageFunctions& pages);
    BundledFontSet(const BundledFontSet&) = delete;
    BundledFontSet& operator=(const BundledFontSet&) = delete;

    // The face the manifest lists under `file_name`.
    [[nodiscard]] FontFaceLoad face(std::string_view file_name);
    // Faces loaded so far and their bytes, for memory reports.
    [[nodiscard]] std::size_t loaded_face_count() const;
    [[nodiscard]] std::uint64_t loaded_bytes() const;

private:
    [[nodiscard]] FontFaceLoad load(std::size_t index) const;

    std::filesystem::path directory_;
    std::span<const BundledFaceRecord> manifest_;
    SealedPageFunctions pages_;
    // Parallel to manifest_.
    std::vector<Sha256Digest> digests_;
    // Guards results_; held while a face is read, so each face is read once.
    mutable std::mutex mutex_;
    // Parallel to manifest_; empty until the face is first requested.
    std::vector<std::optional<FontFaceLoad>> results_;
};

// The process's bundled font set for `directory`. Every window that renders bundled text
// holds it, and so does an application run for its length, so all of them share one copy of
// each face; when the last holder lets go, the set and its pages go too, and the next request
// makes a new one. A request for another directory makes a separate set, which then becomes
// the shared one. UI thread only.
[[nodiscard]] std::shared_ptr<BundledFontSet> shared_bundled_font_set(
    const std::filesystem::path& directory, const SealedPageFunctions& pages);

// One directory a host offers to locate_bundled_fonts, and the source it stands for. An empty
// directory (the host could not work it out) is skipped.
struct BundledFontCandidate final {
    BundledFontSource source{BundledFontSource::none};
    std::filesystem::path directory{};
};

// Where locate_bundled_fonts found the fonts: the source and its directory, or none and an
// empty directory.
struct BundledFontLocation final {
    BundledFontSource source{BundledFontSource::none};
    std::filesystem::path directory{};
};

// Whether `directory` holds a regular file for every required face of bundled_face_uses. Only
// the names are checked; the set checks each face's size and digest when it reads it.
[[nodiscard]] bool directory_holds_required_faces(const std::filesystem::path& directory);

// The first of `candidates`, in order, whose directory holds the required faces. Records the
// lookup in bundled_font_diagnostics; when no candidate qualifies it also records the refusal
// (bundled_font_refusal) naming every directory tried, and the process's first such lookup
// writes that refusal to stderr. UI thread, as the hosts' window setup is.
[[nodiscard]] BundledFontLocation locate_bundled_fonts(
    std::span<const BundledFontCandidate> candidates);

} // namespace gui_forms::detail
