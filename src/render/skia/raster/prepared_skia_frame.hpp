#pragma once

#include "../../../core/text/prepared/prepared_storage.hpp"
#include "../../../core/text/composite/text_composite.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "include/core/SkSurface.h"

#include <stxlib/threads.hpp>

namespace gui_forms::render {

// Composites prepared-text coverage into a canvas with the shared ADR-004
// rule and the owner's recorded clips. False when it cannot address the
// canvas; the frame then draws the mask through Skia.
class PreparedCoverageSink {
public:
    virtual ~PreparedCoverageSink() = default;
    [[nodiscard]] virtual bool composite(SkCanvas& target, const detail::TextCoverage& coverage, Color color) = 0;
};

// Private CPU frame owner. All calls/borrows belong to the construction thread.
// Front pixels remain immutable until commit; candidate pixels are never exposed.
class PreparedSkiaFrame final {
public:
    static constexpr std::size_t pixel_limit = 16'777'216;
    PreparedSkiaFrame() = default;
    PreparedSkiaFrame(const PreparedSkiaFrame&) = delete;
    PreparedSkiaFrame& operator=(const PreparedSkiaFrame&) = delete;
    [[nodiscard]] PreparedTextStatus begin(const Size size, const double scale, DamageRegion& damage);
    [[nodiscard]] PreparedTextStatus commit(const PaintReceipt receipt);
    void abort() noexcept;
    [[nodiscard]] PreparedTextPaintResult draw(const PreparedTextLayout& layout,
        const LayoutAuthority expected, const Point baseline, const Color color,
        PreparedCoverageSink* sink = nullptr);
    [[nodiscard]] SkCanvas* canvas() const noexcept;
    [[nodiscard]] SkSurface* front() const noexcept {
        SkSurface* const result = front_.get();
        return result;
    }
    [[nodiscard]] PaintReceipt receipt() const noexcept { return receipt_; }
    [[nodiscard]] double front_scale() const noexcept { return front_scale_; }
    [[nodiscard]] bool matches(const Size size, const double scale) const noexcept;
    [[nodiscard]] bool on_executor() const noexcept {
        const bool result = stxlib::current_thread() == executor_;
        return result;
    }
private:
    friend struct PreparedSkiaFrameTestAccess;
    [[nodiscard]] static sk_sp<SkImage> make_mask_image(GrayTextMask& mask);
    struct Authorities final {
        std::array<std::shared_ptr<const detail::PreparedTextStorage>, 64> owners{};
        std::size_t count{0};
        void clear() noexcept;
        [[nodiscard]] PreparedTextStatus retain(const std::shared_ptr<const detail::PreparedTextStorage>& storage);
        [[nodiscard]] bool current() const;
    };
    sk_sp<SkSurface> front_{};
    sk_sp<SkSurface> candidate_{};
    Authorities front_authorities_{};
    Authorities candidate_authorities_{};
    Size front_size_{};
    Size candidate_size_{};
    double front_scale_{1.0};
    double candidate_scale_{1.0};
    PaintReceipt receipt_{};
    stxlib::ThreadIdentity executor_{stxlib::current_thread()};
    bool active_{false};
    bool failed_{false};
    // Deterministic allocation refusal fixture; never selected by the host.
    bool fail_next_allocation_{false};
};
} // namespace gui_forms::render
