// PageSet (stage DK, phase P1): the renderer-free page model. Invariants after
// random edit sequences, refusals that leave the set unchanged, one change per
// batch, cancellable selection, fallback selection under both policies, the
// activation history, the pinned boundary on every move path, take and put
// with content, event order and reentrancy. Links the core only: the model
// has no control, painter or host dependency.

#include "gui_forms/component.hpp"
#include "gui_forms/pages.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

PageId page_id(const char* text) {
    const PageId id{text};
    return id;
}

PageRecord page(const char* id, bool pinned = false) {
    PageRecord record{};
    record.id = page_id(id);
    record.title = std::string("Title ") + id;
    record.pinned = pinned;
    return record;
}

// Counts the content objects that report to it. Each CountedContent keeps a
// reference to its count, so the count must outlive every one of them,
// including those a PageSet still owns when the set is destroyed: declare
// the count before the set. A count destroyed while content is live fails
// the test, which turns a write through a dangling reference into a
// deterministic failure in every build, not only under AddressSanitizer.
struct ContentCount final {
    int destroyed{};
    int live{};

    ContentCount() {}
    ContentCount(const ContentCount&) = delete;
    ContentCount& operator=(const ContentCount&) = delete;
    ~ContentCount() { require(live == 0, "a content count must outlive the content that reports to it"); }
};

class CountedContent final : public PageContent {
public:
    explicit CountedContent(ContentCount& count) : count_(count) { ++count_.live; }
    ~CountedContent() override {
        --count_.live;
        ++count_.destroyed;
    }

private:
    ContentCount& count_;
};

// Everything observable about a set, for "unchanged" comparisons.
struct SetSnapshot final {
    std::vector<std::string> ids{};
    std::vector<std::string> titles{};
    std::vector<std::string> image_keys{};
    std::vector<bool> pinned{};
    std::vector<bool> closable{};
    std::vector<const PageContent*> contents{};
    std::optional<std::string> selected{};
    std::vector<std::string> history{};
    std::size_t pinned_count{};
    std::uint64_t revision{};

    friend bool operator==(const SetSnapshot& left, const SetSnapshot& right) {
        const bool equal = left.ids == right.ids && left.titles == right.titles &&
                           left.image_keys == right.image_keys &&
                           left.pinned == right.pinned && left.closable == right.closable &&
                           left.contents == right.contents &&
                           left.selected == right.selected && left.history == right.history &&
                           left.pinned_count == right.pinned_count &&
                           left.revision == right.revision;
        return equal;
    }
};

SetSnapshot snapshot(const PageSet& set) {
    SetSnapshot result{};
    for (std::size_t index = 0U; index < set.count(); ++index) {
        const PageRecord& record = set.record_at(PagePosition{index});
        result.ids.push_back(record.id.value);
        result.titles.push_back(record.title);
        result.image_keys.push_back(record.image_key);
        result.pinned.push_back(record.pinned);
        result.closable.push_back(record.closable);
        result.contents.push_back(set.content(record.id));
    }
    if (set.selected_page().has_value()) {
        result.selected = (*set.selected_page()).value;
    }
    for (const PageId& id : set.activation_history()) {
        result.history.push_back(id.value);
    }
    result.pinned_count = set.pinned_count();
    result.revision = set.revision();
    return result;
}

std::string order_of(const PageSet& set) {
    std::string order;
    for (std::size_t index = 0U; index < set.count(); ++index) {
        if (!order.empty()) order += ' ';
        order += set.record_at(PagePosition{index}).id.value;
    }
    return order;
}

std::string history_of(const PageSet& set) {
    std::string history;
    for (const PageId& id : set.activation_history()) {
        if (!history.empty()) history += ' ';
        history += id.value;
    }
    return history;
}

std::string selected_of(const PageSet& set) {
    if (!set.selected_page().has_value()) return "-";
    const std::string selected = (*set.selected_page()).value;
    return selected;
}

// Checks every invariant the header states.
void require_invariants(const PageSet& set) {
    require(set.count() <= set.options().capacity, "the count never exceeds the capacity");
    std::size_t pinned_prefix = 0U;
    bool ordinary_seen = false;
    for (std::size_t index = 0U; index < set.count(); ++index) {
        const PageRecord& record = set.record_at(PagePosition{index});
        require(!record.id.value.empty(), "every id is non-empty");
        for (std::size_t other = index + 1U; other < set.count(); ++other) {
            require(set.record_at(PagePosition{other}).id != record.id, "ids are unique");
        }
        const stxlib::Optional<PagePosition> found = set.find(record.id);
        require(found.present() && (*found.value()).value == index, "find returns the position");
        if (record.pinned) {
            require(!ordinary_seen, "pinned pages come before ordinary pages");
            require(set.options().pinning_enabled, "only a pinning set holds pinned pages");
            ++pinned_prefix;
        } else {
            ordinary_seen = true;
        }
    }
    require(pinned_prefix == set.pinned_count(), "pinned_count counts the pinned group");
    if (set.count() == 0U) {
        require(!set.selected_page().has_value(), "an empty set selects nothing");
        require(set.activation_history().empty(), "an empty set has no history");
        return;
    }
    require(set.selected_page().has_value(), "a non-empty set has a selected page");
    require(set.find(*set.selected_page()).present(), "the selected page is in the set");
    const std::span<const PageId> history = set.activation_history();
    require(!history.empty() && history.front() == *set.selected_page(),
            "the selected page leads the history");
    require(history.size() <= set.count(), "the history holds each page at most once");
    for (std::size_t index = 0U; index < history.size(); ++index) {
        require(set.find(history[index]).present(), "the history holds only pages in the set");
        for (std::size_t other = index + 1U; other < history.size(); ++other) {
            require(history[other] != history[index], "the history has no duplicates");
        }
    }
}

// Records notifications as text, in order.
class Recorder final : public Component {
public:
    void on_selecting(PageSelectingEvent& event) {
        trace.push_back("selecting " + optional_text(event.old_page) + ">" + event.new_page.value);
        if (cancel_next) {
            event.cancel = true;
        }
        saw_cancel = event.cancel;
        if (set != nullptr) {
            edit_from_selecting = (*set).insert(page("intruder"), PagePlacement::end_of_group());
            select_from_selecting = (*set).select(event.new_page, PageSelectionCause::programmatic);
        }
    }
    void on_selection(const PageSelectionChange& change) {
        trace.push_back("selected " + optional_text(change.old_page) + ">" +
                        optional_text(change.new_page));
        last_selection = change;
    }
    void on_pages(const PageSetChange& change) {
        trace.push_back("pages " + std::to_string(change.edit_count));
        last_pages = change;
        ++pages_events;
    }

    static std::string optional_text(const std::optional<PageId>& id) {
        if (!id.has_value()) return "-";
        const std::string text = (*id).value;
        return text;
    }

    void connect(PageSet& target) {
        on(target.selecting(), *this, &Recorder::on_selecting);
        on(target.selected_page_changed(), *this, &Recorder::on_selection);
        on(target.pages_changed(), *this, &Recorder::on_pages);
    }

    std::vector<std::string> trace{};
    PageSelectionChange last_selection{};
    PageSetChange last_pages{};
    int pages_events{};
    bool cancel_next{};
    bool saw_cancel{};
    PageSet* set{};
    PageEditStatus edit_from_selecting{PageEditStatus::applied};
    PageEditStatus select_from_selecting{PageEditStatus::applied};
};

std::string joined(const std::vector<std::string>& trace) {
    std::string text;
    for (const std::string& line : trace) {
        if (!text.empty()) text += " | ";
        text += line;
    }
    return text;
}

void basics() {
    PageSet set;
    Recorder recorder;
    recorder.connect(set);
    require(set.count() == 0U && !set.selected_page().has_value(), "a new set is empty");
    require(set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "a page inserts");
    require(selected_of(set) == "a", "the first page of an empty set is selected");
    require(joined(recorder.trace) == "pages 1 | selected ->a",
            "the first insert publishes the change, then the first-page selection");
    require(recorder.last_selection.cause == PageSelectionCause::first_page,
            "the first selection's cause is first_page");
    require(recorder.last_pages.membership && recorder.last_pages.order &&
                !recorder.last_pages.records && !recorder.last_pages.content,
            "an insert changes membership and order");
    require(set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("c"), PagePlacement::at(PagePosition{0U})) == PageEditStatus::applied,
            "more pages insert at the end and at a position");
    require(order_of(set) == "c a b", "positions follow the placements");
    require(selected_of(set) == "a", "later inserts keep the selection");
    require(set.set_title(page_id("b"), "Bee") == PageEditStatus::applied &&
                set.record_at(PagePosition{2U}).title == "Bee",
            "set_title changes the record");
    require(recorder.last_pages.records && !recorder.last_pages.membership,
            "a title change is a record change");
    require(set.set_title(page_id("b"), "Bee") == PageEditStatus::unchanged,
            "the same title is unchanged");
    require(set.set_image_key(page_id("b"), "doc") == PageEditStatus::applied &&
                set.record_at(PagePosition{2U}).image_key == "doc",
            "set_image_key changes the record");
    require(set.set_closable(page_id("b"), true) == PageEditStatus::applied &&
                set.record_at(PagePosition{2U}).closable,
            "set_closable changes the record");
    bool threw = false;
    try {
        static_cast<void>(set.record_at(PagePosition{3U}));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "record_at past the end throws");
    threw = false;
    try {
        PageSetOptions options{};
        options.capacity = 0U;
        PageSet invalid(options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a zero capacity is a programmer error");
    threw = false;
    try {
        PageSetOptions options{};
        options.capacity = PageSetLimits::maximum_capacity + 1U;
        PageSet invalid(options);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a capacity above the maximum is a programmer error");
    require_invariants(set);
}

void refusals_leave_the_set_unchanged() {
    PageSetOptions options{};
    options.pinning_enabled = true;
    options.capacity = 4U;
    ContentCount content_count{};
    PageSet set(options);
    require(set.insert(page("p", true), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fixture inserts");
    std::unique_ptr<PageContent> content = std::make_unique<CountedContent>(content_count);
    require(set.set_content(page_id("a"), content) == PageEditStatus::applied && content == nullptr,
            "late content attaches and leaves the argument null");
    Recorder recorder;
    recorder.connect(set);
    const SetSnapshot before = snapshot(set);

    struct Case final {
        PageEditStatus status;
        PageEditStatus expected;
        const char* message;
    };
    std::vector<Case> cases;
    cases.push_back({set.insert(page(""), PagePlacement::end_of_group()),
                     PageEditStatus::empty_id, "an empty id is refused"});
    cases.push_back({set.insert(page("new_page"), PagePlacement::end_of_group()),
                     PageEditStatus::invalid_id, "an id that breaks the StableId segment rule is refused"});
    cases.push_back({set.insert(page("a"), PagePlacement::end_of_group()),
                     PageEditStatus::duplicate_page, "a duplicate id is refused"});
    cases.push_back({set.insert(page("c"), PagePlacement::at(PagePosition{4U})),
                     PageEditStatus::position_out_of_range, "a position past the end is refused"});
    cases.push_back({set.insert(page("c"), PagePlacement::at(PagePosition{0U})),
                     PageEditStatus::crosses_pinned_boundary,
                     "an ordinary page inside the pinned group is refused"});
    cases.push_back({set.insert(page("q", true), PagePlacement::at(PagePosition{2U})),
                     PageEditStatus::crosses_pinned_boundary,
                     "a pinned page among the ordinary pages is refused"});
    cases.push_back({set.remove(page_id("zz")), PageEditStatus::unknown_page,
                     "removing an unknown page is refused"});
    cases.push_back({set.take(page_id("zz")).status, PageEditStatus::unknown_page,
                     "taking an unknown page is refused"});
    cases.push_back({set.move(page_id("zz"), PagePosition{0U}), PageEditStatus::unknown_page,
                     "moving an unknown page is refused"});
    cases.push_back({set.move(page_id("a"), PagePosition{3U}),
                     PageEditStatus::position_out_of_range, "a move past the end is refused"});
    cases.push_back({set.move(page_id("a"), PagePosition{0U}),
                     PageEditStatus::crosses_pinned_boundary,
                     "moving an ordinary page into the pinned group is refused"});
    cases.push_back({set.move(page_id("p"), PagePosition{1U}),
                     PageEditStatus::crosses_pinned_boundary,
                     "moving a pinned page out of the pinned group is refused"});
    cases.push_back({set.select(page_id("zz"), PageSelectionCause::pointer),
                     PageEditStatus::unknown_page, "selecting an unknown page is refused"});
    cases.push_back({set.set_title(page_id("zz"), "x"), PageEditStatus::unknown_page,
                     "titling an unknown page is refused"});
    cases.push_back({set.set_pinned(page_id("zz"), true), PageEditStatus::unknown_page,
                     "pinning an unknown page is refused"});
    std::unique_ptr<PageContent> second = std::make_unique<CountedContent>(content_count);
    const PageContent* const second_address = second.get();
    cases.push_back({set.set_content(page_id("a"), second), PageEditStatus::content_present,
                     "replacing content is refused"});
    require(second.get() == second_address, "refused content stays the caller's");
    cases.push_back({set.move(page_id("a"), PagePosition{1U}), PageEditStatus::unchanged,
                     "a move to the same position is unchanged"});
    cases.push_back({set.select(page_id("p"), PageSelectionCause::pointer),
                     PageEditStatus::unchanged, "selecting the selected page is unchanged"});
    cases.push_back({set.set_pinned(page_id("p"), true), PageEditStatus::unchanged,
                     "pinning a pinned page is unchanged"});
    for (const Case& refusal : cases) {
        require(refusal.status == refusal.expected, refusal.message);
        require(!page_edit_succeeded(refusal.status) || refusal.status == PageEditStatus::unchanged,
                refusal.message);
    }
    require(set.insert(page("c"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fourth page fits");
    const SetSnapshot full = snapshot(set);
    require(set.insert(page("d"), PagePlacement::end_of_group()) == PageEditStatus::capacity_reached,
            "a page beyond the capacity is refused");
    TakenPage spare{};
    spare.record = page("d");
    spare.content = std::make_unique<CountedContent>(content_count);
    const PageContent* const spare_address = spare.content.get();
    require(set.put(spare, PagePlacement::end_of_group()) == PageEditStatus::capacity_reached,
            "a put beyond the capacity is refused");
    require(spare.record.id == page_id("d") && spare.content.get() == spare_address,
            "a refused put leaves the page with the caller");
    require(snapshot(set) == full, "capacity refusals leave the set unchanged");
    require(set.remove(page_id("c")) == PageEditStatus::applied, "the fourth page leaves");
    SetSnapshot expected = before;
    expected.revision += 2U;
    require(snapshot(set) == expected,
            "every refusal left the set unchanged; only the two applied edits counted");
    require(recorder.pages_events == 2, "only the two applied edits published");

    PageSet plain;
    require(plain.insert(page("a", true), PagePlacement::end_of_group()) ==
                PageEditStatus::pinning_disabled,
            "a pinned page is refused where pinning is off");
    require(plain.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                plain.set_pinned(page_id("a"), true) == PageEditStatus::pinning_disabled,
            "pinning is refused where pinning is off");
    require(plain.count() == 1U && !plain.record_at(PagePosition{0U}).pinned,
            "the refused pin left the page ordinary");
    require(content_count.destroyed == 0, "no refusal destroyed content");
}

void edits_from_selecting_are_refused() {
    PageSet set;
    require(set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fixture inserts");
    Recorder recorder;
    recorder.set = &set;
    recorder.connect(set);
    require(set.select(page_id("b"), PageSelectionCause::keyboard) == PageEditStatus::applied,
            "the selection applies");
    require(recorder.edit_from_selecting == PageEditStatus::selecting_in_progress &&
                recorder.select_from_selecting == PageEditStatus::selecting_in_progress,
            "a selecting handler may not edit or select");
    require(order_of(set) == "a b" && selected_of(set) == "b",
            "the refused edits changed nothing");
    require(set.insert(page("c"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "edits work again after the emission");
    require_invariants(set);
}

void cancelled_selection_changes_nothing() {
    PageSet set;
    require(set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fixture inserts");
    Recorder first;
    first.connect(set);
    first.cancel_next = true;
    Recorder second;
    second.connect(set);
    const SetSnapshot before = snapshot(set);
    require(set.select(page_id("b"), PageSelectionCause::pointer) ==
                PageEditStatus::selection_cancelled,
            "a cancelled selection reports it");
    require(snapshot(set) == before, "a cancelled selection changes nothing");
    require(joined(first.trace) == "selecting a>b", "only selecting was raised");
    require(second.saw_cancel, "a later handler sees the cancel");
    first.cancel_next = false;
    require(set.select(page_id("b"), PageSelectionCause::pointer) == PageEditStatus::applied,
            "an uncancelled selection applies");
    require(joined(first.trace) == "selecting a>b | selecting a>b | selected a>b",
            "selecting precedes selected_page_changed");
    require(first.last_selection.cause == PageSelectionCause::pointer, "the cause is passed on");
    require(set.revision() == before.revision + 1U, "a selection is one revision");
}

void batch_publishes_one_change() {
    PageSetOptions options{};
    options.pinning_enabled = true;
    PageSet set(options);
    Recorder recorder;
    recorder.connect(set);
    {
        PageSetBatch batch(set);
        require(set.batch_open(), "the batch is open");
        require(set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                    set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                    set.insert(page("c"), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "three inserts apply");
        require(set.select(page_id("c"), PageSelectionCause::programmatic) == PageEditStatus::applied,
                "a selection inside the batch applies");
        require(joined(recorder.trace) == "selecting a>c",
                "inside a batch only selecting is raised");
        {
            PageSetBatch nested(set);
            require(set.set_pinned(page_id("b"), true) == PageEditStatus::applied,
                    "a nested batch edit applies");
        }
        require(joined(recorder.trace) == "selecting a>c", "closing a nested batch publishes nothing");
        require(set.move(page_id("zz"), PagePosition{0U}) == PageEditStatus::unknown_page,
                "a refusal inside the batch leaves the earlier edits");
    }
    require(!set.batch_open(), "the batch closed");
    require(joined(recorder.trace) == "selecting a>c | pages 4 | selected ->c",
            "closing the batch publishes one change, then one selection");
    require(recorder.last_pages.membership && recorder.last_pages.order &&
                recorder.last_pages.records,
            "the change accumulates every aspect");
    require(recorder.last_pages.revision == set.revision(), "the change carries the revision");
    require(recorder.last_selection.cause == PageSelectionCause::programmatic,
            "the batch selection's cause is the last one");
    require(order_of(set) == "b a c", "the pinned page leads");

    recorder.trace.clear();
    {
        PageSetBatch batch(set);
        require(set.select(page_id("a"), PageSelectionCause::programmatic) == PageEditStatus::applied &&
                    set.select(page_id("c"), PageSelectionCause::programmatic) == PageEditStatus::applied,
                "two selections apply");
    }
    require(joined(recorder.trace) == "selecting c>a | selecting a>c",
            "a batch that ends on its first selection publishes no selection change");
    recorder.trace.clear();
    {
        PageSetBatch batch(set);
        batch.close();
        require(!set.batch_open(), "close ends the batch");
    }
    require(recorder.trace.empty(), "an empty batch publishes nothing");
    {
        PageSetBatch batch(set);
        require(set.remove(page_id("c")) == PageEditStatus::applied &&
                    set.remove(page_id("a")) == PageEditStatus::applied,
                "two removals apply");
    }
    require(joined(recorder.trace) == "pages 2 | selected c>b",
            "two removals publish one change and one selection");
    require_invariants(set);
}

// Throws from pages_changed while armed (once per arming).
class ThrowingHandler final : public Component {
public:
    void on_pages(const PageSetChange&) {
        ++pages_events;
        if (!armed) return;
        armed = false;
        throw std::runtime_error("a pages_changed handler failed");
    }
    void on_selection(const PageSelectionChange&) { ++selection_events; }

    bool armed{};
    int pages_events{};
    int selection_events{};
};

// A batch closed by its destructor publishes without letting a handler's
// exception out (std::terminate otherwise). The set stays consistent and
// usable, the batch is closed, and the rest of that publication (here the
// first page's selection) is dropped, as for any handler exception; close()
// still hands the exception to its caller.
void batch_destructor_contains_a_throwing_handler() {
    PageSet set{};
    ThrowingHandler handler{};
    on(set.pages_changed(), handler, &ThrowingHandler::on_pages);
    on(set.selected_page_changed(), handler, &ThrowingHandler::on_selection);
    handler.armed = true;
    {
        PageSetBatch batch(set);
        require(set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                    set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "two inserts apply inside the batch");
    }
    require(handler.pages_events == 1 && handler.selection_events == 0 && !set.batch_open() &&
                set.count() == 2U && set.selected_page() == page_id("a"),
            "the destructor contains the exception; the edits stand and the rest of the publication is dropped");
    require_invariants(set);
    require(set.insert(page("c"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                handler.pages_events == 2,
            "the set publishes the next edit as usual");
    handler.armed = true;
    bool threw = false;
    {
        PageSetBatch batch(set);
        require(set.remove(page_id("c")) == PageEditStatus::applied, "a removal applies inside the batch");
        try {
            batch.close();
        } catch (const std::runtime_error&) {
            threw = true;
        }
    }
    require(threw && !set.batch_open() && set.count() == 2U, "an explicit close hands the exception to its caller");
}

void fallback_under_both_policies() {
    PageSet strip;
    for (const char* id : {"a", "b", "c", "d"}) {
        require(strip.insert(page(id), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "the strip fixture inserts");
    }
    Recorder recorder;
    recorder.connect(strip);
    require(strip.select(page_id("b"), PageSelectionCause::pointer) == PageEditStatus::applied,
            "b selected");
    require(strip.remove(page_id("b")) == PageEditStatus::applied && selected_of(strip) == "c",
            "the strip policy selects the next page");
    require(recorder.last_selection.cause == PageSelectionCause::fallback,
            "the fallback's cause is fallback");
    require(strip.select(page_id("d"), PageSelectionCause::pointer) == PageEditStatus::applied &&
                strip.remove(page_id("d")) == PageEditStatus::applied && selected_of(strip) == "c",
            "the strip policy selects the previous page at the end");
    require(strip.remove(page_id("a")) == PageEditStatus::applied && selected_of(strip) == "c",
            "removing an unselected page keeps the selection");
    recorder.trace.clear();
    require(strip.remove(page_id("c")) == PageEditStatus::applied &&
                !strip.selected_page().has_value(),
            "removing the last page selects nothing");
    require(joined(recorder.trace) == "pages 1 | selected c>-",
            "the change comes before the selection");

    PageSetOptions options{};
    options.fallback = PageSelectionFallback::most_recent;
    PageSet documents(options);
    for (const char* id : {"a", "b", "c", "d"}) {
        require(documents.insert(page(id), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "the document fixture inserts");
    }
    require(documents.select(page_id("c"), PageSelectionCause::pointer) == PageEditStatus::applied &&
                documents.select(page_id("b"), PageSelectionCause::pointer) == PageEditStatus::applied,
            "c then b selected");
    require(history_of(documents) == "b c a", "the history is most recent first");
    require(documents.remove(page_id("b")) == PageEditStatus::applied && selected_of(documents) == "c",
            "the document policy selects the most recent page");
    require(documents.remove(page_id("c")) == PageEditStatus::applied && selected_of(documents) == "a",
            "and again");
    require(documents.select(page_id("d"), PageSelectionCause::pointer) == PageEditStatus::applied &&
                documents.remove(page_id("a")) == PageEditStatus::applied &&
                selected_of(documents) == "d" && history_of(documents) == "d",
            "removing an unselected page drops it from the history");
    PageSet fresh(options);
    require(fresh.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                fresh.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                fresh.insert(page("c"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fresh fixture inserts");
    require(fresh.remove(page_id("a")) == PageEditStatus::applied && selected_of(fresh) == "b",
            "with no other history the document policy falls back to the neighbour");
    require_invariants(documents);
    require_invariants(fresh);
}

void history() {
    PageSet set;
    for (const char* id : {"a", "b", "c"}) {
        require(set.insert(page(id), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "the fixture inserts");
    }
    require(history_of(set) == "a", "only the selected page has been activated");
    require(set.select(page_id("c"), PageSelectionCause::keyboard) == PageEditStatus::applied &&
                set.select(page_id("b"), PageSelectionCause::keyboard) == PageEditStatus::applied &&
                set.select(page_id("c"), PageSelectionCause::keyboard) == PageEditStatus::applied,
            "c, b, c selected");
    require(history_of(set) == "c b a", "a reselected page moves to the front once");
    require(set.move(page_id("c"), PagePosition{0U}) == PageEditStatus::applied &&
                history_of(set) == "c b a",
            "a move does not change the history");
    TakenPage taken = set.take(page_id("b")).page;
    require(taken.record.id == page_id("b") && history_of(set) == "c a",
            "a taken page leaves the history");
    require(set.put(taken, PagePlacement::end_of_group()) == PageEditStatus::applied &&
                history_of(set) == "c a",
            "a put page is not activated until selected");
    require_invariants(set);
}

void pinned_boundary_on_every_path() {
    PageSetOptions options{};
    options.pinning_enabled = true;
    PageSet set(options);
    for (const char* id : {"a", "b", "c", "d"}) {
        require(set.insert(page(id), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "the fixture inserts");
    }
    require(set.set_pinned(page_id("c"), true) == PageEditStatus::applied &&
                order_of(set) == "c a b d" && set.pinned_count() == 1U,
            "pinning moves the page to the end of the pinned group");
    require(set.set_pinned(page_id("d"), true) == PageEditStatus::applied &&
                order_of(set) == "c d a b" && set.pinned_count() == 2U,
            "a second pin follows the first");
    require(set.set_pinned(page_id("c"), false) == PageEditStatus::applied &&
                order_of(set) == "d c a b" && set.pinned_count() == 1U,
            "unpinning moves the page to the start of the ordinary pages");
    require(set.set_pinned(page_id("c"), true) == PageEditStatus::applied &&
                order_of(set) == "d c a b",
            "re-pinning the first ordinary page keeps its place");
    // Every position each page may and may not move to.
    for (std::size_t from = 0U; from < set.count(); ++from) {
        for (std::size_t to = 0U; to < set.count(); ++to) {
            const PageId id = set.record_at(PagePosition{from}).id;
            const bool pinned = set.record_at(PagePosition{from}).pinned;
            const bool inside = pinned ? to < set.pinned_count() : to >= set.pinned_count();
            const SetSnapshot before = snapshot(set);
            const PageEditStatus status = set.move(id, PagePosition{to});
            if (!inside) {
                require(status == PageEditStatus::crosses_pinned_boundary,
                        "a move across the boundary is refused");
                require(snapshot(set) == before, "the refused move changed nothing");
            } else {
                require(page_edit_succeeded(status), "a move inside the group applies");
                require(set.record_at(PagePosition{to}).id == id, "the page lands at its destination");
                const PageEditStatus back = set.move(id, PagePosition{from});
                require(page_edit_succeeded(back), "the page moves back");
            }
            require_invariants(set);
        }
    }
    require(order_of(set) == "d c a b", "the order survives the move table");
    require(set.insert(page("e", true), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("f"), PagePlacement::at(PagePosition{3U})) == PageEditStatus::applied &&
                set.insert(page("g", true), PagePlacement::at(PagePosition{0U})) == PageEditStatus::applied,
            "inserts land inside their groups");
    require(order_of(set) == "g d c e f a b", "insert placements respect the groups");
    TakenPage taken = set.take(page_id("d")).page;
    require(taken.record.pinned && set.pinned_count() == 3U, "a taken pinned page keeps its flag");
    require(set.put(taken, PagePlacement::at(PagePosition{4U})) ==
                PageEditStatus::crosses_pinned_boundary,
            "a pinned page may not be put among the ordinary pages");
    require(set.put(taken, PagePlacement::at(PagePosition{3U})) == PageEditStatus::applied,
            "a pinned page puts at the end of the pinned group");
    require(order_of(set) == "g c e d f a b", "the put respects the boundary");
    require_invariants(set);
}

void take_and_put_round_trip() {
    ContentCount content_count{};
    PageSet source;
    PageSet target;
    PageRecord record = page("doc");
    record.image_key = "text";
    record.closable = true;
    require(source.insert(page("other"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                source.insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the source fixture inserts");
    std::unique_ptr<PageContent> content = std::make_unique<CountedContent>(content_count);
    const PageContent* const address = content.get();
    require(source.set_content(page_id("doc"), content) == PageEditStatus::applied,
            "the content attaches");
    require(source.content(page_id("doc")) == address, "the set lends the content");
    require(source.content(page_id("other")) == nullptr && source.content(page_id("zz")) == nullptr,
            "pages without content and unknown pages lend nothing");
    PageTakeResult taken = source.take(page_id("doc"));
    require(taken.status == PageEditStatus::applied, "the page is taken");
    require(taken.page.content.get() == address && content_count.destroyed == 0,
            "take moves the content out without destroying it");
    require(taken.page.record.id == page_id("doc") && taken.page.record.title == "Title doc" &&
                taken.page.record.image_key == "text" && taken.page.record.closable,
            "take returns the whole record");
    require(!source.find(page_id("doc")).has_value() && source.count() == 1U,
            "the page left the source");
    require(target.put(taken.page, PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the page is put into another set");
    require(taken.page.content == nullptr && taken.page.record.id.value.empty(),
            "put leaves the taken page empty");
    require(target.content(page_id("doc")) == address && content_count.destroyed == 0,
            "the same content arrived");
    require(selected_of(target) == "doc", "the first page put into an empty set is selected");
    require(target.record_at(PagePosition{0U}).image_key == "text", "the record arrived");
    require(target.remove(page_id("doc")) == PageEditStatus::applied && content_count.destroyed == 1,
            "remove destroys the content");
    {
        PageSet scoped;
        std::unique_ptr<PageContent> owned = std::make_unique<CountedContent>(content_count);
        require(scoped.insert(page("x"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                    scoped.set_content(page_id("x"), owned) == PageEditStatus::applied,
                "scoped content attaches");
    }
    require(content_count.destroyed == 2, "destroying the set destroys its content");
}

// Content whose destructor reads the set: it runs after the removal commits.
class ObservingContent final : public PageContent {
public:
    ObservingContent(const PageSet& set, std::size_t& count_seen)
        : set_(set), count_seen_(count_seen) {}
    ~ObservingContent() override { count_seen_ = set_.count(); }

private:
    const PageSet& set_;
    std::size_t& count_seen_;
};

// Edits from change handlers queue behind the notifications being published.
class Reentrant final : public Component {
public:
    void on_pages(const PageSetChange&) {
        trace.push_back("pages");
        if (!edited) {
            edited = true;
            status = (*set).select(page_id("a"), PageSelectionCause::programmatic);
        }
    }
    void on_selection(const PageSelectionChange& change) {
        trace.push_back("selected " + Recorder::optional_text(change.old_page) + ">" +
                        Recorder::optional_text(change.new_page));
    }

    PageSet* set{};
    bool edited{};
    PageEditStatus status{PageEditStatus::unchanged};
    std::vector<std::string> trace{};
};

class Destroyer final : public Component {
public:
    void on_pages(const PageSetChange&) {
        ++calls;
        owned.reset();
    }

    std::unique_ptr<PageSet> owned{};
    int calls{};
};

void event_order_and_reentrancy() {
    PageSet set;
    std::size_t count_seen = 99U;
    require(set.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("b"), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                set.insert(page("c"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fixture inserts");
    std::unique_ptr<PageContent> content = std::make_unique<ObservingContent>(set, count_seen);
    require(set.set_content(page_id("b"), content) == PageEditStatus::applied &&
                set.select(page_id("b"), PageSelectionCause::pointer) == PageEditStatus::applied,
            "b has content and is selected");
    Reentrant reentrant;
    reentrant.set = &set;
    on(set.pages_changed(), reentrant, &Reentrant::on_pages);
    on(set.selected_page_changed(), reentrant, &Reentrant::on_selection);
    require(set.remove(page_id("b")) == PageEditStatus::applied, "b is removed");
    require(count_seen == 2U, "the content's destructor saw the committed removal");
    require(reentrant.status == PageEditStatus::applied, "a change handler may select");
    require(joined(reentrant.trace) == "pages | selected b>c | selected c>a",
            "the handler's selection is published after the removal's own");
    require(selected_of(set) == "a" && history_of(set) == "a c", "the state follows the edits");
    require_invariants(set);

    Destroyer destroyer;
    destroyer.owned = std::make_unique<PageSet>();
    PageSet& doomed = *destroyer.owned;
    on(doomed.pages_changed(), destroyer, &Destroyer::on_pages);
    require(doomed.insert(page("a"), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "an edit whose handler destroys the set returns");
    require(destroyer.calls == 1 && destroyer.owned == nullptr,
            "publication stopped when the handler destroyed the set");
}

// A small deterministic generator (64-bit LCG, Knuth's constants), so a
// failing sequence repeats exactly.
struct Generator final {
    std::uint64_t state{};

    std::size_t below(std::size_t bound) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t high = state >> 33U;
        const std::size_t value = static_cast<std::size_t>(high % bound);
        return value;
    }
};

void random_edit_sequences() {
    const char* const ids[] = {"a", "b", "c", "d", "e", "f", "g", "h", "", "a"};
    const std::size_t id_count = sizeof(ids) / sizeof(ids[0]);
    ContentCount content_count{};
    int created = 0;
    int applied = 0;
    int refused = 0;
    for (std::uint64_t seed = 1U; seed <= 24U; ++seed) {
        PageSetOptions options{};
        options.pinning_enabled = (seed % 2U) == 0U;
        options.fallback = (seed % 3U) == 0U ? PageSelectionFallback::most_recent
                                             : PageSelectionFallback::neighbour;
        options.capacity = 6U;
        PageSet set(options);
        Recorder recorder;
        recorder.connect(set);
        Generator generator{seed};
        std::vector<TakenPage> outside;
        for (int step = 0; step < 400; ++step) {
            const SetSnapshot before = snapshot(set);
            const int events_before = recorder.pages_events;
            const PageId id{ids[generator.below(id_count)]};
            const std::size_t operation = generator.below(9U);
            const std::size_t position = generator.below(set.count() + 2U);
            PageEditStatus status = PageEditStatus::unchanged;
            bool page_edit = true;
            if (operation == 0U) {
                PageRecord record{};
                record.id = id;
                record.title = "t" + std::to_string(step);
                record.pinned = generator.below(3U) == 0U;
                const PagePlacement placement = generator.below(2U) == 0U
                    ? PagePlacement::end_of_group()
                    : PagePlacement::at(PagePosition{position});
                status = set.insert(record, placement);
            } else if (operation == 1U) {
                status = set.remove(id);
            } else if (operation == 2U) {
                PageTakeResult taken = set.take(id);
                status = taken.status;
                if (status == PageEditStatus::applied) {
                    outside.push_back(std::move(taken.page));
                }
            } else if (operation == 3U && !outside.empty()) {
                TakenPage& candidate = outside.back();
                const std::string kept = candidate.record.id.value;
                status = set.put(candidate, PagePlacement::at(PagePosition{position}));
                if (status == PageEditStatus::applied) {
                    outside.pop_back();
                } else {
                    require(candidate.record.id.value == kept, "a refused put keeps the page");
                }
            } else if (operation == 4U) {
                status = set.move(id, PagePosition{position});
            } else if (operation == 5U) {
                status = set.set_pinned(id, generator.below(2U) == 0U);
            } else if (operation == 6U) {
                recorder.cancel_next = generator.below(4U) == 0U;
                status = set.select(id, PageSelectionCause::pointer);
                recorder.cancel_next = false;
                page_edit = false;
            } else if (operation == 7U) {
                std::unique_ptr<PageContent> content = std::make_unique<CountedContent>(content_count);
                ++created;
                status = set.set_content(id, content);
            } else {
                PageSetBatch batch(set);
                status = set.set_title(id, "batched" + std::to_string(step));
                const PageEditStatus second = set.move(id, PagePosition{0U});
                static_cast<void>(second);
            }
            require_invariants(set);
            if (status == PageEditStatus::applied) {
                ++applied;
            } else if (!page_edit_succeeded(status)) {
                ++refused;
            }
            if (!page_edit_succeeded(status) && operation != 8U) {
                require(snapshot(set) == before, "a refused random edit changed nothing");
                require(recorder.pages_events == events_before, "a refused random edit published nothing");
            }
            if (status == PageEditStatus::applied && page_edit && operation != 8U) {
                require(recorder.pages_events == events_before + 1,
                        "an applied edit outside a batch publishes one change");
            }
            if (operation == 8U) {
                require(recorder.pages_events <= events_before + 1,
                        "a batch publishes at most one change");
            }
        }
    }
    require(content_count.destroyed == created, "every content object was destroyed exactly once");
    // 24 sequences of 400 steps: both outcomes must be well represented for
    // the invariants above to mean anything.
    require(applied > 2000 && refused > 2000, "the sequences mix applied and refused edits");
}

} // namespace

int main() {
    basics();
    refusals_leave_the_set_unchanged();
    edits_from_selecting_are_refused();
    cancelled_selection_changes_nothing();
    batch_publishes_one_change();
    batch_destructor_contains_a_throwing_handler();
    fallback_under_both_policies();
    history();
    pinned_boundary_on_every_path();
    take_and_put_round_trip();
    event_order_and_reentrancy();
    random_edit_sequences();
    std::cout << "PageSet: invariants, refusals, batches, selection, fallback, history, pinned "
                 "boundary, take and put, event order passed\n";
    return 0;
}
