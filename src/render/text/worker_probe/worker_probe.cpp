#include "worker_probe.hpp"
#include "../../../core/threading/start_thread.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <stxlib/threads.hpp>
#include <utility>

namespace gui_forms::render::worker_probe {

bool same_identity(const Identity& left, const Identity& right) noexcept {
    const DocumentPageRequest& a = left.page;
    const DocumentPageRequest& b = right.page;
    const bool equal = a.revision.document == b.revision.document &&
        a.revision.revision == b.revision.revision && a.serial == b.serial &&
        a.permitted.begin.value == b.permitted.begin.value &&
        a.permitted.end.value == b.permitted.end.value &&
        a.viewport.anchor.value == b.viewport.anchor.value &&
        a.viewport.horizontal_dip == b.viewport.horizontal_dip &&
        left.layout_serial == right.layout_serial &&
        left.provider_instance == right.provider_instance &&
        left.provider_generation == right.provider_generation &&
        left.font_set == right.font_set && left.font_generation == right.font_generation &&
        left.context_generation == right.context_generation && left.font == right.font &&
        left.scale == right.scale && left.wrap_width == right.wrap_width &&
        left.tab_columns == right.tab_columns;
    return equal;
}

Worker::Worker(std::shared_ptr<const FontSet> fonts) : fonts_(std::move(fonts)) {
    if (!fonts_) { throw std::invalid_argument("Worker requires encoded fonts"); }
}
Worker::~Worker() { close(); }

void Worker::start() {
    const stxlib::ScopedLock lock(mutex_);
    if (state_.closing || thread_.identity().present()) { throw std::logic_error("Worker already started/closed"); }
    thread_ = gui_forms::detail::start_library_thread(&Worker::thread_main, this, "Worker could not start its thread");
}

bool Worker::valid(const Identity& identity) const noexcept {
    const DocumentPageRequest& page = identity.page;
    const bool valid_key = page.revision.document != 0 && page.revision.revision != 0 &&
        page.serial != 0 && identity.layout_serial != 0 && identity.provider_instance != 0 &&
        identity.provider_generation != 0 && identity.context_generation != 0 &&
        identity.font_set == (*fonts_).identity && identity.font_generation == (*fonts_).generation &&
        page.permitted.begin.value <= page.viewport.anchor.value &&
        page.viewport.anchor.value <= page.permitted.end.value &&
        std::isfinite(page.viewport.horizontal_dip) && page.viewport.horizontal_dip >= 0 &&
        valid_font_spec(identity.font) && identity.scale == 1.0 &&
        identity.wrap_width == 0.0 && identity.tab_columns == 4;
    return valid_key;
}

Outcome Worker::desire(const Identity& identity) {
    const stxlib::ScopedLock lock(mutex_);
    if (state_.closing) { return Outcome::closed; }
    if (!valid(identity)) { return Outcome::invalid; }
    if (authority_epoch_ == std::numeric_limits<std::uint64_t>::max()) { return Outcome::invalid; }
    ++authority_epoch_;
    desired_ = identity;
    state_.desired = true;
    return Outcome::success;
}

Outcome Worker::submit(const Identity& identity, std::string_view input) {
    const stxlib::ScopedLock lock(mutex_);
    if (state_.closing) { return Outcome::closed; }
    if (state_.slot != Slot::empty) { return Outcome::busy; }
    if (!thread_.joinable()) { return Outcome::invalid; }
    if (!valid(identity) || input.size() > 16'384) { return Outcome::invalid; }
    if (!desired_ || !same_identity(identity, *desired_)) { return Outcome::stale; }
    // Allocate only after slot admission; an exception preserves the empty slot.
    std::unique_ptr<Job> prepared = std::make_unique<Job>();
    Job& pending = *prepared;
    pending.identity = identity;
    pending.authority_epoch = authority_epoch_;
    pending.text.assign(input);
    job_ = std::move(prepared);
    state_.slot = Slot::queued;
    wake_.notify_one();
    return Outcome::success;
}

void Worker::cancel() {
    const stxlib::ScopedLock lock(mutex_);
    desired_.reset();
    state_.desired = false;
}

Outcome Worker::publish() {
    const stxlib::ScopedLock lock(mutex_);
    if (state_.closing) { return Outcome::closed; }
    if (state_.slot != Slot::ready) { return Outcome::busy; }
    if (state_.completion != Outcome::success || !result_) { return Outcome::failed; }
    if (!desired_ || (*result_).authority_epoch != authority_epoch_ ||
        !same_identity((*result_).identity, *desired_)) { return Outcome::stale; }
    displayed_ = std::move(result_);
    state_.slot = Slot::empty;
    return Outcome::success;
}

Outcome Worker::discard() {
    const stxlib::ScopedLock lock(mutex_);
    if (state_.slot != Slot::ready) { return Outcome::busy; }
    result_.reset();
    state_.slot = Slot::empty;
    return Outcome::success;
}

Snapshot Worker::snapshot() {
    const stxlib::ScopedLock lock(mutex_);
    return state_;
}
const Prepared* Worker::displayed() const noexcept {
    const Prepared* result = displayed_.get();
    return result;
}

void Worker::close() {
    {
        const stxlib::ScopedLock lock(mutex_);
        state_.closing = true;
        desired_.reset();
        state_.desired = false;
        wake_.notify_one();
    }
    gui_forms::detail::join_library_thread(thread_);
    const stxlib::ScopedLock lock(mutex_);
    job_.reset();
    result_.reset();
    state_.slot = Slot::empty;
    state_.joined = true;
}

void Worker::thread_main(const stxlib::StopSignal& stop, void* const context) noexcept {
    // state_.closing ends the loop; STXlib ends the thread's records after
    // this returns.
    static_cast<void>(stop);
    (*static_cast<Worker*>(context)).run();
}

bool Worker::job_or_closing(const void* const context) noexcept {
    const Worker& worker = *static_cast<const Worker*>(context);
    return worker.state_.closing || worker.state_.slot == Slot::queued;
}

void Worker::run() {
    // This unique engine and every mutable native font live/die on this thread.
    std::unique_ptr<text::HarfBuzzFontEngine> engine{};
    std::array<FontFaceId, 4> face_ids{};
    try {
        engine = std::make_unique<text::HarfBuzzFontEngine>();
        const std::size_t face_count = (*fonts_).faces.size();
        for (std::size_t index = 0; index < face_count; ++index) {
            const FontBytes& face = (*fonts_).faces[index];
            const std::optional<FontFaceId> registered = (*engine).register_shared_typeface(
                face.role, face.weight, face.italic, face.encoded, face.face_index);
            if (!registered) { throw std::runtime_error("Worker font registration failed"); }
            face_ids[index] = *registered;
        }
    } catch (const std::exception&) {
        engine.reset();
    }
    {
        const stxlib::ScopedLock lock(mutex_);
        state_.engine_alive = static_cast<bool>(engine);
    }
    for (;;) {
        std::unique_ptr<Job> active{};
        {
            stxlib::ScopedLock lock(mutex_);
            wake_.wait(lock, &Worker::job_or_closing, this);
            if (state_.closing) { break; }
            active = std::move(job_);
            state_.slot = Slot::running;
        }
        Outcome completion = Outcome::failed;
        std::unique_ptr<Prepared> prepared{};
        try {
            if (!engine) { throw std::runtime_error("Worker unavailable"); }
            prepared = std::make_unique<Prepared>();
            Job& active_job = *active;
            Prepared& pending_result = *prepared;
            pending_result.identity = active_job.identity;
            pending_result.authority_epoch = active_job.authority_epoch;
            pending_result.display_utf8 = std::move(active_job.text);
            pending_result.fonts = fonts_;
            pending_result.face_ids = face_ids;
            pending_result.geometry = (*engine).shape(pending_result.display_utf8, pending_result.identity.font);
            if (pending_result.geometry.missing_primary_face) { throw std::runtime_error("Missing primary face"); }
            completion = Outcome::success;
        } catch (const std::exception&) {
            prepared.reset();
        }
        active.reset();
        {
            const stxlib::ScopedLock lock(mutex_);
            result_ = std::move(prepared);
            state_.completion = completion;
            ++state_.completed;
            state_.slot = Slot::ready;
        }
    }
    engine.reset();
    const stxlib::ScopedLock lock(mutex_);
    state_.engine_alive = false;
}

} // namespace gui_forms::render::worker_probe
