#pragma once

// The faces a prepared font bank's rasterizations draw with (house style:
// allocate before repeated processing and reuse). A rasterization leases one
// set: one ExecutorFace per bank face, each opened over the bank's typeface
// the first time that set draws it and kept open, with the outlines it has
// fitted. Sets are made as concurrent rasterizations need them; a returned
// set is kept for the next lease while fewer than kept_sets are idle and
// released otherwise. So rasterizing a bank again and again opens its faces
// once per concurrent rasterizer, not once per call, and allocates its
// working storage likewise. The pool belongs to its
// bank (PreparedFontBank::face_pool) and is destroyed before the bank's
// typefaces. Prepared rasterization and text masks (whose banks are their
// own) both lease from it. Any thread: the idle sets are kept under the
// pool's mutex, and a leased set is used by its one rasterization only.

#include "gui_forms/prepared_text.hpp"
#include "../../resources/fonts/program_typefaces.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <stxlib/threads.hpp>
#include <utility>
#include <vector>

namespace gui_forms::detail {

// One rasterization's faces, in the bank's face order, and its working
// storage, kept with them from lease to lease: the placed glyphs of one run
// and STXlib's mask of one run.
struct PreparedRasterFaces final {
    std::array<ExecutorFace, PreparedTextLimits::font_faces> faces{};
    std::vector<stxlib::glyphs::RunGlyph> placed{};
    stxlib::glyphs::RunCoverage coverage{};
};

class PreparedFacePool final {
public:
    // The most idle sets kept between rasterizations.
    static constexpr std::size_t kept_sets = 4U;

    PreparedFacePool() = default;
    PreparedFacePool(const PreparedFacePool&) = delete;
    PreparedFacePool& operator=(const PreparedFacePool&) = delete;

    // An idle set, or a new one with no face open. std::bad_alloc when a new
    // one cannot be made.
    [[nodiscard]] std::unique_ptr<PreparedRasterFaces> lease();

    // Takes back a set lease gave: kept while fewer than kept_sets are idle,
    // released otherwise. Null is ignored.
    void restore(std::unique_ptr<PreparedRasterFaces> faces) noexcept;

    // The fitted bytes the idle sets' faces hold (their typefaces' are the
    // bank's).
    [[nodiscard]] std::size_t idle_face_bytes() const noexcept;

private:
    mutable stxlib::Mutex mutex_{};
    // Capacity kept_sets from the first lease, so restore never allocates.
    std::vector<std::unique_ptr<PreparedRasterFaces>> idle_{};
};

// One rasterization's lease of a set of a pool's faces, returned to the pool
// when the lease goes, on every path. The pool must outlive the lease: hold
// what holds the bank for longer.
class PreparedFaceLease final {
public:
    // std::bad_alloc when no set can be made.
    explicit PreparedFaceLease(PreparedFacePool& pool) : pool_(pool), set_(pool.lease()) {}
    ~PreparedFaceLease() { pool_.restore(std::move(set_)); }
    PreparedFaceLease(const PreparedFaceLease&) = delete;
    PreparedFaceLease& operator=(const PreparedFaceLease&) = delete;

    [[nodiscard]] PreparedRasterFaces& set() noexcept { return *set_; }

private:
    PreparedFacePool& pool_;
    std::unique_ptr<PreparedRasterFaces> set_;
};

} // namespace gui_forms::detail
