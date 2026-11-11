#include "gui_forms/binding.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

#include "../binding_view/binding_view.hpp"
#include "../support/binding_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gui_forms {
namespace detail {

// The state a provider's link shares with its source (binding_provider.hpp,
// BindingProviderLink). source is read and written on the UI thread only and
// is null once revoked; posted and post_pending are guarded by mutex.
struct BindingLinkState final {
    std::mutex mutex;
    BindingSource* source{};
    std::atomic<bool> revoked{false};
    std::thread::id ui_thread;
    WindowDispatcher dispatcher;
    std::vector<BindingNotification> posted;
    bool post_pending{};
};

// The posted work that applies a link's queued notifications on the UI
// thread, at a dispatcher turn: never inside a read, a layout or a paint.
struct ApplyPostedNotifications final {
    std::shared_ptr<BindingLinkState> state;

    void operator()() const {
        std::vector<BindingNotification> taken;
        {
            const std::lock_guard<std::mutex> lock((*state).mutex);
            taken.swap((*state).posted);
            (*state).post_pending = false;
        }
        if ((*state).revoked.load()) {
            return;
        }
        BindingSource* source = (*state).source;
        if (source == nullptr) {
            return;
        }
        (*source).apply_posted(taken);
    }
};

// Reads key cells for the view index through the source's validated calls.
class SourceKeyReader final : public BindingKeyReader {
public:
    explicit SourceKeyReader(const BindingSource& source) noexcept : source_(&source) {}

    bool read_key(std::size_t row, std::size_t column, BindingValue& value) override {
        std::string refusal;
        const bool valid = (*source_).read_provider_cell(row, column, value, refusal);
        return valid;
    }

private:
    const BindingSource* source_ = nullptr;
};

} // namespace detail

namespace {

// Notifications of more rows than this are raised as one reset rather than
// one list change per row.
constexpr std::size_t maximum_item_events = 64U;
// The longest stable ID kept from a provider.
constexpr std::size_t maximum_stable_id_bytes = 1024U;
// The column lookup cache is cleared past this many spellings.
constexpr std::size_t maximum_column_lookups = 256U;
// The values the row cache holds at most, whatever the column count.
constexpr std::size_t row_cache_values = 64U * 1024U;

// Counts the source as in a call to its provider for the scope's life.
class ProviderCallScope final {
public:
    explicit ProviderCallScope(std::size_t& depth) noexcept : depth_(&depth) { *depth_ = *depth_ + 1U; }
    ~ProviderCallScope() { *depth_ = *depth_ - 1U; }
    ProviderCallScope(const ProviderCallScope&) = delete;
    ProviderCallScope& operator=(const ProviderCallScope&) = delete;

private:
    std::size_t* depth_ = nullptr;
};

// Queues notification on the link and posts its application, once per
// batch. Any thread; a revoked link drops it.
void post_notification(const std::shared_ptr<detail::BindingLinkState>& state,
                       const BindingNotification& notification) {
    bool post = false;
    {
        const std::lock_guard<std::mutex> lock((*state).mutex);
        if ((*state).revoked.load()) {
            return;
        }
        if ((*state).posted.size() >= BindingProviderLink::binding_maximum_posted_notifications) {
            // The queue collapses to one reset, which supersedes it all.
            (*state).posted.clear();
            (*state).posted.push_back(BindingNotification{BindingNotificationKind::reset, 0U, 0U, 0U});
        } else {
            (*state).posted.push_back(notification);
        }
        if (!(*state).post_pending) {
            (*state).post_pending = true;
            post = true;
        }
    }
    if (post) {
        std::function<void()> work = detail::ApplyPostedNotifications{state};
        const DispatchOperation operation = (*state).dispatcher.begin_invoke(std::move(work));
        static_cast<void>(operation);
    }
}

// A provider's text, bounded (an error text or a failure's message).
std::string bounded_text(std::string_view text, std::size_t maximum) {
    const std::string kept(text.substr(0U, std::min(text.size(), maximum)));
    return kept;
}

} // namespace

// ---------------------------------------------------------------------------
// The link.

BindingProviderLink::BindingProviderLink(std::shared_ptr<detail::BindingLinkState> state) noexcept
    : state_(std::move(state)) {}

void BindingProviderLink::reset() const {
    notify(BindingNotification{BindingNotificationKind::reset, 0U, 0U, 0U});
}

void BindingProviderLink::inserted(std::size_t row, std::size_t count) const {
    notify(BindingNotification{BindingNotificationKind::inserted, row, count, 0U});
}

void BindingProviderLink::removed(std::size_t row, std::size_t count) const {
    notify(BindingNotification{BindingNotificationKind::removed, row, count, 0U});
}

void BindingProviderLink::moved(std::size_t row, std::size_t count, std::size_t target) const {
    notify(BindingNotification{BindingNotificationKind::moved, row, count, target});
}

void BindingProviderLink::changed(std::size_t row, std::size_t column) const {
    notify(BindingNotification{BindingNotificationKind::changed, row, 0U, column});
}

void BindingProviderLink::notify(const BindingNotification& notification) const {
    if (revoked()) {
        return;
    }
    if (std::this_thread::get_id() == (*state_).ui_thread) {
        BindingSource* source = (*state_).source;
        if (source != nullptr) {
            (*source).receive_notification(notification);
        }
        return;
    }
    post_notification(state_, notification);
}

bool BindingProviderLink::revoked() const noexcept {
    const bool gone = !state_ || (*state_).revoked.load();
    return gone;
}

// ---------------------------------------------------------------------------
// Construction, ownership and access.

BindingSource::BindingSource(Window& window)
    : window_lifetime_(window.lifetime_),
      currency_manager_(std::unique_ptr<CurrencyManager>(
          new CurrencyManager(*this))) {
    window.verify_access("BindingSource construction");
    bind_provider(std::make_shared<BindingTable>());
}

BindingSource::~BindingSource() {
    if (is_alive()) {
        try { dispose(); } catch (...) {}
    }
    revoke_link();
}

Window* BindingSource::bound_window() const noexcept {
    const std::shared_ptr<gui_forms::detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

void BindingSource::require_access(std::string_view operation) const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot use a disposed BindingSource");
    }
    Window* owner = bound_window();
    if (!owner) {
        throw std::logic_error("GUI.Forms cannot use BindingSource after Window shutdown");
    }
    (*owner).verify_access(operation);
}

void BindingSource::require_row(std::size_t row) const {
    if (row >= count()) {
        throw std::out_of_range("GUI.Forms BindingSource row is past the rows");
    }
}

void BindingSource::revoke_link() noexcept {
    if (link_) {
        (*link_).revoked.store(true);
        {
            const std::lock_guard<std::mutex> lock((*link_).mutex);
            (*link_).posted.clear();
        }
        (*link_).source = nullptr;
        link_.reset();
    }
    if (provider_) {
        (*provider_).detach();
    }
}

void BindingSource::bind_provider(std::shared_ptr<BindingProvider> provider) {
    revoke_link();
    provider_ = std::move(provider);
    std::shared_ptr<detail::BindingLinkState> state = std::make_shared<detail::BindingLinkState>();
    (*state).source = this;
    (*state).ui_thread = std::this_thread::get_id();
    if (Window* owner = bound_window()) {
        (*state).dispatcher = (*owner).dispatcher();
    }
    link_ = state;
    // The schema and capabilities are data: a provider that fails to give
    // them binds with none.
    try {
        schema_ = (*provider_).schema();
        capabilities_ = (*provider_).capabilities();
    } catch (...) {
        schema_ = BindingSchema{};
        capabilities_ = BindingProviderCapabilities{};
    }
    column_lookup_.clear();
    row_cache_.clear();
    cache_capacity_ = std::clamp(row_cache_values / std::max<std::size_t>(schema_.size(), 1U), std::size_t{16},
                                 row_cache_rows);
    (*provider_).attach(BindingProviderLink(state));
    provider_count_ = read_row_count();
}

void BindingSource::set_provider(std::shared_ptr<BindingProvider> provider) {
    require_access("BindingSource provider replacement");
    if (!provider) {
        provider = std::make_shared<BindingTable>();
    }
    const std::ptrdiff_t old_position = position_;
    const std::string old_current = current_stable_id();
    editing_ = false;
    edit_values_.clear();
    edit_stable_id_.clear();
    bind_provider(std::move(provider));
    // A sort or filter whose columns the new schema lacks is dropped.
    bool columns_present = true;
    for (const BindingSortKey& key : sort_) {
        columns_present = columns_present && schema_.find(key.column).has_value();
    }
    if (!columns_present) {
        sort_.clear();
    }
    bool filter_present = true;
    for (const BindingPredicate& predicate : filter_.all_of) {
        filter_present = filter_present && schema_.find(predicate.column).has_value();
    }
    if (!filter_present) {
        filter_.all_of.clear();
    }
    view_mode_ = BindingViewMode::provider_order;
    rebuild_view();
    bump_generation();
    // A new provider starts at its first row, placed before the reset is
    // published so a bound control reads a row that exists.
    position_ = count() > 0U ? 0 : -1;
    data_source_changed_.emit();
    publish_model_change({BindingListChangeKind::reset, -1, {}, {}, true});
    ++resets_;
    if (position_ != old_position || current_stable_id() != old_current) {
        ++position_changes_;
        position_changed_.emit(position_);
        current_changed_.emit();
        current_item_changed_.emit();
    }
}

std::shared_ptr<BindingTable> BindingSource::table() const noexcept {
    std::shared_ptr<BindingTable> typed = std::dynamic_pointer_cast<BindingTable>(provider_);
    return typed;
}

std::optional<std::size_t> BindingSource::column_of(std::string_view name) const {
    const std::map<std::string, std::size_t, std::less<>>::const_iterator found = column_lookup_.find(name);
    if (found != column_lookup_.end()) {
        return (*found).second;
    }
    const std::optional<std::size_t> column = schema_.find(name);
    if (column) {
        if (column_lookup_.size() >= maximum_column_lookups) {
            column_lookup_.clear();
        }
        column_lookup_.emplace(std::string(name), *column);
    }
    return column;
}

std::size_t BindingSource::count() const noexcept {
    const std::size_t rows = view_ ? (*view_).count() : provider_count_;
    return rows;
}

std::size_t BindingSource::provider_row(std::size_t row) const {
    require_row(row);
    const std::size_t provider = view_ ? (*view_).row_of(row) : row;
    return provider;
}

// ---------------------------------------------------------------------------
// Provider calls, each validated.

std::size_t BindingSource::read_row_count() const {
    if (!provider_) {
        return 0U;
    }
    const ProviderCallScope scope(provider_depth_);
    ++provider_reads_;
    std::size_t rows = 0U;
    try {
        rows = (*provider_).row_count();
    } catch (...) {
        return 0U;
    }
    if (rows > binding_maximum_rows) {
        return 0U;
    }
    return rows;
}

bool BindingSource::read_provider_cell(std::size_t row, std::size_t column, BindingValue& value,
                                       std::string& refusal) const {
    value = BindingValue{};
    if (!provider_ || row >= provider_count_ || column >= schema_.size()) {
        refusal = "the cell is past the provider's rows or columns";
        return false;
    }
    const ProviderCallScope scope(provider_depth_);
    ++provider_reads_;
    bool answered = false;
    try {
        answered = (*provider_).read_cell(row, column, value);
    } catch (const std::exception& failure) {
        value = BindingValue{};
        refusal = "the provider failed: " + bounded_text(failure.what(), binding_maximum_error_bytes);
        return false;
    } catch (...) {
        value = BindingValue{};
        refusal = "the provider failed";
        return false;
    }
    if (!answered) {
        value = BindingValue{};
        refusal = "the provider has no value for the cell";
        return false;
    }
    if (!schema_.admits(column, value)) {
        value = BindingValue{};
        refusal = "the provider's value is not of the column's kind";
        return false;
    }
    const std::string* text = std::get_if<std::string>(&value);
    if (text != nullptr && (*text).size() > binding_maximum_text_bytes) {
        value = BindingValue{};
        refusal = "the provider's text is longer than binding_maximum_text_bytes";
        return false;
    }
    return true;
}

const BindingSource::CachedRow& BindingSource::cached_row(std::size_t row) const {
    if (row_cache_.size() != cache_capacity_) {
        row_cache_.assign(cache_capacity_, CachedRow{});
    }
    CachedRow& entry = row_cache_[row % cache_capacity_];
    if (entry.row == row && entry.generation == generation_) {
        return entry;
    }
    const std::size_t width = schema_.size();
    const std::size_t provider = view_ ? (*view_).row_of(row) : row;
    entry.row = binding_whole_row;
    entry.values.resize(width);
    entry.valid.assign(width, 0U);
    entry.read_only.assign(width, 0U);
    entry.errors.resize(width + 1U);
    for (std::string& text : entry.errors) {
        text.clear();
    }
    for (std::size_t column = 0U; column < width; ++column) {
        const bool valid = read_provider_cell(provider, column, entry.values[column], entry.errors[column]);
        entry.valid[column] = valid ? 1U : 0U;
    }
    {
        const ProviderCallScope scope(provider_depth_);
        for (std::size_t column = 0U; column <= width; ++column) {
            const std::size_t asked = column == width ? binding_whole_row : column;
            if (column < width && entry.valid[column] == 0U) {
                continue;
            }
            std::string text;
            bool has_error = false;
            ++provider_reads_;
            try {
                has_error = (*provider_).read_error(provider, asked, text);
            } catch (...) {
                has_error = false;
            }
            if (has_error) {
                entry.errors[column] = bounded_text(text, binding_maximum_error_bytes);
            }
            if (column < width) {
                bool locked = false;
                ++provider_reads_;
                try {
                    locked = (*provider_).cell_read_only(provider, column);
                } catch (...) {
                    // A provider that cannot say keeps the cell from writes.
                    locked = true;
                }
                entry.read_only[column] = locked ? 1U : 0U;
            }
        }
        std::string identity;
        bool identified = false;
        ++provider_reads_;
        try {
            identified = (*provider_).read_stable_id(provider, identity);
        } catch (...) {
            identified = false;
        }
        if (identified && identity.size() <= maximum_stable_id_bytes && valid_stable_id(identity)) {
            entry.stable_id = std::move(identity);
        } else {
            entry.stable_id.clear();
            if (entry.errors[width].empty()) {
                entry.errors[width] = "the provider gave no valid stable ID for the row";
            }
        }
    }
    entry.row = row;
    entry.generation = generation_;
    return entry;
}

// ---------------------------------------------------------------------------
// Reads and writes by view row.

std::optional<BindingValue> BindingSource::value(std::size_t row, std::size_t column) const {
    require_access("BindingSource read");
    require_row(row);
    if (column >= schema_.size()) {
        throw std::out_of_range("GUI.Forms BindingSource column is past the schema");
    }
    const CachedRow& entry = cached_row(row);
    if (entry.valid[column] == 0U) {
        return std::nullopt;
    }
    return entry.values[column];
}

std::optional<BindingValue> BindingSource::value_at(std::uint64_t generation, std::size_t row,
                                                    std::size_t column) const {
    if (generation != generation_) {
        return std::nullopt;
    }
    std::optional<BindingValue> read = value(row, column);
    return read;
}

std::optional<BindingValue> BindingSource::field(std::size_t row, std::string_view name) const {
    const std::optional<std::size_t> column = column_of(name);
    if (!column) {
        return std::nullopt;
    }
    std::optional<BindingValue> read = value(row, *column);
    return read;
}

std::string BindingSource::stable_id(std::size_t row) const {
    require_access("BindingSource read");
    require_row(row);
    const CachedRow& entry = cached_row(row);
    return entry.stable_id;
}

std::string BindingSource::error(std::size_t row, std::size_t column) const {
    require_access("BindingSource read");
    require_row(row);
    const CachedRow& entry = cached_row(row);
    if (column == binding_whole_row) {
        return entry.errors[schema_.size()];
    }
    if (column >= schema_.size()) {
        throw std::out_of_range("GUI.Forms BindingSource column is past the schema");
    }
    return entry.errors[column];
}

bool BindingSource::read_only(std::size_t row, std::size_t column) const {
    require_access("BindingSource read");
    require_row(row);
    if (column >= schema_.size()) {
        throw std::out_of_range("GUI.Forms BindingSource column is past the schema");
    }
    if (!allow_edit_ || schema_.column(column).read_only) {
        return true;
    }
    const CachedRow& entry = cached_row(row);
    const bool locked = entry.read_only[column] != 0U || entry.valid[column] == 0U;
    return locked;
}

BindingWriteResult BindingSource::write(std::size_t row, std::size_t column, BindingValue value) {
    require_access("BindingSource write");
    require_row(row);
    if (column >= schema_.size()) {
        throw std::out_of_range("GUI.Forms BindingSource column is past the schema");
    }
    if (!schema_.admits(column, value)) {
        return BindingWriteResult{BindingWriteStatus::kind_mismatch, "the value is not of the column's kind"};
    }
    if (read_only(row, column)) {
        return BindingWriteResult{BindingWriteStatus::read_only, "the cell is read-only"};
    }
    const std::size_t provider = provider_row(row);
    BindingWriteResult result;
    {
        const ProviderCallScope scope(provider_depth_);
        ++provider_reads_;
        try {
            result = (*provider_).write_cell(provider, column, value);
        } catch (const std::exception& failure) {
            result = BindingWriteResult{BindingWriteStatus::refused,
                                        "the provider failed: " +
                                            bounded_text(failure.what(), binding_maximum_error_bytes)};
        } catch (...) {
            result = BindingWriteResult{BindingWriteStatus::refused, "the provider failed"};
        }
    }
    if (result.accepted()) {
        apply_notification(BindingNotification{BindingNotificationKind::changed, provider, 0U, column});
    }
    return result;
}

std::string BindingSource::current_stable_id() const {
    if (position_ < 0 || static_cast<std::size_t>(position_) >= count()) {
        return {};
    }
    const std::string identity = stable_id(static_cast<std::size_t>(position_));
    return identity;
}

std::optional<BindingValue> BindingSource::current_field(std::string_view field_name) const {
    if (position_ < 0) {
        return std::nullopt;
    }
    std::optional<BindingValue> read = field(static_cast<std::size_t>(position_), field_name);
    return read;
}

std::string BindingSource::current_error(std::string_view field_name) const {
    if (position_ < 0) {
        return {};
    }
    const std::size_t row = static_cast<std::size_t>(position_);
    if (field_name.empty()) {
        return error(row, binding_whole_row);
    }
    const std::optional<std::size_t> column = column_of(field_name);
    if (!column) {
        return {};
    }
    return error(row, *column);
}

std::vector<std::shared_ptr<Binding>> BindingSource::bindings() const {
    require_access("BindingSource binding query");
    std::vector<std::shared_ptr<Binding>> result;
    result.reserve(bindings_.size());
    for (const std::weak_ptr<gui_forms::Binding>& item : bindings_) {
        if (const std::shared_ptr<gui_forms::Binding> binding = item.lock();
            binding && (*binding).is_alive() && (*binding).active()) {
            result.push_back(binding);
        }
    }
    return result;
}

bool BindingSource::set_current_field(std::string_view field_name, BindingValue incoming) {
    require_access("BindingSource field mutation");
    if (position_ < 0 || !allow_edit_) {
        return false;
    }
    std::optional<std::size_t> column = column_of(field_name);
    if (!column) {
        // The built-in table grows a field it has not got, as a record
        // took any field; another provider's schema is fixed.
        const std::shared_ptr<BindingTable> built_in = table();
        if (!built_in) {
            return false;
        }
        static_cast<void>((*built_in).find_or_add_column(field_name));
        column = column_of(field_name);
        if (!column || position_ < 0) {
            return false;
        }
    }
    const std::size_t row = static_cast<std::size_t>(position_);
    const std::optional<BindingValue> present = value(row, *column);
    if (present && *present == incoming) {
        return false;
    }
    if (read_only(row, *column) || !schema_.admits(*column, incoming)) {
        return false;
    }
    static_cast<void>(begin_edit());
    const BindingWriteResult result = write(row, *column, std::move(incoming));
    return result.accepted();
}

// ---------------------------------------------------------------------------
// Position.

bool BindingSource::set_position(std::ptrdiff_t value_position) {
    require_access("BindingSource position mutation");
    if (value_position < -1 || value_position >= static_cast<std::ptrdiff_t>(count())) {
        throw std::out_of_range("GUI.Forms BindingSource position is out of range");
    }
    if (position_ == value_position) return false;
    end_edit();
    const std::ptrdiff_t old = position_;
    position_ = value_position;
    publish_current_transition(old);
    return true;
}

bool BindingSource::move_first() {
    return count() == 0U ? false : set_position(0);
}

bool BindingSource::move_last() {
    return count() == 0U ? false : set_position(static_cast<std::ptrdiff_t>(count() - 1U));
}

bool BindingSource::move_next() {
    if (count() == 0U || position_ >= static_cast<std::ptrdiff_t>(count() - 1U)) {
        return false;
    }
    return set_position(position_ + 1);
}

bool BindingSource::move_previous() {
    if (position_ <= 0) return false;
    return set_position(position_ - 1);
}

// ---------------------------------------------------------------------------
// Edits.

bool BindingSource::begin_edit() {
    require_access("BindingSource edit begin");
    if (!allow_edit_ || position_ < 0) {
        return false;
    }
    const std::size_t row = static_cast<std::size_t>(position_);
    const std::size_t width = schema_.size();
    bool writable = width == 0U;
    for (std::size_t column = 0U; column < width && !writable; ++column) {
        writable = !read_only(row, column);
    }
    if (!writable) {
        return false;
    }
    const std::string identity = stable_id(row);
    if (editing_ && edit_stable_id_ == identity) {
        return true;
    }
    const CachedRow& entry = cached_row(row);
    edit_values_ = entry.values;
    edit_stable_id_ = identity;
    editing_ = true;
    return true;
}

void BindingSource::cancel_edit() {
    require_access("BindingSource edit cancellation");
    if (!editing_) return;
    const std::vector<BindingValue> saved = std::move(edit_values_);
    const std::string identity = edit_stable_id_;
    editing_ = false;
    edit_values_.clear();
    edit_stable_id_.clear();
    std::optional<std::size_t> row;
    if (position_ >= 0 && stable_id(static_cast<std::size_t>(position_)) == identity) {
        row = static_cast<std::size_t>(position_);
    } else {
        row = find_stable_id(identity);
    }
    if (!row) {
        return;
    }
    // The saved values go back straight to the provider, then one change
    // for the row.
    const std::size_t provider = provider_row(*row);
    const std::size_t width = std::min(saved.size(), schema_.size());
    for (std::size_t column = 0U; column < width; ++column) {
        const std::optional<BindingValue> present = value(*row, column);
        if (present && *present == saved[column]) {
            continue;
        }
        const ProviderCallScope scope(provider_depth_);
        ++provider_reads_;
        try {
            static_cast<void>((*provider_).write_cell(provider, column, saved[column]));
        } catch (...) {
            // A provider that fails keeps its value; the row is refreshed.
        }
    }
    apply_notification(BindingNotification{BindingNotificationKind::changed, provider, 0U, binding_whole_row});
}

void BindingSource::end_edit() {
    require_access("BindingSource edit completion");
    editing_ = false;
    edit_values_.clear();
    edit_stable_id_.clear();
}

// ---------------------------------------------------------------------------
// Rows through the provider.

std::optional<std::size_t> BindingSource::add(BindingRow row) {
    std::optional<std::size_t> position = insert(count(), std::move(row));
    return position;
}

std::optional<std::size_t> BindingSource::insert(std::size_t index, BindingRow row) {
    require_access("BindingSource insertion");
    if (!allow_new_) throw std::logic_error("GUI.Forms BindingSource additions are disabled");
    if (index > count()) {
        throw std::out_of_range("GUI.Forms BindingSource insertion index is out of range");
    }
    if (!capabilities_.inserts) {
        throw std::logic_error("GUI.Forms BindingSource provider does not insert rows");
    }
    const std::size_t provider = index == count() ? provider_count_ : provider_row(index);
    BindingWriteResult result;
    {
        const ProviderCallScope scope(provider_depth_);
        ++provider_reads_;
        try {
            result = (*provider_).insert_row(provider, row);
        } catch (const std::exception& failure) {
            result = BindingWriteResult{BindingWriteStatus::refused,
                                        "the provider failed: " +
                                            bounded_text(failure.what(), binding_maximum_error_bytes)};
        } catch (...) {
            result = BindingWriteResult{BindingWriteStatus::refused, "the provider failed"};
        }
    }
    if (!result.accepted()) {
        throw std::invalid_argument("GUI.Forms BindingSource: " + result.reason);
    }
    apply_notification(BindingNotification{BindingNotificationKind::inserted, provider, 1U, 0U});
    if (!view_) {
        return provider;
    }
    if (provider >= (*view_).row_count()) {
        return std::nullopt;
    }
    std::optional<std::size_t> position = (*view_).position_of(provider);
    return position;
}

bool BindingSource::remove_at(std::size_t index) {
    require_access("BindingSource removal");
    if (!allow_remove_) {
        throw std::logic_error("GUI.Forms BindingSource removals are disabled");
    }
    if (index >= count()) return false;
    if (!capabilities_.removes) {
        throw std::logic_error("GUI.Forms BindingSource provider does not remove rows");
    }
    const std::size_t provider = provider_row(index);
    if (editing_ && stable_id(index) == edit_stable_id_) {
        editing_ = false;
        edit_values_.clear();
        edit_stable_id_.clear();
    }
    BindingWriteResult result;
    {
        const ProviderCallScope scope(provider_depth_);
        ++provider_reads_;
        try {
            result = (*provider_).remove_rows(provider, 1U);
        } catch (...) {
            result = BindingWriteResult{BindingWriteStatus::refused, "the provider failed"};
        }
    }
    if (!result.accepted()) {
        return false;
    }
    apply_notification(BindingNotification{BindingNotificationKind::removed, provider, 1U, 0U});
    return true;
}

bool BindingSource::remove_current() {
    return position_ < 0 ? false : remove_at(static_cast<std::size_t>(position_));
}

void BindingSource::clear() {
    require_access("BindingSource clear");
    if (provider_count_ == 0U) return;
    if (!allow_remove_) {
        throw std::logic_error("GUI.Forms BindingSource removals are disabled");
    }
    editing_ = false;
    edit_values_.clear();
    edit_stable_id_.clear();
    const std::shared_ptr<BindingTable> built_in = table();
    if (built_in) {
        // The table reports its reset through the link.
        (*built_in).clear();
        return;
    }
    if (!capabilities_.removes) {
        throw std::logic_error("GUI.Forms BindingSource provider does not remove rows");
    }
    {
        const ProviderCallScope scope(provider_depth_);
        ++provider_reads_;
        try {
            static_cast<void>((*provider_).remove_rows(0U, provider_count_));
        } catch (...) {
            // Whatever the provider removed, the reset below re-reads it.
        }
    }
    apply_notification(BindingNotification{BindingNotificationKind::reset, 0U, 0U, 0U});
}

std::optional<std::size_t> BindingSource::find(
    std::string_view field_name, const BindingValue& wanted) const {
    require_access("BindingSource search");
    const std::optional<std::size_t> column = column_of(field_name);
    if (!column) {
        return std::nullopt;
    }
    BindingValue read;
    std::string refusal;
    for (std::size_t row = 0U; row < count(); ++row) {
        const std::size_t provider = view_ ? (*view_).row_of(row) : row;
        if (read_provider_cell(provider, *column, read, refusal) && read == wanted) {
            return row;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> BindingSource::find_stable_id(std::string_view wanted) const {
    require_access("BindingSource search");
    if (!provider_) {
        return std::nullopt;
    }
    std::string identity;
    for (std::size_t row = 0U; row < count(); ++row) {
        const std::size_t provider = view_ ? (*view_).row_of(row) : row;
        const ProviderCallScope scope(provider_depth_);
        ++provider_reads_;
        bool identified = false;
        try {
            identified = (*provider_).read_stable_id(provider, identity);
        } catch (...) {
            identified = false;
        }
        if (identified && identity == wanted) {
            return row;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Sort, filter and the view.

void BindingSource::set_sort(std::vector<BindingSortKey> keys) {
    require_access("BindingSource sort");
    for (const BindingSortKey& key : keys) {
        if (!schema_.find(key.column)) {
            throw std::invalid_argument("GUI.Forms BindingSource sort names an unknown column " + key.column);
        }
    }
    const std::ptrdiff_t old_position = position_;
    const std::string old_current = current_stable_id();
    const std::size_t anchor = position_ >= 0 ? provider_row(static_cast<std::size_t>(position_)) : binding_whole_row;
    sort_ = std::move(keys);
    rebuild_view();
    bump_generation();
    place_position(anchor, old_position);
    publish_model_change({BindingListChangeKind::reset, -1, {}, {}, false});
    ++resets_;
    announce_position(old_position, old_current);
}

void BindingSource::remove_sort() {
    set_sort({});
}

void BindingSource::set_filter(BindingFilter filter_value) {
    require_access("BindingSource filter");
    for (const BindingPredicate& predicate : filter_value.all_of) {
        if (!schema_.find(predicate.column)) {
            throw std::invalid_argument("GUI.Forms BindingSource filter names an unknown column " +
                                        predicate.column);
        }
    }
    const std::ptrdiff_t old_position = position_;
    const std::string old_current = current_stable_id();
    const std::size_t anchor = position_ >= 0 ? provider_row(static_cast<std::size_t>(position_)) : binding_whole_row;
    filter_ = std::move(filter_value);
    rebuild_view();
    bump_generation();
    place_position(anchor, old_position);
    publish_model_change({BindingListChangeKind::reset, -1, {}, {}, false});
    ++resets_;
    announce_position(old_position, old_current);
}

void BindingSource::remove_filter() {
    set_filter(BindingFilter{});
}

void BindingSource::rebuild_view() {
    view_.reset();
    const bool sorting = !sort_.empty();
    const bool filtering = !filter_.empty();
    const bool was_delegated = view_mode_ == BindingViewMode::provider_view;
    bool sort_delegated = false;
    bool filter_delegated = false;
    {
        const ProviderCallScope scope(provider_depth_);
        try {
            if (sorting && capabilities_.sorts) {
                sort_delegated = (*provider_).apply_sort(sort_);
            } else if (!sorting && was_delegated && capabilities_.sorts) {
                static_cast<void>((*provider_).apply_sort({}));
            }
            if (filtering && capabilities_.filters) {
                filter_delegated = (*provider_).apply_filter(filter_);
            } else if (!filtering && was_delegated && capabilities_.filters) {
                static_cast<void>((*provider_).apply_filter(BindingFilter{}));
            }
        } catch (...) {
            sort_delegated = false;
            filter_delegated = false;
        }
    }
    // The provider reordered itself (or restored its order): its rows are
    // read afresh, whether or not it reports the reset too.
    provider_count_ = read_row_count();
    const bool own_sort = sorting && !sort_delegated;
    const bool own_filter = filtering && !filter_delegated;
    if (!own_sort && !own_filter) {
        view_mode_ = (sort_delegated || filter_delegated) ? BindingViewMode::provider_view
                                                          : BindingViewMode::provider_order;
        return;
    }
    std::vector<detail::BindingViewKey> keys;
    if (own_sort) {
        for (const BindingSortKey& key : sort_) {
            keys.push_back(detail::BindingViewKey{*schema_.find(key.column),
                                                  key.direction == BindingSortDirection::descending});
        }
    }
    std::vector<detail::BindingViewPredicate> predicates;
    if (own_filter) {
        for (const BindingPredicate& predicate : filter_.all_of) {
            predicates.push_back(detail::BindingViewPredicate{*schema_.find(predicate.column), predicate});
        }
    }
    std::unique_ptr<detail::BindingView> built =
        std::make_unique<detail::BindingView>(std::move(keys), std::move(predicates));
    detail::SourceKeyReader reader(*this);
    (*built).build(provider_count_, reader);
    view_ = std::move(built);
    view_mode_ = BindingViewMode::index;
}

std::size_t BindingSource::view_node_count() const noexcept {
    return view_ ? (*view_).node_count() : 0U;
}

std::size_t BindingSource::view_text_bytes() const noexcept {
    return view_ ? (*view_).text_bytes() : 0U;
}

// ---------------------------------------------------------------------------
// Notifications.

void BindingSource::bump_generation() noexcept {
    detail::bump_counter(generation_);
}

void BindingSource::receive_notification(const BindingNotification& notification) {
    if (provider_depth_ > 0U) {
        // Reported from inside a call to the provider: applied at the next
        // dispatcher turn, never in the middle of a read.
        post_notification(link_, notification);
        return;
    }
    apply_notification(notification);
}

void BindingSource::apply_posted(const std::vector<BindingNotification>& notifications) {
    if (!is_alive()) {
        return;
    }
    for (const BindingNotification& notification : notifications) {
        if (!link_ || !provider_) {
            return;
        }
        apply_notification(notification);
    }
}

void BindingSource::reset_view_state(bool metadata_changed) {
    const std::ptrdiff_t old_position = position_;
    const std::string old_current = current_stable_id();
    // The schema may have changed: re-read it, and drop a sort or filter
    // whose columns are gone.
    BindingSchema reread = schema_;
    BindingProviderCapabilities capabilities = capabilities_;
    try {
        reread = (*provider_).schema();
        capabilities = (*provider_).capabilities();
    } catch (...) {
        reread = BindingSchema{};
        capabilities = BindingProviderCapabilities{};
    }
    const bool schema_changed = !(reread == schema_);
    if (schema_changed) {
        schema_ = std::move(reread);
        column_lookup_.clear();
        row_cache_.clear();
        cache_capacity_ = std::clamp(row_cache_values / std::max<std::size_t>(schema_.size(), 1U), std::size_t{16},
                                     row_cache_rows);
        bool present = true;
        for (const BindingSortKey& key : sort_) {
            present = present && schema_.find(key.column).has_value();
        }
        for (const BindingPredicate& predicate : filter_.all_of) {
            present = present && schema_.find(predicate.column).has_value();
        }
        if (!present) {
            sort_.clear();
            filter_.all_of.clear();
        }
    }
    capabilities_ = capabilities;
    provider_count_ = read_row_count();
    if (view_mode_ == BindingViewMode::index || schema_changed) {
        rebuild_view();
    }
    if (editing_ && !find_stable_id(edit_stable_id_)) {
        editing_ = false;
        edit_values_.clear();
        edit_stable_id_.clear();
    }
    bump_generation();
    // The current row is kept when the row at its position has its ID, or,
    // in the built-in table, which finds an ID without a scan, wherever the
    // row with its ID now is. Another provider's row that moved is not
    // searched for: that would read every row.
    std::size_t anchor = binding_whole_row;
    if (old_position >= 0 && static_cast<std::size_t>(old_position) < count() &&
        stable_id(static_cast<std::size_t>(old_position)) == old_current) {
        anchor = provider_row(static_cast<std::size_t>(old_position));
    } else if (!old_current.empty()) {
        const std::shared_ptr<BindingTable> built_in = table();
        if (built_in) {
            const std::optional<std::size_t> found = (*built_in).find(old_current);
            if (found) {
                anchor = *found;
            }
        }
    }
    // The position is placed before the reset is published, so a bound
    // control that reads the current row then reads one that exists.
    place_position(anchor, old_position);
    publish_model_change({BindingListChangeKind::reset, -1, {}, {}, metadata_changed || schema_changed});
    ++resets_;
    announce_position(old_position, old_current);
}

void BindingSource::apply_notification(const BindingNotification& notification) {
    if (!provider_) {
        return;
    }
    const std::size_t old_count = provider_count_;
    bool valid = true;
    std::size_t expected = old_count;
    switch (notification.kind) {
    case BindingNotificationKind::reset:
        valid = false;
        break;
    case BindingNotificationKind::inserted:
        valid = notification.count > 0U && notification.row <= old_count &&
                notification.count <= binding_maximum_rows - old_count;
        expected = valid ? old_count + notification.count : old_count;
        break;
    case BindingNotificationKind::removed:
        valid = notification.count > 0U && notification.row < old_count &&
                notification.count <= old_count - notification.row;
        expected = valid ? old_count - notification.count : old_count;
        break;
    case BindingNotificationKind::moved:
        valid = notification.count > 0U && notification.row < old_count &&
                notification.count <= old_count - notification.row &&
                notification.target <= old_count - notification.count;
        break;
    case BindingNotificationKind::changed:
        valid = notification.row < old_count &&
                (notification.target == binding_whole_row || notification.target < schema_.size());
        break;
    }
    if (valid) {
        // A count that disagrees with the notification makes it a reset.
        valid = read_row_count() == expected;
    }
    if (!valid) {
        reset_view_state(false);
        return;
    }
    const std::ptrdiff_t old_position = position_;
    const std::string old_current = current_stable_id();
    std::size_t anchor = position_ >= 0 ? provider_row(static_cast<std::size_t>(position_)) : binding_whole_row;
    provider_count_ = expected;
    bump_generation();
    detail::SourceKeyReader reader(*this);
    // The list changes are held until the position is placed in the
    // changed list, so a bound control that reads the current row while
    // they are published reads a row that exists.
    std::vector<BindingListChange> held;
    std::vector<std::size_t> positions;
    const std::size_t first = notification.row;
    const std::size_t rows = notification.count;
    switch (notification.kind) {
    case BindingNotificationKind::inserted: {
        if (anchor != binding_whole_row && anchor >= first) {
            anchor = anchor + rows;
        }
        if (view_ && rows > old_count / 8U && rows > maximum_item_events) {
            // A large insertion rebuilds the index instead.
            rebuild_view();
            held.push_back(BindingListChange{BindingListChangeKind::reset, -1, {}, {}, false});
            ++resets_;
            break;
        }
        if (view_) {
            (*view_).insert_rows(first, rows, reader, positions);
        } else {
            for (std::size_t index = 0U; index < rows && index <= maximum_item_events; ++index) {
                positions.push_back(first + index);
            }
        }
        if (positions.size() > maximum_item_events) {
            held.push_back(BindingListChange{BindingListChangeKind::reset, -1, {}, {}, false});
            ++resets_;
            break;
        }
        for (const std::size_t position : positions) {
            held.push_back(BindingListChange{BindingListChangeKind::item_added, static_cast<std::ptrdiff_t>(position),
                                  stable_id(position), {}, false});
        }
        break;
    }
    case BindingNotificationKind::removed: {
        if (anchor != binding_whole_row) {
            if (anchor >= first && anchor < first + rows) {
                anchor = binding_whole_row;
            } else if (anchor >= first + rows) {
                anchor = anchor - rows;
            }
        }
        if (view_) {
            (*view_).remove_rows(first, rows, positions);
            if ((*view_).wants_rebuild()) {
                rebuild_view();
            }
        } else {
            for (std::size_t index = rows; index > 0U && positions.size() <= maximum_item_events; --index) {
                positions.push_back(first + index - 1U);
            }
        }
        if (positions.size() > maximum_item_events) {
            held.push_back(BindingListChange{BindingListChangeKind::reset, -1, {}, {}, false});
            ++resets_;
            break;
        }
        for (const std::size_t position : positions) {
            held.push_back(BindingListChange{BindingListChangeKind::item_removed, static_cast<std::ptrdiff_t>(position),
                                  {}, {}, false});
        }
        break;
    }
    case BindingNotificationKind::moved: {
        if (anchor != binding_whole_row) {
            if (anchor >= first && anchor < first + rows) {
                anchor = notification.target + (anchor - first);
            } else {
                std::size_t remaining = anchor >= first + rows ? anchor - rows : anchor;
                if (remaining >= notification.target) {
                    remaining = remaining + rows;
                }
                anchor = remaining;
            }
        }
        if (view_) {
            (*view_).move_rows(first, rows, notification.target, reader);
        }
        held.push_back(BindingListChange{BindingListChangeKind::reset, -1, {}, {}, false});
        ++resets_;
        break;
    }
    case BindingNotificationKind::changed: {
        ++field_changes_;
        const std::string field_name =
            notification.target == binding_whole_row ? std::string{} : schema_.column(notification.target).name;
        std::optional<std::size_t> before = first;
        std::optional<std::size_t> after = first;
        if (view_) {
            const detail::BindingView::RowChange change = (*view_).change_row(first, notification.target, reader);
            before = change.before;
            after = change.after;
        }
        if (before && after && *before == *after) {
            held.push_back(BindingListChange{BindingListChangeKind::item_changed, static_cast<std::ptrdiff_t>(*after),
                                  stable_id(*after), field_name, false});
        } else {
            if (before) {
                held.push_back(BindingListChange{BindingListChangeKind::item_removed, static_cast<std::ptrdiff_t>(*before),
                                      {}, {}, false});
            }
            if (after) {
                held.push_back(BindingListChange{BindingListChangeKind::item_added, static_cast<std::ptrdiff_t>(*after),
                                      stable_id(*after), {}, false});
            }
        }
        const bool current_row = old_position >= 0 && anchor == first;
        place_position(anchor, old_position);
        for (const BindingListChange& change : held) {
            publish_model_change(change);
        }
        announce_position(old_position, old_current);
        if (current_row && position_ == old_position) {
            current_item_changed_.emit();
        }
        return;
    }
    case BindingNotificationKind::reset:
        break;
    }
    place_position(anchor, old_position);
    for (const BindingListChange& change : held) {
        publish_model_change(change);
    }
    announce_position(old_position, old_current);
}

void BindingSource::place_position(std::size_t anchor, std::ptrdiff_t old_position) {
    const std::size_t rows = count();
    std::ptrdiff_t next = -1;
    if (anchor != binding_whole_row && anchor < provider_count_) {
        if (view_) {
            const std::optional<std::size_t> shown = (*view_).position_of(anchor);
            if (shown) {
                next = static_cast<std::ptrdiff_t>(*shown);
            }
        } else {
            next = static_cast<std::ptrdiff_t>(anchor);
        }
    }
    if (next < 0 && rows > 0U) {
        // The current row is gone: the same position, clamped, or the
        // first row of a list that was empty.
        next = old_position < 0 ? 0 : std::min(old_position, static_cast<std::ptrdiff_t>(rows - 1U));
    }
    position_ = next;
}

void BindingSource::announce_position(std::ptrdiff_t old_position, const std::string& old_current) {
    const std::string current = current_stable_id();
    if (position_ != old_position || current != old_current) {
        ++position_changes_;
        position_changed_.emit(position_);
        current_changed_.emit();
        current_item_changed_.emit();
    }
}

void BindingSource::settle_position(std::size_t anchor, std::ptrdiff_t old_position, const std::string& old_current) {
    place_position(anchor, old_position);
    announce_position(old_position, old_current);
}

// ---------------------------------------------------------------------------
// Policies, suspension and resets.

void BindingSource::set_allow_edit(bool allow) {
    require_access("BindingSource edit policy mutation");
    if (allow_edit_ == allow) return;
    if (!allow) end_edit();
    allow_edit_ = allow;
    reset_bindings(true);
}

void BindingSource::set_allow_new(bool allow) {
    require_access("BindingSource addition policy mutation");
    if (allow_new_ == allow) return;
    allow_new_ = allow;
    reset_bindings(true);
}

void BindingSource::set_allow_remove(bool allow) {
    require_access("BindingSource removal policy mutation");
    if (allow_remove_ == allow) return;
    allow_remove_ = allow;
    reset_bindings(true);
}

void BindingSource::suspend_binding() {
    require_access("BindingSource suspension");
    suspended_ = true;
}

void BindingSource::resume_binding() {
    require_access("BindingSource resume");
    if (!suspended_) return;
    suspended_ = false;
    if (pending_reset_) {
        pending_reset_ = false;
        BindingListChange reset{BindingListChangeKind::reset, -1, {}, {}, false};
        model_changed_.emit(reset);
        if (raise_list_changed_events_) list_changed_.emit(reset);
        ++resets_;
    }
}

void BindingSource::reset_bindings(bool metadata_changed) {
    require_access("BindingSource reset");
    // Every row is read afresh: the cache is invalidated with the generation.
    bump_generation();
    publish_model_change({BindingListChangeKind::reset, -1, {}, {},
                          metadata_changed});
    ++resets_;
}

void BindingSource::reset_current_item() {
    if (position_ >= 0) static_cast<void>(reset_item(
        static_cast<std::size_t>(position_)));
}

bool BindingSource::reset_item(std::size_t index) {
    require_access("BindingSource item reset");
    if (index >= count()) return false;
    bump_generation();
    publish_model_change({BindingListChangeKind::item_changed,
                          static_cast<std::ptrdiff_t>(index),
                          stable_id(index), {}, false});
    if (static_cast<std::ptrdiff_t>(index) == position_) {
        current_item_changed_.emit();
    }
    return true;
}

void BindingSource::set_data_member(std::string member) {
    require_access("BindingSource data-member mutation");
    if (data_member_ == member) return;
    data_member_ = std::move(member);
    data_member_changed_.emit(data_member_);
    reset_bindings(true);
}

BindingSourceSnapshot BindingSource::snapshot() const {
    BindingSourceSnapshot result;
    result.count = count();
    result.position = position_;
    // The current row's ID is read through the provider, on its thread.
    Window* owner = is_alive() ? bound_window() : nullptr;
    if (owner != nullptr && (*owner).check_access()) {
        result.current_stable_id = current_stable_id();
    }
    result.revision = revision_;
    result.generation = generation_;
    result.resets = resets_;
    result.field_changes = field_changes_;
    result.position_changes = position_changes_;
    result.suspended_mutations = suspended_mutations_;
    result.binding_suspended = suspended_;
    result.editing = editing_;
    result.raise_list_changed_events = raise_list_changed_events_;
    return result;
}

void BindingSource::publish_model_change(const BindingListChange& change) {
    detail::bump_counter(revision_);
    if (suspended_) {
        pending_reset_ = true;
        ++suspended_mutations_;
    } else {
        model_changed_.emit(change);
    }
    if (raise_list_changed_events_) list_changed_.emit(change);
}

void BindingSource::publish_current_transition(std::ptrdiff_t old_position) {
    ++position_changes_;
    publish_model_change({BindingListChangeKind::position_changed, position_,
                          current_stable_id(), {}, false});
    static_cast<void>(old_position);
    position_changed_.emit(position_);
    current_changed_.emit();
    current_item_changed_.emit();
}

void BindingSource::verify_dispose_thread() {
    if (Window* owner = bound_window()) (*owner).verify_access("BindingSource disposal");
}

void BindingSource::on_dispose() noexcept {
    try { disposed_event_.emit(); } catch (...) {}
    model_changed_.disconnect_all();
    list_changed_.disconnect_all();
    current_changed_.disconnect_all();
    current_item_changed_.disconnect_all();
    position_changed_.disconnect_all();
    data_error_.disconnect_all();
    data_source_changed_.disconnect_all();
    data_member_changed_.disconnect_all();
    binding_complete_.disconnect_all();
    disposed_event_.disconnect_all();
    bindings_.clear();
    // The provider is let go: its link is revoked, then it is detached.
    revoke_link();
    provider_.reset();
    view_.reset();
    row_cache_.clear();
    provider_count_ = 0U;
    editing_ = false;
    edit_values_.clear();
    currency_manager_.reset();
    window_lifetime_.reset();
    position_ = -1;
}

// ---------------------------------------------------------------------------
// Bindings.

void BindingSource::register_binding(const std::shared_ptr<Binding>& binding) {
    require_access("Binding registration");
    compact_expired_bindings();
    for (const std::weak_ptr<Binding>& item : bindings_) {
        const std::shared_ptr<Binding> existing = item.lock();
        if (existing && existing.get() == binding.get()) return;
    }
    bindings_.push_back(binding);
}

void BindingSource::unregister_binding(const Binding* binding) noexcept {
    WeakBindingList::iterator item = bindings_.begin();
    while (item != bindings_.end()) {
        const std::shared_ptr<Binding> existing = (*item).lock();
        if (!existing || existing.get() == binding) {
            item = bindings_.erase(item);
        } else {
            ++item;
        }
    }
}

void BindingSource::compact_expired_bindings() noexcept {
    WeakBindingList::iterator item = bindings_.begin();
    while (item != bindings_.end()) {
        if ((*item).expired()) {
            item = bindings_.erase(item);
        } else {
            ++item;
        }
    }
}

bool BindingSource::transfer_bindings(bool source_to_control) {
    require_access(source_to_control ? "Binding push" : "Binding pull");
    const std::vector<std::weak_ptr<Binding>> snapshot = bindings_;
    bool accepted = true;
    for (const std::weak_ptr<gui_forms::Binding>& item : snapshot) {
        if (const std::shared_ptr<gui_forms::Binding> binding = item.lock(); binding && (*binding).is_alive()) {
            accepted = (source_to_control ? (*binding).read_value()
                                          : (*binding).write_value()) && accepted;
        }
    }
    compact_expired_bindings();
    return accepted;
}

namespace {

// The events BindingSource adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 9> binding_source_interactions{{
    {.name = "list_changed", .payload = "BindingListChange", .mutable_payload = false},
    {.name = "current_changed", .payload = "", .mutable_payload = false},
    {.name = "current_item_changed", .payload = "", .mutable_payload = false},
    {.name = "position_changed", .payload = "std::ptrdiff_t", .mutable_payload = false},
    {.name = "data_error", .payload = "std::string", .mutable_payload = false},
    {.name = "data_source_changed", .payload = "", .mutable_payload = false},
    {.name = "data_member_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "binding_complete", .payload = "BindingCompleteEvent", .mutable_payload = true},
    {.name = "disposed_event", .payload = "", .mutable_payload = false},
}};

} // namespace

void BindingSource::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), binding_source_interactions.begin(), binding_source_interactions.end());
}

} // namespace gui_forms
