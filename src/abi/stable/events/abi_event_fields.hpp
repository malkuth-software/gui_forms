#pragma once

// The fields of one event (stage C5): what a subscription's forwarder lends
// its callback, as named gf_event_field records (gf_abi.h, "Event fields").
//
// A forwarder builds an EventFields from the C++ event's arguments with the
// append_field overloads: a scalar becomes one field, a struct its members
// (nested members dotted, generated in abi_event_catalog.hpp), a collection
// one COLLECTION field whose elements are read on demand, a fixed-size
// array (std::array) one COLLECTION field whose elements are also lent in
// place (lend_elements), an object one OBJECT field holding an identity lent
// for the callback. One argument is
// named "value" when it is a scalar and adds no prefix when it is a struct;
// several are "value_1", "value_2", ... A reference to a component (the
// sender, which the event names already) adds nothing.
//
// The fields of a mutable argument are writable through events.write_field
// while the callback runs; when it returns the forwarder reads the written
// fields back into the argument with the read_field overloads.
//
// Every EventFields lives on the thread that raises its events. Its
// storage (names, values, texts) is stable from publish() until the frame
// ends, so the records it lends stay valid for the whole invoke.
//
// Checked build (GUI_FORMS_CHECKED_BORROWS, borrow sanitizer study section
// 2.5): when the invoke is over (release), the scratch the records lent is
// scribbled and, under AddressSanitizer, poisoned, so a C or C# caller that
// kept a record, a value or its text reads poisoned memory and ASan reports
// it in the caller's frame. The next emission (claim) or the destructor
// unpoisons exactly what was poisoned before the storage is touched again.
//
// Reuse (N12): a subscription's forwarder keeps one EventFields for all of
// its emissions. An emission claims it, builds, publishes and reads back,
// then releases it: release() returns the lent identities and empties the
// fields but keeps every buffer, so after the first emission an event of the
// same shape builds without allocating. A nested emission of the same
// subscription (a callback that raises the event again) finds it claimed
// and builds in its own EventFields.

#include "gui_forms/c/gf_abi.h"

#include "../registry/abi_registry.hpp"
#include "abi_lent_scratch.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/binding.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui_forms {
class DockDragOutcome;
class Theme;
class ThemeCompileResult;
} // namespace gui_forms

namespace gui_forms::abi1 {

// One choice of an enumeration a payload reaches, and the generated table
// of each such enumeration (abi_event_catalog.hpp).
struct EnumerationChoice final {
    std::string_view name;
    std::int64_t value = 0;
};

template <typename Enumeration>
struct EnumerationChoices;

class EventFields;

// Where the current struct's members start in the dotted name: the length
// of the name before enter().
struct EventFieldPrefix final {
    std::size_t length = 0U;
};

// Whether a write of value is admitted to a field of one C++ type (range of
// an integer, a known choice of an enumeration).
using EventFieldAccepts = bool (*)(const gf_value& value);

// Reads one element of a collection field into out, its text placed in the
// fields' storage. False for an element that is not a single value.
using EventElementReader = bool (*)(EventFields& fields, const void* items, std::size_t index, gf_value& out);

class EventFields final {
public:
    EventFields() = default;
    EventFields(const EventFields&) = delete;
    EventFields& operator=(const EventFields&) = delete;
    ~EventFields();

    // Building. writable applies to the fields appended until the next
    // call: those of a mutable argument.
    void set_writable(bool writable) noexcept;
    [[nodiscard]] EventFieldPrefix enter(std::string_view name);
    void leave(EventFieldPrefix prefix) noexcept;
    // Appends a field named name below the current prefix (an empty name
    // in no struct is "value") and returns its value to fill. flags
    // GF_EVENT_FIELD_WRITABLE is dropped unless the fields are writable.
    gf_value& append(std::string_view name, gf_value_kind kind, gf_event_field_flags flags,
                     EventFieldAccepts accepts);
    // Keeps a copy of text for the frame and returns a view of the copy.
    [[nodiscard]] std::string_view keep_text(std::string_view text);
    // The text a text field lends the callback, valid only during the
    // callback (the invoke). The plain build lends `text` itself (the
    // payload's storage, which outlives the invoke's callbacks: zero copy);
    // the checked build copies it through keep_text into the lent scratch,
    // so it is scribbled and poisoned when the invoke returns and a caller
    // that kept it reads poisoned memory.
    [[nodiscard]] std::string_view lend_text(std::string_view text);
    // Records a collection's elements for the field just appended.
    void set_collection(const void* items, std::size_t count, EventElementReader reader) noexcept;
    // Makes the field just appended (a COLLECTION) a fixed-size array of
    // count elements of kind, each admitted by accepts when written, and
    // returns its count element records to fill: contiguous, zeroed with
    // struct_size and kind set, in place until release(). The field's
    // value lists them in elements and element_count (and its count in
    // unsigned_integer); events.field_element reads the same records.
    [[nodiscard]] gf_value* lend_elements(std::size_t count, gf_value_kind kind, EventFieldAccepts accepts);
    // Lends object an identity for the frame: a control owned by a shared
    // pointer, or a component the registry already knows. Zero otherwise.
    [[nodiscard]] gf_object lend(const Component* object);
    // Moves the identities `from` has lent into this frame, which then gives
    // them back at its release instead. Reserves first: std::bad_alloc
    // leaves both as they were.
    void take_lent(EventFields& from);
    // The fields appended since mark: an empty optional is one field.
    [[nodiscard]] std::size_t count() const noexcept;
    // Marks the fields appended since mark as an optional named name. When
    // one field was appended under that name (a scalar) it becomes
    // OPTIONAL, and NULL when absent; otherwise (a struct) an absent value
    // is replaced by one NULL field.
    void mark_optional(std::size_t mark, std::string_view name, bool absent);

    // Reuse: claim() marks the fields in use for one emission and is false
    // when they already are; release() ends the emission (end_publication,
    // the lent identities released, the fields emptied, storage kept).
    [[nodiscard]] bool claim() noexcept;
    void release() noexcept;

    // Publication: fills event's field_count and fields (and payload, for
    // one argument that is one field named "value"), then makes the frame
    // current for write_field and field_element until it ends.
    void publish(gf_event& event, bool single_argument);
    void end_publication() noexcept;

    // Reading back: true, with value, when the field named name below the
    // current prefix was written by the callback.
    [[nodiscard]] bool written(std::string_view name, gf_value& value) const;
    // True, with value, when the field named name below the current prefix
    // is an any-kind field the callback wrote: the BindingValue write()
    // converted and owns, never the caller's record again (A-gf-abi-02-F1).
    [[nodiscard]] bool written_binding(std::string_view name, BindingValue& value) const;
    // True, with value and its text, when the fields are one field named
    // "value" (a collection's element).
    [[nodiscard]] bool single(gf_value& value, std::string_view& text) const;

    // The current frame for event (UI thread), or null.
    [[nodiscard]] static EventFields* frame_of(const gf_event* event) noexcept;
    // events.write_field and events.field_element on this frame.
    [[nodiscard]] gf_status write(std::uint64_t field, const gf_value* value, SlotIdentity slot);
    [[nodiscard]] gf_status element(std::uint64_t field, std::uint64_t index, gf_value* out, SlotIdentity slot);

private:
    struct Field final {
        std::size_t name = 0U;
        gf_value_kind kind = GF_VALUE_KIND_NULL;
        gf_event_field_flags flags = 0U;
        EventFieldAccepts accepts = nullptr;
        const void* items = nullptr;
        std::size_t count = 0U;
        EventElementReader reader = nullptr;
        bool written = false;
        // An any-kind field's written value, converted by write() into
        // owned storage while the caller's record was still valid. Empty
        // until written; release() drops it with the field.
        std::optional<BindingValue> converted;
        // A fixed-size array (lend_elements): its element records are
        // element_runs_[run], count of element_kind.
        bool fixed_count = false;
        std::size_t run = 0U;
        gf_value_kind element_kind = GF_VALUE_KIND_NULL;
        EventFieldAccepts element_accepts = nullptr;
    };

    // A write of a fixed-size array: value lists exactly target.count
    // elements of target's element kind, each admitted. Reads only.
    [[nodiscard]] gf_status check_written_elements(const Field& target, const gf_value& value,
                                                   SlotIdentity slot) const;
    // Copies the checked elements of value into target's records and points
    // value at them.
    void take_written_elements(const Field& target, gf_value& value) noexcept;

    // Writes the full name of name below the current prefix into out (an
    // empty name in no struct is "value", in a struct the struct's name).
    void compose_name(std::string_view name, std::string& out) const;
    // Whether stored is the full name of name below the current prefix.
    [[nodiscard]] bool name_matches(const std::string& stored, std::string_view name) const noexcept;

    bool writable_ = false;
    bool claimed_ = false;
    std::string prefix_;
    // Stable pooled storage: a deque keeps its elements in place as it
    // grows, and release() keeps them. names_[i] and values_[i] belong to
    // fields_[i] for i < fields_.size(); entries past that are spare.
    // texts_[0, texts_used_) are this emission's texts, the rest spare;
    // each sits in its own granule block (abi_lent_scratch.hpp) so the
    // checked build can poison even a short one.
    std::deque<std::string> names_;
    std::deque<gf_value> values_;
    std::deque<LentTextBlock> texts_;
    std::size_t texts_used_ = 0U;
    // The element records of this emission's fixed-size arrays, one
    // contiguous run per array field: element_runs_[0, element_runs_used_)
    // are in use, the rest spare. A run keeps its capacity, so an array of
    // the same count builds without allocating.
    std::deque<std::vector<gf_value>> element_runs_;
    std::size_t element_runs_used_ = 0U;
    std::vector<Field> fields_;
    std::vector<gf_event_field> records_;
    std::vector<gf_object> lent_;
    const gf_event* event_ = nullptr;
    // The frame published before this one on the same thread, while this
    // one is published (the thread's frame stack, abi_event_fields.cpp):
    // an observer, as every frame lives in its caller's scope.
    EventFields* previous_frame_ = nullptr;

#if GUI_FORMS_CHECKED_BORROWS
    // One region of lent scratch retired by the last release().
    struct RetiredRegion final {
        void* address = nullptr;
        std::size_t size = 0U;
    };
    // Scribbles and poisons this emission's names, values, texts and
    // records (before release() empties the fields); records each value
    // region (the name and text blocks keep their own state).
    void retire_lent_scratch() noexcept;
    // Unpoisons every recorded region and forgets them.
    void reclaim_lent_scratch() noexcept;
    // Records and poisons one region; regions past the reserved capacity
    // are scribbled but left unpoisoned (retiring allocates nothing).
    void retire_region(void* address, std::size_t size) noexcept;
    std::vector<RetiredRegion> retired_;
    // The names publish() lends, copied out of names_ into granule blocks:
    // name_blocks_[i] is fields_[i]'s for i < fields_.size().
    std::deque<LentTextBlock> name_blocks_;
#endif
};

// ---------------------------------------------------------------------------
// Admission of a written value, by C++ type.

bool accepts_any(const gf_value& value);

template <typename Integer>
bool accepts_integer(const gf_value& value) {
    if constexpr (std::is_signed_v<Integer>) {
        const bool fits = value.signed_integer >= static_cast<std::int64_t>(std::numeric_limits<Integer>::min()) &&
                          value.signed_integer <= static_cast<std::int64_t>(std::numeric_limits<Integer>::max());
        return fits;
    } else {
        const bool fits = value.unsigned_integer <= static_cast<std::uint64_t>(std::numeric_limits<Integer>::max());
        return fits;
    }
}

template <typename Enumeration>
bool accepts_enumeration(const gf_value& value) {
    using Underlying = std::underlying_type_t<Enumeration>;
    if (!accepts_integer<std::int64_t>(value)) {
        return false;
    }
    if (EnumerationChoices<Enumeration>::flags) {
        // A combination of flags: any value of the underlying type.
        const bool fits = value.signed_integer >= 0 &&
                          value.signed_integer <= static_cast<std::int64_t>(std::numeric_limits<Underlying>::max());
        return fits;
    }
    bool known = false;
    for (const EnumerationChoice& choice : EnumerationChoices<Enumeration>::choices) {
        if (choice.value == value.signed_integer) {
            known = true;
        }
    }
    return known;
}

// The name of an enumeration value: its choice, the choices of a
// combination of flags joined by ", ", or empty for an unknown value.
std::string_view enumeration_name(EventFields& fields, std::span<const EnumerationChoice> choices, bool flags,
                                  std::int64_t value);

// ---------------------------------------------------------------------------
// Writers: one field (or a struct's fields) per value.

void append_field(EventFields& fields, std::string_view name, bool value);
void append_field(EventFields& fields, std::string_view name, const std::string& value);
void append_field(EventFields& fields, std::string_view name, std::string_view value);
void append_field(EventFields& fields, std::string_view name, Point value);
void append_field(EventFields& fields, std::string_view name, Size value);
void append_field(EventFields& fields, std::string_view name, const Rect& value);
void append_field(EventFields& fields, std::string_view name, const Insets& value);
void append_field(EventFields& fields, std::string_view name, Color value);
void append_field(EventFields& fields, std::string_view name, const BindingValue& value);

// No field carries a drag's outcome yet (docking family, C5d): nothing.
void append_field(EventFields& fields, std::string_view name, const DockDragOutcome& value);

// A theme has no field form (a window's theme_changed is a notification;
// the catalog lists it as deferred): nothing.
void append_field(EventFields& fields, std::string_view name, const Theme& value);

// A theme sheet's compilation result has no field form yet (a control's
// theme_sheet_compiled is a notification; the catalog lists it as deferred):
// nothing.
void append_field(EventFields& fields, std::string_view name, const ThemeCompileResult& value);

template <typename Number>
    requires(std::is_arithmetic_v<Number> && !std::is_same_v<Number, bool>)
void append_field(EventFields& fields, std::string_view name, Number value) {
    if constexpr (std::is_floating_point_v<Number>) {
        gf_value& record = fields.append(name, GF_VALUE_KIND_NUMBER, GF_EVENT_FIELD_WRITABLE, &accepts_any);
        record.number = static_cast<double>(value);
    } else if constexpr (std::is_signed_v<Number>) {
        gf_value& record =
            fields.append(name, GF_VALUE_KIND_SIGNED, GF_EVENT_FIELD_WRITABLE, &accepts_integer<Number>);
        record.signed_integer = static_cast<std::int64_t>(value);
    } else {
        gf_value& record =
            fields.append(name, GF_VALUE_KIND_UNSIGNED, GF_EVENT_FIELD_WRITABLE, &accepts_integer<Number>);
        record.unsigned_integer = static_cast<std::uint64_t>(value);
    }
}

template <typename Enumeration>
    requires std::is_enum_v<Enumeration>
void append_field(EventFields& fields, std::string_view name, Enumeration value) {
    const std::int64_t number = static_cast<std::int64_t>(value);
    gf_value& record = fields.append(name, GF_VALUE_KIND_ENUMERATION, GF_EVENT_FIELD_WRITABLE,
                                     &accepts_enumeration<Enumeration>);
    record.signed_integer = number;
    const std::string_view name_text = enumeration_name(fields, EnumerationChoices<Enumeration>::choices,
                                                        EnumerationChoices<Enumeration>::flags, number);
    // Valid only during the callback (EventFields::lend_text).
    const std::string_view text = fields.lend_text(name_text);
    record.text.data = text.data();
    record.text.size = text.size();
}

template <typename Tag>
void append_field(EventFields& fields, std::string_view name, TextUnitIndex<Tag> value) {
    gf_value& record =
        fields.append(name, GF_VALUE_KIND_UNSIGNED, GF_EVENT_FIELD_WRITABLE, &accepts_integer<std::size_t>);
    record.unsigned_integer = static_cast<std::uint64_t>(value.value());
}

template <typename Object>
    requires std::is_base_of_v<Component, Object>
void append_field(EventFields& fields, std::string_view name, const Object* value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_OBJECT, 0U, nullptr);
    record.unsigned_integer = fields.lend(value);
}

template <typename Object>
    requires std::is_base_of_v<Component, Object>
void append_field(EventFields& fields, std::string_view name, const std::shared_ptr<Object>& value) {
    const Object* held = value.get();
    append_field(fields, name, held);
}

// The sender: the event names it already.
template <typename Object>
    requires std::is_base_of_v<Component, Object>
void append_field(EventFields& fields, std::string_view name, const Object& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

template <typename Element>
bool read_element(EventFields& fields, const void* items, std::size_t index, gf_value& out);

// -- fixed-size arrays (gf_abi.h, "Event fields") ----------------------------
// A std::array<Element, Count> is one COLLECTION field of Count elements
// lent in place. Element is a single value with no text to keep: bool, an
// integer, a floating-point number, an enumeration, Point, Size, Rect,
// Insets or Color (the catalog defers an array of anything else).

void fill_value(gf_value& record, Point value) noexcept;
void fill_value(gf_value& record, Size value) noexcept;
void fill_value(gf_value& record, const Rect& value) noexcept;
void fill_value(gf_value& record, const Insets& value) noexcept;
void fill_value(gf_value& record, Color value) noexcept;
void take_value(const gf_value& record, Point& value) noexcept;
void take_value(const gf_value& record, Size& value) noexcept;
void take_value(const gf_value& record, Rect& value) noexcept;
void take_value(const gf_value& record, Insets& value) noexcept;
void take_value(const gf_value& record, Color& value) noexcept;

// The kind of a fixed-size array's element.
template <typename Element>
constexpr gf_value_kind fixed_element_kind() noexcept {
    if constexpr (std::is_same_v<Element, bool>) {
        return GF_VALUE_KIND_BOOLEAN;
    } else if constexpr (std::is_enum_v<Element>) {
        return GF_VALUE_KIND_ENUMERATION;
    } else if constexpr (std::is_floating_point_v<Element>) {
        return GF_VALUE_KIND_NUMBER;
    } else if constexpr (std::is_integral_v<Element> && std::is_signed_v<Element>) {
        return GF_VALUE_KIND_SIGNED;
    } else if constexpr (std::is_integral_v<Element>) {
        return GF_VALUE_KIND_UNSIGNED;
    } else if constexpr (std::is_same_v<Element, Point>) {
        return GF_VALUE_KIND_POINT;
    } else if constexpr (std::is_same_v<Element, Size>) {
        return GF_VALUE_KIND_SIZE;
    } else if constexpr (std::is_same_v<Element, Rect>) {
        return GF_VALUE_KIND_RECTANGLE;
    } else if constexpr (std::is_same_v<Element, Insets>) {
        return GF_VALUE_KIND_INSETS;
    } else {
        static_assert(std::is_same_v<Element, Color>, "a fixed-size array's element is a single value");
        return GF_VALUE_KIND_COLOR;
    }
}

// Whether a written element is admitted (range of an integer, a known
// choice of an enumeration).
template <typename Element>
constexpr EventFieldAccepts fixed_element_accepts() noexcept {
    if constexpr (std::is_enum_v<Element>) {
        return &accepts_enumeration<Element>;
    } else if constexpr (std::is_integral_v<Element> && !std::is_same_v<Element, bool>) {
        return &accepts_integer<Element>;
    } else {
        return &accepts_any;
    }
}

// Fills one element record (its kind already set) from value; an
// enumeration's choice name is lent for the callback.
template <typename Element>
void fill_element(EventFields& fields, gf_value& record, const Element& value) {
    if constexpr (std::is_same_v<Element, bool>) {
        record.boolean = value ? 1U : 0U;
    } else if constexpr (std::is_enum_v<Element>) {
        const std::int64_t number = static_cast<std::int64_t>(value);
        record.signed_integer = number;
        const std::string_view name_text = enumeration_name(fields, EnumerationChoices<Element>::choices,
                                                            EnumerationChoices<Element>::flags, number);
        const std::string_view text = fields.lend_text(name_text);
        record.text.data = text.data();
        record.text.size = text.size();
    } else if constexpr (std::is_floating_point_v<Element>) {
        record.number = static_cast<double>(value);
    } else if constexpr (std::is_integral_v<Element> && std::is_signed_v<Element>) {
        record.signed_integer = static_cast<std::int64_t>(value);
    } else if constexpr (std::is_integral_v<Element>) {
        record.unsigned_integer = static_cast<std::uint64_t>(value);
    } else {
        fill_value(record, value);
    }
}

// Reads one written element record (of the element's kind, admitted) into
// value.
template <typename Element>
void take_element(const gf_value& record, Element& value) noexcept {
    if constexpr (std::is_same_v<Element, bool>) {
        value = record.boolean == 1U;
    } else if constexpr (std::is_enum_v<Element>) {
        value = static_cast<Element>(record.signed_integer);
    } else if constexpr (std::is_floating_point_v<Element>) {
        value = static_cast<Element>(record.number);
    } else if constexpr (std::is_integral_v<Element> && std::is_signed_v<Element>) {
        value = static_cast<Element>(record.signed_integer);
    } else if constexpr (std::is_integral_v<Element>) {
        value = static_cast<Element>(record.unsigned_integer);
    } else {
        take_value(record, value);
    }
}

// The field: COLLECTION, count Count in unsigned_integer and element_count,
// the Count element records in elements. Writable when the argument is.
template <typename Element, std::size_t Count>
void append_field(EventFields& fields, std::string_view name, const std::array<Element, Count>& value) {
    constexpr gf_value_kind element_kind = fixed_element_kind<Element>();
    constexpr EventFieldAccepts element_accepts = fixed_element_accepts<Element>();
    // The field's value is filled by lend_elements.
    fields.append(name, GF_VALUE_KIND_COLLECTION, GF_EVENT_FIELD_WRITABLE, nullptr);
    gf_value* elements = fields.lend_elements(Count, element_kind, element_accepts);
    std::size_t index = 0U;
    for (index = 0U; index < Count; ++index) {
        fill_element(fields, elements[index], value[index]);
    }
}

template <typename Element>
void append_collection(EventFields& fields, std::string_view name, std::span<const Element> value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_COLLECTION, 0U, nullptr);
    record.unsigned_integer = static_cast<std::uint64_t>(value.size());
    fields.set_collection(value.data(), value.size(), &read_element<Element>);
}

template <typename Element>
void append_field(EventFields& fields, std::string_view name, const std::vector<Element>& value) {
    const std::span<const Element> items(value.data(), value.size());
    append_collection<Element>(fields, name, items);
}

template <typename Element>
void append_field(EventFields& fields, std::string_view name, std::span<const Element> value) {
    append_collection<Element>(fields, name, value);
}

#if GUI_FORMS_CHECKED_BORROWS
// A payload's call-scope borrow (checked build; the ordinary build's is the
// span above): its plain view, which the raise's scope keeps valid for the
// whole invoke.
template <typename Element>
void append_field(EventFields& fields, std::string_view name, const BasicBorrowedSpan<const Element>& value) {
    append_collection<Element>(fields, name, unchecked_view(value));
}
#endif

template <typename Value>
void append_field(EventFields& fields, std::string_view name, const std::optional<Value>& value) {
    const std::size_t mark = fields.count();
    if (value) {
        append_field(fields, name, *value);
        fields.mark_optional(mark, name, false);
        return;
    }
    // The declared kind comes from a default value's field.
    const Value absent{};
    append_field(fields, name, absent);
    fields.mark_optional(mark, name, true);
}

// ---------------------------------------------------------------------------
// Readers: a written field back into the argument. A type no field of which
// is writable reads nothing.

void read_field(EventFields& fields, std::string_view name, bool& value);
void read_field(EventFields& fields, std::string_view name, std::string& value);
void read_field(EventFields& fields, std::string_view name, std::string_view& value);
void read_field(EventFields& fields, std::string_view name, Point& value);
void read_field(EventFields& fields, std::string_view name, Size& value);
void read_field(EventFields& fields, std::string_view name, Rect& value);
void read_field(EventFields& fields, std::string_view name, Insets& value);
void read_field(EventFields& fields, std::string_view name, Color& value);
void read_field(EventFields& fields, std::string_view name, BindingValue& value);

template <typename Number>
    requires(std::is_arithmetic_v<Number> && !std::is_same_v<Number, bool>)
void read_field(EventFields& fields, std::string_view name, Number& value) {
    gf_value written{};
    if (!fields.written(name, written)) {
        return;
    }
    if constexpr (std::is_floating_point_v<Number>) {
        value = static_cast<Number>(written.number);
    } else if constexpr (std::is_signed_v<Number>) {
        value = static_cast<Number>(written.signed_integer);
    } else {
        value = static_cast<Number>(written.unsigned_integer);
    }
}

template <typename Enumeration>
    requires std::is_enum_v<Enumeration>
void read_field(EventFields& fields, std::string_view name, Enumeration& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = static_cast<Enumeration>(written.signed_integer);
    }
}

template <typename Tag>
void read_field(EventFields& fields, std::string_view name, TextUnitIndex<Tag>& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = TextUnitIndex<Tag>(static_cast<std::size_t>(written.unsigned_integer));
    }
}

template <typename Object>
void read_field(EventFields& fields, std::string_view name, Object*& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

template <typename Object>
void read_field(EventFields& fields, std::string_view name, std::shared_ptr<Object>& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

template <typename Object>
    requires std::is_base_of_v<Component, Object>
void read_field(EventFields& fields, std::string_view name, Object& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

template <typename Element>
void read_field(EventFields& fields, std::string_view name, std::vector<Element>& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

template <typename Element>
void read_field(EventFields& fields, std::string_view name, std::span<const Element>& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

// A written fixed-size array: every element, as the write left them (a
// write replaces the whole array; see EventFields::write).
template <typename Element, std::size_t Count>
void read_field(EventFields& fields, std::string_view name, std::array<Element, Count>& value) {
    gf_value written{};
    if (!fields.written(name, written)) {
        return;
    }
    if (written.kind != GF_VALUE_KIND_COLLECTION || written.element_count != Count) {
        return;
    }
    std::size_t index = 0U;
    for (index = 0U; index < Count; ++index) {
        take_element(written.elements[index], value[index]);
    }
}

#if GUI_FORMS_CHECKED_BORROWS
template <typename Element>
void read_field(EventFields& fields, std::string_view name, BasicBorrowedSpan<const Element>& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}
#endif

template <typename Value>
void read_field(EventFields& fields, std::string_view name, std::optional<Value>& value) {
    gf_value written{};
    if (!fields.written(name, written)) {
        // A struct's members may have been written one by one.
        if (value) {
            read_field(fields, name, *value);
        }
        return;
    }
    if (written.kind == GF_VALUE_KIND_NULL) {
        value.reset();
        return;
    }
    Value present = value ? *value : Value{};
    read_field(fields, name, present);
    value = std::move(present);
}

template <typename Element>
bool read_element(EventFields& fields, const void* items, std::size_t index, gf_value& out) {
    const Element* elements = static_cast<const Element*>(items);
    EventFields scratch;
    append_field(scratch, std::string_view{}, elements[index]);
    gf_value single{};
    std::string_view text;
    if (!scratch.single(single, text)) {
        return false;
    }
    // An object element's identity was lent by scratch; the frame keeps it
    // (A-gf-abi-02-F8), or it would end with scratch before the caller
    // reads it.
    fields.take_lent(scratch);
    out = single;
    // A nested collection's lent elements end with scratch: not lent here.
    out.elements = nullptr;
    out.element_count = 0U;
    if (!text.empty()) {
        const std::string_view kept = fields.keep_text(text);
        out.text.data = kept.data();
        out.text.size = kept.size();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Connection.

// What a connector subscribes for: the subscription's binding, which the
// forwarder owns from then on.
struct EventSink final {
    std::shared_ptr<SubscriptionBinding> binding;
};

// One (declaring type, interaction) of the generated catalog. The type and
// the interaction name are static storage.
struct EventConnector final {
    std::string_view interaction;
    std::string_view type_name;
    SubscriptionToken (*connect)(Component& source, const EventSink& sink) = nullptr;
};

template <typename Argument>
inline constexpr bool mutable_argument =
    std::is_lvalue_reference_v<Argument> && !std::is_const_v<std::remove_reference_t<Argument>>;

// The name of argument index of count.
std::string argument_name(std::size_t index, std::size_t count);

template <bool Writable, typename Argument>
void append_argument(EventFields& fields, std::size_t index, std::size_t count, Argument& argument) {
    fields.set_writable(Writable);
    const std::string name = argument_name(index, count);
    append_field(fields, name, argument);
}

template <bool Writable, typename Argument>
void read_argument(EventFields& fields, std::size_t index, std::size_t count, Argument& argument) {
    if constexpr (Writable) {
        const std::string name = argument_name(index, count);
        read_field(fields, name, argument);
    } else {
        static_cast<void>(fields);
        static_cast<void>(index);
        static_cast<void>(count);
        static_cast<void>(argument);
    }
}

// Holds an EventFields claimed for one emission and releases it when the
// emission's forward ends, however it ends.
class EventFieldsClaim final {
public:
    explicit EventFieldsClaim(EventFields& fields) noexcept : fields_(&fields) {}
    ~EventFieldsClaim() { (*fields_).release(); }
    EventFieldsClaim(const EventFieldsClaim&) = delete;
    EventFieldsClaim& operator=(const EventFieldsClaim&) = delete;

private:
    EventFields* fields_ = nullptr;
};

// Forwards one C++ event of Arguments to a subscription's callback. The
// event's copy of the forwarder owns the binding and the reused fields.
template <typename... Arguments>
struct EventForwarder final {
    std::shared_ptr<SubscriptionBinding> binding;
    // Reused by every emission of this subscription (N12).
    std::shared_ptr<EventFields> reusable;

    void operator()(Arguments... arguments) const {
        forward(std::index_sequence_for<Arguments...>{}, arguments...);
    }

    template <std::size_t... Indices>
    void forward(std::index_sequence<Indices...>, Arguments&... arguments) const {
        constexpr std::size_t count = sizeof...(Arguments);
        constexpr bool any_mutable = (false || ... || mutable_argument<Arguments>);
        // A nested emission of this subscription builds in its own fields;
        // nested is declared first so the claim ends before it is destroyed.
        std::optional<EventFields> nested;
        EventFields* claimed = reusable.get();
        if (!(*claimed).claim()) {
            nested.emplace();
            claimed = &nested.value();
            static_cast<void>((*claimed).claim());
        }
        const EventFieldsClaim claim(*claimed);
        EventFields& fields = *claimed;
        gf_event event{};
        try {
            (append_argument<mutable_argument<Arguments>>(fields, Indices, count, arguments), ...);
            fields.publish(event, count == 1U);
        } catch (...) {
            // Out of memory building the fields: the callback is not called
            // for this emission.
            return;
        }
        (*binding).emit(event);
        fields.end_publication();
        if constexpr (any_mutable) {
            (read_argument<mutable_argument<Arguments>>(fields, Indices, count, arguments), ...);
        }
    }
};

template <typename... Arguments>
SubscriptionToken subscribe_forwarded(Event<Arguments...>& event, const EventSink& sink) {
    EventForwarder<Arguments...> forwarder{sink.binding, std::make_shared<EventFields>()};
    SubscriptionToken token = event.subscribe(std::move(forwarder));
    return token;
}

} // namespace gui_forms::abi1
