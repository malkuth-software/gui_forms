// Property capability lab (design study PROPERTY_CAPABILITIES_STUDY.md):
// measures what properties cost today on real controls, and prototypes the
// capability design on real Controls:
//
//   setter      (*button).set_text("OK");                  today's form
//   capability  (*button).text = "OK";                     capability mixin, event at the write
//   touch       (*button).text = "OK";                     capability mixin, event at the flush
//   key         (*button)[KeyButton::text] = "OK";         qualified key baseline
//
// The setter, capability and key sets compare, store, invalidate
// (measure | paint | semantics) and publish the change event, as
// ButtonBase::set_text does; the touch set compares, stores, invalidates and
// sets its changed slot, and a gather at the flush publishes. Also measured:
// a per-type static property table (name lookup, full walk) and the
// changed-slot gather. Not a test; a measurement program. Each timing is
// the fastest of several interleaved rounds; one line per measurement.

#include "gui_forms/binding/property_cell/property_cell.hpp"
#include "gui_forms/binding/property_table/property_table.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/binding/value/binding_value_kind/binding_value_kind.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/shared_capabilities/shared_capabilities.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/value/value.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lab {

using gui_forms::BindingValue;
using gui_forms::Control;
using gui_forms::Dirty;
using gui_forms::Event;
using gui_forms::StableId;

// ---- The capability mixin, event at the write ------------------------------

// One property's storage: the only member of its capability, so a pointer
// to it is a pointer to the capability (pointer-interconvertible, the
// capability being standard-layout), and the capability is a base of the
// control (a static_cast down). Writes go through operator=, reads through
// read(); there is no conversion operator.
template <typename Value, typename Capability>
class PropertyCell final {
public:
    PropertyCell() = default;
    PropertyCell(const PropertyCell&) = delete;
    PropertyCell& operator=(const PropertyCell&) = delete;

    PropertyCell& operator=(Value value) {
        Capability* capability = reinterpret_cast<Capability*>(this);
        Capability::assign(*capability, std::move(value));
        return *this;
    }

    [[nodiscard]] const Value& read() const noexcept { return value_; }

private:
    friend Capability;
    Value value_{};
};

template <typename Derived>
struct TextCapability {
    PropertyCell<std::string, TextCapability<Derived>> text;

    static constexpr Dirty text_dirt = Dirty::measure | Dirty::paint | Dirty::semantics;

    // The shared set: compare, store, mark, publish.
    static void assign(TextCapability& capability, std::string value) {
        Derived& control = static_cast<Derived&>(capability);
        if (capability.text.value_ == value) return;
        capability.text.value_ = std::move(value);
        control.invalidate(text_dirt);
        control.publish_text_changed(capability.text.value_);
    }
};

class CapabilityButton final : public Control, public TextCapability<CapabilityButton> {
public:
    explicit CapabilityButton(StableId stable_id) : Control(std::move(stable_id)) {}
    void publish_text_changed(const std::string& text) { publish_change(text_changed_, text); }

private:
    Event<const std::string&> text_changed_;
};

static_assert(std::is_standard_layout_v<TextCapability<CapabilityButton>>,
              "a capability must be standard-layout for its cell to reach it");

// ---- The capability mixin, event at the flush (a touch) ---------------------

// A slot's entry in its type's property table: what enumeration, lookup by
// name, binding and the gather read. Static storage.
struct PropertyEntry final {
    std::string_view name{};
    gui_forms::BindingValueKind kind{gui_forms::BindingValueKind::text};
    Dirty dirt{Dirty::none};
    std::uint8_t slot{};
};

// The changed-slot set and the table fetch every touching control shares.
class TableControl : public Control {
public:
    explicit TableControl(StableId stable_id) : Control(std::move(stable_id)) {}

    // The type's table: one virtual call.
    [[nodiscard]] virtual std::span<const PropertyEntry> lab_property_table() const noexcept = 0;

    // The write's touch: the typed dirtiness and the slot's bit.
    void touch(std::uint8_t slot, Dirty dirt) {
        changed_slots_ |= std::uint64_t{1} << slot;
        invalidate(dirt);
    }

    // The flush's gather: one change event per changed slot, in slot order;
    // the bits are cleared first, so a handler's own write is the next
    // flush's.
    void gather() {
        std::uint64_t changed = changed_slots_;
        changed_slots_ = 0U;
        while (changed != 0U) {
            const int slot = std::countr_zero(changed);
            changed &= changed - 1U;
            property_changed_.emit(static_cast<std::uint8_t>(slot));
        }
    }

    [[nodiscard]] Event<std::uint8_t>& property_changed() noexcept { return property_changed_; }

private:
    std::uint64_t changed_slots_{};
    Event<std::uint8_t> property_changed_;
};

template <typename Derived, std::uint8_t Slot>
struct TouchTextCapability {
    PropertyCell<std::string, TouchTextCapability<Derived, Slot>> text;

    static constexpr Dirty text_dirt = Dirty::measure | Dirty::paint | Dirty::semantics;

    static void assign(TouchTextCapability& capability, std::string value) {
        Derived& control = static_cast<Derived&>(capability);
        if (capability.text.value_ == value) return;
        capability.text.value_ = std::move(value);
        control.touch(Slot, text_dirt);
    }
};

// A table of `count` slots, the first being text, like a control with that
// many properties.
template <std::size_t Count>
constexpr std::array<PropertyEntry, Count> make_table(std::array<std::string_view, Count> names) {
    std::array<PropertyEntry, Count> table{};
    for (std::size_t index = 0U; index < Count; ++index) {
        table[index].name = names[index];
        table[index].kind = index == 0U ? gui_forms::BindingValueKind::text : gui_forms::BindingValueKind::number;
        table[index].dirt = Dirty::paint;
        table[index].slot = static_cast<std::uint8_t>(index);
    }
    return table;
}

// Button's 32 registered property names today (sorted as the registry map
// holds them), standing for its table.
constexpr std::array<std::string_view, 32> button_names{{
    "accessible_description", "accessible_name", "allow_drop", "anchor", "auto_size", "auto_size_mode",
    "back_color", "bounds", "cursor", "dialog_result", "dock", "enabled", "flat_style", "font",
    "fore_color", "image", "image_align", "margin", "maximum_size", "minimum_size", "name", "padding",
    "right_to_left", "size", "tab_index", "tab_stop", "tag", "text", "text_align", "text_image_relation",
    "use_mnemonic", "visible",
}};

constexpr std::array<PropertyEntry, 32> touch_button_table = make_table<32>(button_names);

class TouchButton final : public TableControl, public TouchTextCapability<TouchButton, 27> {
public:
    explicit TouchButton(StableId stable_id) : TableControl(std::move(stable_id)) {}
    [[nodiscard]] std::span<const PropertyEntry> lab_property_table() const noexcept override {
        return touch_button_table;
    }
};

// ---- The setter (today's form) --------------------------------------------

class SetterButton final : public Control {
public:
    explicit SetterButton(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] const std::string& text() const& noexcept { return text_; }
    const std::string& text() const&& = delete;
    void set_text(std::string text) {
        if (text_ == text) return;
        text_ = std::move(text);
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
        publish_change(text_changed_, text_);
    }

private:
    std::string text_;
    Event<const std::string&> text_changed_;
};

// ---- The qualified key ----------------------------------------------------

template <typename Owner, typename Key>
class KeyHandle final {
public:
    explicit KeyHandle(Owner& owner) noexcept : owner_(owner) {}
    KeyHandle& operator=(typename Key::Value value) {
        Key::assign(owner_, std::move(value));
        return *this;
    }

private:
    Owner& owner_;
};

class KeyButton final : public Control {
public:
    struct TextKey final {
        using Value = std::string;
        static void assign(KeyButton& owner, std::string value) {
            if (owner.text_ == value) return;
            owner.text_ = std::move(value);
            owner.invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
            owner.publish_change(owner.text_changed_, owner.text_);
        }
    };
    static constexpr TextKey text{};

    explicit KeyButton(StableId stable_id) : Control(std::move(stable_id)) {}

    template <typename Key>
    [[nodiscard]] KeyHandle<KeyButton, Key> operator[](Key) noexcept {
        const KeyHandle<KeyButton, Key> handle(*this);
        return handle;
    }

private:
    std::string text_;
    Event<const std::string&> text_changed_;
};

// ---- Measurements -----------------------------------------------------------

constexpr std::size_t set_iterations = 1000000U;
constexpr std::size_t construct_iterations = 2000U;
constexpr std::size_t rounds = 5U;

using Clock = std::chrono::steady_clock;

double nanoseconds_since(Clock::time_point start, std::size_t count) {
    const Clock::time_point finish = Clock::now();
    const std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start);
    const double each = static_cast<double>(elapsed.count()) / static_cast<double>(count);
    return each;
}

// Two strings alternated, so that every set changes the value.
const std::string first_text = "Apply changes";
const std::string second_text = "Cancel the operation";

const std::string& text_for(std::size_t index) {
    const bool even = (index & 1U) == 0U;
    return even ? first_text : second_text;
}

void report(std::string_view name, double value, std::string_view unit) {
    std::cout << name << ": " << value << ' ' << unit << '\n';
}

// The fastest of the rounds of each spelling, the spellings interleaved.
struct SpellingTimes final {
    double setter{1.0e300};
    double capability{1.0e300};
    double touch{1.0e300};
    double touch_with_gather{1.0e300};
    double key{1.0e300};
    double equal_value{1.0e300};
};

void measure_spellings() {
    const std::shared_ptr<SetterButton> setter = gui_forms::make_control<SetterButton>(StableId("lab.setter"));
    const std::shared_ptr<CapabilityButton> capability =
        gui_forms::make_control<CapabilityButton>(StableId("lab.capability"));
    const std::shared_ptr<TouchButton> touch = gui_forms::make_control<TouchButton>(StableId("lab.touch"));
    const std::shared_ptr<KeyButton> key = gui_forms::make_control<KeyButton>(StableId("lab.key"));
    SpellingTimes times{};
    for (std::size_t round = 0U; round < rounds; ++round) {
        Clock::time_point start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) (*setter).set_text(text_for(index));
        times.setter = std::min(times.setter, nanoseconds_since(start, set_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) (*capability).text = text_for(index);
        times.capability = std::min(times.capability, nanoseconds_since(start, set_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) (*touch).text = text_for(index);
        times.touch = std::min(times.touch, nanoseconds_since(start, set_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) {
            (*touch).text = text_for(index);
            (*touch).gather();
        }
        times.touch_with_gather = std::min(times.touch_with_gather, nanoseconds_since(start, set_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) (*key)[KeyButton::text] = text_for(index);
        times.key = std::min(times.key, nanoseconds_since(start, set_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) (*capability).text = first_text;
        times.equal_value = std::min(times.equal_value, nanoseconds_since(start, set_iterations));
    }
    report("set text, setter", times.setter, "ns");
    report("set text, capability (event at write)", times.capability, "ns");
    report("set text, touch (no event)", times.touch, "ns");
    report("set text, touch + gather of one slot", times.touch_with_gather, "ns");
    report("set text, key", times.key, "ns");
    report("set text, capability, equal value", times.equal_value, "ns");

    // The setter, touch and key loops all end on the same odd index.
    const bool same = (*setter).text() == (*touch).text.read();
    report("setter and touch values agree", same ? 1.0 : 0.0, "");
    report("sizeof SetterButton", static_cast<double>(sizeof(SetterButton)), "bytes");
    report("sizeof CapabilityButton", static_cast<double>(sizeof(CapabilityButton)), "bytes");
    report("sizeof TouchButton (+ slot set, table pointer via vtable)", static_cast<double>(sizeof(TouchButton)),
           "bytes");
    report("sizeof KeyButton", static_cast<double>(sizeof(KeyButton)), "bytes");
}

// The slot of `name` in a table, by a flat walk; the count when absent.
std::size_t find_slot(std::span<const PropertyEntry> table, std::string_view name) {
    for (std::size_t index = 0U; index < table.size(); ++index) {
        if (table[index].name == name) return index;
    }
    return table.size();
}

// The static table, read from a Control& through one virtual call: lookup
// of a name (the loader's and binding's path) and a full walk (enumeration);
// and the gather of 1, 4 and all 32 changed slots.
void measure_table_and_gather() {
    const std::shared_ptr<TouchButton> button = gui_forms::make_control<TouchButton>(StableId("lab.table"));
    const TableControl& control = *button;
    double lookup = 1.0e300;
    double walk = 1.0e300;
    std::size_t sink = 0U;
    for (std::size_t round = 0U; round < rounds; ++round) {
        Clock::time_point start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) {
            const std::span<const PropertyEntry> table = control.lab_property_table();
            sink += find_slot(table, (index & 1U) == 0U ? "tab_index" : "visible");
        }
        lookup = std::min(lookup, nanoseconds_since(start, set_iterations));
        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) {
            const std::span<const PropertyEntry> table = control.lab_property_table();
            for (const PropertyEntry& entry : table) sink += entry.slot;
        }
        walk = std::min(walk, nanoseconds_since(start, set_iterations));
    }
    report("table: lookup by name (32 entries)", lookup, "ns");
    report("table: full walk (32 entries)", walk, "ns");

    const std::array<std::size_t, 3> touched_counts{1U, 4U, 32U};
    for (const std::size_t touched : touched_counts) {
        double gather = 1.0e300;
        for (std::size_t round = 0U; round < rounds; ++round) {
            const Clock::time_point start = Clock::now();
            for (std::size_t index = 0U; index < set_iterations; ++index) {
                for (std::size_t slot = 0U; slot < touched; ++slot) {
                    (*button).touch(static_cast<std::uint8_t>(slot), Dirty::paint);
                }
                (*button).gather();
            }
            gather = std::min(gather, nanoseconds_since(start, set_iterations));
        }
        report("touch + gather of " + std::to_string(touched) + " slots", gather, "ns");
    }
    report("(sink)", static_cast<double>(sink & 1U), "");
}

// Today's controls: their size, their per-instance property registrations,
// construction, a typed set and the by-name set the loader and binding use.
template <typename ControlType>
void measure_control(std::string_view name) {
    const std::shared_ptr<ControlType> control = gui_forms::make_control<ControlType>(StableId("lab.control"));
    const std::size_t properties = (*control).property_descriptors().size();
    report(std::string(name) + " sizeof", static_cast<double>(sizeof(ControlType)), "bytes");
    report(std::string(name) + " registered properties", static_cast<double>(properties), "");
    double construct = 1.0e300;
    double typed = 1.0e300;
    double by_name = 1.0e300;
    for (std::size_t round = 0U; round < rounds; ++round) {
        Clock::time_point start = Clock::now();
        for (std::size_t index = 0U; index < construct_iterations; ++index) {
            const std::shared_ptr<ControlType> made = gui_forms::make_control<ControlType>(StableId("lab.made"));
            static_cast<void>(made);
        }
        construct = std::min(construct, nanoseconds_since(start, construct_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) {
            (*control).tab_index = static_cast<std::uint32_t>(index & 1U);
        }
        typed = std::min(typed, nanoseconds_since(start, set_iterations));

        start = Clock::now();
        for (std::size_t index = 0U; index < set_iterations; ++index) {
            const BindingValue value{static_cast<std::uint64_t>(index & 1U)};
            (*control).set_property_value("tab_index", value);
        }
        by_name = std::min(by_name, nanoseconds_since(start, set_iterations));
    }
    report(std::string(name) + " construct and release", construct, "ns");
    report(std::string(name) + " set_tab_index", typed, "ns");
    report(std::string(name) + " set_property_value tab_index", by_name, "ns");
}

// ---- One shared dirty structure per window ---------------------------------
//
// The owner's correction (2026-11-03): no per-control changed bits; a write
// marks its control's dense index in one structure the window owns, and the
// flush walks the marked indices once and clears them. Candidates, each
// holding three kinds (layout, paint, hit-test), measured on N controls with
// D of them marked; "processing" a marked control is a sink, so the numbers
// are the structure's own cost.

enum class Kind : std::uint8_t { layout = 0U, paint = 1U, hit_test = 2U };
constexpr std::size_t kind_count = 3U;

std::uint64_t dirty_sink = 0U;

// (A) dense bitsets, one per kind, walked word by word.
class KindBitsets final {
public:
    explicit KindBitsets(std::size_t controls) {
        const std::size_t words = (controls + 63U) / 64U;
        for (std::vector<std::uint64_t>& bits : bits_) bits.assign(words, 0U);
    }
    void mark(std::uint32_t index, Kind kind) noexcept {
        bits_[static_cast<std::size_t>(kind)][index >> 6U] |= std::uint64_t{1} << (index & 63U);
    }
    void flush() noexcept {
        for (std::vector<std::uint64_t>& bits : bits_) {
            for (std::size_t word = 0U; word < bits.size(); ++word) {
                std::uint64_t pending = bits[word];
                if (pending == 0U) continue;
                bits[word] = 0U;
                while (pending != 0U) {
                    const int bit = std::countr_zero(pending);
                    pending &= pending - 1U;
                    dirty_sink += word * 64U + static_cast<std::uint64_t>(bit);
                }
            }
        }
    }

private:
    std::array<std::vector<std::uint64_t>, kind_count> bits_{};
};

// (A2) the same with a summary bit per word, so the walk skips clean words
// 64 at a time.
class SummarisedBitsets final {
public:
    explicit SummarisedBitsets(std::size_t controls) {
        const std::size_t words = (controls + 63U) / 64U;
        const std::size_t summaries = (words + 63U) / 64U;
        for (std::size_t kind = 0U; kind < kind_count; ++kind) {
            bits_[kind].assign(words, 0U);
            summary_[kind].assign(summaries, 0U);
        }
    }
    void mark(std::uint32_t index, Kind kind) noexcept {
        const std::size_t which = static_cast<std::size_t>(kind);
        const std::uint32_t word = index >> 6U;
        bits_[which][word] |= std::uint64_t{1} << (index & 63U);
        summary_[which][word >> 6U] |= std::uint64_t{1} << (word & 63U);
    }
    void flush() noexcept {
        for (std::size_t kind = 0U; kind < kind_count; ++kind) {
            std::vector<std::uint64_t>& bits = bits_[kind];
            std::vector<std::uint64_t>& summary = summary_[kind];
            for (std::size_t top = 0U; top < summary.size(); ++top) {
                std::uint64_t words = summary[top];
                summary[top] = 0U;
                while (words != 0U) {
                    const std::size_t word = top * 64U + static_cast<std::size_t>(std::countr_zero(words));
                    words &= words - 1U;
                    std::uint64_t pending = bits[word];
                    bits[word] = 0U;
                    while (pending != 0U) {
                        const int bit = std::countr_zero(pending);
                        pending &= pending - 1U;
                        dirty_sink += word * 64U + static_cast<std::uint64_t>(bit);
                    }
                }
            }
        }
    }

private:
    std::array<std::vector<std::uint64_t>, kind_count> bits_{};
    std::array<std::vector<std::uint64_t>, kind_count> summary_{};
};

// (B) a deduplicated list: each control's kinds in a dense byte array, and
// the indices in mark order; a control enters the list on its first mark.
class DirtyList final {
public:
    explicit DirtyList(std::size_t controls) : kinds_(controls, std::uint8_t{0U}) { list_.reserve(controls); }
    void mark(std::uint32_t index, Kind kind) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(kind));
        if (kinds_[index] == 0U) list_.push_back(index);
        kinds_[index] |= bit;
    }
    void flush() noexcept {
        for (const std::uint32_t index : list_) {
            dirty_sink += index + kinds_[index];
            kinds_[index] = 0U;
        }
        list_.clear();
    }

private:
    std::vector<std::uint8_t> kinds_;
    std::vector<std::uint32_t> list_;
};

// (C) dense bitsets with atomic words (fetch_or), as any-thread marking
// would need; the flush exchanges each nonzero word.
class AtomicBitsets final {
public:
    explicit AtomicBitsets(std::size_t controls) : words_((controls + 63U) / 64U) {
        for (std::unique_ptr<std::atomic<std::uint64_t>[]>& bits : bits_) {
            bits = std::make_unique<std::atomic<std::uint64_t>[]>(words_);
            for (std::size_t word = 0U; word < words_; ++word) bits[word].store(0U, std::memory_order_relaxed);
        }
    }
    void mark(std::uint32_t index, Kind kind) noexcept {
        bits_[static_cast<std::size_t>(kind)][index >> 6U].fetch_or(std::uint64_t{1} << (index & 63U),
                                                                    std::memory_order_release);
    }
    void flush() noexcept {
        for (std::unique_ptr<std::atomic<std::uint64_t>[]>& bits : bits_) {
            for (std::size_t word = 0U; word < words_; ++word) {
                if (bits[word].load(std::memory_order_relaxed) == 0U) continue;
                std::uint64_t pending = bits[word].exchange(0U, std::memory_order_acquire);
                while (pending != 0U) {
                    const int bit = std::countr_zero(pending);
                    pending &= pending - 1U;
                    dirty_sink += word * 64U + static_cast<std::uint64_t>(bit);
                }
            }
        }
    }

private:
    std::size_t words_{};
    std::array<std::unique_ptr<std::atomic<std::uint64_t>[]>, kind_count> bits_{};
};

// (D) a per-thread staging buffer of (index, kind) records, appended without
// deduplication and merged into the list at the flush.
class StagedList final {
public:
    explicit StagedList(std::size_t controls) : merged_(controls) { staged_.reserve(2U * controls); }
    void mark(std::uint32_t index, Kind kind) {
        const StagedMark record{index, kind};
        staged_.push_back(record);
    }
    void flush() {
        for (const StagedMark& record : staged_) merged_.mark(record.index, record.kind);
        staged_.clear();
        merged_.flush();
    }

private:
    struct StagedMark final {
        std::uint32_t index{};
        Kind kind{Kind::paint};
    };
    std::vector<StagedMark> staged_;
    DirtyList merged_;
};

// D distinct indices of N in a scattered order (a multiplicative step).
std::vector<std::uint32_t> scattered_indices(std::size_t controls, std::size_t marked) {
    std::vector<std::uint32_t> indices{};
    indices.reserve(marked);
    std::vector<std::uint8_t> taken(controls, std::uint8_t{0U});
    const std::uint64_t step = 2654435761U % controls + 1U;
    std::uint64_t cursor = 0U;
    while (indices.size() < marked) {
        cursor = (cursor + step) % controls;
        while (taken[cursor] != 0U) cursor = (cursor + 1U) % controls;
        taken[cursor] = 1U;
        indices.push_back(static_cast<std::uint32_t>(cursor));
    }
    return indices;
}

struct DirtyTimes final {
    double mark{1.0e300};
    double flush{1.0e300};
};

// Marks every index paint (and every fourth layout too), then flushes; the
// fastest of the rounds, per mark and per flush.
template <typename Structure>
DirtyTimes measure_structure(std::size_t controls, std::span<const std::uint32_t> indices) {
    Structure structure(controls);
    DirtyTimes times{};
    const std::size_t repeats = std::max<std::size_t>(1U, 200000U / indices.size());
    for (std::size_t round = 0U; round < rounds; ++round) {
        double mark_total = 0.0;
        double flush_total = 0.0;
        for (std::size_t repeat = 0U; repeat < repeats; ++repeat) {
            Clock::time_point start = Clock::now();
            for (std::size_t position = 0U; position < indices.size(); ++position) {
                structure.mark(indices[position], Kind::paint);
                if ((position & 3U) == 0U) structure.mark(indices[position], Kind::layout);
            }
            mark_total += nanoseconds_since(start, indices.size());
            start = Clock::now();
            structure.flush();
            flush_total += nanoseconds_since(start, 1U);
        }
        times.mark = std::min(times.mark, mark_total / static_cast<double>(repeats));
        times.flush = std::min(times.flush, flush_total / static_cast<double>(repeats));
    }
    return times;
}

void report_structure(std::string_view name, std::size_t controls, std::size_t marked, DirtyTimes times) {
    std::cout << "dirty " << name << " N=" << controls << " D=" << marked << ": mark " << times.mark
              << " ns, flush " << times.flush / 1000.0 << " us\n";
}

void measure_shared_dirty() {
    const std::array<std::size_t, 3> control_counts{1000U, 10000U, 100000U};
    for (const std::size_t controls : control_counts) {
        const std::array<std::size_t, 3> marked_counts{1U, controls / 100U, controls};
        for (const std::size_t marked : marked_counts) {
            const std::vector<std::uint32_t> indices = scattered_indices(controls, marked);
            report_structure("A bitsets", controls, marked, measure_structure<KindBitsets>(controls, indices));
            report_structure("A2 summarised", controls, marked,
                             measure_structure<SummarisedBitsets>(controls, indices));
            report_structure("B list", controls, marked, measure_structure<DirtyList>(controls, indices));
            report_structure("C atomic", controls, marked, measure_structure<AtomicBitsets>(controls, indices));
            report_structure("D staged", controls, marked, measure_structure<StagedList>(controls, indices));
        }
    }
    std::cout << "(dirty sink " << (dirty_sink & 1U) << ")\n";
}

// Today's marking, for comparison: Control::invalidate on an attached
// control at the bottom of a chain of `depth` controls under a window root
// (Window::mark_dirty: flags, the ancestor walk through shared_ptr parents,
// damage bounds), and Window::flush over N flat children with D marked.
void measure_today_marking() {
    const std::array<std::size_t, 3> depths{1U, 8U, 32U};
    for (const std::size_t depth : depths) {
        const std::shared_ptr<Control> root = gui_forms::make_control<Control>(StableId("lab.root"));
        std::shared_ptr<Control> leaf = root;
        for (std::size_t level = 0U; level < depth; ++level) {
            const std::shared_ptr<Control> child =
                gui_forms::make_control<Control>(StableId("lab.chain." + std::to_string(level)));
            (*leaf).add_child(child);
            leaf = child;
        }
        gui_forms::Window window(root, gui_forms::Size{360.0, 240.0});
        window.flush();
        double paint = 1.0e300;
        double measure = 1.0e300;
        constexpr std::size_t marks = 100000U;
        for (std::size_t round = 0U; round < rounds; ++round) {
            Clock::time_point start = Clock::now();
            for (std::size_t index = 0U; index < marks; ++index) (*leaf).invalidate(Dirty::paint);
            paint = std::min(paint, nanoseconds_since(start, marks));
            window.flush();
            start = Clock::now();
            for (std::size_t index = 0U; index < marks; ++index) (*leaf).invalidate(Dirty::measure);
            measure = std::min(measure, nanoseconds_since(start, marks));
            window.flush();
        }
        std::cout << "today invalidate depth " << depth << ": paint " << paint << " ns, measure " << measure
                  << " ns\n";
    }
    const std::array<std::size_t, 3> control_counts{1000U, 10000U, 100000U};
    for (const std::size_t controls : control_counts) {
        const std::shared_ptr<Control> root = gui_forms::make_control<Control>(StableId("lab.flat"));
        std::vector<std::shared_ptr<Control>> children{};
        children.reserve(controls);
        for (std::size_t index = 0U; index < controls; ++index) {
            std::shared_ptr<Control> child =
                gui_forms::make_control<Control>(StableId("lab.flat." + std::to_string(index)));
            (*root).add_child(child);
            children.push_back(std::move(child));
        }
        gui_forms::Window window(root, gui_forms::Size{360.0, 240.0});
        window.flush();
        const std::array<std::size_t, 3> marked_counts{1U, controls / 100U, controls};
        for (const std::size_t marked : marked_counts) {
            const std::vector<std::uint32_t> indices = scattered_indices(controls, marked);
            double mark = 1.0e300;
            double flush = 1.0e300;
            for (std::size_t round = 0U; round < rounds; ++round) {
                Clock::time_point start = Clock::now();
                for (const std::uint32_t index : indices) (*children[index]).invalidate(Dirty::paint);
                mark = std::min(mark, nanoseconds_since(start, indices.size()));
                start = Clock::now();
                window.flush();
                flush = std::min(flush, nanoseconds_since(start, 1U));
            }
            std::cout << "today flat N=" << controls << " D=" << marked << ": mark " << mark << " ns, flush "
                      << flush / 1000.0 << " us\n";
        }
    }
}

void measure_registration_size() {
    report("sizeof PropertyRegistration", static_cast<double>(sizeof(gui_forms::PropertyRegistration)), "bytes");
    report("sizeof PropertyDescriptor", static_cast<double>(sizeof(gui_forms::PropertyDescriptor)), "bytes");
    report("sizeof PropertyEntry (table)", static_cast<double>(sizeof(PropertyEntry)), "bytes");
}

} // namespace lab

int main(int argument_count, char** arguments) {
    const bool dirty_only = argument_count > 1 && std::string_view(arguments[1]) == "--dirty";
    lab::measure_shared_dirty();
    lab::measure_today_marking();
    if (dirty_only) return 0;
    lab::measure_spellings();
    lab::measure_table_and_gather();
    lab::measure_registration_size();
    lab::measure_control<gui_forms::Button>("Button");
    lab::measure_control<gui_forms::TextBox>("TextBox");
    lab::measure_control<gui_forms::ListBox>("ListBox");
    return 0;
}
