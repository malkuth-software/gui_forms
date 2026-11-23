#include "prepared_skia_frame.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>
#include <optional>
#include <stxlib/threads.hpp>

namespace gui_forms::render {
namespace {
struct MaskImageOwner final {
    GrayTextMask mask{};
    std::atomic<bool> released{false};
};
void release_mask_image(const void* const, void* const context) {
    std::shared_ptr<MaskImageOwner>* const retained = static_cast<std::shared_ptr<MaskImageOwner>*>(context);
    (*(*retained)).released.store(true, std::memory_order_release);
    delete retained;
}
// Holds up to 64 authorities' mutexes, taken in order and released in the
// reverse order when it goes, as an array of held locks would be.
class HeldAuthorityLocks final {
public:
    HeldAuthorityLocks() noexcept = default;
    ~HeldAuthorityLocks() {
        for (std::size_t index = count_; index > 0U; --index) (*held_[index - 1U]).unlock();
    }
    HeldAuthorityLocks(const HeldAuthorityLocks&) = delete;
    HeldAuthorityLocks& operator=(const HeldAuthorityLocks&) = delete;
    HeldAuthorityLocks(HeldAuthorityLocks&&) = delete;
    HeldAuthorityLocks& operator=(HeldAuthorityLocks&&) = delete;

    void take(stxlib::Mutex& mutex) noexcept {
        mutex.lock();
        held_[count_] = &mutex;
        ++count_;
    }

private:
    std::array<stxlib::Mutex*, 64> held_{};
    std::size_t count_{};
};
}
sk_sp<SkImage> PreparedSkiaFrame::make_mask_image(GrayTextMask& mask) {
    const std::shared_ptr<MaskImageOwner> owner = std::make_shared<MaskImageOwner>();
    (*owner).mask = std::move(mask);
    const GrayTextMask& source = (*owner).mask;
    const SkImageInfo info = SkImageInfo::Make(static_cast<int>(source.width()), static_cast<int>(source.height()),
        kAlpha_8_SkColorType, kPremul_SkAlphaType);
    const SkPixmap pixels(info, source.pixels().data(), source.width());
    std::shared_ptr<MaskImageOwner>* const context = new std::shared_ptr<MaskImageOwner>(owner);
    sk_sp<SkImage> result{};
    try {
        result = SkImages::RasterFromPixmap(pixels, release_mask_image, context);
    } catch (...) {
        if (!(*owner).released.load(std::memory_order_acquire)) delete context;
        throw;
    }
    // The pinned factory rejects invalid arguments before installing its release
    // callback. The local owner also survives a synchronous failure callback.
    if (!result && !(*owner).released.load(std::memory_order_acquire)) delete context;
    return result;
}

namespace {
bool full_damage(const DamageRegion& damage, const Size size) {
    const std::span<const Rect> rectangles = damage.rectangles();
    for (std::size_t index = 0; index < rectangles.size(); ++index) {
        const Rect rect = rectangles[index];
        if (rect.x <= 0.0 && rect.y <= 0.0 && rect.right() >= size.width && rect.bottom() >= size.height) return true;
    }
    return false;
}
}

void PreparedSkiaFrame::Authorities::clear() noexcept {
    for (std::size_t index = 0; index < count; ++index) owners[index].reset();
    count = 0;
}
PreparedTextStatus PreparedSkiaFrame::Authorities::retain(const std::shared_ptr<const detail::PreparedTextStorage>& storage) {
    const LayoutAuthority authority = (*storage).authority;
    std::size_t position = 0;
    while (position < count && (*owners[position]).authority.session < authority.session) ++position;
    if (position < count && (*owners[position]).authority.session == authority.session) {
        if (!same_layout_authority((*owners[position]).authority, authority)) return PreparedTextStatus::stale;
        return PreparedTextStatus::success;
    }
    if (count == owners.size()) return PreparedTextStatus::budget_exceeded;
    for (std::size_t index = count; index > position; --index) owners[index] = std::move(owners[index - 1U]);
    owners[position] = storage;
    ++count;
    return PreparedTextStatus::success;
}
bool PreparedSkiaFrame::Authorities::current() const {
    for (std::size_t index = 0; index < count; ++index) {
        if (!detail::prepared_authority_current(*owners[index], (*owners[index]).authority)) return false;
    }
    return true;
}

bool PreparedSkiaFrame::matches(const Size size, const double scale) const noexcept {
    const bool result = front_ && size.width == front_size_.width && size.height == front_size_.height && scale == front_scale_;
    return result;
}
SkCanvas* PreparedSkiaFrame::canvas() const noexcept {
    SkCanvas* result = nullptr;
    if (active_ && candidate_) result = (*candidate_).getCanvas();
    return result;
}
PreparedTextStatus PreparedSkiaFrame::begin(const Size size, const double scale, DamageRegion& damage) {
    if (stxlib::current_thread() != executor_) return PreparedTextStatus::wrong_executor;
    if (active_) return PreparedTextStatus::busy;
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || !std::isfinite(scale) ||
        size.width <= 0.0 || size.height <= 0.0 || scale < 0.5 || scale > 4.0) return PreparedTextStatus::invalid_geometry;
    const double width_value = std::ceil(size.width * scale);
    const double height_value = std::ceil(size.height * scale);
    if (!std::isfinite(width_value) || !std::isfinite(height_value) || width_value > 16384.0 || height_value > 16384.0) {
        return PreparedTextStatus::budget_exceeded;
    }
    const int width = static_cast<int>(width_value);
    const int height = static_cast<int>(height_value);
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (count > pixel_limit) return PreparedTextStatus::budget_exceeded;
    try {
        // Drop obsolete scratch before allocating: at most front + candidate.
        if (candidate_ && ((*candidate_).width() != width || (*candidate_).height() != height)) candidate_.reset();
        if (!candidate_) {
            if (fail_next_allocation_) {
                fail_next_allocation_ = false;
                return PreparedTextStatus::resource_failure;
            }
            const SkImageInfo info = SkImageInfo::Make(width, height, kBGRA_8888_SkColorType,
                kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
            candidate_ = SkSurfaces::Raster(info);
        }
        if (!candidate_) return PreparedTextStatus::resource_failure;
        DamageRegion bounded_damage{};
        const std::span<const Rect> rectangles = damage.rectangles();
        const Rect extent{0.0, 0.0, size.width, size.height};
        for (std::size_t index = 0; index < rectangles.size(); ++index) {
            const Rect rect = rectangles[index];
            if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
                !std::isfinite(rect.right()) || !std::isfinite(rect.bottom())) return PreparedTextStatus::invalid_geometry;
            const Rect clipped = Rect::intersection(rect, extent);
            bounded_damage.add(clipped);
        }
        const bool reuse = matches(size, scale) && front_authorities_.current() && !bounded_damage.empty() && !full_damage(bounded_damage, size);
        candidate_authorities_.clear();
        SkCanvas& target = *(*candidate_).getCanvas();
        target.restoreAll();
        target.resetMatrix();
        if (reuse) {
            const std::optional<SkPixmap> source = (*front_).peekPixels();
            const std::optional<SkPixmap> destination = (*candidate_).peekPixels();
            if (!source || !destination) return PreparedTextStatus::native_failure;
            const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
            for (int row = 0; row < height; ++row) {
                std::memcpy((*destination).writable_addr(0, row), (*source).addr(0, row), row_bytes);
            }
            candidate_authorities_ = front_authorities_;
        } else {
            target.clear(SK_ColorTRANSPARENT);
            bounded_damage.add(extent);
        }
        candidate_size_ = size;
        candidate_scale_ = scale;
        failed_ = false;
        damage = std::move(bounded_damage);
        active_ = true;
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) {
        abort();
        return PreparedTextStatus::resource_failure;
    } catch (...) {
        abort();
        return PreparedTextStatus::native_failure;
    }
}
void PreparedSkiaFrame::abort() noexcept {
    if (stxlib::current_thread() != executor_) return;
    if (candidate_) (*(*candidate_).getCanvas()).restoreAll();
    candidate_authorities_.clear();
    active_ = false;
    failed_ = false;
}
PreparedTextStatus PreparedSkiaFrame::commit(const PaintReceipt receipt) {
    if (stxlib::current_thread() != executor_) return PreparedTextStatus::wrong_executor;
    if (!active_ || !receipt || failed_) {
        abort();
        return PreparedTextStatus::invalid_input;
    }
    // Owners precede guards, including on abort, so guarded mutexes survive.
    const Authorities retained = candidate_authorities_;
    HeldAuthorityLocks locks{};
    for (std::size_t index = 0; index < retained.count; ++index) {
        detail::PreparedAuthorityState& authority = *(*retained.owners[index]).authority_state;
        locks.take(authority.mutex);
        if (!detail::prepared_authority_current_locked(*retained.owners[index], (*retained.owners[index]).authority)) {
            abort();
            return PreparedTextStatus::stale;
        }
    }
    (*(*candidate_).getCanvas()).restoreAll();
    front_.swap(candidate_);
    front_authorities_ = retained;
    front_size_ = candidate_size_;
    front_scale_ = candidate_scale_;
    receipt_ = receipt;
    abort();
    return PreparedTextStatus::success;
}

PreparedTextPaintResult PreparedSkiaFrame::draw(const PreparedTextLayout& layout,
    const LayoutAuthority expected, const Point baseline, const Color color, PreparedCoverageSink* const sink) {
    PreparedTextPaintResult result{};
    if (stxlib::current_thread() != executor_) {
        result.status = PreparedTextStatus::wrong_executor;
        return result;
    }
    const std::shared_ptr<const detail::PreparedTextStorage>& storage = detail::PreparedTextAccess::layout(layout);
    SkCanvas* const target = canvas();
    try {
        if (!storage || target == nullptr) result.status = PreparedTextStatus::invalid_input;
        else if ((*(*storage).authority_state).executor != executor_) result.status = PreparedTextStatus::wrong_executor;
        else if ((*storage).metrics.device_scale != candidate_scale_) result.status = PreparedTextStatus::unsupported_profile;
        else {
            const SkMatrix matrix = (*target).getTotalMatrix();
            const double x = baseline.x * candidate_scale_ + static_cast<double>(matrix.getTranslateX());
            const double y = baseline.y * candidate_scale_ + static_cast<double>(matrix.getTranslateY());
            const bool transform_valid = !matrix.hasPerspective() && matrix.getSkewX() == 0.0F && matrix.getSkewY() == 0.0F &&
                matrix.getScaleX() == static_cast<SkScalar>(candidate_scale_) && matrix.getScaleY() == static_cast<SkScalar>(candidate_scale_);
            if (!transform_valid || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 8'000'000.0 || std::abs(y) > 8'000'000.0) {
                result.status = PreparedTextStatus::invalid_geometry;
            } else {
                GrayTextMask mask{};
                result.status = rasterize_prepared_text(layout, expected, mask);
                if (result.status == PreparedTextStatus::success) {
                    const std::int64_t left = std::llround(x) + mask.left();
                    const std::int64_t top = std::llround(y) + mask.top();
                    const bool has_ink = mask.width() != 0 && mask.height() != 0;
                    if (result.status == PreparedTextStatus::success) {
                        detail::PreparedAuthorityState& authority = *(*storage).authority_state;
                        const stxlib::ScopedLock lock(authority.mutex);
                        if (!detail::prepared_authority_current_locked(*storage, expected)) result.status = PreparedTextStatus::stale;
                        else result.status = candidate_authorities_.retain(storage);
                        bool composited = false;
                        if (result.status == PreparedTextStatus::success && sink != nullptr && has_ink) {
                            // ADR-004: the same compositor and clips as ordinary text.
                            const detail::TextCoverage coverage{.pixels = mask.pixels(), .width = mask.width(),
                                                                .height = mask.height(), .left = left, .top = top};
                            composited = (*sink).composite(*target, coverage, color);
                        }
                        sk_sp<SkImage> image{};
                        if (result.status == PreparedTextStatus::success && has_ink && !composited) {
                            // Transfer the existing charged mask to the named release
                            // context. Any native image retention extends its owner.
                            image = make_mask_image(mask);
                            if (!image) result.status = PreparedTextStatus::resource_failure;
                        }
                        if (result.status == PreparedTextStatus::success && image && !composited) {
                            SkPaint paint{};
                            paint.setColor(SkColorSetARGB(color.alpha, color.red, color.green, color.blue));
                            paint.setAntiAlias(false);
                            (*target).save();
                            (*target).resetMatrix();
                            (*target).drawImage(image, static_cast<SkScalar>(left), static_cast<SkScalar>(top), SkSamplingOptions{}, &paint);
                            (*target).restore();
                        }
                    }
                }
            }
        }
    } catch (const std::bad_alloc&) { result.status = PreparedTextStatus::resource_failure; }
    catch (...) { result.status = PreparedTextStatus::native_failure; }
    if (result.status == PreparedTextStatus::success) result.disposition = PreparedTextPaintDisposition::staged;
    else failed_ = true;
    return result;
}
} // namespace gui_forms::render
