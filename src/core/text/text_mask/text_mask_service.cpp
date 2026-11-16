#include "text_mask_state.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include <cstring>

namespace gui_forms {
namespace {
TextMaskResult executor_result(const detail::TextMaskSessionState& state) noexcept {
    if (state.executor != std::this_thread::get_id()) return {TextMaskStatus::wrong_executor};
    if (state.joined) return {TextMaskStatus::closed};
    if (state.closing) return {TextMaskStatus::closing};
    return {TextMaskStatus::success};
}
detail::MaskSlot* find_slot(detail::TextMaskSessionState& state, const TextMaskRequestId id) noexcept {
    if (id.session != state.identity || id.serial == 0) return nullptr;
    for (std::size_t index = 0; index < state.slots.size(); ++index) {
        detail::MaskSlot& slot = state.slots[index];
        if (slot.state != TextMaskSlot::empty && slot.id.serial == id.serial) return &slot;
    }
    return nullptr;
}
void close_service(detail::TextMaskServiceState& state, const bool join) {
    state.closing = true;
    const std::shared_ptr<detail::TextMaskSessionState> session = state.session.lock();
    if (session) {
        (*session).close();
        if (join) (*session).join();
    }
}
}

TextMaskService::TextMaskService() {
    std::shared_ptr<detail::MaskLedger> ledger = detail::make_mask_ledger();
    const detail::MaskAllocator<detail::TextMaskServiceState> allocator(ledger);
    state_ = std::allocate_shared<detail::TextMaskServiceState>(allocator);
    (*state_).ledger = std::move(ledger);
    (*state_).executor = std::this_thread::get_id();
    (*state_).backend = &detail::native_mask_backend();
}
TextMaskService::~TextMaskService() { close_service(*state_, true); }
TextMaskBudgetSnapshot TextMaskService::budget_snapshot() const {
    const TextMaskBudgetSnapshot result = (*(*state_).ledger).snapshot();
    return result;
}
void TextMaskService::begin_close() {
    detail::require_mask_executor((*state_).executor);
    close_service(*state_, false);
}
TextMaskResult TextMaskService::create_font_bank(const std::span<const PreparedFontSource> sources,
    EncodedFontLease& output) {
    detail::TextMaskServiceState& state = *state_;
    if (state.executor != std::this_thread::get_id()) return {TextMaskStatus::wrong_executor};
    if (state.closing) return {TextMaskStatus::closing};
    if (sources.empty() || sources.size() > 8) return {TextMaskStatus::invalid_input};
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const PreparedFontSource& source = sources[index];
        if (source.encoded.empty() || source.weight < 1 || source.weight > 1000 || source.face_index > 255) {
            return {TextMaskStatus::invalid_input};
        }
        if (!valid_font_family(source.family) || (source.family.value != 0U && source.role.present())) {
            return {TextMaskStatus::invalid_input};
        }
        if (source.encoded.size() > 4U*1024U*1024U || source.encoded.size() > 8U*1024U*1024U - bytes) {
            return {TextMaskStatus::limit_exceeded, TextMaskLimit::font_bytes};
        }
        bytes += source.encoded.size();
    }
    std::size_t available = state.banks.size();
    for (std::size_t index = 0; index < state.banks.size(); ++index) {
        if (state.banks[index].expired()) { available = index; break; }
    }
    if (available == state.banks.size()) return {TextMaskStatus::limit_exceeded, TextMaskLimit::font_banks};
    try {
        detail::MaskCharge count{};
        detail::MaskCharge payload{};
        count.reserve(state.ledger, detail::MaskResource::banks, 1);
        payload.reserve(state.ledger, detail::MaskResource::fonts, bytes);
        // Charge font payload before any copied byte allocation. The aliasing
        // lease owns this wrapper, so all encoded bytes outlive native borrows.
        const detail::MaskAllocator<detail::MaskFontOwner> allocator(state.ledger);
        std::shared_ptr<detail::MaskFontOwner> candidate = std::allocate_shared<detail::MaskFontOwner>(allocator);
        detail::MaskFontOwner& owner = *candidate;
        owner.count = std::move(count);
        owner.bytes = std::move(payload);
        owner.bank.identity = detail::next_mask_identity();
        owner.bank.face_count = sources.size();
        owner.bank.encoded_bytes = bytes;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            const PreparedFontSource& source = sources[index];
            detail::PreparedFontFace& face = owner.bank.faces[index];
            face.size = source.encoded.size();
            face.bytes = std::make_unique<std::byte[]>(face.size);
            std::memcpy(face.bytes.get(), source.encoded.data(), face.size);
            face.role = source.role;
            face.weight = source.weight;
            face.italic = source.italic;
            face.index = source.face_index;
            face.family = source.family;
        }
        // The bank's typefaces over the copied bytes and its face pool, which every job's raster
        // faces are leased from and kept in between jobs. A face STXlib refuses leaves them
        // empty; the job reports it (unsupported_profile), as the native faces' own refusal does.
        static_cast<void>(detail::open_bank_typefaces(owner.bank));
        const std::shared_ptr<const detail::PreparedFontBank> alias(candidate, &owner.bank);
        owner.count.commit();
        owner.bytes.commit();
        state.banks[available] = candidate;
        const std::shared_ptr<detail::TextMaskSessionState> session = state.session.lock();
        if (session) {
            std::lock_guard<std::mutex> lock((*session).mutex);
            (*session).banks = state.banks;
        }
        detail::PreparedTextAccess::fonts(output) = alias;
        return {TextMaskStatus::success};
    } catch (const detail::MaskBudgetFailure& failure) {
        const TextMaskResult result = detail::mask_budget_result(failure.resource);
        return result;
    }
    catch (const std::bad_alloc&) { return {TextMaskStatus::resource_failure}; }
    catch (const std::overflow_error&) { return {TextMaskStatus::identifier_exhausted}; }
}
TextMaskResult TextMaskService::open_session(PreparedTextWakeTarget* const wake,
    std::unique_ptr<TextMaskSession>& output) {
    detail::TextMaskServiceState& state = *state_;
    if (state.executor != std::this_thread::get_id()) return {TextMaskStatus::wrong_executor};
    if (state.closing) return {TextMaskStatus::closing};
    if (output) {
        const std::shared_ptr<detail::TextMaskSessionState>& old_output = detail::TextMaskAccess::state(*output);
        std::lock_guard<std::mutex> lock((*old_output).mutex);
        if (!(*old_output).joined) return {TextMaskStatus::busy};
    }
    const std::shared_ptr<detail::TextMaskSessionState> previous = state.session.lock();
    if (previous) {
        std::lock_guard<std::mutex> lock((*previous).mutex);
        if (!(*previous).joined) return {TextMaskStatus::busy};
    }
    try {
        const detail::MaskAllocator<detail::TextMaskSessionState> allocator(state.ledger);
        std::shared_ptr<detail::TextMaskSessionState> candidate = std::allocate_shared<detail::TextMaskSessionState>(allocator);
        detail::TextMaskSessionState& session = *candidate;
        session.ledger = state.ledger;
        session.executor = state.executor;
        session.identity = detail::next_mask_identity();
        session.wake = wake;
        session.backend = state.backend;
        session.banks = state.banks;
        session.handle_charge.reserve(state.ledger, detail::MaskResource::metadata, sizeof(TextMaskSession));
        std::unique_ptr<TextMaskSession> handle = detail::TextMaskAccess::session(candidate);
        session.handle_charge.commit();
        session.worker = std::thread(&detail::TextMaskSessionState::run, candidate.get());
        state.session = candidate;
        output = std::move(handle);
        return {TextMaskStatus::success};
    } catch (const detail::MaskBudgetFailure& failure) {
        const TextMaskResult result = detail::mask_budget_result(failure.resource);
        return result;
    }
    catch (const std::bad_alloc&) { return {TextMaskStatus::resource_failure}; }
    catch (const std::overflow_error&) { return {TextMaskStatus::identifier_exhausted}; }
    catch (const std::system_error&) { return {TextMaskStatus::resource_failure}; }
}

TextMaskSession::TextMaskSession(std::shared_ptr<detail::TextMaskSessionState> state) : state_(std::move(state)) {}
TextMaskSession::~TextMaskSession() { (*state_).join(); }
std::unique_ptr<TextMaskSession> detail::TextMaskAccess::session(std::shared_ptr<TextMaskSessionState> state) {
    std::unique_ptr<TextMaskSession> result(new TextMaskSession(std::move(state)));
    return result;
}
TextMaskResult TextMaskSession::lookup(const EncodedFontLease& fonts, const TextMaskRequest& request, TextMaskLease& output) {
    detail::TextMaskSessionState& state = *state_;
    std::lock_guard<std::mutex> lock(state.mutex);
    TextMaskResult result = executor_result(state);
    if (result.status != TextMaskStatus::success) return result;
    detail::MaskOptions options{};
    result = detail::normalize_mask_request(request, options);
    if (result.status != TextMaskStatus::success) return result;
    const std::shared_ptr<const detail::MaskFontOwner> bank = state.find_bank(fonts);
    if (!bank || request.primary_face >= (*bank).bank.face_count) return {TextMaskStatus::invalid_input};
    std::shared_ptr<const detail::TextMaskStorage> found = state.cache_lookup_locked(*bank, options, request.utf8);
    if (!found) return {TextMaskStatus::cache_miss};
    detail::TextMaskAccess::storage(output) = std::move(found);
    return {TextMaskStatus::success};
}
TextMaskResult TextMaskSession::submit(const EncodedFontLease& fonts, const TextMaskRequest& request, TextMaskRequestId& output) {
    detail::TextMaskSessionState& state = *state_;
    std::lock_guard<std::mutex> lock(state.mutex);
    TextMaskResult result = executor_result(state);
    if (result.status != TextMaskStatus::success) return result;
    detail::MaskOptions options{};
    result = detail::normalize_mask_request(request, options);
    if (result.status != TextMaskStatus::success) return result;
    const std::shared_ptr<const detail::MaskFontOwner> bank = state.find_bank(fonts);
    if (!bank || request.primary_face >= (*bank).bank.face_count) return {TextMaskStatus::invalid_input};
    if (state.next_serial == std::numeric_limits<std::uint64_t>::max()) return {TextMaskStatus::identifier_exhausted};
    std::size_t selected = state.slots.size();
    bool assigned = false;
    std::size_t queued = 0;
    for (std::size_t index = 0; index < state.slots.size(); ++index) {
        const TextMaskSlot slot = state.slots[index].state;
        if (slot == TextMaskSlot::empty && selected == state.slots.size()) selected = index;
        if (slot == TextMaskSlot::queued) ++queued;
        if (slot == TextMaskSlot::running || slot == TextMaskSlot::dispatched || slot == TextMaskSlot::retiring) assigned = true;
    }
    if (selected == state.slots.size() || (assigned && queued == 8)) return {TextMaskStatus::busy};
    detail::MaskSlot& slot = state.slots[selected];
    try {
        slot.charge.reserve(state.ledger, detail::MaskResource::slots, 1);
        slot.result = state.cache_lookup_locked(*bank, options, request.utf8);
        if (slot.result) slot.key = (*slot.result).key;
        else {
            detail::MaskCharge count{};
            count.reserve(state.ledger, detail::MaskResource::keys, 1);
            const detail::MaskAllocator<detail::MaskKey> allocator(state.ledger);
            std::shared_ptr<detail::MaskKey> key = std::allocate_shared<detail::MaskKey>(allocator, state.ledger, std::move(count));
            (*key).options = options;
            (*key).fonts = bank;
            (*key).text.resize(request.utf8.size());
            if (!request.utf8.empty()) std::memcpy((*key).text.data(), request.utf8.data(), request.utf8.size());
            slot.key = std::move(key);
        }
        slot.charge.commit();
        slot.id = {state.identity, state.next_serial};
        ++state.next_serial;
        if (slot.result) {
            slot.state = TextMaskSlot::completed;
            slot.completion = {TextMaskStatus::success};
            if (!state.wake_pending) {
                state.wake_pending = true;
                state.wake_requested = true;
            }
        } else slot.state = assigned ? TextMaskSlot::queued : TextMaskSlot::dispatched;
        output = slot.id;
        state.condition.notify_one();
        return {TextMaskStatus::success};
    } catch (const detail::MaskBudgetFailure& failure) {
        slot.retire();
        const TextMaskResult failure_result = detail::mask_budget_result(failure.resource);
        return failure_result;
    }
    catch (const std::bad_alloc&) { slot.retire(); return {TextMaskStatus::resource_failure}; }
}
TextMaskSessionSnapshot TextMaskSession::snapshot() const {
    detail::TextMaskSessionState& state = *state_;
    detail::require_mask_executor(state.executor);
    std::lock_guard<std::mutex> lock(state.mutex);
    state.wake_pending = false;
    TextMaskSessionSnapshot result{};
    result.closing = state.closing;
    result.joined = state.joined;
    for (std::size_t index = 0; index < state.slots.size(); ++index) {
        const detail::MaskSlot& slot = state.slots[index];
        result.requests[index] = {slot.id, slot.state, slot.completion};
        if (slot.state != TextMaskSlot::empty) ++result.occupied;
        if (slot.state == TextMaskSlot::dispatched || slot.state == TextMaskSlot::running) ++result.assigned;
        if (slot.state == TextMaskSlot::queued) ++result.queued;
        if (slot.state == TextMaskSlot::completed) ++result.completed;
        if (slot.state == TextMaskSlot::retiring) ++result.retiring;
    }
    return result;
}
TextMaskResult TextMaskSession::take(const TextMaskRequestId id, TextMaskLease& output) {
    detail::TextMaskSessionState& state = *state_;
    if (state.executor != std::this_thread::get_id()) return {TextMaskStatus::wrong_executor};
    std::lock_guard<std::mutex> lock(state.mutex);
    detail::MaskSlot* slot = find_slot(state, id);
    if (slot == nullptr) return {TextMaskStatus::stale};
    if ((*slot).state == TextMaskSlot::retiring) return {TextMaskStatus::cancelled};
    if ((*slot).state != TextMaskSlot::completed) return {TextMaskStatus::pending};
    const TextMaskResult result = (*slot).completion;
    if (result.status == TextMaskStatus::success) detail::TextMaskAccess::storage(output) = (*slot).result;
    (*slot).retire();
    return result;
}
TextMaskCancellation TextMaskSession::cancel(const TextMaskRequestId id) {
    detail::TextMaskSessionState& state = *state_;
    if (state.executor != std::this_thread::get_id()) return {{TextMaskStatus::wrong_executor}};
    std::lock_guard<std::mutex> lock(state.mutex);
    detail::MaskSlot* slot = find_slot(state, id);
    if (slot == nullptr) return {{TextMaskStatus::stale}};
    if ((*slot).state == TextMaskSlot::running || (*slot).state == TextMaskSlot::retiring) {
        (*slot).state = TextMaskSlot::retiring;
        return {{TextMaskStatus::success}, TextMaskRetirement::pending_retirement};
    }
    (*slot).retire();
    return {{TextMaskStatus::success}, TextMaskRetirement::released};
}
TextMaskResult TextMaskSession::discard(const TextMaskRequestId id) {
    const TextMaskCancellation result = cancel(id);
    return result.result;
}
void TextMaskSession::clear_cache() {
    detail::TextMaskSessionState& state = *state_;
    detail::require_mask_executor(state.executor);
    std::lock_guard<std::mutex> lock(state.mutex);
    state.clear_cache_locked();
}
void TextMaskSession::begin_close() { detail::require_mask_executor((*state_).executor); (*state_).close(); }
void TextMaskSession::join_and_release() { detail::require_mask_executor((*state_).executor); (*state_).join(); }
} // namespace gui_forms
