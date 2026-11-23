#pragma once
#include "prepared_window_input.hpp"

#include <stxlib/threads.hpp>

namespace gui_forms::detail {
// Private admission state, confined to the recorded executor for desire and
// admission. The mutex also permits closing/revocation by the lifetime owner.
struct PreparedWindowBatchAuthority final {
    stxlib::Mutex mutex{};
    stxlib::ThreadIdentity executor{stxlib::current_thread()};
    std::optional<PreparedWindowKey> desired{};
    // Revoking desired must not erase lifetime identity or permit epoch reuse.
    std::uint64_t controller_instance{};
    std::uint64_t session{};
    std::uint64_t last_epoch{};
    bool closing{};
};
// Admission descriptors. Shaping adds a separate immutable geometry table;
// paragraph bytes remain solely in immutable input storage.
struct PreparedWindowRowStorage final {
    std::size_t paragraph_index{};
};
// Glyph/run coordinates remain paragraph-relative device units. Source mapping
// belongs to the retained paragraph descriptor, never to a glyph index.
// Arrays become const at transfer; even a const unique_ptr alone would not
// make mutable array elements immutable.
struct PreparedWindowGeometryRow final {
    std::size_t paragraph_index{};
    PreparedTextMetrics metrics{};
    // Candidate screen line boxes, all in device pixels relative to batch top.
    // Original shaped metrics above remain unchanged; these are not caret data.
    double top_device{};
    double baseline_device{};
    double height_device{};
    std::unique_ptr<const render::text::BoundedFontRun[]> runs{};
    std::unique_ptr<const render::text::ShapedGlyph[]> glyphs{};
    std::size_t run_count{};
    std::size_t glyph_count{};
};
struct PreparedWindowBatchStorage final {
    PreparedReservation reservation{};
    std::shared_ptr<PreparedLedger> ledger{};
    std::shared_ptr<PreparedWindowBatchAuthority> authority{};
    std::shared_ptr<const PreparedFontBank> fonts{};
    PreparedWindowKey key{};
    std::unique_ptr<const PreparedWindowInputData> input{};
    std::unique_ptr<const PreparedWindowRowStorage[]> rows{};
    std::size_t row_count{};
    std::size_t requested_bytes{};
    std::unique_ptr<const PreparedWindowGeometryRow[]> geometry{};
    std::array<FontFaceId, PreparedTextLimits::font_faces> face_ids{};
    std::size_t run_count{};
    std::size_t glyph_count{};
    std::size_t workspace_peak_bytes{};
    // Logical advance/line extent, not ink bounds or a raster allocation/crop.
    double width_device{};
    double height_device{};
};
// After the first desire, session/controller remain fixed and epoch must
// strictly increase, even for identical text. Exhaustion cannot wrap or revive.
[[nodiscard]] PreparedTextStatus desire_prepared_window(PreparedWindowBatchAuthority&,
    const PreparedWindowKey&);
// Ownership admission only. Occupied output, stale/closing/wrong-executor,
// incompatible ledger/font, allocation or budget failure preserve input/output.
// Success transfers arrays without copying and releases the input-slot charge.
// The single payload reservation survives through the final retained owner.
[[nodiscard]] PreparedTextStatus admit_prepared_window(const PreparedWindowKey& expected,
    std::shared_ptr<PreparedWindowBatchAuthority>, std::shared_ptr<PreparedLedger>,
    std::shared_ptr<const PreparedFontBank>, PreparedWindowInput&,
    std::unique_ptr<PreparedWindowBatchStorage>& output);
[[nodiscard]] bool prepared_window_batch_current(const PreparedWindowBatchStorage&);
// Observation only: does not grant desire/admission rights to the worker.
[[nodiscard]] PreparedTextStatus prepared_window_worker_current(const PreparedWindowBatchStorage&);
// Caller holds authority.mutex then ledger.mutex through check and publication.
[[nodiscard]] PreparedTextStatus prepared_window_worker_current_locked(
    const PreparedWindowBatchStorage&) noexcept;
}
