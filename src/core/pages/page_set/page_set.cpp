#include "gui_forms/pages/page_set/page_set.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace gui_forms {

PageContent::~PageContent() = default;

// Marks a `selecting` emission. Edits refuse while it is held.
class PageSet::SelectingScope final {
public:
    explicit SelectingScope(PageSet& set) noexcept
        : set_(set), alive_(set.alive_) {
        set_.selecting_active_ = true;
    }
    ~SelectingScope() {
        if (*alive_) {
            set_.selecting_active_ = false;
        }
    }
    SelectingScope(const SelectingScope&) = delete;
    SelectingScope& operator=(const SelectingScope&) = delete;

private:
    PageSet& set_;
    const std::shared_ptr<bool> alive_;
};

// Marks the outermost publication. On exit the queue is cleared: a handler's
// exception drops the notifications not yet emitted rather than replaying
// them on the next edit.
class PageSet::PublicationScope final {
public:
    explicit PublicationScope(PageSet& set) noexcept
        : set_(set), alive_(set.alive_) {
        set_.publishing_ = true;
    }
    ~PublicationScope() {
        if (*alive_) {
            set_.notifications_.clear();
            set_.publishing_ = false;
        }
    }
    PublicationScope(const PublicationScope&) = delete;
    PublicationScope& operator=(const PublicationScope&) = delete;

private:
    PageSet& set_;
    const std::shared_ptr<bool> alive_;
};

PageSet::PageSet(PageSetOptions options)
    : options_(options), alive_(gui_forms::detail::make_library_shared<bool>(true)) {
    if (options_.capacity == 0U || options_.capacity > PageSetLimits::maximum_capacity) {
        throw std::invalid_argument(
            "PageSet capacity must be from 1 to PageSetLimits::maximum_capacity");
    }
}

PageSet::~PageSet() {
    *alive_ = false;
}

stxlib::Optional<PagePosition> PageSet::find(const PageId& id) const {
    const std::size_t count = entries_.size();
    for (std::size_t index = 0U; index < count; ++index) {
        if (entries_[index].record.id == id) {
            const stxlib::Optional<PagePosition> found = stxlib::Optional<PagePosition>::of(PagePosition{index});
            return found;
        }
    }
    return stxlib::Optional<PagePosition>::none();
}

const PageRecord& PageSet::record_at(PagePosition position) const {
    if (position.value >= entries_.size()) {
        throw std::out_of_range("PageSet::record_at position is past the last page");
    }
    const PageRecord& record = entries_[position.value].record;
    return record;
}

PageContent* PageSet::content(const PageId& id) const {
    const stxlib::Optional<PagePosition> position = find(id);
    if (position.absent()) {
        return nullptr;
    }
    PageContent* const borrowed = entries_[(*position.value()).value].content.get();
    return borrowed;
}

// Validation shared by insert and put, in a fixed order so a record with
// several faults always reports the same one.
PageEditStatus PageSet::validate_entry(const PageRecord& record,
                                       PagePlacement placement) const {
    if (record.id.value.empty()) {
        return PageEditStatus::empty_id;
    }
    if (!valid_stable_id(record.id.value)) {
        return PageEditStatus::invalid_id;
    }
    if (find(record.id).present()) {
        return PageEditStatus::duplicate_page;
    }
    if (entries_.size() >= options_.capacity) {
        return PageEditStatus::capacity_reached;
    }
    if (record.pinned && !options_.pinning_enabled) {
        return PageEditStatus::pinning_disabled;
    }
    if (placement.kind == PagePlacement::Kind::end_of_group) {
        return PageEditStatus::applied;
    }
    // After the insert the page sits at `position`, so any position up to
    // the current count is a real slot.
    const std::size_t position = placement.position.value;
    if (position > entries_.size()) {
        return PageEditStatus::position_out_of_range;
    }
    if (record.pinned && position > pinned_count_) {
        return PageEditStatus::crosses_pinned_boundary;
    }
    if (!record.pinned && position < pinned_count_) {
        return PageEditStatus::crosses_pinned_boundary;
    }
    return PageEditStatus::applied;
}

std::size_t PageSet::placement_position(const PageRecord& record,
                                        PagePlacement placement) const noexcept {
    if (placement.kind == PagePlacement::Kind::at) {
        return placement.position.value;
    }
    if (record.pinned) {
        return pinned_count_;
    }
    return entries_.size();
}

void PageSet::insert_entry(StoredPage entry, std::size_t position) {
    const bool pinned = entry.record.pinned;
    const std::vector<StoredPage>::iterator where =
        entries_.begin() + static_cast<std::ptrdiff_t>(position);
    static_cast<void>(entries_.insert(where, std::move(entry)));
    if (pinned) {
        ++pinned_count_;
    }
}

PageSet::StoredPage PageSet::extract_entry(std::size_t position) {
    StoredPage entry = std::move(entries_[position]);
    const std::vector<StoredPage>::iterator where =
        entries_.begin() + static_cast<std::ptrdiff_t>(position);
    static_cast<void>(entries_.erase(where));
    if (entry.record.pinned) {
        --pinned_count_;
    }
    erase_history(entry.record.id);
    return entry;
}

void PageSet::relocate(std::size_t from, std::size_t to) {
    const std::vector<StoredPage>::iterator first = entries_.begin();
    if (from < to) {
        // [from, to] rotates left by one: the page lands at `to`.
        static_cast<void>(std::rotate(first + static_cast<std::ptrdiff_t>(from),
                                      first + static_cast<std::ptrdiff_t>(from + 1U),
                                      first + static_cast<std::ptrdiff_t>(to + 1U)));
    } else if (from > to) {
        // [to, from] rotates right by one: the page lands at `to`.
        static_cast<void>(std::rotate(first + static_cast<std::ptrdiff_t>(to),
                                      first + static_cast<std::ptrdiff_t>(from),
                                      first + static_cast<std::ptrdiff_t>(from + 1U)));
    }
}

void PageSet::push_history(const PageId& id) {
    erase_history(id);
    static_cast<void>(history_.insert(history_.begin(), id));
}

void PageSet::erase_history(const PageId& id) {
    const std::vector<PageId>::iterator found =
        std::find(history_.begin(), history_.end(), id);
    if (found != history_.end()) {
        static_cast<void>(history_.erase(found));
    }
}

// Called after the selected page was extracted from `vacated`; the history no
// longer holds it, so its front is the most recent page still in the set.
std::optional<PageId> PageSet::fallback_selection(std::size_t vacated) const {
    if (entries_.empty()) {
        return std::nullopt;
    }
    if (options_.fallback == PageSelectionFallback::most_recent && !history_.empty()) {
        const std::optional<PageId> recent = history_.front();
        return recent;
    }
    if (vacated < entries_.size()) {
        const std::optional<PageId> next = entries_[vacated].record.id;
        return next;
    }
    const std::optional<PageId> previous = entries_.back().record.id;
    return previous;
}

void PageSet::change_selection(const std::optional<PageId>& selected,
                               PageSelectionCause cause) {
    if (!pending_.selection_changed) {
        pending_.selection_changed = true;
        pending_.selection_origin = selected_;
    }
    pending_.selection_cause = cause;
    selected_ = selected;
    if (selected.has_value()) {
        push_history(*selected);
    }
}

void PageSet::note_pages(bool membership, bool order, bool records, bool content) {
    PageSetChange& pages = pending_.pages;
    pages.membership = pages.membership || membership;
    pages.order = pages.order || order;
    pages.records = pages.records || records;
    pages.content = pages.content || content;
    ++pages.edit_count;
}

PageEditStatus PageSet::insert(const PageRecord& record, PagePlacement placement) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const PageEditStatus status = validate_entry(record, placement);
    if (status != PageEditStatus::applied) {
        return status;
    }
    const std::size_t position = placement_position(record, placement);
    StoredPage entry{record, nullptr};
    insert_entry(std::move(entry), position);
    if (!selected_.has_value()) {
        change_selection(record.id, PageSelectionCause::first_page);
    }
    ++revision_;
    note_pages(true, true, false, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::put(TakenPage& page, PagePlacement placement) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const PageEditStatus status = validate_entry(page.record, placement);
    if (status != PageEditStatus::applied) {
        return status;
    }
    const std::size_t position = placement_position(page.record, placement);
    const PageId id = page.record.id;
    StoredPage entry{std::move(page.record), std::move(page.content)};
    // Moved-from strings are unspecified; leave the caller's page empty.
    page.record = PageRecord{};
    page.content.reset();
    insert_entry(std::move(entry), position);
    if (!selected_.has_value()) {
        change_selection(id, PageSelectionCause::first_page);
    }
    ++revision_;
    note_pages(true, true, false, false);
    publish_pending();
    return PageEditStatus::applied;
}

// Validates and commits one removal without publishing: the entry moves to
// `out`, and when it was selected the fallback rule selects another.
PageEditStatus PageSet::extract_page(const PageId& id, StoredPage& out) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    const std::size_t position = (*found.value()).value;
    const bool was_selected = selected_ == id;
    out = extract_entry(position);
    if (was_selected) {
        const std::optional<PageId> fallback = fallback_selection(position);
        change_selection(fallback, PageSelectionCause::fallback);
    }
    ++revision_;
    note_pages(true, true, false, false);
    return PageEditStatus::applied;
}

PageEditStatus PageSet::remove(const PageId& id) {
    StoredPage entry{};
    const PageEditStatus status = extract_page(id, entry);
    if (status != PageEditStatus::applied) {
        return status;
    }
    // The set is consistent; the content's destructor runs before any
    // notification, inside a batch or not.
    entry.content.reset();
    publish_pending();
    return status;
}

PageTakeResult PageSet::take(const PageId& id) {
    PageTakeResult result{};
    StoredPage entry{};
    result.status = extract_page(id, entry);
    if (result.status != PageEditStatus::applied) {
        return result;
    }
    result.page.record = std::move(entry.record);
    result.page.content = std::move(entry.content);
    publish_pending();
    return result;
}

PageEditStatus PageSet::move(const PageId& id, PagePosition destination) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    if (destination.value >= entries_.size()) {
        return PageEditStatus::position_out_of_range;
    }
    const std::size_t position = (*found.value()).value;
    const bool pinned = entries_[position].record.pinned;
    if (pinned && destination.value >= pinned_count_) {
        return PageEditStatus::crosses_pinned_boundary;
    }
    if (!pinned && destination.value < pinned_count_) {
        return PageEditStatus::crosses_pinned_boundary;
    }
    if (destination.value == position) {
        return PageEditStatus::unchanged;
    }
    relocate(position, destination.value);
    ++revision_;
    note_pages(false, true, false, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::set_pinned(const PageId& id, bool pinned) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    if (!options_.pinning_enabled) {
        return PageEditStatus::pinning_disabled;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    const std::size_t position = (*found.value()).value;
    if (entries_[position].record.pinned == pinned) {
        return PageEditStatus::unchanged;
    }
    if (pinned) {
        // The first ordinary position becomes the last pinned one.
        relocate(position, pinned_count_);
        entries_[pinned_count_].record.pinned = true;
        ++pinned_count_;
    } else {
        // The last pinned position becomes the first ordinary one.
        const std::size_t first_ordinary = pinned_count_ - 1U;
        relocate(position, first_ordinary);
        entries_[first_ordinary].record.pinned = false;
        --pinned_count_;
    }
    ++revision_;
    note_pages(false, true, true, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::set_title(const PageId& id, std::string title) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    PageRecord& record = entries_[(*found.value()).value].record;
    if (record.title == title) {
        return PageEditStatus::unchanged;
    }
    record.title = std::move(title);
    ++revision_;
    note_pages(false, false, true, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::set_image_key(const PageId& id, std::string image_key) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    PageRecord& record = entries_[(*found.value()).value].record;
    if (record.image_key == image_key) {
        return PageEditStatus::unchanged;
    }
    record.image_key = std::move(image_key);
    ++revision_;
    note_pages(false, false, true, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::set_closable(const PageId& id, bool closable) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    PageRecord& record = entries_[(*found.value()).value].record;
    if (record.closable == closable) {
        return PageEditStatus::unchanged;
    }
    record.closable = closable;
    ++revision_;
    note_pages(false, false, true, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::set_modified(const PageId& id, bool modified) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    PageRecord& record = entries_[(*found.value()).value].record;
    if (record.modified == modified) {
        return PageEditStatus::unchanged;
    }
    record.modified = modified;
    ++revision_;
    note_pages(false, false, true, false);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::set_content(const PageId& id,
                                    std::unique_ptr<PageContent>& content) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    StoredPage& entry = entries_[(*found.value()).value];
    if (entry.content != nullptr) {
        return PageEditStatus::content_present;
    }
    if (content == nullptr) {
        return PageEditStatus::unchanged;
    }
    entry.content = std::move(content);
    ++revision_;
    note_pages(false, false, false, true);
    publish_pending();
    return PageEditStatus::applied;
}

PageEditStatus PageSet::select(const PageId& id, PageSelectionCause cause) {
    if (selecting_active_) {
        return PageEditStatus::selecting_in_progress;
    }
    const stxlib::Optional<PagePosition> found = find(id);
    if (found.absent()) {
        return PageEditStatus::unknown_page;
    }
    if (selected_ == id) {
        return PageEditStatus::unchanged;
    }
    PageSelectingEvent selecting{};
    selecting.old_page = selected_;
    selecting.new_page = id;
    selecting.cause = cause;
    const std::shared_ptr<bool> alive = alive_;
    {
        SelectingScope scope(*this);
        selecting_.emit(selecting);
    }
    if (!*alive || selecting.cancel) {
        return PageEditStatus::selection_cancelled;
    }
    // `selecting` handlers cannot edit the set, so `id` is still present.
    change_selection(id, cause);
    ++revision_;
    publish_pending();
    return PageEditStatus::applied;
}

void PageSet::begin_batch() noexcept {
    ++batch_depth_;
}

void PageSet::end_batch() {
    if (batch_depth_ == 0U) {
        return;
    }
    --batch_depth_;
    publish_pending();
}

// Turns the pending change into queued notifications (pages first, then the
// selection) and drains the queue, unless a batch is still open.
void PageSet::publish_pending() {
    if (batch_depth_ != 0U) {
        return;
    }
    const bool pages_changed = pending_.pages.edit_count != 0U;
    const bool selection_moved = pending_.selection_changed &&
                                 pending_.selection_origin != selected_;
    if (pages_changed) {
        Notification notification{};
        notification.kind = Notification::Kind::pages;
        notification.pages = pending_.pages;
        notification.pages.revision = revision_;
        notifications_.push_back(notification);
    }
    if (selection_moved) {
        Notification notification{};
        notification.kind = Notification::Kind::selection;
        notification.selection.old_page = pending_.selection_origin;
        notification.selection.new_page = selected_;
        notification.selection.cause = pending_.selection_cause;
        notifications_.push_back(std::move(notification));
    }
    pending_ = PendingChange{};
    drain_notifications();
}

// Emits queued notifications in order. A handler's edit appends to the queue
// and returns; the outermost drain emits it after the current notification.
// Each notification is copied out first because a handler's edit can
// reallocate the queue.
void PageSet::drain_notifications() {
    if (publishing_) {
        return;
    }
    const std::shared_ptr<bool> alive = alive_;
    PublicationScope scope(*this);
    for (std::size_t index = 0U; index < notifications_.size(); ++index) {
        const Notification notification = notifications_[index];
        if (notification.kind == Notification::Kind::pages) {
            pages_changed_.emit(notification.pages);
        } else {
            selected_page_changed_.emit(notification.selection);
        }
        if (!*alive) {
            return;
        }
    }
}

PageSetBatch::PageSetBatch(PageSet& set) noexcept : set_(&set) {
    (*set_).begin_batch();
}

// Publishing can run a handler that throws; a destructor may not let that
// out (see the class comment for what is left behind).
PageSetBatch::~PageSetBatch() {
    try {
        close();
    } catch (...) {
    }
}

void PageSetBatch::close() {
    if (set_ == nullptr) {
        return;
    }
    PageSet* const closing = std::exchange(set_, nullptr);
    (*closing).end_batch();
}

} // namespace gui_forms
