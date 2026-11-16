#include "presentation_shim.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace gui_forms::host::presentation {

namespace {

// Whether a frame's layout can be read: pixels, a size in range and rows of
// at least width * 4 bytes.
bool source_usable(const PresentSourcePixels& source) noexcept {
    if (source.pixels == nullptr || source.extent.empty()) return false;
    if (source.extent.width > maximum_present_side || source.extent.height > maximum_present_side) return false;
    const bool rows = source.row_bytes >= static_cast<std::size_t>(source.extent.width) * 4U;
    return rows;
}

// Whether a granted buffer matches the extent asked for and can be written.
bool buffer_usable(const DirectBuffer& buffer, PresentExtent extent) noexcept {
    if (buffer.pixels == nullptr || buffer.extent != extent) return false;
    const bool rows = buffer.row_bytes >= static_cast<std::size_t>(extent.width) * 4U;
    return rows;
}

std::uint8_t trace_detail(std::uint32_t value) noexcept {
    const std::uint8_t detail = static_cast<std::uint8_t>(std::min<std::uint32_t>(value, 255U));
    return detail;
}

std::uint32_t trace_count(std::size_t value) noexcept {
    const std::uint32_t count = static_cast<std::uint32_t>(std::min<std::size_t>(value, 0xffffffffU));
    return count;
}

// An outcome the copy path may return; anything else is a failure.
PresentOutcome copy_outcome(PresentOutcome outcome) noexcept {
    switch (outcome) {
    case PresentOutcome::presented:
    case PresentOutcome::inactive:
    case PresentOutcome::failed:
        return outcome;
    case PresentOutcome::pending:
    case PresentOutcome::lost:
    default:
        break;
    }
    return PresentOutcome::failed;
}

PresentTargetPixels target_of(const DirectBuffer& buffer) noexcept {
    const PresentTargetPixels target{buffer.pixels, buffer.row_bytes, buffer.extent};
    return target;
}

} // namespace

DirectAcquire DirectAcquire::granted(DirectBuffer buffer) noexcept {
    const DirectAcquire result(std::variant<DirectBuffer, DirectRefusal>(std::in_place_index<0>, buffer));
    return result;
}

DirectAcquire DirectAcquire::refused(DirectRefusal refusal) noexcept {
    const DirectAcquire result(std::variant<DirectBuffer, DirectRefusal>(std::in_place_index<1>, refusal));
    return result;
}

const DirectBuffer* DirectAcquire::buffer() const noexcept {
    const DirectBuffer* const buffer = std::get_if<DirectBuffer>(&state_);
    return buffer;
}

std::optional<DirectRefusal> DirectAcquire::refusal() const noexcept {
    const DirectRefusal* const refusal = std::get_if<DirectRefusal>(&state_);
    if (refusal == nullptr) return std::nullopt;
    return *refusal;
}

PresentationCopyPath::~PresentationCopyPath() = default;
PresentationDirectPath::~PresentationDirectPath() = default;

bool present_list_names(std::string_view list, std::string_view word) noexcept {
    // Each word runs to the next comma or the end.
    std::size_t start = 0U;
    while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? list.size() : comma;
        if (list.substr(start, end - start) == word) return true;
        start = end + 1U;
    }
    return false;
}

bool present_policy_names(std::string_view word) noexcept {
    const char* const value = std::getenv("GUI_FORMS_PRESENT");
    if (value == nullptr) return false;
    const bool named = present_list_names(std::string_view(value), word);
    return named;
}

PresentPolicy present_policy_from_environment() noexcept {
    PresentPolicy policy{};
    policy.direct = present_policy_names("direct");
    policy.host_scroll = present_policy_names("scroll");
    return policy;
}

PresentationShim::PresentationShim(PresentationCopyPath* copy, std::unique_ptr<PresentationDirectPath> direct,
                                   PresentTrace* trace) noexcept
    : copy_(copy), direct_(std::move(direct)), trace_(trace) {
    state_ = direct_ ? DirectState::ready : DirectState::absent;
}

PresentationShim::~PresentationShim() { release_direct(); }

void PresentationShim::record(PresentTraceEvent event, PresentPath path, std::uint8_t detail, std::uint32_t rects,
                              std::uint64_t pixels) noexcept {
    if (trace_ == nullptr) return;
    PresentTraceRecord entry{};
    entry.frame = frame_;
    entry.event = event;
    entry.path = path;
    entry.detail = detail;
    entry.rect_count = rects;
    entry.pixel_count = pixels;
    (*trace_).record(entry);
}

void PresentationShim::consider_resume(PresentExtent extent) noexcept {
    if (state_ == DirectState::paused) {
        if (pause_left_ > 0U) --pause_left_;
        if (pause_left_ > 0U) return;
        state_ = DirectState::ready;
        record(PresentTraceEvent::resume, PresentPath::none, 0U, 0U, 0U);
        return;
    }
    if (state_ == DirectState::waiting_resize && extent != refused_extent_) {
        state_ = DirectState::ready;
        record(PresentTraceEvent::resume, PresentPath::none, 0U, 0U, 0U);
    }
}

void PresentationShim::apply_refusal(DirectRefusal refusal, PresentExtent extent) noexcept {
    if (state_ == DirectState::absent || state_ == DirectState::disabled) return;
    switch (refusal) {
    case DirectRefusal::extent:
        state_ = DirectState::waiting_resize;
        refused_extent_ = extent;
        return;
    case DirectRefusal::resources:
    case DirectRefusal::busy:
        state_ = DirectState::paused;
        pause_left_ = retry_frames;
        return;
    case DirectRefusal::lost:
        ++counters_.losses;
        ++consecutive_losses_;
        if (consecutive_losses_ >= maximum_consecutive_losses) {
            state_ = DirectState::disabled;
        } else {
            state_ = DirectState::paused;
            pause_left_ = retry_frames;
        }
        return;
    case DirectRefusal::unsupported:
    default:
        break;
    }
    // Unsupported, and the fallback for an unknown refusal: off for good.
    state_ = DirectState::disabled;
}

void PresentationShim::release_direct() noexcept {
    holding_ = false;
    if (!direct_ || !direct_holds_) return;
    (*direct_).release();
    direct_holds_ = false;
    pending_ = false;
    history_.reset();
    record(PresentTraceEvent::release, PresentPath::direct, 0U, 0U, 0U);
}

PresentReport PresentationShim::present(const PresentFrame& frame) {
    PresentReport report{};
    if (!source_usable(frame.source)) return report;
    // While the window system reads a pending present nothing is written;
    // the caller keeps its damage (as for failed).
    if (pending_) return report;
    ++frame_;
    ++counters_.frames;
    const PresentExtent extent = frame.source.extent;
    const std::size_t damage_count = clip_present_damage(frame.damage, extent, frame_rects_);
    if (damage_count == 0U && frame.scrolls.empty()) {
        report.outcome = PresentOutcome::presented;
        report.path = last_path_;
        return report;
    }
    consider_resume(extent);
    if (state_ == DirectState::ready) {
        const std::optional<PresentReport> direct = present_direct(frame);
        if (direct.has_value()) return *direct;
    }
    // A frame after the direct path showed: the copy path writes it whole,
    // so that nothing the direct path showed survives.
    const bool whole_frame = last_path_ == PresentPath::direct;
    if (whole_frame && loss_fallback_pending_) {
        ++counters_.fallbacks;
        record(PresentTraceEvent::fallback, PresentPath::none, static_cast<std::uint8_t>(FallbackReason::lost), 0U,
               0U);
    }
    loss_fallback_pending_ = false;
    release_direct();
    const PresentReport copied = present_copy(frame, whole_frame);
    return copied;
}

std::optional<PresentReport> PresentationShim::present_direct(const PresentFrame& frame) {
    const PresentExtent extent = frame.source.extent;
    // present() clipped the damage into frame_rects_ for this frame.
    const std::size_t damage_count = clip_present_damage(frame.damage, extent, frame_rects_);
    const DirectAcquire acquired = (*direct_).acquire(extent);
    const DirectBuffer* const granted = acquired.buffer();
    std::optional<DirectRefusal> refusal = acquired.refusal();
    if (granted != nullptr) {
        direct_holds_ = true;
        // A buffer that does not match is the path's defect: treated as
        // lost.
        if (!buffer_usable(*granted, extent)) refusal = DirectRefusal::lost;
    }
    if (refusal.has_value()) {
        ++counters_.refusals;
        record(PresentTraceEvent::refuse, PresentPath::direct, static_cast<std::uint8_t>(*refusal), 0U, 0U);
        apply_refusal(*refusal, extent);
        release_direct();
        ++counters_.fallbacks;
        record(PresentTraceEvent::fallback, PresentPath::none, static_cast<std::uint8_t>(FallbackReason::refused),
               0U, 0U);
        return std::nullopt;
    }
    const DirectBuffer buffer = *granted;
    const PresentTargetPixels target = target_of(buffer);
    // A buffer's contents count only if the direct path presented the
    // frame before (a path change writes everything).
    const std::uint32_t age = last_path_ == PresentPath::direct ? buffer.age : 0U;
    record(PresentTraceEvent::acquire, PresentPath::direct, trace_detail(age), 0U, 0U);
    // What changed on screen (presented as damage, and remembered for
    // older buffers) and what this buffer needs written if it is the one
    // presented last.
    PresentRectCollector changed(extent, present_rects_);
    PresentRectCollector needed(extent, write_rects_);
    for (std::size_t index = 0U; index < frame.scrolls.size(); ++index) {
        const PresentScrollPlan plan = plan_present_scroll(frame.scrolls[index], extent);
        if (plan.area.empty()) continue;
        changed.add(plan.area);
        const bool moved = age == 1U && plan.moves() && move_present_pixels(target, plan);
        if (moved) {
            ++counters_.scrolls_moved;
            counters_.pixels_moved += plan.destination.area();
            record(PresentTraceEvent::scroll, PresentPath::direct, 0U, 1U, plan.destination.area());
            for (std::size_t band = 0U; band < plan.exposed_count; ++band) needed.add(plan.exposed[band]);
        } else {
            ++counters_.scrolls_refused;
            record(PresentTraceEvent::scroll_refused, PresentPath::direct, 0U, 0U, plan.area.area());
            needed.add(plan.area);
        }
    }
    for (std::size_t index = 0U; index < damage_count; ++index) {
        changed.add(frame_rects_[index]);
        needed.add(frame_rects_[index]);
    }
    const std::size_t changed_count = changed.finish();
    std::size_t needed_count = needed.finish();
    // The rectangles written: what this frame needs for a buffer of age 1;
    // for an older one also what the frames since changed; all of it for
    // contents unknown. write_rects_ is rewritten from a copy, since
    // accumulate must not read the array it writes.
    std::array<PresentRect, maximum_frame_rects> current{};
    std::copy(write_rects_.begin(), write_rects_.begin() + static_cast<std::ptrdiff_t>(needed_count),
              current.begin());
    const std::span<const PresentRect> current_rects(current.data(), needed_count);
    const bool accumulated =
        age == 1U || history_.accumulate(age, current_rects, extent, write_rects_, needed_count);
    if (!accumulated) {
        write_rects_[0] = present_extent_rect(extent);
        needed_count = 1U;
    }
    const std::span<const PresentRect> written(write_rects_.data(), needed_count);
    const std::uint64_t pixels = present_rects_area(written);
    const bool wrote = copy_present_rects(frame.source, target, written);
    record(PresentTraceEvent::damage, PresentPath::direct, 0U, wrote ? trace_count(needed_count) : 0U,
           wrote ? pixels : 0U);
    const std::span<const PresentRect> damage(present_rects_.data(), changed_count);
    // A write the kernel refused (a frame overlapping the buffer) is not
    // presented: the frame goes to the copy path instead.
    const PresentOutcome outcome = wrote && changed_count > 0U ? (*direct_).present(damage) : PresentOutcome::failed;
    record(PresentTraceEvent::present, PresentPath::direct, static_cast<std::uint8_t>(outcome),
           trace_count(changed_count), present_rects_area(damage));
    switch (outcome) {
    case PresentOutcome::presented:
    case PresentOutcome::pending: {
        history_.record(damage);
        consecutive_losses_ = 0U;
        last_path_ = PresentPath::direct;
        pending_ = outcome == PresentOutcome::pending;
        // The copy path's storage no longer shows the frame before.
        copy_in_sync_ = false;
        ++counters_.direct_frames;
        counters_.pixels_written += pixels;
        PresentReport report{};
        report.outcome = outcome;
        report.path = PresentPath::direct;
        report.rects = trace_count(needed_count);
        report.pixels = pixels;
        return report;
    }
    case PresentOutcome::inactive: {
        // Nothing shown: the buffers' contents are no longer known.
        history_.reset();
        last_path_ = PresentPath::direct;
        PresentReport report{};
        report.outcome = outcome;
        report.path = PresentPath::direct;
        return report;
    }
    case PresentOutcome::lost:
        record(PresentTraceEvent::lost, PresentPath::direct, 0U, 0U, 0U);
        apply_refusal(DirectRefusal::lost, extent);
        release_direct();
        ++counters_.fallbacks;
        record(PresentTraceEvent::fallback, PresentPath::none, static_cast<std::uint8_t>(FallbackReason::lost), 0U,
               0U);
        last_path_ = PresentPath::direct;
        return std::nullopt;
    case PresentOutcome::failed:
    default:
        break;
    }
    // Failed, and the fallback for an unknown outcome: the copy path shows
    // this frame and the direct path rests.
    state_ = DirectState::paused;
    pause_left_ = retry_frames;
    release_direct();
    ++counters_.fallbacks;
    record(PresentTraceEvent::fallback, PresentPath::none, static_cast<std::uint8_t>(FallbackReason::failed), 0U, 0U);
    last_path_ = PresentPath::direct;
    return std::nullopt;
}

PresentReport PresentationShim::present_copy(const PresentFrame& frame, bool whole_frame) {
    PresentReport report{};
    if (copy_ == nullptr) return report;
    const PresentExtent extent = frame.source.extent;
    const std::size_t damage_count = clip_present_damage(frame.damage, extent, frame_rects_);
    PresentRectCollector copied(extent, write_rects_);
    // Pixels move in the window system's storage only while it shows the
    // frame before, on this path.
    const bool can_move = !whole_frame && (*copy_).scrolls() && copy_in_sync_ && last_path_ == PresentPath::copy;
    for (std::size_t index = 0U; index < frame.scrolls.size(); ++index) {
        const PresentScrollPlan plan = plan_present_scroll(frame.scrolls[index], extent);
        if (plan.area.empty()) continue;
        const bool moved = can_move && plan.moves() && (*copy_).scroll(plan);
        if (moved) {
            ++counters_.scrolls_moved;
            counters_.pixels_moved += plan.destination.area();
            record(PresentTraceEvent::scroll, PresentPath::copy, 0U, 1U, plan.destination.area());
            for (std::size_t band = 0U; band < plan.exposed_count; ++band) copied.add(plan.exposed[band]);
        } else {
            ++counters_.scrolls_refused;
            record(PresentTraceEvent::scroll_refused, PresentPath::copy, 0U, 0U, plan.area.area());
            copied.add(plan.area);
        }
    }
    if (whole_frame) {
        copied.add(present_extent_rect(extent));
    } else {
        for (std::size_t index = 0U; index < damage_count; ++index) copied.add(frame_rects_[index]);
    }
    const std::size_t count = copied.finish();
    const std::span<const PresentRect> rects(write_rects_.data(), count);
    const std::uint64_t pixels = present_rects_area(rects);
    record(PresentTraceEvent::damage, PresentPath::copy, 0U, trace_count(count), pixels);
    const PresentOutcome outcome = count > 0U ? copy_outcome((*copy_).copy(frame.source, rects))
                                              : PresentOutcome::presented;
    record(PresentTraceEvent::present, PresentPath::copy, static_cast<std::uint8_t>(outcome), trace_count(count),
           pixels);
    copy_in_sync_ = outcome == PresentOutcome::presented;
    last_path_ = PresentPath::copy;
    ++counters_.copy_frames;
    counters_.pixels_written += pixels;
    report.outcome = outcome;
    report.path = PresentPath::copy;
    report.rects = trace_count(count);
    report.pixels = pixels;
    return report;
}

DirectAcquire PresentationShim::acquire_for_writing(PresentExtent extent) {
    ++frame_;
    ++counters_.frames;
    if (!direct_ || state_ == DirectState::disabled) {
        record(PresentTraceEvent::refuse, PresentPath::direct, static_cast<std::uint8_t>(DirectRefusal::unsupported),
               0U, 0U);
        return DirectAcquire::refused(DirectRefusal::unsupported);
    }
    if (pending_ || holding_) {
        record(PresentTraceEvent::refuse, PresentPath::direct, static_cast<std::uint8_t>(DirectRefusal::busy), 0U,
               0U);
        return DirectAcquire::refused(DirectRefusal::busy);
    }
    const DirectAcquire acquired = (*direct_).acquire(extent);
    const DirectBuffer* const granted = acquired.buffer();
    if (granted == nullptr || !buffer_usable(*granted, extent)) {
        const DirectRefusal refusal = granted == nullptr ? *acquired.refusal() : DirectRefusal::lost;
        if (granted != nullptr) direct_holds_ = true;
        ++counters_.refusals;
        record(PresentTraceEvent::refuse, PresentPath::direct, static_cast<std::uint8_t>(refusal), 0U, 0U);
        if (refusal == DirectRefusal::lost) {
            ++counters_.losses;
            release_direct();
        }
        return DirectAcquire::refused(refusal);
    }
    direct_holds_ = true;
    holding_ = true;
    held_ = *granted;
    record(PresentTraceEvent::acquire, PresentPath::direct, trace_detail(held_.age), 0U, 0U);
    return acquired;
}

PresentScrollResult PresentationShim::scroll_written(const PresentScroll& scroll) {
    PresentScrollResult result{};
    if (!holding_) return result;
    result.plan = plan_present_scroll(scroll, held_.extent);
    if (result.plan.area.empty()) return result;
    result.moved = held_.age == 1U && result.plan.moves() && move_present_pixels(target_of(held_), result.plan);
    if (result.moved) {
        ++counters_.scrolls_moved;
        counters_.pixels_moved += result.plan.destination.area();
        record(PresentTraceEvent::scroll, PresentPath::direct, 0U, 1U, result.plan.destination.area());
    } else {
        ++counters_.scrolls_refused;
        record(PresentTraceEvent::scroll_refused, PresentPath::direct, 0U, 0U, result.plan.area.area());
    }
    return result;
}

void PresentationShim::decline_scroll(const PresentScroll& scroll) {
    if (!holding_) return;
    const PresentScrollPlan plan = plan_present_scroll(scroll, held_.extent);
    if (plan.area.empty()) return;
    ++counters_.scrolls_refused;
    record(PresentTraceEvent::scroll_refused, PresentPath::direct, 0U, 0U, plan.area.area());
}

PresentOutcome PresentationShim::present_written(std::span<const PresentRect> damage) {
    if (!holding_) return PresentOutcome::failed;
    const std::size_t count = clip_present_damage(damage, held_.extent, present_rects_);
    const std::span<const PresentRect> rects(present_rects_.data(), count);
    const std::uint64_t pixels = present_rects_area(rects);
    record(PresentTraceEvent::damage, PresentPath::direct, 0U, trace_count(count), pixels);
    holding_ = false;
    // Nothing to show: the grant ends unpresented, which a held buffer
    // allows (its contents are what the caller left).
    const PresentOutcome outcome = count > 0U ? (*direct_).present(rects) : PresentOutcome::presented;
    record(PresentTraceEvent::present, PresentPath::direct, static_cast<std::uint8_t>(outcome), trace_count(count),
           pixels);
    switch (outcome) {
    case PresentOutcome::presented:
    case PresentOutcome::pending:
        history_.record(rects);
        last_path_ = PresentPath::direct;
        pending_ = outcome == PresentOutcome::pending;
        ++counters_.direct_frames;
        counters_.pixels_written += pixels;
        break;
    case PresentOutcome::lost:
        record(PresentTraceEvent::lost, PresentPath::direct, 0U, 0U, 0U);
        ++counters_.losses;
        release_direct();
        break;
    case PresentOutcome::inactive:
    case PresentOutcome::failed:
    default:
        // The caller keeps its damage; the buffer's contents stay its own.
        break;
    }
    return outcome;
}

void PresentationShim::notify_lost() {
    if (!direct_) return;
    record(PresentTraceEvent::lost, PresentPath::direct, 0U, 0U, 0U);
    history_.reset();
    apply_refusal(DirectRefusal::lost, refused_extent_);
    release_direct();
    loss_fallback_pending_ = true;
}

void PresentationShim::notify_released() {
    if (!pending_) return;
    pending_ = false;
    record(PresentTraceEvent::released_by_host, PresentPath::direct, 0U, 0U, 0U);
}

PresentShimSnapshot PresentationShim::snapshot() const noexcept {
    PresentShimSnapshot snapshot = counters_;
    snapshot.next_path = state_ == DirectState::ready ? PresentPath::direct : PresentPath::copy;
    snapshot.direct_disabled = state_ == DirectState::disabled;
    return snapshot;
}

bool PresentationShim::direct_available() const noexcept {
    const bool available = direct_ && state_ != DirectState::absent && state_ != DirectState::disabled;
    return available;
}

} // namespace gui_forms::host::presentation
