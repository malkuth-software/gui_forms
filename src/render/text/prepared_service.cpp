#include "../../core/text/prepared/prepared_storage.hpp"
#include "harfbuzz/harfbuzz_font_engine.hpp"
#include "../../core/threading/start_thread.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <stxlib/threads.hpp>

namespace gui_forms {
namespace {

std::atomic<std::uint64_t> next_service_instance{1};

std::uint64_t acquire_service_instance() {
    std::uint64_t observed = next_service_instance.load(std::memory_order_relaxed);
    for (;;) {
        if (observed == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Prepared text instance identities exhausted");
        }
        const bool exchanged = next_service_instance.compare_exchange_weak(observed, observed + 1U,
            std::memory_order_relaxed, std::memory_order_relaxed);
        if (exchanged) return observed;
    }
}

bool owner_executor(const detail::PreparedServiceState& service) noexcept {
    const bool current = service.executor == stxlib::current_thread();
    return current;
}

bool matching_primary(const detail::PreparedFontBank& bank, const FontSpec font) noexcept {
    for (std::size_t index = 0; index < bank.face_count; ++index) {
        const detail::PreparedFontFace& face = bank.faces[index];
        if (face.role && *face.role == font.role && face.weight == font.weight && face.italic == font.italic) return true;
    }
    return false;
}
} // namespace

PreparedTextService::PreparedTextService() {
    std::shared_ptr<detail::PreparedServiceState> candidate = std::make_shared<detail::PreparedServiceState>();
    (*candidate).ledger = std::make_shared<detail::PreparedLedger>();
    (*candidate).executor = stxlib::current_thread();
    (*candidate).instance = acquire_service_instance();
    state_ = std::move(candidate);
}
PreparedTextService::~PreparedTextService() { begin_close(); }
std::uint64_t PreparedTextService::instance() const noexcept { return (*state_).instance; }
PreparedTextBudgetSnapshot PreparedTextService::budget_snapshot() const {
    detail::PreparedLedger& ledger = *(*state_).ledger;
    const stxlib::ScopedLock lock(ledger.mutex);
    return ledger.usage;
}
PreparedTextStatus PreparedTextService::create_font_bank(const std::span<const PreparedFontSource> sources,
    EncodedFontLease& output) {
    if (!owner_executor(*state_)) return PreparedTextStatus::wrong_executor;
    if (sources.empty() || sources.size() > PreparedTextLimits::font_faces) return PreparedTextStatus::invalid_input;
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const PreparedFontSource& source = sources[index];
        if (source.encoded.empty() || source.weight < 1 || source.weight > 1000 || source.face_index > 255) {
            return PreparedTextStatus::invalid_input;
        }
        if (source.role) {
            FontSpec specification{};
            specification.role = *source.role;
            if (!valid_font_spec(specification)) return PreparedTextStatus::invalid_input;
        }
        if (!valid_font_family(source.family) || (source.family.value != 0U && source.role)) {
            return PreparedTextStatus::invalid_input;
        }
        if (source.encoded.size() > PreparedTextLimits::font_face_bytes ||
            source.encoded.size() > PreparedTextLimits::font_bytes - bytes) return PreparedTextStatus::budget_exceeded;
        bytes += source.encoded.size();
    }
    detail::PreparedReservation reservation{};
    const PreparedTextStatus admitted = reservation.acquire((*state_).ledger, detail::PreparedResource::font_bank, bytes);
    if (admitted != PreparedTextStatus::success) return admitted;
    try {
        std::shared_ptr<detail::PreparedFontBank> candidate = std::make_shared<detail::PreparedFontBank>();
        detail::PreparedFontBank& bank = *candidate;
        bank.reservation = std::move(reservation);
        bank.ledger = (*state_).ledger;
        bank.face_count = sources.size();
        bank.encoded_bytes = bytes;
        {
            detail::PreparedLedger& ledger = *bank.ledger;
            const stxlib::ScopedLock lock(ledger.mutex);
            if (ledger.next_bank == std::numeric_limits<std::uint64_t>::max()) return PreparedTextStatus::generation_exhausted;
            bank.identity = ledger.next_bank;
            ++ledger.next_bank;
        }
        for (std::size_t index = 0; index < sources.size(); ++index) {
            const PreparedFontSource& source = sources[index];
            detail::PreparedFontFace& face = bank.faces[index];
            face.size = source.encoded.size();
            face.bytes = std::make_unique<std::byte[]>(face.size);
            std::memcpy(face.bytes.get(), source.encoded.data(), face.size);
            face.role = source.role;
            face.weight = source.weight;
            face.italic = source.italic;
            face.index = source.face_index;
            face.family = source.family;
        }
        const PreparedTextStatus compatible = detail::validate_prepared_fonts(bank);
        if (compatible != PreparedTextStatus::success) return compatible;
        detail::PreparedTextAccess::fonts(output) = std::move(candidate);
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) {
        return PreparedTextStatus::resource_failure;
    }
}

PreparedTextStatus PreparedTextService::create_input(const PreparedTextKey& key, const std::string_view text,
    const std::span<const DocumentMapSpan> mapping, const std::span<const PreparedSourceEndpoint> endpoints,
    const PreparedParagraphProof proof, PrepareInput& output) {
    if (!owner_executor(*state_)) return PreparedTextStatus::wrong_executor;
    std::size_t bytes = 0;
    const PreparedTextStatus validated = detail::validate_prepared_input(key, text, mapping, endpoints, proof, bytes);
    if (validated != PreparedTextStatus::success) return validated;
    if (key.provider_instance != (*state_).instance || key.provider_generation != 1) return PreparedTextStatus::stale;
    detail::PreparedReservation reservation{};
    const PreparedTextStatus admitted = reservation.acquire((*state_).ledger, detail::PreparedResource::input, bytes);
    if (admitted != PreparedTextStatus::success) return admitted;
    try {
        std::unique_ptr<detail::PreparedInputStorage> candidate = std::make_unique<detail::PreparedInputStorage>();
        detail::PreparedInputStorage& input = *candidate;
        input.reservation = std::move(reservation);
        input.ledger = (*state_).ledger;
        input.key = key;
        input.proof = proof;
        input.requested_bytes = bytes;
        input.text_bytes = text.size();
        if (!text.empty()) {
            input.text = std::make_unique<char[]>(text.size());
            std::memcpy(input.text.get(), text.data(), text.size());
        }
        input.mapping_count = mapping.size();
        if (!mapping.empty()) {
            input.mapping = std::make_unique<DocumentMapSpan[]>(mapping.size());
            std::copy(mapping.begin(), mapping.end(), input.mapping.get());
        }
        input.endpoint_count = endpoints.size();
        input.endpoints = std::make_unique<PreparedSourceEndpoint[]>(endpoints.size());
        std::copy(endpoints.begin(), endpoints.end(), input.endpoints.get());
        detail::PreparedTextAccess::input(output) = std::move(candidate);
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) {
        return PreparedTextStatus::resource_failure;
    }
}

PreparedTextStatus PreparedTextService::open_session(const EncodedFontLease& fonts,
    PreparedTextWakeTarget* const wake, std::unique_ptr<PreparedTextSession>& output) {
    if (!owner_executor(*state_)) return PreparedTextStatus::wrong_executor;
    const std::shared_ptr<const detail::PreparedFontBank>& bank = detail::PreparedTextAccess::fonts(fonts);
    if (!bank || (*bank).ledger != (*state_).ledger) return PreparedTextStatus::invalid_input;
    const std::shared_ptr<detail::PreparedSessionState> previous = (*state_).session.lock();
    if (previous) {
        const stxlib::ScopedLock lock((*previous).mutex);
        if (!(*previous).snapshot.joined) return PreparedTextStatus::busy;
    }
    std::uint64_t session_id = 0;
    {
        detail::PreparedLedger& ledger = *(*state_).ledger;
        const stxlib::ScopedLock lock(ledger.mutex);
        if (ledger.closing) return PreparedTextStatus::closing;
    }
    const PreparedTextStatus identified = detail::acquire_prepared_session_instance(session_id);
    if (identified != PreparedTextStatus::success) return identified;
    try {
        std::shared_ptr<detail::PreparedSessionState> state = std::make_shared<detail::PreparedSessionState>();
        const PreparedTextStatus claimed = (*state).claim.acquire((*state_).ledger);
        if (claimed != PreparedTextStatus::success) return claimed;
        (*state).service = state_;
        (*state).fonts = bank;
        (*state).authority = std::make_shared<detail::PreparedAuthorityState>();
        (*(*state).authority).current.session = session_id;
        (*(*state).authority).executor = (*state_).executor;
        (*state).executor = (*state_).executor;
        (*state).wake = wake;
        std::unique_ptr<PreparedTextSession> session = detail::PreparedTextAccess::session(state);
        (*state).worker = detail::start_library_thread(&detail::PreparedSessionState::thread_main, state.get(),
                                                       "the prepared text session could not start its worker");
        (*state_).session = state;
        output = std::move(session);
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) {
        return PreparedTextStatus::resource_failure;
    } catch (const std::system_error&) {
        return PreparedTextStatus::resource_failure;
    }
}

void PreparedTextService::begin_close() {
    if (!owner_executor(*state_)) std::terminate();
    {
        detail::PreparedLedger& ledger = *(*state_).ledger;
        const stxlib::ScopedLock lock(ledger.mutex);
        ledger.closing = true;
    }
    const std::shared_ptr<detail::PreparedSessionState> session = (*state_).session.lock();
    if (session) { (*session).close(); (*session).join(); }
}

PreparedTextSession::PreparedTextSession(std::shared_ptr<detail::PreparedSessionState> state) : state_(std::move(state)) {}
PreparedTextSession::~PreparedTextSession() { begin_close(); join_and_release(); }

PreparedTextStatus PreparedTextSession::desire(const PreparedTextKey& key, LayoutAuthority& authority) {
    if ((*state_).executor != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const PreparedTextStatus validated = detail::validate_prepared_key(key);
    if (validated != PreparedTextStatus::success) return validated;
    const detail::PreparedFontBank& fonts = *(*state_).fonts;
    if (key.provider_instance != (*(*state_).service).instance || key.provider_generation != 1 ||
        key.font_set != fonts.identity || key.font_generation != fonts.generation) return PreparedTextStatus::stale;
    if (!matching_primary(fonts, key.font)) return PreparedTextStatus::incompatible_font;
    detail::PreparedAuthorityState& state = *(*state_).authority;
    const stxlib::ScopedLock lock(state.mutex);
    if (state.closing) return PreparedTextStatus::closing;
    if (state.current.epoch == std::numeric_limits<std::uint64_t>::max()) return PreparedTextStatus::generation_exhausted;
    ++state.current.epoch;
    state.key = key;
    authority = state.current;
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedTextSession::submit(const LayoutAuthority authority, PrepareInput& input) {
    if ((*state_).executor != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    std::unique_ptr<detail::PreparedInputStorage>& payload = detail::PreparedTextAccess::input(input);
    if (!payload || (*payload).ledger != (*(*state_).service).ledger) return PreparedTextStatus::invalid_input;
    {
        detail::PreparedAuthorityState& current = *(*state_).authority;
        const stxlib::ScopedLock lock(current.mutex);
        if (current.closing) return PreparedTextStatus::closing;
        if (!current.key || !same_layout_authority(current.current, authority) ||
            !same_prepared_text_key(*current.key, (*payload).key)) return PreparedTextStatus::stale;
    }
    const stxlib::ScopedLock lock((*state_).mutex);
    if ((*state_).snapshot.closing) return PreparedTextStatus::closing;
    if ((*state_).snapshot.slot != PreparedTextSlot::empty) return PreparedTextStatus::busy;
    detail::PreparedReservation reservation{};
    const PreparedTextStatus admitted = reservation.acquire((*(*state_).service).ledger,
        detail::PreparedResource::payload, PreparedTextLimits::payload_bytes);
    if (admitted != PreparedTextStatus::success) return admitted;
    try {
        std::unique_ptr<detail::PreparedTextStorage> job = std::make_unique<detail::PreparedTextStorage>();
        (*job).reservation = std::move(reservation);
        (*job).ledger = (*(*state_).service).ledger;
        (*job).authority_state = (*state_).authority;
        (*job).fonts = (*state_).fonts;
        (*job).authority = authority;
        (*job).input = std::move(payload);
        // The full payload reservation now owns these same allocations. End the
        // input-slot charge without freeing/copying their bytes.
        (*(*job).input).reservation.release();
        (*state_).job = std::move(job);
        (*state_).snapshot.slot = PreparedTextSlot::queued;
        (*state_).snapshot.completion = PreparedTextStatus::pending;
        (*state_).condition.notify_one();
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) {
        return PreparedTextStatus::resource_failure;
    }
}

PreparedTextSessionSnapshot PreparedTextSession::inspect_ready() const {
    if ((*state_).executor != stxlib::current_thread()) throw std::logic_error("Prepared session executor mismatch");
    PreparedTextSessionSnapshot snapshot{};
    {
        const stxlib::ScopedLock lock((*state_).mutex);
        snapshot = (*state_).snapshot;
        (*state_).wake_pending = false;
    }
    {
        const stxlib::ScopedLock lock((*(*state_).authority).mutex);
        snapshot.desired = (*(*state_).authority).current;
    }
    return snapshot;
}

PreparedTextStatus PreparedTextSession::adopt_ready(const LayoutAuthority expected, PreparedTextLayout& output) {
    if ((*state_).executor != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock((*state_).mutex);
    if ((*state_).snapshot.closing) return PreparedTextStatus::closing;
    if ((*state_).snapshot.slot != PreparedTextSlot::ready) return PreparedTextStatus::pending;
    if ((*state_).snapshot.completion != PreparedTextStatus::success) return (*state_).snapshot.completion;
    if (!(*state_).ready || !detail::prepared_authority_current(*(*state_).ready, expected)) return PreparedTextStatus::stale;
    detail::PreparedTextAccess::layout(output) = std::move((*state_).ready);
    (*state_).snapshot.slot = PreparedTextSlot::empty;
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedTextSession::discard_ready() {
    if ((*state_).executor != stxlib::current_thread()) return PreparedTextStatus::wrong_executor;
    const stxlib::ScopedLock lock((*state_).mutex);
    if ((*state_).snapshot.slot != PreparedTextSlot::ready) return PreparedTextStatus::busy;
    (*state_).ready.reset();
    (*state_).job.reset();
    (*state_).snapshot.slot = PreparedTextSlot::empty;
    return PreparedTextStatus::success;
}
void PreparedTextSession::cancel() {
    if ((*state_).executor != stxlib::current_thread()) std::terminate();
    detail::PreparedAuthorityState& state = *(*state_).authority;
    const stxlib::ScopedLock lock(state.mutex);
    state.key.reset();
}
void PreparedTextSession::begin_close() {
    if ((*state_).executor != stxlib::current_thread()) std::terminate();
    (*state_).close();
}
void PreparedTextSession::join_and_release() {
    if ((*state_).executor != stxlib::current_thread()) std::terminate();
    (*state_).close();
    (*state_).join();
}

namespace detail {
std::unique_ptr<PreparedTextSession> PreparedTextAccess::session(std::shared_ptr<PreparedSessionState> state) {
    std::unique_ptr<PreparedTextSession> result(new PreparedTextSession(std::move(state)));
    return result;
}
PreparedSessionState::~PreparedSessionState() { close(); join(); }
void PreparedSessionState::close() {
    if (authority) {
        const stxlib::ScopedLock lock((*authority).mutex);
        (*authority).closing = true;
        (*authority).key.reset();
    }
    {
        const stxlib::ScopedLock lock(mutex);
        snapshot.closing = true;
        wake = nullptr;
        condition.notify_one();
    }
}
void PreparedSessionState::join() {
    join_library_thread(worker);
    {
        const stxlib::ScopedLock lock(mutex);
        job.reset();
        ready.reset();
        snapshot.slot = PreparedTextSlot::empty;
        snapshot.joined = true;
    }
    claim.release();
}

void PreparedSessionState::thread_main(const stxlib::StopSignal& stop, void* const context) noexcept {
    // The snapshot's closing ends the loop; STXlib ends the thread's records
    // after this returns.
    static_cast<void>(stop);
    (*static_cast<PreparedSessionState*>(context)).run();
}

bool PreparedSessionState::job_or_closing(const void* const context) noexcept {
    const PreparedSessionState& state = *static_cast<const PreparedSessionState*>(context);
    return state.snapshot.closing || state.snapshot.slot == PreparedTextSlot::queued;
}

void PreparedSessionState::run() noexcept {
    std::unique_ptr<render::text::HarfBuzzFontEngine> engine{};
    PreparedTextStatus initialization = PreparedTextStatus::success;
    std::size_t engine_bytes = 0;
    std::array<FontFaceId, PreparedTextLimits::font_faces> face_ids{};
    try {
        engine = std::make_unique<render::text::HarfBuzzFontEngine>();
        const PreparedFontBank& bank = *fonts;
        engine_bytes = (*engine).configure_bounded_registration(bank.face_count, PreparedTextLimits::workspace_bytes);
        for (std::size_t index = 0; index < bank.face_count; ++index) {
            const PreparedFontFace& face = bank.faces[index];
            const std::span<const std::byte> encoded(face.bytes.get(), face.size);
            const std::optional<FontFaceId> registered = face.family.value != 0U
                ? (*engine).register_owned_family_typeface(face.family, face.weight, face.italic, encoded, fonts,
                                                           face.index)
                : (*engine).register_owned_typeface(face.role, face.weight, face.italic, encoded, fonts, face.index);
            if (!registered) {
                initialization = PreparedTextStatus::incompatible_font;
                engine.reset();
                break;
            }
            face_ids[index] = *registered;
        }
    } catch (const std::bad_alloc&) {
        initialization = PreparedTextStatus::resource_failure;
        engine.reset();
    } catch (...) {
        initialization = PreparedTextStatus::native_failure;
        engine.reset();
    }
    for (;;) {
        std::unique_ptr<PreparedTextStorage> active{};
        {
            stxlib::ScopedLock lock(mutex);
            condition.wait(lock, &PreparedSessionState::job_or_closing, this);
            if (snapshot.closing) break;
            active = std::move(job);
            snapshot.slot = PreparedTextSlot::running;
        }
        PreparedTextStatus completion = PreparedTextStatus::success;
        std::shared_ptr<const PreparedTextStorage> result{};
        try {
            if (!engine) completion = initialization;
            else if (!prepared_authority_current(*active, (*active).authority)) completion = PreparedTextStatus::cancelled;
            else {
                PreparedTextStorage& storage = *active;
                const PreparedInputStorage& input = *storage.input;
                const PreparedTextKey& key = input.key;
                storage.metrics = prepared_device_metrics(key.font, key.scale);
                FontSpec device_font = key.font;
                device_font.size = static_cast<double>(storage.metrics.device_size_26_6) / 64.0;
                device_font.letter_spacing *= key.scale;
                render::text::ShapeStorageLimits limits{};
                limits.workspace_bytes = PreparedTextLimits::workspace_bytes - engine_bytes;
                const std::size_t metadata_bytes = sizeof(PreparedTextStorage) + input.requested_bytes;
                if (metadata_bytes >= PreparedTextLimits::payload_bytes) throw std::length_error("Prepared metadata budget exceeded");
                limits.output_bytes = PreparedTextLimits::payload_bytes - metadata_bytes;
                const std::string_view text(input.text.get(), input.text_bytes);
                storage.geometry = (*engine).shape_bounded(text, device_font, limits);
                const render::text::BoundedShapedText& shaped = *storage.geometry;
                if (shaped.missing_primary_face || shaped.missing_clusters != 0) completion = PreparedTextStatus::missing_font_coverage;
                else {
                    storage.face_ids = face_ids;
                    storage.requested_bytes = metadata_bytes + shaped.controlled_output_bytes;
                    storage.metrics.advance_dip = shaped.width / key.scale;
                    storage.metrics.height_dip = shaped.height / key.scale;
                    storage.metrics.ascent_dip = shaped.ascent / key.scale;
                    storage.metrics.descent_dip = shaped.descent / key.scale;
                    storage.workspace_peak_bytes = engine_bytes + shaped.controlled_workspace_peak;
                    result = std::move(active);
                }
            }
        } catch (const std::length_error&) { completion = PreparedTextStatus::budget_exceeded; }
        catch (const std::bad_alloc&) { completion = PreparedTextStatus::resource_failure; }
        catch (...) { completion = PreparedTextStatus::native_failure; }
        {
            const stxlib::ScopedLock lock(mutex);
            ready = std::move(result);
            // Failed/cancelled work retains its occupied reservation until the
            // owner discards it or joins; completion is not deallocation.
            job = std::move(active);
            snapshot.completion = completion;
            snapshot.slot = PreparedTextSlot::ready;
            if (!snapshot.closing && wake != nullptr && !wake_pending) {
                wake_pending = true;
                (*wake).post_prepared_text_wake();
            }
        }
    }
    engine.reset();
}
} // namespace detail
} // namespace gui_forms
