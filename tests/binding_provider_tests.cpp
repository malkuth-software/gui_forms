// The data provider model of BindingSource (audit N31; the design note in
// the cp28 queue entry for claude/cp28-binding, accepted 2026-11-10): a
// million-row provider read through the bounded row cache, generations,
// hostile providers, revocation, notifications from another thread, the
// built-in BindingTable, and sort and filter views checked against a brute
// force under inserts, removes and changes.

#include "gui_forms/gui_forms.hpp"

#include "../src/core/binding/binding_view/binding_order_tree.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A window for a source to belong to.
struct Host final {
    Host() : root(make_control<Panel>(StableId("provider.root"))), window(root, Size{320.0, 200.0}) {}

    std::shared_ptr<Panel> root;
    Window window;
};

// A deterministic generator (a 64-bit LCG), so failures reproduce.
struct Generator final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};

    std::uint64_t next() noexcept {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33U;
    }
    std::size_t below(std::size_t bound) noexcept { return static_cast<std::size_t>(next() % bound); }
};

// Counts the list changes a source raises.
struct ListCounter final : Component {
    std::size_t changes{};
    std::size_t resets{};
    std::size_t additions{};

    void on_change(const BindingListChange& change) {
        ++changes;
        if (change.kind == BindingListChangeKind::reset) ++resets;
        if (change.kind == BindingListChangeKind::item_added) ++additions;
    }
};

// ---------------------------------------------------------------------------
// A million rows computed on demand: id (signed) and name (text), with the
// edits written to it kept aside. Counts its calls.

class MillionProvider final : public BindingProvider {
public:
    static constexpr std::size_t rows = 1000000U;

    BindingSchema schema() const override {
        BindingSchema made(std::vector<BindingColumn>{
            BindingColumn{"id", BindingValueKind::signed_integer, true},
            BindingColumn{"name", BindingValueKind::text, false},
        });
        return made;
    }
    std::size_t row_count() override {
        ++calls;
        return rows;
    }
    bool read_cell(std::size_t row, std::size_t column, BindingValue& value) override {
        ++calls;
        if (column == 0U) {
            value = static_cast<std::int64_t>(row);
            return true;
        }
        const std::map<std::size_t, std::string>::const_iterator edited = edits.find(row);
        value = edited != edits.end() ? (*edited).second : "row " + std::to_string(row);
        return true;
    }
    bool read_stable_id(std::size_t row, std::string& stable_id) override {
        ++calls;
        stable_id = "row." + std::to_string(row);
        return true;
    }
    BindingWriteResult write_cell(std::size_t row, std::size_t column, const BindingValue& value) override {
        ++calls;
        const std::string* text = std::get_if<std::string>(&value);
        if (column != 1U || text == nullptr) {
            return BindingWriteResult{BindingWriteStatus::refused, "only names are written"};
        }
        edits[row] = *text;
        return BindingWriteResult{BindingWriteStatus::accepted, {}};
    }

    std::uint64_t calls{};
    std::map<std::size_t, std::string> edits;
};

// Reads rows [first, first + count) of every column, as a painted window
// of a grid would.
void read_window(const BindingSource& source, std::size_t first, std::size_t count) {
    for (std::size_t row = first; row < first + count; ++row) {
        static_cast<void>(source.stable_id(row));
        for (std::size_t column = 0U; column < source.schema().size(); ++column) {
            static_cast<void>(source.value(row, column));
        }
    }
}

void test_million_rows_scroll_jump_and_edit() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    std::shared_ptr<MillionProvider> provider = std::make_shared<MillionProvider>();
    (*source).set_provider(provider);
    require((*source).count() == MillionProvider::rows && (*source).position() == 0,
            "a million-row provider binds with its count and the first row current");

    // A screen of 40 rows reads each of its cells, ids and states once: a
    // bound independent of the provider's size.
    const std::uint64_t before = (*source).provider_reads();
    read_window(*source, 0U, 40U);
    const std::uint64_t first_screen = (*source).provider_reads() - before;
    require(first_screen > 0U && first_screen <= 40U * 10U, "a screen of rows costs a bounded number of reads");
    read_window(*source, 0U, 40U);
    require((*source).provider_reads() - before == first_screen, "a screen read again comes from the row cache");

    // Scrolling one screen at a time keeps the cost per screen.
    for (std::size_t screen = 1U; screen < 30U; ++screen) {
        const std::uint64_t start = (*source).provider_reads();
        read_window(*source, screen * 40U, 40U);
        require((*source).provider_reads() - start <= 40U * 10U, "each scrolled screen costs a bounded number of reads");
    }

    // Jumping to the end costs one screen too.
    const std::uint64_t jump_start = (*source).provider_reads();
    require((*source).set_position(static_cast<std::ptrdiff_t>(MillionProvider::rows - 1U)) &&
                (*source).current_stable_id() == "row.999999" &&
                (*source).current_field("name") == std::optional<BindingValue>{std::string("row 999999")},
            "jumping to the last row shows it");
    read_window(*source, MillionProvider::rows - 40U, 40U);
    require((*source).provider_reads() - jump_start <= 41U * 10U, "a jump costs one screen of reads");

    // An edit goes straight to the provider's data and advances the
    // generation; the row reads back the new value.
    const std::uint64_t generation = (*source).generation();
    const BindingWriteResult written = (*source).write(500000U, 1U, std::string("edited"));
    require(written.accepted() && (*provider).edits.at(500000U) == "edited" && (*source).generation() != generation &&
                (*source).value(500000U, 1U) == std::optional<BindingValue>{std::string("edited")},
            "an edit writes in place and reads back");
    const BindingWriteResult locked = (*source).write(500000U, 0U, std::int64_t{7});
    require(!locked.accepted() && locked.status == BindingWriteStatus::read_only,
            "a read-only column refuses a write before the provider sees it");
    const BindingWriteResult wrong = (*source).write(500000U, 1U, std::int64_t{7});
    require(!wrong.accepted() && wrong.status == BindingWriteStatus::kind_mismatch,
            "a value of the wrong kind is refused before the provider sees it");
    require((*source).set_current_field("name", std::string("current")) &&
                (*provider).edits.at(MillionProvider::rows - 1U) == "current",
            "a bound edit of the current row writes in place");
}

void test_stale_generations_are_refused() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    BindingTable& table = *(*source).table();
    static_cast<void>(table.add_column(BindingColumn{"name", BindingValueKind::text}));
    table.assign({BindingRow{"a", {std::string("Alpha")}}, BindingRow{"b", {std::string("Beta")}}});
    const std::uint64_t generation = (*source).generation();
    require((*source).value_at(generation, 1U, 0U) == std::optional<BindingValue>{std::string("Beta")},
            "a read at the current generation answers");
    require(table.set_value(1U, 0U, std::string("Gamma")), "the table changes a cell");
    require((*source).generation() != generation && !(*source).value_at(generation, 1U, 0U) &&
                (*source).value_at((*source).generation(), 1U, 0U) == std::optional<BindingValue>{std::string("Gamma")},
            "a read at a stale generation is refused, the current one answers");
}

// ---------------------------------------------------------------------------
// A provider that answers badly in every way it can.

class HostileProvider final : public BindingProvider {
public:
    BindingSchema schema() const override {
        BindingSchema made(std::vector<BindingColumn>{
            BindingColumn{"name", BindingValueKind::text, false},
            BindingColumn{"amount", BindingValueKind::number, false},
        });
        return made;
    }
    std::size_t row_count() override {
        if (throw_count) throw std::runtime_error("no count");
        return count;
    }
    bool read_cell(std::size_t row, std::size_t column, BindingValue& value) override {
        if (row == 0U) {
            // The wrong kind for both columns.
            value = column == 0U ? BindingValue{std::int64_t{1}} : BindingValue{std::string("one")};
            return true;
        }
        if (row == 1U) throw std::runtime_error("cell failure");
        if (row == 2U) {
            value = std::string(binding_maximum_text_bytes + 1U, 'x');
            return true;
        }
        if (row == 3U) return false;
        value = column == 0U ? BindingValue{std::string("fine")} : BindingValue{2.5};
        return true;
    }
    bool read_stable_id(std::size_t row, std::string& stable_id) override {
        if (row == 0U) {
            stable_id = "not a stable id";
            return true;
        }
        if (row == 1U) throw std::runtime_error("id failure");
        stable_id = "row." + std::to_string(row);
        return true;
    }
    BindingWriteResult write_cell(std::size_t row, std::size_t column, const BindingValue& value) override {
        static_cast<void>(row);
        static_cast<void>(column);
        static_cast<void>(value);
        throw std::runtime_error("write failure");
    }
    void attach(const BindingProviderLink& provider_link) override { link = provider_link; }

    std::size_t count{5U};
    bool throw_count{};
    BindingProviderLink link;
};

void test_hostile_provider_answers_become_cell_errors() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    std::shared_ptr<HostileProvider> provider = std::make_shared<HostileProvider>();
    (*source).set_provider(provider);
    require((*source).count() == 5U, "a hostile provider still binds");
    require(!(*source).value(0U, 0U) && !(*source).value(0U, 1U) && !(*source).error(0U, 0U).empty(),
            "a value of the wrong kind is a cell error");
    require(!(*source).value(1U, 0U) && !(*source).error(1U, 0U).empty(), "a throwing read is a cell error");
    require(!(*source).value(2U, 0U) && !(*source).error(2U, 0U).empty(), "oversized text is a cell error");
    require(!(*source).value(3U, 1U), "no answer is an empty cell");
    require((*source).value(4U, 1U) == std::optional<BindingValue>{2.5}, "a good row of a hostile provider reads");
    require((*source).stable_id(0U).empty() && (*source).stable_id(1U).empty() && (*source).stable_id(4U) == "row.4",
            "an id that breaks the rule, or a throwing id read, gives no id");
    const BindingWriteResult written = (*source).write(4U, 0U, std::string("new"));
    require(!written.accepted(), "a throwing write is refused, not thrown");

    // Notifications past the rows are no answer either: the source re-reads.
    (*provider).count = 3U;
    (*provider).link.removed(10U, 4U);
    require((*source).count() == 3U, "an inconsistent notification resets to the re-read count");
    (*provider).count = std::size_t{1} << 40U;
    (*provider).link.reset();
    require((*source).count() == 0U, "a count past the bounds is no rows");
    (*provider).throw_count = true;
    (*provider).link.reset();
    require((*source).count() == 0U && (*source).position() == -1, "a throwing count is no rows");

    // Misuse of the source itself is a programming error.
    bool thrown = false;
    try {
        static_cast<void>((*source).value(99U, 0U));
    } catch (const std::out_of_range&) {
        thrown = true;
    }
    require(thrown, "a row past the count throws");
    thrown = false;
    try {
        (*source).set_sort({BindingSortKey{"missing", BindingSortDirection::ascending}});
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown && (*source).sort().empty(), "a sort on an unknown column throws and changes nothing");
}

// ---------------------------------------------------------------------------
// A provider that keeps its link and counts detachment; its row count may
// change from another thread.

class SharedProvider final : public BindingProvider {
public:
    BindingSchema schema() const override {
        BindingSchema made(std::vector<BindingColumn>{BindingColumn{"value", BindingValueKind::signed_integer}});
        return made;
    }
    std::size_t row_count() override { return count.load(); }
    bool read_cell(std::size_t row, std::size_t column, BindingValue& value) override {
        static_cast<void>(column);
        value = static_cast<std::int64_t>(row);
        return true;
    }
    bool read_stable_id(std::size_t row, std::string& stable_id) override {
        stable_id = "row." + std::to_string(row);
        return true;
    }
    BindingWriteResult write_cell(std::size_t row, std::size_t column, const BindingValue& value) override {
        static_cast<void>(row);
        static_cast<void>(column);
        static_cast<void>(value);
        return BindingWriteResult{BindingWriteStatus::refused, "read only data"};
    }
    void attach(const BindingProviderLink& provider_link) override { link = provider_link; }
    void detach() noexcept override { ++detached; }

    std::atomic<std::size_t> count{10U};
    BindingProviderLink link;
    std::size_t detached{};
};

void test_revocation_on_replacement_and_disposal() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    std::shared_ptr<SharedProvider> provider = std::make_shared<SharedProvider>();
    (*source).set_provider(provider);
    require(!(*provider).link.revoked() && (*source).count() == 10U, "a bound provider's link is live");
    const BindingProviderLink kept = (*provider).link;
    (*source).set_provider(nullptr);
    require(kept.revoked() && (*provider).detached == 1U && (*source).table() && (*source).count() == 0U,
            "replacing the provider revokes its link and detaches it");
    (*provider).count = 20U;
    kept.reset();
    require((*source).count() == 0U, "a revoked link reaches nothing");

    std::shared_ptr<SharedProvider> second = std::make_shared<SharedProvider>();
    std::shared_ptr<BindingSource> other = std::make_shared<BindingSource>(host.window);
    (*other).set_provider(second);
    (*other).dispose();
    require((*second).link.revoked() && (*second).detached == 1U, "disposing the source revokes the link");
}

// Reports an insertion from another thread.
struct InsertFromThread final {
    SharedProvider* provider{};
    void operator()() const {
        (*provider).count = 11U;
        (*provider).link.inserted(10U, 1U);
    }
};

void test_cross_thread_notifications_apply_at_a_safe_point() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    std::shared_ptr<SharedProvider> provider = std::make_shared<SharedProvider>();
    (*source).set_provider(provider);
    ListCounter counter;
    on((*source).list_changed(), counter, &ListCounter::on_change);
    std::thread worker(InsertFromThread{provider.get()});
    worker.join();
    require((*source).count() == 10U && counter.changes == 0U,
            "a notification from another thread waits for the UI thread");
    static_cast<void>(host.window.drain_posted_work());
    require((*source).count() == 11U && counter.additions == 1U && (*source).stable_id(10U) == "row.10",
            "the posted notification applies on the UI thread, in order");
}

// ---------------------------------------------------------------------------
// The built-in table.

void test_binding_table_checks_whole_and_keeps_errors_by_id() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    BindingTable& table = *(*source).table();
    const std::size_t name = table.add_column(BindingColumn{"Name", BindingValueKind::text});
    require(table.find_or_add_column(" NAME ") == name && (*source).column_of("name") == std::optional<std::size_t>{name},
            "column names fold");
    table.assign({BindingRow{"a", {std::string("Alpha")}}, BindingRow{"b", {std::string("Beta")}}});
    bool refused = false;
    try {
        table.assign({BindingRow{"c", {std::string("Gamma")}}, BindingRow{"c", {std::string("Again")}}});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && table.count() == 2U && table.stable_id(0U) == "a", "a refused assign changes nothing");
    refused = false;
    try {
        static_cast<void>(table.append(BindingRow{"d", {std::int64_t{4}}}));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && table.count() == 2U, "a value of the wrong kind is refused");
    table.set_error(1U, name, "Check the name.");
    table.set_error(1U, binding_whole_row, "Check the row.");
    static_cast<void>(table.insert(0U, BindingRow{"z", {std::string("Zeta")}}));
    require(table.error(2U, name) == "Check the name." && (*source).error(2U, name) == "Check the name." &&
                (*source).error(2U, binding_whole_row) == "Check the row." && table.error(0U, name).empty(),
            "errors follow their row's stable ID");
    require((*source).count() == 3U && (*source).find_stable_id("b") == std::optional<std::size_t>{2U} &&
                (*source).find("name", std::string("Zeta")) == std::optional<std::size_t>{0U},
            "the source shows the table's rows in its order");
}

// ---------------------------------------------------------------------------
// Sort and filter views against a brute force.

// The view the source should show: provider rows passing the filter, in a
// stable sort by the keys.
struct ExpectedOrder final {
    const BindingTable* table{};
    std::vector<std::size_t> columns;
    std::vector<bool> descending;

    bool operator()(std::size_t left, std::size_t right) const {
        for (std::size_t key = 0U; key < columns.size(); ++key) {
            int order = compare_binding_keys((*table).value(left, columns[key]), (*table).value(right, columns[key]));
            if (descending[key]) order = -order;
            if (order != 0) return order < 0;
        }
        return false;
    }
};

std::vector<std::string> expected_view(const BindingTable& table, const ExpectedOrder& order,
                                       const std::vector<BindingPredicate>& predicates,
                                       const std::vector<std::size_t>& predicate_columns) {
    std::vector<std::size_t> rows;
    for (std::size_t row = 0U; row < table.count(); ++row) {
        bool kept = true;
        for (std::size_t index = 0U; index < predicates.size() && kept; ++index) {
            kept = binding_predicate_holds(predicates[index], table.value(row, predicate_columns[index]));
        }
        if (kept) rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), order);
    std::vector<std::string> identities;
    identities.reserve(rows.size());
    for (const std::size_t row : rows) identities.push_back(table.stable_id(row));
    return identities;
}

std::vector<std::string> shown_view(const BindingSource& source) {
    std::vector<std::string> identities;
    identities.reserve(source.count());
    for (std::size_t row = 0U; row < source.count(); ++row) identities.push_back(source.stable_id(row));
    return identities;
}

// A random row: a group (some null), a mixed-case word and an amount.
BindingRow random_row(Generator& generator, std::size_t serial) {
    static const char* const words[] = {"apple", "Banana", "cherry", "Date", "elder", "FIG", "grape", "Apple"};
    BindingRow row;
    row.stable_id = "r." + std::to_string(serial);
    const std::size_t group = generator.below(6U);
    row.values.push_back(group == 5U ? BindingValue{} : BindingValue{static_cast<std::int64_t>(group)});
    row.values.push_back(std::string(words[generator.below(8U)]));
    row.values.push_back(static_cast<double>(generator.below(1000U)) / 1000.0);
    return row;
}

void test_sort_and_filter_views_match_brute_force_under_changes() {
    Host host;
    std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(host.window);
    BindingTable& table = *(*source).table();
    const std::size_t group = table.add_column(BindingColumn{"group", BindingValueKind::signed_integer});
    const std::size_t word = table.add_column(BindingColumn{"word", BindingValueKind::text});
    const std::size_t amount = table.add_column(BindingColumn{"amount", BindingValueKind::number});
    Generator generator;
    std::size_t serial = 0U;
    std::vector<BindingRow> rows;
    for (; serial < 400U; ++serial) rows.push_back(random_row(generator, serial));
    table.assign(std::move(rows));

    (*source).set_sort({BindingSortKey{"group", BindingSortDirection::ascending},
                        BindingSortKey{"word", BindingSortDirection::descending}});
    const BindingPredicate above{"amount", BindingPredicateOperator::greater, 0.3};
    const BindingPredicate has_p{"word", BindingPredicateOperator::contains, std::string("P")};
    (*source).set_filter(BindingFilter{{above, has_p}});
    require((*source).view_mode() == BindingViewMode::index, "a table that cannot sort gets the source's index");

    const ExpectedOrder order{&table, {group, word}, {false, true}};
    const std::vector<BindingPredicate> predicates{above, has_p};
    const std::vector<std::size_t> predicate_columns{amount, word};
    require(shown_view(*source) == expected_view(table, order, predicates, predicate_columns),
            "the built view matches the brute force");

    for (std::size_t step = 0U; step < 600U; ++step) {
        const std::size_t action = generator.below(4U);
        if (action == 0U || table.count() < 50U) {
            static_cast<void>(table.insert(generator.below(table.count() + 1U), random_row(generator, serial)));
            ++serial;
        } else if (action == 1U) {
            const std::size_t first = generator.below(table.count());
            static_cast<void>(table.remove(first, 1U + generator.below(3U)));
        } else if (action == 2U) {
            const BindingRow source_row = random_row(generator, serial);
            const std::size_t column = generator.below(3U);
            static_cast<void>(table.set_value(generator.below(table.count()), column, source_row.values[column]));
        } else {
            const BindingRow source_row = random_row(generator, serial);
            static_cast<void>(table.set_value(generator.below(table.count()), amount, source_row.values[amount]));
        }
        if (shown_view(*source) != expected_view(table, order, predicates, predicate_columns)) {
            throw std::runtime_error("the view diverged from the brute force at step " + std::to_string(step));
        }
    }

    // Equal keys keep the provider order (the tie-break).
    (*source).set_filter(BindingFilter{});
    (*source).set_sort({BindingSortKey{"group", BindingSortDirection::descending}});
    const ExpectedOrder by_group{&table, {group}, {true}};
    require(shown_view(*source) == expected_view(table, by_group, {}, {}), "equal keys keep the provider order");

    // Clearing both returns to the provider's order.
    (*source).remove_sort();
    require((*source).view_mode() == BindingViewMode::provider_order && (*source).count() == table.count() &&
                (*source).stable_id(0U) == table.stable_id(0U),
            "no sort and no filter show the provider's order");
}

void test_text_compares_ignoring_case_by_code_point() {
    require(compare_binding_keys(std::string("apple"), std::string("APPLE")) == 0 &&
                compare_binding_keys(std::string("apple"), std::string("Banana")) < 0 &&
                compare_binding_keys(std::string("\xC3\xA9t\xC3\xA9"), std::string("\xC3\x89T\xC3\x89")) == 0 &&
                compare_binding_keys(std::int64_t{2}, 2.5) < 0 &&
                compare_binding_keys(std::uint64_t{18446744073709551615ULL}, std::int64_t{-1}) > 0 &&
                compare_binding_keys(BindingValue{}, std::int64_t{0}) < 0,
            "keys compare text ignoring case and numbers across their kinds");
}

// ---------------------------------------------------------------------------
// The view index's order-statistic tree against a vector.

void test_order_tree_matches_a_vector() {
    detail::BindingOrderTree tree;
    std::vector<std::uint32_t> reference;
    std::vector<std::uint32_t> initial;
    for (std::uint32_t element = 0U; element < 1000U; ++element) initial.push_back(element);
    tree.assign(initial, 20000U);
    reference = initial;
    Generator generator;
    std::uint32_t next = 1000U;
    for (std::size_t step = 0U; step < 6000U; ++step) {
        if (reference.empty() || generator.below(2U) == 0U) {
            const std::size_t position = generator.below(reference.size() + 1U);
            tree.insert_at(position, next);
            reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(position), next);
            ++next;
        } else {
            const std::size_t position = generator.below(reference.size());
            tree.erase(reference[position]);
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(position));
        }
        if (step % 500U == 0U) {
            require(tree.size() == reference.size(), "the tree's size matches");
            for (std::size_t position = 0U; position < reference.size(); ++position) {
                require(tree.select(position) == reference[position] && tree.rank(reference[position]) == position,
                        "select and rank match the vector");
            }
        }
    }
    while (!reference.empty()) {
        tree.erase(reference.back());
        reference.pop_back();
    }
    require(tree.size() == 0U && tree.node_count() == 1U, "an emptied tree collapses to its root");
}

} // namespace

int main() {
    try {
        test_million_rows_scroll_jump_and_edit();
        test_stale_generations_are_refused();
        test_hostile_provider_answers_become_cell_errors();
        test_revocation_on_replacement_and_disposal();
        test_cross_thread_notifications_apply_at_a_safe_point();
        test_binding_table_checks_whole_and_keeps_errors_by_id();
        test_sort_and_filter_views_match_brute_force_under_changes();
        test_text_compares_ignoring_case_by_code_point();
        test_order_tree_matches_a_vector();
        std::cout << "gui_forms_binding_provider_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_binding_provider_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
