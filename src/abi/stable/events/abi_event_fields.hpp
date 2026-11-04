#pragma once

// The fields of one event (stage C5): what a subscription's forwarder lends
// its callback, as named gf_event_field records (gf_abi.h, "Event fields").
//
// A forwarder builds an EventFields from the C++ event's arguments with the
// append_field overloads: a scalar becomes one field, a struct its members
// (nested members dotted, generated in abi_event_catalog.hpp), a collection
// one COLLECTION field whose elements are read on demand, an object one
// OBJECT field holding an identity lent for the callback. One argument is
// named "value" when it is a scalar and adds no prefix when it is a struct;
// several are "value_1", "value_2", ... A reference to a component (the
// sender, which the event names already) adds nothing.
//
// The fields of a mutable argument are writable through events.write_field
// while the callback runs; when it returns the forwarder reads the written
// fields back into the argument with the read_field overloads.
//
// Every EventFields lives on the UI thread, in one forwarder's frame. Its
// storage (names, values, texts) is stable from publish() until the frame
// ends, so the records it lends stay valid for the whole invoke.

#include "gui_forms/c/gf_abi.h"

#include "../registry/abi_registry.hpp"
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
    // Records a collection's elements for the field just appended.
    void set_collection(const void* items, std::size_t count, EventElementReader reader) noexcept;
    // Lends object an identity for the frame: a control owned by a shared
    // pointer, or a component the registry already knows. Zero otherwise.
    [[nodiscard]] gf_object lend(const Component* object);
    // The fields appended since mark: an empty optional is one field.
    [[nodiscard]] std::size_t count() const noexcept;
    // Marks the fields appended since mark as an optional named name. When
    // one field was appended under that name (a scalar) it becomes
    // OPTIONAL, and NULL when absent; otherwise (a struct) an absent value
    // is replaced by one NULL field.
    void mark_optional(std::size_t mark, std::string_view name, bool absent);

    // Publication: fills event's field_count and fields (and payload, for
    // one argument that is one field named "value"), then makes the frame
    // current for write_field and field_element until it ends.
    void publish(gf_event& event, bool single_argument);
    void end_publication() noexcept;

    // Reading back: true, with value, when the field named name below the
    // current prefix was written by the callback.
    [[nodiscard]] bool written(std::string_view name, gf_value& value) const;
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
    };

    [[nodiscard]] std::string full_name(std::string_view name) const;

    bool writable_ = false;
    std::string prefix_;
    // Stable storage: a deque keeps its elements in place as it grows.
    std::deque<std::string> names_;
    std::deque<gf_value> values_;
    std::deque<std::string> texts_;
    std::vector<Field> fields_;
    std::vector<gf_event_field> records_;
    std::vector<gf_object> lent_;
    const gf_event* event_ = nullptr;
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
    const std::string_view text = enumeration_name(fields, EnumerationChoices<Enumeration>::choices,
                                                   EnumerationChoices<Enumeration>::flags, number);
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
    out = single;
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

// Forwards one C++ event of Arguments to a subscription's callback. The
// event's copy of the forwarder owns the binding.
template <typename... Arguments>
struct EventForwarder final {
    std::shared_ptr<SubscriptionBinding> binding;

    void operator()(Arguments... arguments) const {
        forward(std::index_sequence_for<Arguments...>{}, arguments...);
    }

    template <std::size_t... Indices>
    void forward(std::index_sequence<Indices...>, Arguments&... arguments) const {
        constexpr std::size_t count = sizeof...(Arguments);
        constexpr bool any_mutable = (false || ... || mutable_argument<Arguments>);
        EventFields fields;
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
    EventForwarder<Arguments...> forwarder{sink.binding};
    SubscriptionToken token = event.subscribe(std::move(forwarder));
    return token;
}

} // namespace gui_forms::abi1
