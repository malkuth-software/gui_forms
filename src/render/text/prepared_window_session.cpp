#include "prepared_window_session.hpp"
#include "../../core/threading/start_thread.hpp"

#include <new>
#include <stxlib/threads.hpp>
#include <system_error>

namespace gui_forms::render::text {

PreparedWindowSession::PreparedWindowSession(const std::uint64_t provider_instance,
    std::shared_ptr<const detail::PreparedFontBank> fonts,
    std::shared_ptr<detail::PreparedWindowBatchAuthority> authority,
    std::shared_ptr<PreparedWindowWakeConnection> connection,
    std::shared_ptr<PreparedWindowSessionTestHook> hook)
    : provider_instance_(provider_instance), fonts_(std::move(fonts)), authority_(std::move(authority)),
      connection_(std::move(connection)), hook_(std::move(hook)) {}

PreparedWindowSession::~PreparedWindowSession() {
    if (owner_ != stxlib::current_thread()) std::terminate();
    const PreparedTextStatus status = join_and_release();
    if (status != PreparedTextStatus::success) std::terminate();
}

PreparedTextStatus PreparedWindowSession::start() {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    if (provider_instance_ == 0U || !fonts_ || !(*fonts_).ledger || !authority_ || !connection_)
        return PreparedTextStatus::invalid_input;
    {
        const stxlib::ScopedLock lock(mutex_);
        if (snapshot_.closing || snapshot_.joined) return PreparedTextStatus::closing;
        if (snapshot_.started) return PreparedTextStatus::busy;
        const stxlib::ScopedLock authority_lock((*authority_).mutex);
        if ((*authority_).executor != owner_) return PreparedTextStatus::wrong_executor;
        if ((*authority_).closing) return PreparedTextStatus::closing;
        if ((*authority_).desired || (*authority_).controller_instance != 0U ||
            (*authority_).session != 0U || (*authority_).last_epoch != 0U) return PreparedTextStatus::invalid_input;
        const stxlib::ScopedLock ledger_lock((*(*fonts_).ledger).mutex);
        if ((*(*fonts_).ledger).closing) return PreparedTextStatus::closing;
    }
    const PreparedTextStatus claimed = claim_.acquire((*fonts_).ledger);
    if (claimed != PreparedTextStatus::success) return claimed;
    std::uint64_t session_instance = 0U;
    std::uint64_t controller_instance = 0U;
    PreparedTextStatus identified = detail::acquire_prepared_session_instance(session_instance);
    if (identified == PreparedTextStatus::success)
        identified = detail::acquire_prepared_controller_instance(controller_instance);
    if (identified != PreparedTextStatus::success) {
        const PreparedTextStatus result = start_failed(identified);
        return result;
    }
    {
        const stxlib::ScopedLock lock(mutex_);
        const stxlib::ScopedLock authority_lock((*authority_).mutex);
        // Recheck lifetime-owner revocation before binding the allocated IDs.
        if ((*authority_).closing) identified = PreparedTextStatus::closing;
        else if ((*authority_).desired || (*authority_).controller_instance != 0U ||
            (*authority_).session != 0U || (*authority_).last_epoch != 0U)
            identified = PreparedTextStatus::invalid_input;
        else {
            (*authority_).session = session_instance;
            (*authority_).controller_instance = controller_instance;
            snapshot_.session_instance = session_instance;
            snapshot_.controller_instance = controller_instance;
        }
    }
    if (identified != PreparedTextStatus::success) {
        const PreparedTextStatus result = start_failed(identified);
        return result;
    }
    try {
        if (hook_) (*hook_).before_thread_start();
        // Owner cannot call control methods concurrently. Worker sees started
        // before construction and its first access is protected by mutex_.
        const stxlib::ScopedLock lock(mutex_);
        snapshot_.initialization = PreparedTextStatus::pending;
        snapshot_.started = true;
        worker_ = detail::start_library_thread(&PreparedWindowSession::thread_main, this,
                                               "the prepared window session could not start its worker");
    } catch (const std::bad_alloc&) {
        const PreparedTextStatus result = start_failed(PreparedTextStatus::resource_failure);
        return result;
    } catch (const std::system_error&) {
        const PreparedTextStatus result = start_failed(PreparedTextStatus::resource_failure);
        return result;
    } catch (...) {
        const PreparedTextStatus result = start_failed(PreparedTextStatus::native_failure);
        return result;
    }
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowSession::start_failed(const PreparedTextStatus failure) {
    {
        const stxlib::ScopedLock lock(mutex_);
        snapshot_.started = false;
        snapshot_.initialization = failure;
    }
    claim_.release();
    return failure;
}

PreparedTextStatus PreparedWindowSession::admission_locked() const noexcept {
    if (snapshot_.closing || snapshot_.joined) return PreparedTextStatus::closing;
    if (!snapshot_.started) return PreparedTextStatus::invalid_input;
    return snapshot_.initialization;
}

PreparedTextStatus PreparedWindowSession::desire(const detail::PreparedWindowKey& key) {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock(mutex_);
    const PreparedTextStatus available = admission_locked();
    if (available != PreparedTextStatus::success) return available;
    if (key.authority.session != snapshot_.session_instance || key.controller_instance != snapshot_.controller_instance)
        return PreparedTextStatus::stale;
    {
        const stxlib::ScopedLock ledger_lock((*(*fonts_).ledger).mutex);
        if ((*(*fonts_).ledger).closing) return PreparedTextStatus::closing;
    }
    if (key.text.font_set != (*fonts_).identity || key.text.font_generation != (*fonts_).generation)
        return PreparedTextStatus::incompatible_font;
    if (key.text.provider_instance != provider_instance_ || key.text.provider_generation != 1U)
        return PreparedTextStatus::stale;
    bool primary = false;
    for (std::size_t index = 0U; index < (*fonts_).face_count; ++index) {
        const detail::PreparedFontFace& face = (*fonts_).faces[index];
        if (face.role && *face.role == key.text.font.role && face.weight == key.text.font.weight &&
            face.italic == key.text.font.italic) primary = true;
    }
    if (!primary) return PreparedTextStatus::incompatible_font;
    const PreparedTextStatus status = detail::desire_prepared_window(*authority_, key);
    if (status == PreparedTextStatus::success) snapshot_.desired = key;
    return status;
}

PreparedTextStatus PreparedWindowSession::submit(std::unique_ptr<detail::PreparedWindowBatchStorage>& batch) {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock(mutex_);
    const PreparedTextStatus available = admission_locked();
    if (available != PreparedTextStatus::success) return available;
    if (snapshot_.slot != PreparedTextSlot::empty) return PreparedTextStatus::busy;
    if (!batch || !(*batch).input || (*batch).geometry || (*batch).authority != authority_ ||
        (*batch).fonts != fonts_ || (*batch).ledger != (*fonts_).ledger) return PreparedTextStatus::invalid_input;
    if (!snapshot_.desired || !detail::same_prepared_window_key(*snapshot_.desired, (*batch).key))
        return PreparedTextStatus::stale;
    const PreparedTextStatus current = detail::prepared_window_worker_current(*batch);
    if (current != PreparedTextStatus::success) return current;
    batch_ = std::move(batch);
    snapshot_.slot = PreparedTextSlot::queued;
    snapshot_.completion = PreparedTextStatus::pending;
    condition_.notify_one();
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowSession::inspect(PreparedWindowSessionSnapshot& output) const {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock(mutex_);
    output = snapshot_;
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowSession::adopt(const detail::PreparedWindowKey& expected,
    std::shared_ptr<const detail::PreparedWindowBatchStorage>& output) {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    if (output) return PreparedTextStatus::busy;
    const stxlib::ScopedLock lock(mutex_);
    const PreparedTextStatus available = admission_locked();
    if (available != PreparedTextStatus::success) return available;
    if (snapshot_.slot != PreparedTextSlot::ready) return PreparedTextStatus::pending;
    if (snapshot_.completion != PreparedTextStatus::success) return snapshot_.completion;
    if (!batch_ || !snapshot_.desired || !detail::same_prepared_window_key(expected, *snapshot_.desired) ||
        !detail::same_prepared_window_key(expected, (*batch_).key)) return PreparedTextStatus::stale;
    const stxlib::ScopedLock authority_lock((*authority_).mutex);
    const stxlib::ScopedLock ledger_lock((*(*fonts_).ledger).mutex);
    const PreparedTextStatus current = detail::prepared_window_worker_current_locked(*batch_);
    if (current != PreparedTextStatus::success) return current;
    try {
        // shared_ptr's unique_ptr constructor preserves the unique owner on
        // control-block allocation failure. Arrays are never copied.
        std::shared_ptr<const detail::PreparedWindowBatchStorage> adopted(std::move(batch_));
        output = std::move(adopted);
    } catch (const std::bad_alloc&) { return PreparedTextStatus::resource_failure; }
    snapshot_.slot = PreparedTextSlot::empty;
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowSession::discard() {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    std::unique_ptr<detail::PreparedWindowBatchStorage> retired{};
    {
        const stxlib::ScopedLock lock(mutex_);
        if (snapshot_.slot != PreparedTextSlot::ready) return PreparedTextStatus::busy;
        retired = std::move(batch_);
        snapshot_.slot = PreparedTextSlot::empty;
    }
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowSession::cancel() {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock(mutex_);
    if (!snapshot_.started || !authority_) return PreparedTextStatus::invalid_input;
    const stxlib::ScopedLock authority_lock((*authority_).mutex);
    (*authority_).desired.reset();
    snapshot_.desired.reset();
    return PreparedTextStatus::success;
}

void PreparedWindowSession::revoke_locked() {
    snapshot_.closing = true;
    snapshot_.desired.reset();
    if (snapshot_.started && authority_) {
        const stxlib::ScopedLock authority_lock((*authority_).mutex);
        (*authority_).closing = true;
        (*authority_).desired.reset();
    }
}

PreparedTextStatus PreparedWindowSession::begin_close() {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock(mutex_);
    revoke_locked();
    condition_.notify_one();
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowSession::join_and_release() {
    if (owner_ != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const PreparedTextStatus closing = begin_close();
    if (closing != PreparedTextStatus::success) return closing;
    detail::join_library_thread(worker_);
    std::unique_ptr<detail::PreparedWindowBatchStorage> retired{};
    {
        const stxlib::ScopedLock lock(mutex_);
        if (snapshot_.started && !snapshot_.exited) return PreparedTextStatus::native_failure;
        retired = std::move(batch_);
        snapshot_.slot = PreparedTextSlot::empty;
        snapshot_.joined = true;
    }
    connection_.reset();
    hook_.reset();
    claim_.release();
    return PreparedTextStatus::success;
}

void PreparedWindowSession::notify_owner() noexcept {
    // Connection ownership cannot change until worker join. No user callback
    // occurs under a session, authority or ledger lock.
    {
        const stxlib::ScopedLock lock(mutex_);
        if (snapshot_.notification == PreparedWindowWakeResult::failed ||
            snapshot_.notification == PreparedWindowWakeResult::closed) return;
    }
    const PreparedWindowWakeResult result = (*connection_).signal();
    const stxlib::ScopedLock lock(mutex_);
    if (snapshot_.notification == PreparedWindowWakeResult::failed ||
        snapshot_.notification == PreparedWindowWakeResult::closed) return;
    snapshot_.notification = result;
    if (result != PreparedWindowWakeResult::signalled) {
        if (result == PreparedWindowWakeResult::none) snapshot_.notification = PreparedWindowWakeResult::failed;
        revoke_locked();
        condition_.notify_one();
    }
}

void PreparedWindowSession::work() {
    PreparedWindowShaper shaper(fonts_, sizeof(PreparedWindowSession));
    if (hook_) (*hook_).before_initialize();
    const PreparedTextStatus initialized = shaper.initialize();
    {
        const stxlib::ScopedLock lock(mutex_);
        snapshot_.initialization = initialized;
        if (initialized != PreparedTextStatus::success) revoke_locked();
    }
    notify_owner();
    for (;;) {
        detail::PreparedWindowBatchStorage* job = nullptr;
        bool closing = false;
        {
            stxlib::ScopedLock lock(mutex_);
            condition_.wait(lock, &PreparedWindowSession::job_or_closing, this);
            if (snapshot_.slot != PreparedTextSlot::queued) break;
            snapshot_.slot = PreparedTextSlot::running;
            closing = snapshot_.closing;
            // Slot ownership cannot move while running. This borrow ends when
            // ready is published; it is never used during/after notification.
            job = batch_.get();
        }
        PreparedTextStatus completion = PreparedTextStatus::closing;
        if (!closing) {
            try {
                if (hook_) (*hook_).before_shape();
                completion = shaper.shape(*job);
            } catch (const std::bad_alloc&) { completion = PreparedTextStatus::resource_failure; }
            catch (...) { completion = PreparedTextStatus::native_failure; }
        }
        {
            const stxlib::ScopedLock lock(mutex_);
            snapshot_.completion = completion;
            snapshot_.slot = PreparedTextSlot::ready;
        }
        notify_owner();
    }
}

void PreparedWindowSession::thread_main(const stxlib::StopSignal& stop, void* const context) noexcept {
    // The snapshot's closing ends the loop; STXlib ends the thread's records
    // after this returns.
    static_cast<void>(stop);
    (*static_cast<PreparedWindowSession*>(context)).run();
}

bool PreparedWindowSession::job_or_closing(const void* const context) noexcept {
    const PreparedWindowSession& session = *static_cast<const PreparedWindowSession*>(context);
    return session.snapshot_.closing || session.snapshot_.slot == PreparedTextSlot::queued;
}

void PreparedWindowSession::run() noexcept {
    try { work(); }
    catch (...) {
        const std::exception_ptr error = std::current_exception();
        PreparedTextStatus failure = PreparedTextStatus::native_failure;
        try { std::rethrow_exception(error); }
        catch (const std::bad_alloc&) { failure = PreparedTextStatus::resource_failure; }
        catch (...) {}
        const stxlib::ScopedLock lock(mutex_);
        if (snapshot_.initialization == PreparedTextStatus::pending) snapshot_.initialization = failure;
        snapshot_.completion = failure;
        if (batch_) snapshot_.slot = PreparedTextSlot::ready;
        revoke_locked();
    }
    // work() has returned/unwound: shaper/native state has died on this worker.
    {
        const stxlib::ScopedLock lock(mutex_);
        snapshot_.exited = true;
    }
    notify_owner();
}

} // namespace gui_forms::render::text
