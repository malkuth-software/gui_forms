#include "gui_forms/live_surface/surface/live_surface.hpp"

#include "../buffer/live_surface_buffer.hpp"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace gui_forms {

LiveSurfacePixelFormat native_live_surface_pixel_format() noexcept {
    return LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
}

LiveSurface::LiveSurface(
    std::shared_ptr<detail::LiveSurfaceState> state) noexcept
    : state_(std::move(state)) {}

LiveSurface::~LiveSurface() = default;

std::shared_ptr<LiveSurface> LiveSurface::create(
    LiveSurfaceDescription description) {
    if (!detail::valid_live_surface_description(description)) return {};
    std::shared_ptr<gui_forms::detail::LiveSurfaceState> state = std::make_shared<detail::LiveSurfaceState>();
    (*state).description = description;
    (*state).presentation_region = detail::full_live_surface_damage(description);
    (*state).buffers.resize(description.buffer_count);
    (*state).published_slot = (*state).buffers.size();
    (*state).writing_slot = (*state).buffers.size();
    for (std::shared_ptr<gui_forms::detail::LiveSurfaceBuffer>& buffer : (*state).buffers) {
        buffer = detail::make_live_surface_buffer(description);
        if (!buffer) return {};
    }
    return std::shared_ptr<LiveSurface>(new LiveSurface(std::move(state)));
}

bool LiveSurface::reconfigure(LiveSurfaceDescription description) {
    if (!state_ || !detail::valid_live_surface_description(description)) {
        return false;
    }
    std::vector<std::shared_ptr<detail::LiveSurfaceBuffer>> replacement(
        description.buffer_count);
    for (std::shared_ptr<gui_forms::detail::LiveSurfaceBuffer>& buffer : replacement) {
        buffer = detail::make_live_surface_buffer(description);
        if (!buffer) return false;
    }
    std::vector<std::shared_ptr<detail::LiveSurfaceWake>> wakes;
    {
        std::scoped_lock lock((*state_).mutex);
        if ((*state_).writing_slot != (*state_).buffers.size()) return false;
        if ((*state_).published_slot < (*state_).buffers.size()) {
            (*state_).carried = (*state_).buffers[(*state_).published_slot];
            (*state_).carried_epoch = (*state_).epoch;
            (*state_).carried_generation = (*state_).generation;
            (*state_).carried_content_tag = (*state_).content_tag;
        }
        (*state_).description = description;
        (*state_).buffers = std::move(replacement);
        (*state_).published_slot = (*state_).buffers.size();
        (*state_).writing_slot = (*state_).buffers.size();
        (*state_).generation = 0U;
        (*state_).damage = {};
        (*state_).presentation_region = detail::full_live_surface_damage(description);
        ++(*state_).epoch;
        wakes = (*state_).wakes;
    }
    for (const std::shared_ptr<gui_forms::detail::LiveSurfaceWake>& wake : wakes) {
        if (!wake || !(*wake).connected.load(std::memory_order_acquire) ||
            !(*wake).callback) {
            continue;
        }
        // Wakes are host and core housekeeping (a posted message, a
        // re-armed drain): no-throw by construction, so nothing is caught.
        (*wake).callback();
    }
    return true;
}

LiveSurfaceWriteLease LiveSurface::try_acquire_write(
    bool preserve_published_contents) noexcept {
    if (!state_) return {};
    // The slot is reserved under the lock and the published pixels copied
    // after it (audit E1), so a reader's snapshot never waits behind a
    // whole-frame copy. Both buffers stay alive through the references held
    // here; the reserved slot is written by this writer alone, and the
    // published one is only read until the next publication, which needs
    // this lease. A reconfigure meanwhile changes the epoch, so a lease
    // filled from the old frame cannot publish.
    std::shared_ptr<detail::LiveSurfaceBuffer> target{};
    std::shared_ptr<detail::LiveSurfaceBuffer> source{};
    std::size_t reserved = 0U;
    std::uint64_t epoch = 0U;
    {
        std::scoped_lock lock((*state_).mutex);
        if ((*state_).writing_slot != (*state_).buffers.size()) {
            ++(*state_).dropped_acquires;
            return {};
        }
        bool found = false;
        for (std::size_t slot = 0; slot < (*state_).buffers.size(); ++slot) {
            if (slot == (*state_).published_slot ||
                (*state_).buffers[slot].use_count() != 1) {
                continue;
            }
            reserved = slot;
            found = true;
            break;
        }
        if (!found) {
            ++(*state_).dropped_acquires;
            return {};
        }
        (*state_).writing_slot = reserved;
        target = (*state_).buffers[reserved];
        epoch = (*state_).epoch;
        if (preserve_published_contents &&
            (*state_).published_slot < (*state_).buffers.size()) {
            source = (*state_).buffers[(*state_).published_slot];
        }
    }
    if (source) (*target).pixels = (*source).pixels;
    return {state_, std::move(target), reserved, epoch};
}

LiveSurfaceFrame LiveSurface::acquire_latest() const noexcept {
    if (!state_) return {};
    std::scoped_lock lock((*state_).mutex);
    if ((*state_).published_slot >= (*state_).buffers.size()) {
        if (!(*state_).carried) return {};
        ++(*state_).read_acquires;
        return {(*state_).carried, (*state_).carried_epoch, (*state_).carried_generation,
                detail::full_live_surface_damage((*(*state_).carried).description),
                (*state_).carried_content_tag};
    }
    ++(*state_).read_acquires;
    (*state_).last_read_generation = (*state_).generation;
    return {(*state_).buffers[(*state_).published_slot], (*state_).epoch,
            (*state_).generation, (*state_).damage, (*state_).content_tag};
}

LiveSurfaceFrame LiveSurface::acquire_for_presentation(const void* const presenter) noexcept {
    if (!state_) return {};
    std::scoped_lock lock((*state_).mutex);
    if ((*state_).published_slot >= (*state_).buffers.size()) {
        if (!(*state_).carried) return {};
        // The carried frame is presented whole; the new epoch's region stays
        // pending for its first real frame.
        ++(*state_).read_acquires;
        (*state_).presenter = presenter;
        return {(*state_).carried, (*state_).carried_epoch, (*state_).carried_generation,
                detail::full_live_surface_damage((*(*state_).carried).description),
                (*state_).carried_content_tag};
    }
    ++(*state_).read_acquires;
    (*state_).last_read_generation = (*state_).generation;
    Rect region = (*state_).presentation_region;
    if (presenter != (*state_).presenter) {
        // Another presenter took the region since this one last drew.
        region = detail::full_live_surface_damage((*state_).description);
        (*state_).presenter = presenter;
    }
    (*state_).presentation_region = {};
    return {(*state_).buffers[(*state_).published_slot], (*state_).epoch,
            (*state_).generation, region, (*state_).content_tag};
}

namespace {

// The tag of the frame acquire_latest() would return: the published one, or
// the carried one between reconfigure and the new epoch's first frame.
std::uint64_t snapshot_content_tag(const detail::LiveSurfaceState& state) noexcept {
    if (state.published_slot < state.buffers.size()) return state.content_tag;
    if (state.carried) return state.carried_content_tag;
    return 0U;
}

} // namespace

LiveSurfaceSnapshot LiveSurface::snapshot() const noexcept {
    if (!state_) return {};
    std::scoped_lock lock((*state_).mutex);
    return {(*state_).description, (*state_).epoch, (*state_).generation,
            (*state_).publishes, (*state_).dropped_acquires,
            (*state_).read_acquires, (*state_).last_read_generation,
            (*state_).writing_slot < (*state_).buffers.size(),
            (*state_).published_slot < (*state_).buffers.size(),
            (*state_).published_slot >= (*state_).buffers.size() && (*state_).carried != nullptr,
            snapshot_content_tag(*state_)};
}

LiveSurfaceWakeConnection LiveSurface::connect_presentation_wake(
    std::function<void()> wake) {
    if (!state_ || !wake) return {};
    std::shared_ptr<gui_forms::detail::LiveSurfaceWake> connection = std::make_shared<detail::LiveSurfaceWake>();
    (*connection).callback = std::move(wake);
    {
        std::scoped_lock lock((*state_).mutex);
        (*connection).sequence = (*state_).next_wake_sequence++;
        if ((*state_).next_wake_sequence == 0U) (*state_).next_wake_sequence = 1U;
        (*state_).wakes.push_back(connection);
    }
    return {state_, std::move(connection)};
}

void LiveSurface::notify_presented(const std::uint64_t generation, const LiveDisplayTiming timing) noexcept {
    if (!state_) return;
    std::vector<std::shared_ptr<detail::LiveSurfaceWake>> wakes;
    {
        std::scoped_lock lock((*state_).mutex);
        LivePresentationRecord& record = (*state_).presentation_record;
        // Generations only grow: a late report of an older frame keeps the
        // newer one, but still counts as a tick.
        if (generation > record.presented_generation) record.presented_generation = generation;
        record.timing = timing;
        ++record.presents;
        wakes = (*state_).presented_wakes;
    }
    (*state_).presented.notify_all();
    for (const std::shared_ptr<gui_forms::detail::LiveSurfaceWake>& wake : wakes) {
        if (!wake || !(*wake).connected.load(std::memory_order_acquire) || !(*wake).callback) continue;
        (*wake).callback();
    }
}

LivePresentationRecord LiveSurface::presentation_record() const noexcept {
    if (!state_) return {};
    std::scoped_lock lock((*state_).mutex);
    return (*state_).presentation_record;
}

LiveSurfaceWakeConnection LiveSurface::connect_presented_wake(std::function<void()> wake) {
    if (!state_ || !wake) return {};
    std::shared_ptr<gui_forms::detail::LiveSurfaceWake> connection = std::make_shared<detail::LiveSurfaceWake>();
    (*connection).callback = std::move(wake);
    {
        std::scoped_lock lock((*state_).mutex);
        (*connection).sequence = (*state_).next_wake_sequence++;
        if ((*state_).next_wake_sequence == 0U) (*state_).next_wake_sequence = 1U;
        (*state_).presented_wakes.push_back(connection);
    }
    return {state_, std::move(connection)};
}

} // namespace gui_forms
