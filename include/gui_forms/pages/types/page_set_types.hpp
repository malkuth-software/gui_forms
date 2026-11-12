#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gui_forms {

// A page's stable identity: the application's string, unique within one set
// (docking uses the panel's identity). It is never a position; positions are
// `PagePosition` values in the current order and change on every reorder.
struct GUI_FORMS_API PageId final {
    std::string value{};

    friend bool operator==(const PageId& left, const PageId& right) noexcept {
        const bool equal = left.value == right.value;
        return equal;
    }
    friend bool operator!=(const PageId& left, const PageId& right) noexcept {
        const bool different = left.value != right.value;
        return different;
    }
};

// A position in a set's current order, counted from 0 at the first pinned
// page (or the first page when nothing is pinned).
struct GUI_FORMS_API PagePosition final {
    std::size_t value{};

    friend bool operator==(const PagePosition& left,
                           const PagePosition& right) noexcept {
        const bool equal = left.value == right.value;
        return equal;
    }
    friend bool operator!=(const PagePosition& left,
                           const PagePosition& right) noexcept {
        const bool different = left.value != right.value;
        return different;
    }
};

// Where an inserted or put page goes. `end_of_group` appends it to its own
// group (the pinned group for a pinned page, the ordinary pages otherwise);
// `at` names the position it occupies after the edit, which must lie inside
// its group.
struct GUI_FORMS_API PagePlacement final {
    enum class Kind : std::uint8_t {
        end_of_group,
        at,
    };

    Kind kind{Kind::end_of_group};
    PagePosition position{};

    [[nodiscard]] static PagePlacement end_of_group() noexcept {
        const PagePlacement placement{Kind::end_of_group, PagePosition{}};
        return placement;
    }
    [[nodiscard]] static PagePlacement at(PagePosition position) noexcept {
        const PagePlacement placement{Kind::at, position};
        return placement;
    }
};

// The descriptive fields of one page. The presenter decides how they look:
// `title` may carry a mnemonic marker, `image_key` names an image in the
// presenter's image list (empty: none).
struct GUI_FORMS_API PageRecord final {
    PageId id{};
    std::string title{};
    std::string image_key{};
    bool closable{false};
    // Kept in the pinned group at the front of the order. Refused unless the
    // set was constructed with pinning enabled.
    bool pinned{false};
    // The page's content has unsaved changes (a document). Display only: the
    // presenter shows it (the custom state `modified`, owner decision DK-35)
    // and the set attaches no meaning to it.
    bool modified{false};
};

// The content a presenter attaches to a page; for `PageView`, the page's
// control subtree. The set owns the content of every page in it: `take`
// moves the content out with its record, `put` moves it in, and removing a
// page or destroying the set destroys it. A page may have no content until
// its presenter supplies it (late content). The derived type is the
// presenter's; the set only destroys it.
class GUI_FORMS_API PageContent {
public:
    virtual ~PageContent();
    PageContent(const PageContent&) = delete;
    PageContent& operator=(const PageContent&) = delete;
    PageContent(PageContent&&) = delete;
    PageContent& operator=(PageContent&&) = delete;

protected:
    PageContent() = default;
};

// A page out of any set: its record and its content, moved together. A
// default-constructed or moved-from `TakenPage` is empty (an empty id and no
// content).
struct GUI_FORMS_API TakenPage final {
    PageRecord record{};
    std::unique_ptr<PageContent> content{};
};

// Which page is selected after the selected page leaves the set.
enum class PageSelectionFallback : std::uint8_t {
    // The page that took its position (the next), else the one before it:
    // the strip policy.
    neighbour,
    // The most recently activated page still in the set: the document
    // policy. A set with no such page falls back to `neighbour`.
    most_recent,
};

// Why the selection changed. The presenter supplies pointer, keyboard and
// semantic causes; the set itself uses `first_page` and `fallback`.
enum class PageSelectionCause : std::uint8_t {
    programmatic,
    pointer,
    keyboard,
    semantic,
    // The first page entered an empty set and was selected.
    first_page,
    // The selected page left the set and the fallback rule chose another.
    fallback,
};

// The outcome of one edit. `applied` and `unchanged` succeed (`unchanged`
// publishes nothing); every other value is a refusal that left the set
// exactly as it was.
enum class PageEditStatus : std::uint8_t {
    applied,
    unchanged,
    empty_id,
    // The id is not '.'-separated segments of lowercase ASCII letters and
    // digits joined by single '-': controls name their parts after the page
    // with it, so it follows the `StableId` segment rule.
    invalid_id,
    unknown_page,
    duplicate_page,
    capacity_reached,
    position_out_of_range,
    crosses_pinned_boundary,
    pinning_disabled,
    content_present,
    selection_cancelled,
    // The edit was attempted from a `selecting` handler, which may not edit
    // the set it is asked about.
    selecting_in_progress,
};

[[nodiscard]] constexpr bool page_edit_succeeded(PageEditStatus status) noexcept {
    const bool succeeded = status == PageEditStatus::applied ||
                           status == PageEditStatus::unchanged;
    return succeeded;
}

// The result of `PageSet::take`: on success the page with its content; on a
// refusal an empty page and the reason.
struct GUI_FORMS_API PageTakeResult final {
    PageEditStatus status{PageEditStatus::unknown_page};
    TakenPage page{};
};

struct GUI_FORMS_API PageSetLimits final {
    static constexpr std::size_t default_capacity = 256U;
    static constexpr std::size_t maximum_capacity = 4096U;
};

struct GUI_FORMS_API PageSetOptions final {
    PageSelectionFallback fallback{PageSelectionFallback::neighbour};
    // Pinning is opt-in per set (decision 31).
    bool pinning_enabled{false};
    // The most pages the set holds, from 1 to `PageSetLimits::maximum_capacity`;
    // an insert or put beyond it is refused with `capacity_reached`.
    std::size_t capacity{PageSetLimits::default_capacity};
};

// Raised before a requested selection changes. Setting `cancel` keeps the
// current selection; later handlers still run and see the flag.
struct GUI_FORMS_API PageSelectingEvent final {
    std::optional<PageId> old_page{};
    PageId new_page{};
    PageSelectionCause cause{PageSelectionCause::programmatic};
    bool cancel{};
};

// Raised after the selected page changed. Inside a batch, `old_page` is the
// selection when the batch began and `cause` the cause of its last change.
struct GUI_FORMS_API PageSelectionChange final {
    std::optional<PageId> old_page{};
    std::optional<PageId> new_page{};
    PageSelectionCause cause{PageSelectionCause::programmatic};
};

// Raised after pages entered, left, moved or changed. Each flag says whether
// any edit since the last publication changed that aspect.
struct GUI_FORMS_API PageSetChange final {
    // Pages were inserted, removed, taken or put.
    bool membership{};
    // Positions changed (a move, a pin or unpin, or a membership change).
    bool order{};
    // A title, image key, closable, pinned or modified flag changed.
    bool records{};
    // Content was attached to a page.
    bool content{};
    // Applied page edits since the last publication (selections are not
    // counted): 1 outside a batch.
    std::size_t edit_count{};
    // The set's revision when the change was published.
    std::uint64_t revision{};
};

} // namespace gui_forms
