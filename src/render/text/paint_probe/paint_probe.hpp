#pragma once

#include "worker_probe.hpp"
#include "../../../core/resources/fonts/program_typefaces.hpp"

#include <array>
#include <stxlib/threads.hpp>

namespace gui_forms::render::paint_probe {
enum class Status { success, stale, missing_font, incompatible_face, incompatible_glyph,
                    invalid_geometry, unsupported_raster, native_failure, resource_failure };

// A private fixed grayscale surface. This is not Skia or a public Painter.
// It draws with STXlib's glyph face: one face per pinned font, opened on
// its executor at the first paint over the program's typeface of the
// pinned bytes, and kept for every later paint.
class Painter final {
public:
    explicit Painter(std::shared_ptr<const worker_probe::FontSet> pinned_fonts);
    [[nodiscard]] Status paint(const worker_probe::Prepared& prepared,
                               const worker_probe::Identity& expected,
                               std::uint64_t authority_epoch);
    // Foreground-only borrow; valid until successful paint or destruction.
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept;
    [[nodiscard]] const worker_probe::Identity& painted_identity() const noexcept;
    static constexpr std::size_t width = 800;
    static constexpr std::size_t height = 128;
private:
    std::shared_ptr<const worker_probe::FontSet> pinned_fonts_{};
    // Declared after pinned_fonts_, destroyed before it: each face's
    // typeface keeps its pinned bytes, and the faces are the executor's.
    std::array<gui_forms::detail::ExecutorFace, 4> faces_{};
    stxlib::ThreadIdentity executor_{};
    std::vector<std::uint8_t> pixels_{};
    worker_probe::Identity painted_identity_{};
};
} // namespace gui_forms::render::paint_probe
