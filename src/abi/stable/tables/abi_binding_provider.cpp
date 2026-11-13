// A data provider a C caller describes. See abi_binding_provider.hpp.

#include "abi_binding_provider.hpp"

#include "abi_records.hpp"

#include "../registry/abi_registry.hpp"
#include "../values/abi_values.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

// The slot a provider's callbacks are converted under: set_provider's. A
// conversion that fails records nothing the caller sees (the failure record
// is restored, ProviderFailureScope), since the source turns it into a cell
// error instead.
constexpr SlotIdentity provider_slot{GF_TABLE_BINDINGS, 13U};

constexpr std::uint32_t column_size_1_2 = 32U;
constexpr std::uint32_t sort_key_size_1_2 = 24U;
constexpr std::uint32_t predicate_size_1_2 = 32U;
constexpr std::uint32_t callbacks_size_1_2 = 96U;
static_assert(sizeof(gf_binding_column) == column_size_1_2);
static_assert(sizeof(gf_binding_sort_key) == sort_key_size_1_2);
static_assert(sizeof(gf_binding_predicate) == predicate_size_1_2);
static_assert(sizeof(gf_binding_provider_callbacks) == callbacks_size_1_2);
// The C operators are the library's, in its order.
static_assert(static_cast<std::uint32_t>(BindingPredicateOperator::equal) == GF_BINDING_PREDICATE_EQUAL);
static_assert(static_cast<std::uint32_t>(BindingPredicateOperator::starts_with) == GF_BINDING_PREDICATE_STARTS_WITH);
static_assert(static_cast<std::uint32_t>(BindingPredicateOperator::is_not_null) == GF_BINDING_PREDICATE_IS_NOT_NULL);

constexpr gf_binding_provider_flags known_flags = GF_BINDING_PROVIDER_SORTS | GF_BINDING_PROVIDER_FILTERS |
                                                  GF_BINDING_PROVIDER_INSERTS | GF_BINDING_PROVIDER_REMOVES;

// The ids of the providers sources hold, and their links. bindings.notify
// may come from any thread, so the map is used under its mutex; a link
// found is copied out and used after the lock is released.
struct ProviderRegistry final {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, BindingProviderLink> links;
};

ProviderRegistry& provider_registry() {
    static ProviderRegistry registry;
    return registry;
}

std::atomic<std::uint64_t>& provider_counter() noexcept {
    static std::atomic<std::uint64_t> counter{0U};
    return counter;
}

// Keeps the calling thread's failure record across a conversion whose
// failure is data, not the caller's error.
class ProviderFailureScope final {
public:
    ProviderFailureScope() noexcept : saved_(thread_failure()) {}
    ~ProviderFailureScope() { thread_failure() = saved_; }
    ProviderFailureScope(const ProviderFailureScope&) = delete;
    ProviderFailureScope& operator=(const ProviderFailureScope&) = delete;

private:
    FailureRecord saved_;
};

// The text a provider's callback lent (gf_abi.h: valid until the library's
// next callback on the provider), as a call-scope borrow of `answer`, the
// scope of that callback's answer, which ends before the next callback. In
// the checked build a use after the scope fails; the library copies the
// text inside it. Empty for text whose size has no data.
BorrowedText lend_caller_text(const detail::BorrowScope& answer, gf_utf8_view view) {
    if (view.data == nullptr || view.size == 0U) {
        return BorrowedText();
    }
    const std::string_view text(view.data, static_cast<std::size_t>(view.size));
    BorrowedText lent = answer.lend_text(text);
    return lent;
}

// Lent text copied.
std::string copy_lent(const BorrowedText& lent) {
    std::string copied(unchecked_view(lent));
    return copied;
}

// Lent text copied and cut to maximum bytes on a whole UTF-8 sequence.
std::string copy_lent_bounded(const BorrowedText& lent, std::size_t maximum) {
    std::string copied = copy_lent(lent);
    if (copied.size() > maximum) {
        copied.resize(utf8_prefix_length(copied, maximum));
    }
    return copied;
}

gf_utf8_view view_of(std::string_view text) noexcept {
    gf_utf8_view view{};
    view.data = text.data();
    view.size = static_cast<std::uint64_t>(text.size());
    return view;
}

// held as a gf_value lent to a callback for its duration: its text points
// into held. False for a value with no form a callback can take whole (an
// object, or a collection, whose elements gf_value only counts).
bool lend_value(const BindingValue& held, gf_value& record) {
    const BindingValueKind kind = binding_value_kind(held);
    if (kind == BindingValueKind::object || kind == BindingValueKind::collection) {
        return false;
    }
    record = gf_value{};
    record.struct_size = static_cast<std::uint32_t>(sizeof(gf_value));
    std::string_view text;
    if (!record_binding_value(held, record, text)) {
        return false;
    }
    record.text = view_of(text);
    return true;
}

} // namespace

AbiBindingProvider::AbiBindingProvider(BindingSchema schema, const gf_binding_provider_callbacks& callbacks,
                                       std::uint64_t id)
    : schema_(std::move(schema)), callbacks_(callbacks), id_(id) {}

AbiBindingProvider::~AbiBindingProvider() {
    if (callbacks_.release != nullptr) {
        callbacks_.release(callbacks_.context);
    }
}

BindingSchema AbiBindingProvider::schema() const {
    return schema_;
}

BindingProviderCapabilities AbiBindingProvider::capabilities() const {
    BindingProviderCapabilities capabilities{};
    capabilities.sorts = (callbacks_.flags & GF_BINDING_PROVIDER_SORTS) != 0U;
    capabilities.filters = (callbacks_.flags & GF_BINDING_PROVIDER_FILTERS) != 0U;
    capabilities.inserts = (callbacks_.flags & GF_BINDING_PROVIDER_INSERTS) != 0U;
    capabilities.removes = (callbacks_.flags & GF_BINDING_PROVIDER_REMOVES) != 0U;
    return capabilities;
}

std::size_t AbiBindingProvider::row_count() {
    std::uint64_t count = 0U;
    const gf_status status = callbacks_.row_count(callbacks_.context, &count);
    if (status != GF_STATUS_OK) {
        // No answer: the source shows no rows and reports the provider.
        throw std::runtime_error("the provider's row_count failed");
    }
    if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("the provider's row count is past the bounds");
    }
    return static_cast<std::size_t>(count);
}

bool AbiBindingProvider::read_cell(std::size_t row, std::size_t column, BindingValue& value) {
    gf_value record{};
    record.struct_size = static_cast<std::uint32_t>(sizeof(gf_value));
    const gf_status status = callbacks_.read_value(callbacks_.context, static_cast<std::uint64_t>(row),
                                                   static_cast<std::uint64_t>(column), &record);
    if (status != GF_STATUS_OK) {
        return false;
    }
    // The struct_size the library set may have been overwritten; the record
    // is the library's own, so it is reset before conversion.
    record.struct_size = static_cast<std::uint32_t>(sizeof(gf_value));
    const ProviderFailureScope keep_failure;
    const gf_status converted = read_value(&record, provider_slot, value);
    return converted == GF_STATUS_OK;
}

bool AbiBindingProvider::read_stable_id(std::size_t row, std::string& stable_id) {
    // The answer's scope: the identity's text is borrowed until it ends.
    const detail::BorrowScope answer;
    gf_utf8_view identity{};
    const gf_status status =
        callbacks_.read_stable_id(callbacks_.context, static_cast<std::uint64_t>(row), &identity);
    if (status != GF_STATUS_OK || (identity.data == nullptr && identity.size != 0U) ||
        identity.size > binding_maximum_text_bytes) {
        return false;
    }
    stable_id = copy_lent(lend_caller_text(answer, identity));
    return true;
}

BindingWriteResult AbiBindingProvider::write_cell(std::size_t row, std::size_t column, const BindingValue& value) {
    gf_value record{};
    if (!lend_value(value, record)) {
        return BindingWriteResult{BindingWriteStatus::kind_mismatch, "the value has no C form a provider takes"};
    }
    // The value's text is valid only during write_value.
    const LentCallText lent(lent_texts_, std::string_view(record.text.data, static_cast<std::size_t>(record.text.size)));
    record.text.data = lent.text().data();
    record.text.size = lent.text().size();
    const detail::BorrowScope answer;
    std::uint32_t accepted = 0U;
    gf_utf8_view reason{};
    const gf_status status = callbacks_.write_value(callbacks_.context, static_cast<std::uint64_t>(row),
                                                    static_cast<std::uint64_t>(column), &record, &accepted, &reason);
    if (status != GF_STATUS_OK) {
        return BindingWriteResult{BindingWriteStatus::refused, "the provider's write_value failed"};
    }
    if (accepted == 1U) {
        return BindingWriteResult{BindingWriteStatus::accepted, {}};
    }
    return BindingWriteResult{BindingWriteStatus::refused,
                              copy_lent_bounded(lend_caller_text(answer, reason), binding_maximum_error_bytes)};
}

bool AbiBindingProvider::cell_read_only(std::size_t row, std::size_t column) {
    if (callbacks_.read_cell_state == nullptr) {
        return false;
    }
    std::uint32_t read_only = 0U;
    gf_utf8_view error{};
    const gf_status status = callbacks_.read_cell_state(callbacks_.context, static_cast<std::uint64_t>(row),
                                                        static_cast<std::uint64_t>(column), &read_only, &error);
    // A failed answer, or a flag other than 0 or 1, keeps the cell
    // read-only: the safe reading of an answer that cannot be trusted.
    const bool locked = status != GF_STATUS_OK || read_only != 0U;
    return locked;
}

bool AbiBindingProvider::read_error(std::size_t row, std::size_t column, std::string& text) {
    if (callbacks_.read_cell_state == nullptr) {
        return false;
    }
    const detail::BorrowScope answer;
    std::uint32_t read_only = 0U;
    gf_utf8_view error{};
    const gf_status status = callbacks_.read_cell_state(callbacks_.context, static_cast<std::uint64_t>(row),
                                                        static_cast<std::uint64_t>(column), &read_only, &error);
    if (status != GF_STATUS_OK || error.size == 0U) {
        return false;
    }
    text = copy_lent_bounded(lend_caller_text(answer, error), binding_maximum_error_bytes);
    return !text.empty();
}

BindingWriteResult AbiBindingProvider::insert_row(std::size_t row, const BindingRow& values) {
    if (callbacks_.insert_row == nullptr) {
        return BindingWriteResult{BindingWriteStatus::unavailable, "the provider does not insert rows"};
    }
    // The values are lent from values for the call; texts point into it.
    std::vector<gf_value> records(values.values.size());
    for (std::size_t index = 0U; index < values.values.size(); ++index) {
        if (!lend_value(values.values[index], records[index])) {
            return BindingWriteResult{BindingWriteStatus::kind_mismatch, "a value has no C form a provider takes"};
        }
    }
    std::uint32_t accepted = 0U;
    const gf_status status =
        callbacks_.insert_row(callbacks_.context, static_cast<std::uint64_t>(row), view_of(values.stable_id),
                              records.data(), static_cast<std::uint64_t>(records.size()), &accepted);
    if (status != GF_STATUS_OK || accepted != 1U) {
        return BindingWriteResult{BindingWriteStatus::refused, "the provider refused the row"};
    }
    return BindingWriteResult{BindingWriteStatus::accepted, {}};
}

BindingWriteResult AbiBindingProvider::remove_rows(std::size_t row, std::size_t count) {
    if (callbacks_.remove_rows == nullptr) {
        return BindingWriteResult{BindingWriteStatus::unavailable, "the provider does not remove rows"};
    }
    std::uint32_t accepted = 0U;
    const gf_status status = callbacks_.remove_rows(callbacks_.context, static_cast<std::uint64_t>(row),
                                                    static_cast<std::uint64_t>(count), &accepted);
    if (status != GF_STATUS_OK || accepted != 1U) {
        return BindingWriteResult{BindingWriteStatus::refused, "the provider refused the removal"};
    }
    return BindingWriteResult{BindingWriteStatus::accepted, {}};
}

bool AbiBindingProvider::apply_sort(std::span<const BindingSortKey> keys) {
    if (callbacks_.apply_sort == nullptr) {
        return false;
    }
    // Column names are lent from keys for the call.
    std::vector<gf_binding_sort_key> records(keys.size());
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        gf_binding_sort_key& record = records[index];
        record.struct_size = sort_key_size_1_2;
        record.direction = keys[index].direction == BindingSortDirection::descending ? GF_BINDING_SORT_DESCENDING
                                                                                      : GF_BINDING_SORT_ASCENDING;
        record.column = view_of(keys[index].column);
    }
    const gf_status status =
        callbacks_.apply_sort(callbacks_.context, records.data(), static_cast<std::uint64_t>(records.size()));
    return status == GF_STATUS_OK;
}

bool AbiBindingProvider::apply_filter(const BindingFilter& filter) {
    if (callbacks_.apply_filter == nullptr) {
        return false;
    }
    // Columns and operands are lent from filter for the call.
    const std::size_t count = filter.all_of.size();
    std::vector<gf_value> operands(count);
    std::vector<gf_binding_predicate> records(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const BindingPredicate& predicate = filter.all_of[index];
        gf_binding_predicate& record = records[index];
        record.struct_size = predicate_size_1_2;
        record.op = static_cast<gf_binding_predicate_operator>(predicate.op);
        record.column = view_of(predicate.column);
        record.operand = nullptr;
        const bool takes_operand =
            predicate.op != BindingPredicateOperator::is_null && predicate.op != BindingPredicateOperator::is_not_null;
        if (takes_operand) {
            if (!lend_value(predicate.operand, operands[index])) {
                return false;
            }
            record.operand = &operands[index];
        }
    }
    const gf_status status =
        callbacks_.apply_filter(callbacks_.context, records.data(), static_cast<std::uint64_t>(records.size()));
    return status == GF_STATUS_OK;
}

void AbiBindingProvider::attach(const BindingProviderLink& link) {
    ProviderRegistry& registry = provider_registry();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    registry.links.insert_or_assign(id_, link);
}

void AbiBindingProvider::detach() noexcept {
    ProviderRegistry& registry = provider_registry();
    const std::lock_guard<std::mutex> lock(registry.mutex);
    static_cast<void>(registry.links.erase(id_));
}

std::uint64_t next_binding_provider_id() noexcept {
    const std::uint64_t previous = provider_counter().fetch_add(1U);
    return previous + 1U;
}

gf_status notify_binding_provider(std::uint64_t id, const BindingNotification& notification, SlotIdentity slot) {
    BindingProviderLink link;
    {
        ProviderRegistry& registry = provider_registry();
        const std::lock_guard<std::mutex> lock(registry.mutex);
        const std::unordered_map<std::uint64_t, BindingProviderLink>::const_iterator found = registry.links.find(id);
        if (found == registry.links.end()) {
            return fail(GF_STATUS_REMOVED, slot, "no source holds the provider");
        }
        link = (*found).second;
    }
    if (link.revoked()) {
        return fail(GF_STATUS_REMOVED, slot, "the source has let the provider go");
    }
    link.notify(notification);
    return GF_STATUS_OK;
}

gf_status read_binding_columns(const gf_binding_column* columns, std::uint64_t count, SlotIdentity slot,
                               BindingSchema& schema) {
    if (count > GF_BINDING_MAXIMUM_FIELDS) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a provider has at most GF_BINDING_MAXIMUM_FIELDS columns");
    }
    std::uint32_t stride = 0U;
    const gf_status arrayed = check_array(columns, count, column_size_1_2, slot, stride);
    if (arrayed != GF_STATUS_OK) {
        return arrayed;
    }
    std::vector<BindingColumn> converted;
    converted.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        const gf_binding_column& column = element_at(columns, stride, index);
        if (column.kind > GF_VALUE_KIND_COLLECTION || column.kind == GF_VALUE_KIND_OBJECT || column.read_only > 1U ||
            column.reserved != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                        "a column's kind is a gf_value_kind other than OBJECT, read_only 0 or 1, reserved 0");
        }
        std::string_view name;
        const gf_status named = read_text(column.name, slot, name);
        if (named != GF_STATUS_OK) {
            return named;
        }
        if (name.empty()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a column's name is not empty");
        }
        BindingColumn made;
        made.name = std::string(name);
        made.kind = static_cast<BindingValueKind>(column.kind);
        made.read_only = column.read_only == 1U;
        converted.push_back(std::move(made));
    }
    try {
        schema = BindingSchema(std::move(converted));
    } catch (const std::invalid_argument&) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a provider's column names are distinct after folding");
    }
    return GF_STATUS_OK;
}

gf_status check_binding_provider_callbacks(const gf_binding_provider_callbacks& callbacks, SlotIdentity slot) {
    if (callbacks.struct_size < callbacks_size_1_2 || (callbacks.flags & ~known_flags) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "the callbacks need struct_size of at least the 1.2 size and known flags");
    }
    if (callbacks.row_count == nullptr || callbacks.read_value == nullptr || callbacks.read_stable_id == nullptr ||
        callbacks.write_value == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a provider needs row_count, read_value, read_stable_id and write_value");
    }
    const bool sorts = (callbacks.flags & GF_BINDING_PROVIDER_SORTS) != 0U;
    const bool filters = (callbacks.flags & GF_BINDING_PROVIDER_FILTERS) != 0U;
    const bool inserts = (callbacks.flags & GF_BINDING_PROVIDER_INSERTS) != 0U;
    const bool removes = (callbacks.flags & GF_BINDING_PROVIDER_REMOVES) != 0U;
    if ((sorts && callbacks.apply_sort == nullptr) || (filters && callbacks.apply_filter == nullptr) ||
        (inserts && callbacks.insert_row == nullptr) || (removes && callbacks.remove_rows == nullptr)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "each flag needs its callback");
    }
    return GF_STATUS_OK;
}

gf_status read_binding_sort_keys(const gf_binding_sort_key* keys, std::uint64_t count, SlotIdentity slot,
                                 std::vector<BindingSortKey>& converted) {
    if (count > GF_BINDING_MAXIMUM_FIELDS) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a sort has at most GF_BINDING_MAXIMUM_FIELDS keys");
    }
    std::uint32_t stride = 0U;
    const gf_status arrayed = check_array(keys, count, sort_key_size_1_2, slot, stride);
    if (arrayed != GF_STATUS_OK) {
        return arrayed;
    }
    converted.clear();
    converted.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        const gf_binding_sort_key& key = element_at(keys, stride, index);
        if (key.direction > GF_BINDING_SORT_DESCENDING) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a sort key's direction is a gf_binding_sort_direction");
        }
        std::string_view column;
        const gf_status named = read_text(key.column, slot, column);
        if (named != GF_STATUS_OK) {
            return named;
        }
        BindingSortKey made;
        made.column = std::string(column);
        made.direction =
            key.direction == GF_BINDING_SORT_DESCENDING ? BindingSortDirection::descending : BindingSortDirection::ascending;
        converted.push_back(std::move(made));
    }
    return GF_STATUS_OK;
}

gf_status read_binding_predicates(const gf_binding_predicate* predicates, std::uint64_t count, SlotIdentity slot,
                                  BindingFilter& converted) {
    if (count > GF_BINDING_MAXIMUM_FIELDS) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "a filter has at most GF_BINDING_MAXIMUM_FIELDS predicates");
    }
    std::uint32_t stride = 0U;
    const gf_status arrayed = check_array(predicates, count, predicate_size_1_2, slot, stride);
    if (arrayed != GF_STATUS_OK) {
        return arrayed;
    }
    converted.all_of.clear();
    converted.all_of.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        const gf_binding_predicate& predicate = element_at(predicates, stride, index);
        if (predicate.op > GF_BINDING_PREDICATE_IS_NOT_NULL) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                        "a predicate's operator is a gf_binding_predicate_operator");
        }
        std::string_view column;
        const gf_status named = read_text(predicate.column, slot, column);
        if (named != GF_STATUS_OK) {
            return named;
        }
        BindingPredicate made;
        made.column = std::string(column);
        made.op = static_cast<BindingPredicateOperator>(predicate.op);
        if (predicate.operand != nullptr) {
            const gf_status valued = read_value(predicate.operand, slot, made.operand);
            if (valued != GF_STATUS_OK) {
                return valued;
            }
        }
        converted.all_of.push_back(std::move(made));
    }
    return GF_STATUS_OK;
}

} // namespace gui_forms::abi1
