#pragma once

#include "../../core/text/prepared_window/prepared_window_batch.hpp"
#include "shaping/bounded_shape_workspace.hpp"
#include "shaping/text_shape_engine.hpp"

namespace gui_forms::render::text {

// Construct, initialize, shape and destroy on one assigned worker executor.
// Does not create a thread, queue, host wake or public session. Font bytes remain
// immutable and alive through native engine destruction and retained results.
class PreparedWindowShaper final {
public:
    // owner_bytes charges fixed first-party session context sharing this
    // workspace allowance. Opaque native/allocator/thread costs are separate.
    explicit PreparedWindowShaper(std::shared_ptr<const detail::PreparedFontBank> fonts,
        const std::size_t owner_bytes = 0U);
    PreparedWindowShaper(const PreparedWindowShaper&) = delete;
    PreparedWindowShaper& operator=(const PreparedWindowShaper&) = delete;
    [[nodiscard]] PreparedTextStatus initialize();
    // Requires the caller's exclusive mutable batch ownership for this call.
    // Existing geometry returns busy unchanged. Every other refusal preserves
    // input, committed fields and reservation. Partial geometry retires locally;
    // only complete current output is transferred as immutable arrays.
    // Cancellation is observed around paragraphs, not inside native calls.
    [[nodiscard]] PreparedTextStatus shape(detail::PreparedWindowBatchStorage& batch);
private:
    std::shared_ptr<const detail::PreparedFontBank> fonts_{};
    const std::size_t owner_bytes_{};
    std::thread::id executor_{std::this_thread::get_id()};
    BoundedShapeWorkspace workspace_{};
    std::unique_ptr<TextShapeEngine> engine_{};
    std::array<FontFaceId, PreparedTextLimits::font_faces> face_ids_{};
};

} // namespace gui_forms::render::text
