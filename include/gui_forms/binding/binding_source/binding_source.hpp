#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/binding_provider/binding_provider.hpp"
#include "gui_forms/binding/binding_table/binding_table.hpp"
#include "gui_forms/binding/currency_manager/currency_manager.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Binding;
class BindingContext;
class ErrorProvider;
class Window;
namespace detail {
struct WindowLifetime;
struct BindingLinkState;
struct ApplyPostedNotifications;
class BindingView;
class SourceKeyReader;
}

// How a source orders and subsets its provider's rows: in the provider's
// own order (no sort or filter), in the order the provider applied itself
// (it sorts or filters, BindingProviderCapabilities), or through the
// source's own view index (binding_view.hpp).
enum class BindingViewMode : std::uint8_t {
    provider_order,
    provider_view,
    index,
};

// A list of rows bound to controls (Windows Forms' BindingSource), over a
// data provider (audit N31, owner ruling 2026-11-10; binding_provider.hpp).
// A source starts with an empty BindingTable, the built-in column-storage
// provider; set_provider binds any other.
//
// Rows are numbered in the source's view: the provider's order, sorted and
// filtered when a sort or filter is set. Every cell read goes through a
// bounded row cache (the rows lately read, at most row_cache_rows of them,
// whatever the row count), so painting a window of a million-row provider
// reads only what it shows. Every change the provider reports bumps
// generation(), which invalidates every cached row; a stale generation
// handed back (value_at) is refused.
//
// Reads never throw for bad data: a provider answer past the bounds, of the
// wrong kind, or an exception it throws becomes a cell error (error()).
// Misuse of the source itself (another thread, an index past count(), an
// unknown column in a sort) throws, as a programming error (N70).
class GUI_FORMS_API BindingSource final : public Component,
                            public std::enable_shared_from_this<BindingSource> {
public:
    explicit BindingSource(Window& window);
    ~BindingSource() override;

    // The rows the row cache holds at most.
    static constexpr std::size_t row_cache_rows = 512U;

    // Binds provider (null binds a new empty BindingTable). The previous
    // provider is detached and its link revoked; the view resets, the sort
    // and filter are kept when the new schema still has their columns, and
    // data_source_changed is raised.
    void set_provider(std::shared_ptr<BindingProvider> provider);
    [[nodiscard]] const std::shared_ptr<BindingProvider>& provider() const noexcept { return provider_; }
    // The provider when it is a BindingTable, else null.
    [[nodiscard]] std::shared_ptr<BindingTable> table() const noexcept;
    // The provider's columns, as last read (on binding and on every reset).
    [[nodiscard]] const BindingSchema& schema() const noexcept { return schema_; }
    [[nodiscard]] std::optional<std::size_t> column_of(std::string_view name) const;
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] std::ptrdiff_t position() const noexcept { return position_; }
    bool set_position(std::ptrdiff_t position);
    bool move_first();
    bool move_last();
    bool move_next();
    bool move_previous();
    // Cells, by view row (below count()). An empty value is a cell the
    // provider gave no valid answer for; error() says why.
    [[nodiscard]] std::optional<BindingValue> value(std::size_t row, std::size_t column) const;
    // value, refused (empty) when generation is not the current one.
    [[nodiscard]] std::optional<BindingValue> value_at(std::uint64_t generation, std::size_t row,
                                                       std::size_t column) const;
    [[nodiscard]] std::optional<BindingValue> field(std::size_t row, std::string_view name) const;
    // The row's stable ID; empty when the provider gave none valid.
    [[nodiscard]] std::string stable_id(std::size_t row) const;
    // The cell's error text (the row's with binding_whole_row): the
    // provider's, or why its answer was refused; empty for none.
    [[nodiscard]] std::string error(std::size_t row, std::size_t column) const;
    // Whether the source may write the cell: the source, the column, the
    // row and the provider's per-cell flag all allow it.
    [[nodiscard]] bool read_only(std::size_t row, std::size_t column) const;
    // Writes the cell straight to the provider's data.
    BindingWriteResult write(std::size_t row, std::size_t column, BindingValue value);
    // The current row's stable ID, or empty.
    [[nodiscard]] std::string current_stable_id() const;
    [[nodiscard]] std::optional<BindingValue> current_field(
        std::string_view field) const;
    [[nodiscard]] std::string current_error(std::string_view field) const;
    [[nodiscard]] std::vector<std::shared_ptr<Binding>> bindings() const;
    bool set_current_field(std::string_view field, BindingValue value);

    // Adds a row through the provider (capabilities().inserts): at the end
    // of the provider's rows, or before the provider row shown at index.
    // Returns the row's view position, or empty when the filter hides it.
    // Throws when additions are off or the provider cannot insert; a row
    // the provider refuses throws std::invalid_argument.
    std::optional<std::size_t> add(BindingRow row);
    std::optional<std::size_t> insert(std::size_t index, BindingRow row);
    // Adds rows at the end of the provider's rows, all or none. A row the
    // provider refuses removes the rows added before it again and throws
    // std::invalid_argument with the refusal, leaving the rows as they
    // were; if the provider then refuses that removal too, the source
    // resets to what the provider holds and throws std::runtime_error.
    // More than one row needs a provider that removes rows (std::logic_error
    // otherwise, before anything changes). One inserted notification is
    // applied for the batch.
    void add_rows(std::vector<BindingRow> rows);
    bool remove_at(std::size_t index);
    bool remove_current();
    void clear();
    // The first view row whose field holds value (operator==, so 2 and 2.0
    // differ), by a linear search.
    [[nodiscard]] std::optional<std::size_t> find(
        std::string_view field, const BindingValue& value) const;
    [[nodiscard]] std::optional<std::size_t> find_stable_id(std::string_view stable_id) const;

    // Sort and filter (BindingSource.Sort, .Filter). A provider that sorts
    // or filters is asked first; otherwise the source builds its own index.
    // An unknown column throws std::invalid_argument and changes nothing.
    void set_sort(std::vector<BindingSortKey> keys);
    void remove_sort();
    [[nodiscard]] const std::vector<BindingSortKey>& sort() const& noexcept { return sort_; }
    const std::vector<BindingSortKey>& sort() const&& = delete;
    void set_filter(BindingFilter filter);
    void remove_filter();
    [[nodiscard]] const BindingFilter& filter() const noexcept { return filter_; }
    [[nodiscard]] BindingViewMode view_mode() const noexcept { return view_mode_; }
    // The provider row behind a view row (below count()).
    [[nodiscard]] std::size_t provider_row(std::size_t row) const;
    // The view index's storage (nodes, key text bytes), for bounded-memory
    // checks; zeros without an index.
    [[nodiscard]] std::size_t view_node_count() const noexcept;
    [[nodiscard]] std::size_t view_text_bytes() const noexcept;
    // The provider calls this source has made, for bounded-cost checks.
    [[nodiscard]] std::uint64_t provider_reads() const noexcept { return provider_reads_; }

    [[nodiscard]] bool allow_edit() const noexcept { return allow_edit_; }
    void set_allow_edit(bool allow);
    [[nodiscard]] bool allow_new() const noexcept { return allow_new_; }
    void set_allow_new(bool allow);
    [[nodiscard]] bool allow_remove() const noexcept { return allow_remove_; }
    void set_allow_remove(bool allow);
    bool begin_edit();
    void cancel_edit();
    void end_edit();

    [[nodiscard]] bool binding_suspended() const noexcept { return suspended_; }
    void suspend_binding();
    void resume_binding();
    [[nodiscard]] bool raise_list_changed_events() const noexcept {
        return raise_list_changed_events_;
    }
    void set_raise_list_changed_events(bool raise) noexcept {
        raise_list_changed_events_ = raise;
    }
    void reset_bindings(bool metadata_changed = false);
    void reset_current_item();
    bool reset_item(std::size_t index);

    [[nodiscard]] const std::string& data_member() const& noexcept {
        return data_member_;
    }
    const std::string& data_member() const&& = delete;
    void set_data_member(std::string member);
    [[nodiscard]] CurrencyManager& currency_manager() noexcept {
        return *currency_manager_;
    }
    [[nodiscard]] const CurrencyManager& currency_manager() const noexcept {
        return *currency_manager_;
    }
    [[nodiscard]] BindingSourceSnapshot snapshot() const;

    [[nodiscard]] Event<const BindingListChange&>& list_changed() noexcept {
        return list_changed_;
    }
    [[nodiscard]] Event<>& current_changed() noexcept { return current_changed_; }
    [[nodiscard]] Event<>& current_item_changed() noexcept {
        return current_item_changed_;
    }
    [[nodiscard]] Event<std::ptrdiff_t>& position_changed() noexcept {
        return position_changed_;
    }
    [[nodiscard]] Event<const std::string&>& data_error() noexcept {
        return data_error_;
    }
    [[nodiscard]] Event<>& data_source_changed() noexcept {
        return data_source_changed_;
    }
    [[nodiscard]] Event<const std::string&>& data_member_changed() noexcept {
        return data_member_changed_;
    }
    [[nodiscard]] Event<BindingCompleteEvent&>& binding_complete() noexcept {
        return binding_complete_;
    }
    [[nodiscard]] Event<>& disposed_event() noexcept { return disposed_event_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    friend class Binding;
    friend class BindingContext;
    friend class CurrencyManager;
    friend class ErrorProvider;
    friend class BindingProviderLink;
    friend struct detail::ApplyPostedNotifications;
    friend class detail::SourceKeyReader;
    using WeakBindingList = std::vector<std::weak_ptr<Binding>>;

    // One cached row: its view row, the generation it was read at, and each
    // cell's value, validity and error text (the row's at index width).
    struct CachedRow final {
        std::size_t row{binding_whole_row};
        std::uint64_t generation{};
        std::string stable_id;
        std::vector<BindingValue> values;
        std::vector<unsigned char> valid;
        std::vector<unsigned char> read_only;
        std::vector<std::string> errors;
    };

    void require_access(std::string_view operation) const;
    void require_row(std::size_t row) const;
    // The provider calls, each validated; the provider is in a call while
    // provider_depth_ is above zero, and notifications wait meanwhile.
    std::size_t read_row_count() const;
    bool read_provider_cell(std::size_t provider_row, std::size_t column, BindingValue& value,
                            std::string& refusal) const;
    const CachedRow& cached_row(std::size_t row) const;
    void bind_provider(std::shared_ptr<BindingProvider> provider);
    void revoke_link() noexcept;
    void rebuild_view();
    void reset_view_state(bool metadata_changed);
    void receive_notification(const BindingNotification& notification);
    void apply_posted(const std::vector<BindingNotification>& notifications);
    void apply_notification(const BindingNotification& notification);
    // Moves the position to the anchor provider row's view position, or,
    // when it is gone, to old_position clamped; raises the position events
    // when the position or the current row's ID changed.
    void settle_position(std::size_t anchor_row, std::ptrdiff_t old_position, const std::string& old_current);
    // settle_position's two halves: the new position, without events; then
    // the position events when the position or the current row changed.
    void place_position(std::size_t anchor_row, std::ptrdiff_t old_position);
    void announce_position(std::ptrdiff_t old_position, const std::string& old_current);
    void bump_generation() noexcept;
    void publish_model_change(const BindingListChange& change);
    void publish_current_transition(std::ptrdiff_t old_position);
    void register_binding(const std::shared_ptr<Binding>& binding);
    void unregister_binding(const Binding* binding) noexcept;
    void compact_expired_bindings() noexcept;
    bool transfer_bindings(bool source_to_control);
    [[nodiscard]] Window* bound_window() const noexcept;

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    std::shared_ptr<BindingProvider> provider_;
    std::shared_ptr<detail::BindingLinkState> link_;
    BindingSchema schema_;
    BindingProviderCapabilities capabilities_{};
    // The provider's row count as last validated.
    std::size_t provider_count_{};
    std::vector<BindingSortKey> sort_;
    BindingFilter filter_;
    BindingViewMode view_mode_{BindingViewMode::provider_order};
    std::unique_ptr<detail::BindingView> view_;
    std::uint64_t generation_{1U};
    // The cache, the depth of provider calls and the read counter change on
    // reads, which are const: they do not change what the source shows.
    mutable std::vector<CachedRow> row_cache_;
    // Rows the cache holds: row_cache_rows, fewer for a wide schema.
    std::size_t cache_capacity_{row_cache_rows};
    mutable std::size_t provider_depth_{};
    mutable std::uint64_t provider_reads_{};
    // Raw column spellings already looked up, found without allocating.
    mutable std::map<std::string, std::size_t, std::less<>> column_lookup_;
    std::unique_ptr<CurrencyManager> currency_manager_;
    std::vector<BindingValue> edit_values_;
    bool editing_{};
    std::string edit_stable_id_;
    std::string data_member_;
    std::ptrdiff_t position_{-1};
    std::uint64_t revision_{1U};
    std::uint64_t resets_{};
    std::uint64_t field_changes_{};
    std::uint64_t position_changes_{};
    std::uint64_t suspended_mutations_{};
    bool allow_edit_{true};
    bool allow_new_{true};
    bool allow_remove_{true};
    bool suspended_{};
    bool pending_reset_{};
    bool raise_list_changed_events_{true};
    Event<const BindingListChange&> model_changed_;
    Event<const BindingListChange&> list_changed_;
    Event<> current_changed_;
    Event<> current_item_changed_;
    Event<std::ptrdiff_t> position_changed_;
    Event<const std::string&> data_error_;
    Event<> data_source_changed_;
    Event<const std::string&> data_member_changed_;
    Event<BindingCompleteEvent&> binding_complete_;
    Event<> disposed_event_;
    WeakBindingList bindings_;
};

} // namespace gui_forms
