#include "prepared_storage.hpp"
#include "gui_forms/text.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {

bool same_layout_authority(const LayoutAuthority left, const LayoutAuthority right) noexcept {
    const bool equal = left.session == right.session && left.epoch == right.epoch;
    return equal;
}

bool same_prepared_text_key(const PreparedTextKey& left, const PreparedTextKey& right) noexcept {
    const DocumentPageRequest& a = left.page;
    const DocumentPageRequest& b = right.page;
    const bool equal = a.revision.document == b.revision.document && a.revision.revision == b.revision.revision &&
        a.serial == b.serial && a.permitted.begin.value == b.permitted.begin.value &&
        a.permitted.end.value == b.permitted.end.value && a.viewport.anchor.value == b.viewport.anchor.value &&
        a.viewport.horizontal_dip == b.viewport.horizontal_dip && left.source.begin.value == right.source.begin.value &&
        left.source.end.value == right.source.end.value && left.display_begin.value == right.display_begin.value &&
        left.display_end.value == right.display_end.value && left.layout_serial == right.layout_serial &&
        left.provider_instance == right.provider_instance && left.provider_generation == right.provider_generation &&
        left.font_set == right.font_set && left.font_generation == right.font_generation &&
        left.context_generation == right.context_generation && left.font == right.font && left.scale == right.scale &&
        left.wrap_width == right.wrap_width && left.tab_columns == right.tab_columns && left.raster == right.raster;
    return equal;
}

bool EncodedFontLease::empty() const noexcept { const bool empty = !storage_; return empty; }
std::uint64_t EncodedFontLease::identity() const noexcept { if (!storage_) return 0; return (*storage_).identity; }
std::uint64_t EncodedFontLease::generation() const noexcept { if (!storage_) return 0; return (*storage_).generation; }
PrepareInput::PrepareInput() = default;
PrepareInput::~PrepareInput() = default;
PrepareInput::PrepareInput(PrepareInput&& other) noexcept = default;
PrepareInput& PrepareInput::operator=(PrepareInput&& other) noexcept = default;
bool PrepareInput::empty() const noexcept { const bool empty = !storage_; return empty; }
PreparedTextLayout::PreparedTextLayout() = default;
PreparedTextLayout::~PreparedTextLayout() = default;
PreparedTextLayout::PreparedTextLayout(PreparedTextLayout&& other) noexcept = default;
PreparedTextLayout& PreparedTextLayout::operator=(PreparedTextLayout&& other) noexcept = default;
bool PreparedTextLayout::empty() const noexcept { const bool empty = !storage_; return empty; }
PreparedTextMetrics PreparedTextLayout::metrics() const noexcept { if (!storage_) return {}; return (*storage_).metrics; }
LayoutAuthority PreparedTextLayout::authority() const noexcept { if (!storage_) return {}; return (*storage_).authority; }
std::size_t PreparedTextLayout::storage_bytes() const noexcept { if (!storage_) return 0; return (*storage_).requested_bytes; }
std::size_t PreparedTextLayout::workspace_peak_bytes() const noexcept { if (!storage_) return 0; return (*storage_).workspace_peak_bytes; }
const PreparedTextKey* PreparedTextLayout::key() const noexcept {
    if (!storage_) return nullptr;
    const PreparedTextKey* key = &(*(*storage_).input).key;
    return key;
}
std::string_view PreparedTextLayout::display_utf8() const noexcept {
    if (!storage_) return {};
    const detail::PreparedInputStorage& input = *(*storage_).input;
    const std::string_view view(input.text.get(), input.text_bytes);
    return view;
}
GrayTextMask::GrayTextMask() = default;
GrayTextMask::~GrayTextMask() = default;
GrayTextMask::GrayTextMask(GrayTextMask&& other) noexcept = default;
GrayTextMask& GrayTextMask::operator=(GrayTextMask&& other) noexcept = default;
bool GrayTextMask::empty() const noexcept { const bool empty = !storage_; return empty; }
std::uint32_t GrayTextMask::width() const noexcept { if (!storage_) return 0; return (*storage_).width; }
std::uint32_t GrayTextMask::height() const noexcept { if (!storage_) return 0; return (*storage_).height; }
std::int32_t GrayTextMask::left() const noexcept { if (!storage_) return 0; return (*storage_).left; }
std::int32_t GrayTextMask::top() const noexcept { if (!storage_) return 0; return (*storage_).top; }
PreparedTextMetrics GrayTextMask::metrics() const noexcept { if (!storage_) return {}; return (*storage_).metrics; }
BorrowedSpan<std::uint8_t> GrayTextMask::pixels() const noexcept {
    if (!storage_) return BorrowedSpan<std::uint8_t>();
    const std::uint8_t* const first = (*storage_).pixels.get();
    BorrowedSpan<std::uint8_t> view = detail::lend_view(first, (*storage_).pixel_count, (*storage_).lender);
    return view;
}

namespace detail {

namespace {
std::atomic<std::uint64_t> next_session_instance{1U};
std::atomic<std::uint64_t> next_controller_instance{1U};
}

PreparedTextStatus acquire_prepared_identity(std::atomic<std::uint64_t>& next,
    std::uint64_t& output) noexcept {
    std::uint64_t observed = next.load(std::memory_order_relaxed);
    for (;;) {
        if (observed == 0U || observed == std::numeric_limits<std::uint64_t>::max())
            return PreparedTextStatus::generation_exhausted;
        const std::uint64_t successor = observed + 1U;
        const bool exchanged = next.compare_exchange_weak(observed, successor,
            std::memory_order_relaxed, std::memory_order_relaxed);
        if (exchanged) {
            output = observed;
            return PreparedTextStatus::success;
        }
    }
}

PreparedTextStatus acquire_prepared_session_instance(std::uint64_t& output) noexcept {
    const PreparedTextStatus status = acquire_prepared_identity(next_session_instance, output);
    return status;
}

PreparedTextStatus acquire_prepared_controller_instance(std::uint64_t& output) noexcept {
    const PreparedTextStatus status = acquire_prepared_identity(next_controller_instance, output);
    return status;
}

PreparedSessionClaim::~PreparedSessionClaim() { release(); }

PreparedTextStatus PreparedSessionClaim::acquire(std::shared_ptr<PreparedLedger> ledger) {
    if (ledger_) return PreparedTextStatus::busy;
    if (!ledger) return PreparedTextStatus::invalid_input;
    std::lock_guard<std::mutex> lock((*ledger).mutex);
    if ((*ledger).closing) return PreparedTextStatus::closing;
    if ((*ledger).session_claimed) return PreparedTextStatus::busy;
    (*ledger).session_claimed = true;
    ledger_ = std::move(ledger);
    return PreparedTextStatus::success;
}

void PreparedSessionClaim::release() noexcept {
    if (!ledger_) return;
    {
        std::lock_guard<std::mutex> lock((*ledger_).mutex);
        (*ledger_).session_claimed = false;
    }
    ledger_.reset();
}

PreparedReservation::~PreparedReservation() { release(); }
PreparedReservation::PreparedReservation(PreparedReservation&& other) noexcept
    : ledger_(std::move(other.ledger_)), resource_(other.resource_), bytes_(std::exchange(other.bytes_, 0)) {}
PreparedReservation& PreparedReservation::operator=(PreparedReservation&& other) noexcept {
    if (this != &other) {
        release();
        ledger_ = std::move(other.ledger_);
        resource_ = other.resource_;
        bytes_ = std::exchange(other.bytes_, 0);
    }
    return *this;
}
namespace {

// Charges `bytes` of `resource` to `usage`, or refuses with the reason and
// leaves `usage` unchanged. The caller holds the ledger's lock.
PreparedTextStatus charge_reservation(PreparedTextBudgetSnapshot& usage, PreparedResource resource,
                                      std::size_t bytes) noexcept {
    switch (resource) {
    case PreparedResource::font_bank:
        if (bytes > PreparedTextLimits::font_bytes) return PreparedTextStatus::budget_exceeded;
        if (usage.font_banks == PreparedTextLimits::font_banks ||
            bytes > PreparedTextLimits::font_bytes - usage.font_bytes) return PreparedTextStatus::busy;
        ++usage.font_banks;
        usage.font_bytes += bytes;
        return PreparedTextStatus::success;
    case PreparedResource::input:
        if (bytes > PreparedTextLimits::display_bytes + PreparedTextLimits::metadata_bytes) return PreparedTextStatus::budget_exceeded;
        if (usage.input_owners != 0) return PreparedTextStatus::busy;
        ++usage.input_owners;
        usage.input_bytes += bytes;
        return PreparedTextStatus::success;
    case PreparedResource::payload:
        if (bytes != PreparedTextLimits::payload_bytes) return PreparedTextStatus::invalid_input;
        if (usage.payload_generations == PreparedTextLimits::payload_generations) return PreparedTextStatus::busy;
        ++usage.payload_generations;
        usage.payload_reserved_bytes += bytes;
        return PreparedTextStatus::success;
    case PreparedResource::mask:
        if (bytes > PreparedTextLimits::mask_bytes) return PreparedTextStatus::budget_exceeded;
        if (usage.mask_owners == PreparedTextLimits::mask_owners ||
            bytes > PreparedTextLimits::mask_bytes * PreparedTextLimits::mask_owners - usage.mask_bytes) {
            return PreparedTextStatus::busy;
        }
        ++usage.mask_owners;
        usage.mask_bytes += bytes;
        return PreparedTextStatus::success;
    }
    // A resource outside the enumeration has no budget to charge.
    return PreparedTextStatus::invalid_input;
}

// Returns a reservation's `bytes` of `resource` to `usage`. The caller holds
// the ledger's lock.
void refund_reservation(PreparedTextBudgetSnapshot& usage, PreparedResource resource,
                        std::size_t bytes) noexcept {
    switch (resource) {
    case PreparedResource::font_bank: --usage.font_banks; usage.font_bytes -= bytes; return;
    case PreparedResource::input: --usage.input_owners; usage.input_bytes -= bytes; return;
    case PreparedResource::payload: --usage.payload_generations; usage.payload_reserved_bytes -= bytes; return;
    case PreparedResource::mask: --usage.mask_owners; usage.mask_bytes -= bytes; return;
    }
    // No reservation is charged to a resource outside the enumeration
    // (charge_reservation refuses it), so there is nothing to return.
    return;
}

} // namespace

PreparedTextStatus PreparedReservation::acquire(std::shared_ptr<PreparedLedger> ledger,
    const PreparedResource resource, const std::size_t bytes) {
    if (ledger_ || !ledger) return PreparedTextStatus::invalid_input;
    std::lock_guard<std::mutex> lock((*ledger).mutex);
    if ((*ledger).closing) return PreparedTextStatus::closing;
    PreparedTextBudgetSnapshot& usage = (*ledger).usage;
    const PreparedTextStatus charged = charge_reservation(usage, resource, bytes);
    if (charged != PreparedTextStatus::success) return charged;
    ledger_ = std::move(ledger);
    resource_ = resource;
    bytes_ = bytes;
    return PreparedTextStatus::success;
}
void PreparedReservation::release() noexcept {
    if (!ledger_) return;
    {
        std::lock_guard<std::mutex> lock((*ledger_).mutex);
        PreparedTextBudgetSnapshot& usage = (*ledger_).usage;
        refund_reservation(usage, resource_, bytes_);
    }
    ledger_.reset();
    bytes_ = 0;
}

PreparedTextStatus validate_prepared_key(const PreparedTextKey& key) noexcept {
    const DocumentPageRequest& page = key.page;
    if (page.revision.document == 0 || page.revision.revision == 0 || page.serial == 0 ||
        key.layout_serial == 0 || key.provider_instance == 0 || key.provider_generation == 0 ||
        key.font_set == 0 || key.font_generation == 0 || key.context_generation == 0 ||
        page.permitted.begin.value > page.permitted.end.value ||
        page.permitted.end.value - page.permitted.begin.value > DocumentViewLimits::source_bytes ||
        page.viewport.anchor.value < page.permitted.begin.value || page.viewport.anchor.value > page.permitted.end.value ||
        !std::isfinite(page.viewport.horizontal_dip) || page.viewport.horizontal_dip < 0 ||
        key.source.begin.value > key.source.end.value || key.source.begin.value < page.permitted.begin.value ||
        key.source.end.value > page.permitted.end.value || key.display_begin.value > key.display_end.value ||
        !valid_font_spec(key.font) || !std::isfinite(key.scale) || !std::isfinite(key.wrap_width)) {
        return PreparedTextStatus::invalid_input;
    }
    if (key.font.size < 4 || key.font.size > 128 || key.scale < 0.5 || key.scale > 4 ||
        key.wrap_width != 0 || key.tab_columns != 4 ||
        key.raster != PreparedRasterProfile::light_gray_v2) return PreparedTextStatus::unsupported_profile;
    if (key.display_end.value - key.display_begin.value > PreparedTextLimits::display_bytes) {
        return PreparedTextStatus::budget_exceeded;
    }
    return PreparedTextStatus::success;
}

bool scalar_edge(const std::string_view text, const std::size_t offset) noexcept {
    if (offset > text.size()) return false;
    if (offset == text.size()) return true;
    const unsigned char value = static_cast<unsigned char>(text[offset]);
    const bool boundary = (value & 0xc0U) != 0x80U;
    return boundary;
}

PreparedTextStatus validate_prepared_input(const PreparedTextKey& key, const std::string_view text,
    const std::span<const DocumentMapSpan> mapping, const std::span<const PreparedSourceEndpoint> endpoints,
    const PreparedParagraphProof proof, std::size_t& bytes) noexcept {
    const PreparedTextStatus key_status = validate_prepared_key(key);
    if (key_status != PreparedTextStatus::success) return key_status;
    if (text.size() != key.display_end.value - key.display_begin.value || !validate_utf8(text).valid()) {
        return PreparedTextStatus::invalid_input;
    }
    if (mapping.size() > PreparedTextLimits::metadata_records ||
        endpoints.size() > PreparedTextLimits::metadata_records - mapping.size()) return PreparedTextStatus::budget_exceeded;
    static_assert(PreparedTextLimits::metadata_records <=
        (std::numeric_limits<std::size_t>::max() - sizeof(PreparedInputStorage)) /
        (sizeof(DocumentMapSpan) + sizeof(PreparedSourceEndpoint)));
    const std::size_t map_bytes = mapping.size() * sizeof(DocumentMapSpan);
    const std::size_t endpoint_bytes = endpoints.size() * sizeof(PreparedSourceEndpoint);
    const std::size_t metadata_bytes = sizeof(PreparedInputStorage) + map_bytes + endpoint_bytes;
    if (metadata_bytes > PreparedTextLimits::metadata_bytes) return PreparedTextStatus::budget_exceeded;
    if (!proof.complete_begin || !proof.complete_end || endpoints.empty()) return PreparedTextStatus::context_required;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if ((byte >= 0x09U && byte <= 0x0dU) || (byte >= 0x1cU && byte <= 0x1eU) ||
            (byte == 0xc2U && index + 1U < text.size() && static_cast<unsigned char>(text[index + 1U]) == 0x85U) ||
            (byte == 0xe2U && index + 2U < text.size() && static_cast<unsigned char>(text[index + 1U]) == 0x80U &&
             (static_cast<unsigned char>(text[index + 2U]) == 0xa8U || static_cast<unsigned char>(text[index + 2U]) == 0xa9U))) {
            return PreparedTextStatus::unsupported_profile;
        }
    }
    std::uint64_t source_end = key.source.begin.value;
    std::uint32_t display_end = key.display_begin.value;
    for (std::size_t index = 0; index < mapping.size(); ++index) {
        const DocumentMapSpan& span = mapping[index];
        if (span.source.begin.value != source_end || span.begin.value != display_end ||
            span.source.end.value <= source_end || span.end.value <= display_end ||
            span.source.end.value > key.source.end.value || span.end.value > key.display_end.value ||
            !scalar_edge(text, span.end.value - key.display_begin.value)) return PreparedTextStatus::invalid_input;
        if (span.kind == DocumentMapKind::identity_utf8) {
            if (span.source.end.value - source_end != span.end.value - display_end) return PreparedTextStatus::invalid_input;
        } else if (span.kind != DocumentMapKind::atomic_token) return PreparedTextStatus::invalid_input;
        source_end = span.source.end.value;
        display_end = span.end.value;
    }
    if (source_end != key.source.end.value || display_end != key.display_end.value) return PreparedTextStatus::invalid_input;
    if (text.empty() && (!mapping.empty() || endpoints.size() != 1U)) return PreparedTextStatus::invalid_input;
    const PreparedSourceEndpoint& first = endpoints.front();
    const PreparedSourceEndpoint& last = endpoints.back();
    if (first.source.value != key.source.begin.value || first.display.value != key.display_begin.value ||
        last.source.value != key.source.end.value || last.display.value != key.display_end.value) {
        return PreparedTextStatus::context_required;
    }
    std::size_t map_index = 0;
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        const PreparedSourceEndpoint& endpoint = endpoints[index];
        if (index != 0 && (endpoint.source.value <= endpoints[index - 1U].source.value ||
                          endpoint.display.value <= endpoints[index - 1U].display.value)) return PreparedTextStatus::invalid_input;
        if (endpoint.source.value < key.source.begin.value || endpoint.source.value > key.source.end.value ||
            endpoint.display.value < key.display_begin.value || endpoint.display.value > key.display_end.value ||
            !scalar_edge(text, endpoint.display.value - key.display_begin.value)) return PreparedTextStatus::invalid_input;
        if (text.empty()) continue;
        while (map_index + 1U < mapping.size() && mapping[map_index].end.value < endpoint.display.value) ++map_index;
        const DocumentMapSpan& span = mapping[map_index];
        if (endpoint.source.value < span.source.begin.value || endpoint.source.value > span.source.end.value ||
            endpoint.display.value < span.begin.value || endpoint.display.value > span.end.value) {
            return PreparedTextStatus::invalid_input;
        }
        if (span.kind == DocumentMapKind::identity_utf8) {
            if (endpoint.source.value - span.source.begin.value != endpoint.display.value - span.begin.value) {
                return PreparedTextStatus::invalid_input;
            }
        } else {
            const bool begin = endpoint.source.value == span.source.begin.value && endpoint.display.value == span.begin.value;
            const bool end = endpoint.source.value == span.source.end.value && endpoint.display.value == span.end.value;
            if (!begin && !end) return PreparedTextStatus::invalid_input;
        }
    }
    bytes = metadata_bytes + text.size();
    return PreparedTextStatus::success;
}

bool prepared_authority_current(const PreparedTextStorage& storage, const LayoutAuthority expected) {
    if (!storage.authority_state || !same_layout_authority(storage.authority, expected)) return false;
    PreparedAuthorityState& state = *storage.authority_state;
    std::lock_guard<std::mutex> lock(state.mutex);
    const bool current = prepared_authority_current_locked(storage, expected);
    return current;
}

bool prepared_authority_current_locked(const PreparedTextStorage& storage, const LayoutAuthority expected) noexcept {
    if (!storage.authority_state || !same_layout_authority(storage.authority, expected)) return false;
    const PreparedInputStorage& input = *storage.input;
    const PreparedAuthorityState& state = *storage.authority_state;
    const bool current = !state.closing && state.key.has_value() &&
        same_layout_authority(state.current, expected) && same_prepared_text_key(*state.key, input.key);
    return current;
}

PreparedTextMetrics prepared_device_metrics(const FontSpec font, const double scale) {
    const double device_fixed = font.size * scale * 64.0;
    const std::int64_t size = static_cast<std::int64_t>(std::llround(device_fixed));
    const PreparedTextMetrics metrics{.device_scale = scale, .device_size_26_6 = size};
    return metrics;
}

} // namespace detail
} // namespace gui_forms
