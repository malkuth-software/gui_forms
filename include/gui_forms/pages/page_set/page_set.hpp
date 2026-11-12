#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/pages/types/page_set_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

class PageSetBatch;

// The page collection behind a page strip, a page view and a docking stack
// (decisions 13, 16, 37): records keyed by stable `PageId`, an order with an
// opt-in pinned group first, one selected page, an activation history, and
// the content of each page. It is renderer-free: no control, painter or host.
//
// Invariants, after every edit:
// - ids are unique and non-empty; the count never exceeds the capacity;
// - pinned pages occupy positions [0, pinned_count) and no edit moves a page
//   across that boundary; pinning and unpinning are the only ways across;
// - a non-empty set has exactly one selected page, an empty set none;
// - the activation history holds each page at most once, only pages in the
//   set, most recent first, with the selected page at its front.
//
// Edits validate before mutating: a refusal returns its reason and leaves the
// set, its selection, its history and the caller's arguments unchanged.
//
// Event order for one edit outside a batch: `selecting` (only for `select`,
// before anything changes), then the edit commits, then the content of a
// removed page is destroyed, then `pages_changed`, then
// `selected_page_changed`. Inside a batch only `selecting` is raised as edits
// happen; closing the outermost batch publishes at most one `pages_changed`
// and then at most one `selected_page_changed`.
//
// Reentrancy: a `selecting` handler may not edit the set (refused with
// `selecting_in_progress`). Handlers of the other two events may edit it;
// their notifications are queued behind the ones being published, so every
// handler sees notifications in the order the edits committed. A handler may
// not destroy the set; publication stops if it does.
//
// Single-threaded: the set and its events belong to one UI thread.
//
// Overflow (decision 37, P13): a page chosen from a strip's overflow list
// takes the last visible position. The visible count is the strip's layout
// state, so the strip computes that position and calls `move` (in a batch with
// `select`); `move` holds the pinned boundary for that path as for any other.
class GUI_FORMS_API PageSet final {
public:
    // Throws std::invalid_argument when `options.capacity` is 0 or above
    // `PageSetLimits::maximum_capacity`.
    explicit PageSet(PageSetOptions options = {});
    ~PageSet();
    PageSet(const PageSet&) = delete;
    PageSet& operator=(const PageSet&) = delete;
    PageSet(PageSet&&) = delete;
    PageSet& operator=(PageSet&&) = delete;

    [[nodiscard]] const PageSetOptions& options() const noexcept { return options_; }
    [[nodiscard]] std::size_t count() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t pinned_count() const noexcept { return pinned_count_; }
    // Increases by one for every applied edit, including selections.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const std::optional<PageId>& selected_page() const noexcept {
        return selected_;
    }
    // Most recent first. The span is valid until the next edit.
    [[nodiscard]] std::span<const PageId> activation_history() const& noexcept {
        const std::span<const PageId> history(history_.data(), history_.size());
        return history;
    }
    std::span<const PageId> activation_history() const&& = delete;
    [[nodiscard]] bool batch_open() const noexcept {
        const bool open = batch_depth_ != 0U;
        return open;
    }

    // The position of `id`, or none when it is not in the set. Linear in the
    // count.
    [[nodiscard]] std::optional<PagePosition> find(const PageId& id) const;
    // Throws std::out_of_range when `position` is not below `count()`. The
    // reference is valid until the next edit.
    [[nodiscard]] const PageRecord& record_at(PagePosition position) const;
    // The content of `id`, borrowed: null when the page has none or is not in
    // the set. Valid until the page leaves the set.
    [[nodiscard]] PageContent* content(const PageId& id) const;

    // Adds a page without content at `placement`. Refusals: `empty_id`,
    // `invalid_id`, `duplicate_page`, `capacity_reached`, `pinning_disabled`,
    // `position_out_of_range`, `crosses_pinned_boundary`,
    // `selecting_in_progress`. The first page of an empty set is selected
    // (cause `first_page`).
    [[nodiscard]] PageEditStatus insert(const PageRecord& record,
                                        PagePlacement placement);
    // Adds a taken page with its content, as `insert`. On success `page` is
    // left empty; on a refusal it is unchanged and still the caller's.
    [[nodiscard]] PageEditStatus put(TakenPage& page, PagePlacement placement);
    // Removes a page and destroys its content. When it was selected, the
    // fallback rule selects another in the same edit. Refusals:
    // `unknown_page`, `selecting_in_progress`.
    [[nodiscard]] PageEditStatus remove(const PageId& id);
    // Removes a page and returns it with its content, for `put` into another
    // set; selection falls back as for `remove`.
    [[nodiscard]] PageTakeResult take(const PageId& id);
    // Moves a page to `destination`, its position after the move, which must
    // lie inside its own group. Refusals: `unknown_page`,
    // `position_out_of_range`, `crosses_pinned_boundary`,
    // `selecting_in_progress`; `unchanged` when it is already there.
    [[nodiscard]] PageEditStatus move(const PageId& id, PagePosition destination);

    // Pinning moves the page to the end of the pinned group; unpinning to the
    // start of the ordinary pages. Refused with `pinning_disabled` unless the
    // set enables pinning.
    [[nodiscard]] PageEditStatus set_pinned(const PageId& id, bool pinned);
    [[nodiscard]] PageEditStatus set_title(const PageId& id, std::string title);
    [[nodiscard]] PageEditStatus set_image_key(const PageId& id, std::string image_key);
    [[nodiscard]] PageEditStatus set_closable(const PageId& id, bool closable);
    // Sets the display-only `modified` flag (PageRecord::modified).
    [[nodiscard]] PageEditStatus set_modified(const PageId& id, bool modified);
    // Attaches late content to a page that has none. On success `content` is
    // left null; on a refusal (`unknown_page`, `content_present`,
    // `selecting_in_progress`) it is unchanged and still the caller's. A null
    // `content` is `unchanged`.
    [[nodiscard]] PageEditStatus set_content(const PageId& id,
                                             std::unique_ptr<PageContent>& content);

    // Raises `selecting`, then selects `id` unless a handler cancelled.
    // Refusals: `unknown_page`, `selection_cancelled`, `selecting_in_progress`;
    // `unchanged` (no events) when `id` is already selected.
    [[nodiscard]] PageEditStatus select(const PageId& id, PageSelectionCause cause);

    [[nodiscard]] Event<PageSelectingEvent&>& selecting() noexcept { return selecting_; }
    [[nodiscard]] Event<const PageSelectionChange&>& selected_page_changed() noexcept {
        return selected_page_changed_;
    }
    [[nodiscard]] Event<const PageSetChange&>& pages_changed() noexcept {
        return pages_changed_;
    }

private:
    friend class PageSetBatch;

    struct StoredPage final {
        PageRecord record{};
        std::unique_ptr<PageContent> content{};
    };

    // One queued notification: exactly one of the two payloads is meaningful,
    // chosen by `kind`.
    struct Notification final {
        enum class Kind : std::uint8_t {
            pages,
            selection,
        };
        Kind kind{Kind::pages};
        PageSetChange pages{};
        PageSelectionChange selection{};
    };

    // Changes accumulated since the last publication.
    struct PendingChange final {
        PageSetChange pages{};
        bool selection_changed{};
        std::optional<PageId> selection_origin{};
        PageSelectionCause selection_cause{PageSelectionCause::programmatic};
    };

    // Hold `selecting_active_` and `publishing_` for one emission and reset
    // them on every exit, including a handler's exception, unless a handler
    // destroyed the set.
    class SelectingScope;
    class PublicationScope;

    [[nodiscard]] PageEditStatus validate_entry(const PageRecord& record,
                                                PagePlacement placement) const;
    [[nodiscard]] std::size_t placement_position(const PageRecord& record,
                                                 PagePlacement placement) const noexcept;
    void insert_entry(StoredPage entry, std::size_t position);
    [[nodiscard]] StoredPage extract_entry(std::size_t position);
    [[nodiscard]] PageEditStatus extract_page(const PageId& id, StoredPage& out);
    // Moves the entry at `from` to `to`, shifting the ones between by one.
    void relocate(std::size_t from, std::size_t to);
    void change_selection(const std::optional<PageId>& selected,
                          PageSelectionCause cause);
    void push_history(const PageId& id);
    void erase_history(const PageId& id);
    [[nodiscard]] std::optional<PageId> fallback_selection(std::size_t vacated) const;
    void note_pages(bool membership, bool order, bool records, bool content);
    void begin_batch() noexcept;
    void end_batch();
    void publish_pending();
    void drain_notifications();

    PageSetOptions options_{};
    // Live pages in order: the pinned group, then the ordinary pages.
    std::vector<StoredPage> entries_{};
    std::size_t pinned_count_{};
    std::optional<PageId> selected_{};
    // Most recent first.
    std::vector<PageId> history_{};
    std::uint64_t revision_{};
    std::size_t batch_depth_{};
    bool selecting_active_{};
    PendingChange pending_{};
    // FIFO of notifications awaiting emission; drained by the outermost
    // publication, its capacity kept between publications.
    std::vector<Notification> notifications_{};
    bool publishing_{};
    // Cleared by the destructor; publication holds a copy and stops when a
    // handler destroyed the set.
    std::shared_ptr<bool> alive_{};
    Event<PageSelectingEvent&> selecting_{};
    Event<const PageSelectionChange&> selected_page_changed_{};
    Event<const PageSetChange&> pages_changed_{};
};

// Groups edits into one publication. Constructing it opens a batch on the
// set; `close` or destruction closes it. Closing the outermost batch publishes
// one `pages_changed` when any edit applied and one `selected_page_changed`
// when the selection ends on a different page than it began. Edits inside a
// batch still apply one at a time: a refused edit leaves the earlier ones in
// place. The set must outlive the scope.
//
// A handler that throws while `close` publishes hands its exception to
// close's caller. While the destructor publishes, the exception is contained
// there (a destructor publishes without throwing, house style section 2) and
// is lost; there is no error channel to report it on, so call `close` where a
// handler may throw. Either way the batch's edits stand, the batch is closed,
// the set stays consistent and usable, and the notifications of that
// publication not yet emitted are dropped, as for any handler exception.
class GUI_FORMS_API PageSetBatch final {
public:
    explicit PageSetBatch(PageSet& set) noexcept;
    ~PageSetBatch();
    PageSetBatch(const PageSetBatch&) = delete;
    PageSetBatch& operator=(const PageSetBatch&) = delete;
    PageSetBatch(PageSetBatch&&) = delete;
    PageSetBatch& operator=(PageSetBatch&&) = delete;

    void close();

private:
    PageSet* set_{};
};

} // namespace gui_forms
