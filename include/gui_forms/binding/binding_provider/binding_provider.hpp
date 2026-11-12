#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/binding_schema/binding_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace gui_forms {

class BindingSource;
namespace detail {
struct BindingLinkState;
}

// What a provider can do beyond reading and writing cells. A provider that
// sorts or filters is handed the source's sort keys or filter and reorders
// or subsets its own rows (IBindingList.SupportsSorting,
// IBindingListView.SupportsFiltering); one that does not is given an index
// the source builds itself. inserts and removes admit the source's add,
// insert and remove calls.
struct GUI_FORMS_API BindingProviderCapabilities final {
    bool sorts{};
    bool filters{};
    bool inserts{};
    bool removes{};
};

enum class BindingWriteStatus : std::uint8_t {
    accepted,
    // The provider declined; reason says why.
    refused,
    read_only,
    kind_mismatch,
    out_of_range,
    // The source has no live provider row there (revoked, stale or failed).
    unavailable,
};

struct GUI_FORMS_API BindingWriteResult final {
    BindingWriteStatus status{BindingWriteStatus::refused};
    std::string reason;

    [[nodiscard]] bool accepted() const noexcept { return status == BindingWriteStatus::accepted; }
};

// A change to a provider's rows, in the provider's own row order.
//
// - reset: anything may have changed, the schema included.
// - inserted: rows [row, row + count) are new.
// - removed: rows [row, row + count) of the order before are gone.
// - moved: rows [row, row + count) moved; target is the index of the first
//   of them in the order after the move.
// - changed: the cell (row, target) changed, or every cell of the row when
//   target is binding_whole_row.
enum class BindingNotificationKind : std::uint8_t {
    reset,
    inserted,
    removed,
    moved,
    changed,
};

struct GUI_FORMS_API BindingNotification final {
    BindingNotificationKind kind{BindingNotificationKind::reset};
    std::size_t row{};
    std::size_t count{};
    std::size_t target{};
};

// A provider's handle to the source it serves: how it reports changes. The
// source hands one to BindingProvider::attach and revokes it when it lets
// the provider go (another provider, or the source's disposal); a revoked
// link drops every notification. The provider may keep and copy it freely.
//
// Threads: on the source's UI thread a notification is applied at once,
// unless the source is in a call to the provider (a provider that reports a
// change from inside read_cell), when it waits until that call returns. On
// any other thread it is queued and posted to the UI thread, which applies
// the queue at a safe point, never during a read, a layout or a paint. The
// queue is bounded; past binding_maximum_posted_notifications it collapses
// to one reset.
class GUI_FORMS_API BindingProviderLink final {
public:
    static constexpr std::size_t binding_maximum_posted_notifications = 4096U;

    // An empty link, revoked from the start.
    BindingProviderLink() = default;

    void reset() const;
    void inserted(std::size_t row, std::size_t count) const;
    void removed(std::size_t row, std::size_t count) const;
    void moved(std::size_t row, std::size_t count, std::size_t target) const;
    void changed(std::size_t row, std::size_t column = binding_whole_row) const;
    void notify(const BindingNotification& notification) const;
    // Whether the source has let the provider go (or the link is empty).
    [[nodiscard]] bool revoked() const noexcept;

private:
    friend class BindingSource;
    explicit BindingProviderLink(std::shared_ptr<detail::BindingLinkState> state) noexcept;

    std::shared_ptr<detail::BindingLinkState> state_;
};

// A source of rows for a BindingSource. The source calls every function on
// its UI thread, one call at a time; nothing it receives is kept past the
// call, and nothing the provider returns is trusted: an index past the
// rows, a value whose kind the column does not take, text past the bounds
// and an exception all become a cell or row error, never a failure of the
// source. A provider reports its changes through the link attach gives it.
//
// The source owns its provider through a shared pointer for as long as it
// uses it, and calls detach exactly once when it lets it go.
class GUI_FORMS_API BindingProvider {
public:
    virtual ~BindingProvider();
    BindingProvider(const BindingProvider&) = delete;
    BindingProvider& operator=(const BindingProvider&) = delete;

    // The columns, read when the source takes the provider and again on
    // every reset.
    [[nodiscard]] virtual BindingSchema schema() const = 0;
    [[nodiscard]] virtual BindingProviderCapabilities capabilities() const;
    [[nodiscard]] virtual std::size_t row_count() = 0;
    // Fills value with the cell; false when there is no answer.
    virtual bool read_cell(std::size_t row, std::size_t column, BindingValue& value) = 0;
    // Fills stable_id with the row's ID; false when there is none.
    virtual bool read_stable_id(std::size_t row, std::string& stable_id) = 0;
    // Writes the cell straight to the provider's data. The source has
    // checked the kind and the read-only flags; the provider may still
    // refuse. An accepted write needs no notification (the source applies
    // it itself); one more is harmless.
    virtual BindingWriteResult write_cell(std::size_t row, std::size_t column, const BindingValue& value) = 0;
    // Optional: a cell made read-only for one row.
    [[nodiscard]] virtual bool cell_read_only(std::size_t row, std::size_t column);
    // Optional: an error text for the cell, or for the row when column is
    // binding_whole_row (IDataErrorInfo). False for none.
    virtual bool read_error(std::size_t row, std::size_t column, std::string& text);
    // Optional (capabilities().inserts, .removes): the source's add, insert
    // and remove calls. Like write_cell they need no notification.
    virtual BindingWriteResult insert_row(std::size_t row, const BindingRow& values);
    virtual BindingWriteResult remove_rows(std::size_t row, std::size_t count);
    // Optional (capabilities().sorts, .filters): reorders or subsets the
    // provider's rows and reports a reset. False declines, and the source
    // builds its own index. An empty list or filter restores the natural
    // order or every row.
    virtual bool apply_sort(std::span<const BindingSortKey> keys);
    virtual bool apply_filter(const BindingFilter& filter);
    // The source took the provider; link reports changes from now on.
    virtual void attach(const BindingProviderLink& link);
    // The source let the provider go; the link is already revoked.
    virtual void detach() noexcept;

protected:
    BindingProvider() = default;
};

} // namespace gui_forms
