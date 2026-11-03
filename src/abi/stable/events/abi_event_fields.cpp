// The fields of one event. See abi_event_fields.hpp and gf_abi.h ("Event
// fields").

#include "abi_event_fields.hpp"

#include "../tables/abi_records.hpp"

#include <cmath>
#include <string>
#include <variant>

namespace gui_forms::abi1 {
namespace {

// Lent identities are released with the events table's first slot, the one
// that made the subscription.
constexpr SlotIdentity lend_slot{GF_TABLE_EVENTS, 0U};

constexpr std::string_view single_name = "value";

// The frames whose callback is running, innermost last. UI thread only:
// every forwarder and every events slot runs there.
std::vector<EventFields*>& active_frames() {
    static std::vector<EventFields*> frames;
    return frames;
}

std::string_view view_of(gf_utf8_view text) noexcept {
    const std::string_view view(text.data, static_cast<std::size_t>(text.size));
    return view;
}

// Whether value is a well-formed value of kind (the kind already matched).
gf_status check_written(const gf_value& value, SlotIdentity slot) {
    switch (value.kind) {
    case GF_VALUE_KIND_BOOLEAN:
        if (value.boolean > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a boolean is 0 or 1");
        }
        return GF_STATUS_OK;
    case GF_VALUE_KIND_NUMBER:
        if (!std::isfinite(value.number)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a number is finite");
        }
        return GF_STATUS_OK;
    case GF_VALUE_KIND_TEXT: {
        std::string_view text;
        const gf_status read = read_text(value.text, slot, text);
        return read;
    }
    case GF_VALUE_KIND_POINT:
        if (!finite_all(value.point.x, value.point.y, 0.0, 0.0)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a point is finite");
        }
        return GF_STATUS_OK;
    case GF_VALUE_KIND_SIZE:
        if (!finite_all(value.size.width, value.size.height, 0.0, 0.0)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a size is finite");
        }
        return GF_STATUS_OK;
    case GF_VALUE_KIND_RECTANGLE:
        if (!finite_all(value.rectangle.x, value.rectangle.y, value.rectangle.width, value.rectangle.height)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a rectangle is finite");
        }
        return GF_STATUS_OK;
    case GF_VALUE_KIND_INSETS:
        if (!finite_all(value.insets.left, value.insets.top, value.insets.right, value.insets.bottom)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "insets are finite");
        }
        return GF_STATUS_OK;
    default:
        return GF_STATUS_OK;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// EventFields.

EventFields::~EventFields() {
    end_publication();
    for (const gf_object object : lent_) {
        const gf_status released = registry().release(object, lend_slot);
        static_cast<void>(released);
    }
}

void EventFields::set_writable(bool writable) noexcept {
    writable_ = writable;
}

EventFieldPrefix EventFields::enter(std::string_view name) {
    const EventFieldPrefix prefix{prefix_.size()};
    if (!name.empty()) {
        prefix_ += name;
        prefix_ += '.';
    }
    return prefix;
}

void EventFields::leave(EventFieldPrefix prefix) noexcept {
    prefix_.resize(prefix.length);
}

std::string EventFields::full_name(std::string_view name) const {
    if (!name.empty()) {
        std::string full = prefix_;
        full += name;
        return full;
    }
    if (prefix_.empty()) {
        const std::string value(single_name);
        return value;
    }
    // A nameless value inside a struct: the struct's own name.
    const std::string enclosing = prefix_.substr(0U, prefix_.size() - 1U);
    return enclosing;
}

gf_value& EventFields::append(std::string_view name, gf_value_kind kind, gf_event_field_flags flags,
                              EventFieldAccepts accepts) {
    std::string full = full_name(name);
    names_.push_back(std::move(full));
    gf_value blank{};
    blank.struct_size = value_size_1_0;
    blank.kind = kind;
    values_.push_back(blank);
    Field field{};
    field.kind = kind;
    field.flags = writable_ ? flags : (flags & ~static_cast<gf_event_field_flags>(GF_EVENT_FIELD_WRITABLE));
    field.accepts = accepts;
    fields_.push_back(field);
    return values_.back();
}

std::string_view EventFields::keep_text(std::string_view text) {
    texts_.emplace_back(text);
    const std::string_view kept = texts_.back();
    return kept;
}

void EventFields::set_collection(const void* items, std::size_t count, EventElementReader reader) noexcept {
    Field& field = fields_.back();
    field.items = items;
    field.count = count;
    field.reader = reader;
}

gf_object EventFields::lend(const Component* object) {
    if (object == nullptr) {
        return 0U;
    }
    lent_.reserve(lent_.size() + 1U);
    const Control* control = dynamic_cast<const Control*>(object);
    gf_object lent = 0U;
    if (control != nullptr) {
        const std::shared_ptr<const Control> owned = (*control).weak_from_this().lock();
        if (owned) {
            lent = registry().add_reference(std::const_pointer_cast<Control>(owned));
        }
    } else {
        lent = registry().add_reference_if_known(object);
    }
    if (lent != 0U) {
        lent_.push_back(lent);
    }
    return lent;
}

std::size_t EventFields::count() const noexcept {
    return fields_.size();
}

void EventFields::mark_optional(std::size_t mark, std::string_view name, bool absent) {
    const std::string expected = full_name(name);
    const bool scalar = fields_.size() == mark + 1U && names_[mark] == expected;
    if (scalar) {
        fields_[mark].flags |= GF_EVENT_FIELD_OPTIONAL;
    }
    if (!absent) {
        return;
    }
    if (scalar) {
        // It keeps its declared kind and may be written.
        Field& field = fields_[mark];
        field.items = nullptr;
        field.count = 0U;
        field.reader = nullptr;
        gf_value blank{};
        blank.struct_size = value_size_1_0;
        blank.kind = GF_VALUE_KIND_NULL;
        values_[mark] = blank;
        return;
    }
    // An absent struct: one NULL field in place of its members.
    while (fields_.size() > mark) {
        fields_.pop_back();
        names_.pop_back();
        values_.pop_back();
    }
    gf_value& null_value = append(name, GF_VALUE_KIND_NULL, GF_EVENT_FIELD_OPTIONAL, nullptr);
    static_cast<void>(null_value);
}

void EventFields::publish(gf_event& event, bool single_argument) {
    records_.clear();
    records_.reserve(fields_.size());
    std::vector<EventFields*>& frames = active_frames();
    frames.reserve(frames.size() + 1U);
    std::size_t index = 0U;
    for (index = 0U; index < fields_.size(); ++index) {
        gf_event_field record{};
        record.name.data = names_[index].data();
        record.name.size = names_[index].size();
        record.kind = fields_[index].kind;
        record.flags = fields_[index].flags;
        record.value = &values_[index];
        records_.push_back(record);
    }
    event.struct_size = sizeof(gf_event);
    event.field_count = records_.size();
    event.fields = records_.empty() ? nullptr : records_.data();
    const bool one_value = single_argument && fields_.size() == 1U && names_[0] == single_name;
    event.payload = one_value ? &values_[0] : nullptr;
    event_ = &event;
    frames.push_back(this);
}

void EventFields::end_publication() noexcept {
    if (event_ == nullptr) {
        return;
    }
    std::vector<EventFields*>& frames = active_frames();
    std::size_t index = frames.size();
    while (index > 0U) {
        --index;
        if (frames[index] == this) {
            frames.erase(frames.begin() + static_cast<std::ptrdiff_t>(index));
            break;
        }
    }
    event_ = nullptr;
}

bool EventFields::written(std::string_view name, gf_value& value) const {
    const std::string full = full_name(name);
    std::size_t index = 0U;
    for (index = 0U; index < fields_.size(); ++index) {
        if (fields_[index].written && names_[index] == full) {
            value = values_[index];
            return true;
        }
    }
    return false;
}

bool EventFields::single(gf_value& value, std::string_view& text) const {
    if (fields_.size() != 1U || names_[0] != single_name) {
        return false;
    }
    value = values_[0];
    text = view_of(values_[0].text);
    return true;
}

EventFields* EventFields::frame_of(const gf_event* event) noexcept {
    if (event == nullptr) {
        return nullptr;
    }
    std::vector<EventFields*>& frames = active_frames();
    std::size_t index = frames.size();
    while (index > 0U) {
        --index;
        if ((*frames[index]).event_ == event) {
            return frames[index];
        }
    }
    return nullptr;
}

gf_status EventFields::write(std::uint64_t field, const gf_value* value, SlotIdentity slot) {
    if (field >= fields_.size()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the event has no field of that index");
    }
    if (value == nullptr || (*value).struct_size < value_size_1_0) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a gf_value needs at least the 1.0 size");
    }
    Field& target = fields_[static_cast<std::size_t>(field)];
    if ((target.flags & GF_EVENT_FIELD_WRITABLE) == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the field is not writable");
    }
    gf_value copy{};
    std::memcpy(&copy, value, value_size_1_0);
    copy.struct_size = value_size_1_0;
    const bool any_kind = (target.flags & GF_EVENT_FIELD_ANY_KIND) != 0U;
    const bool absent = copy.kind == GF_VALUE_KIND_NULL && (target.flags & GF_EVENT_FIELD_OPTIONAL) != 0U;
    if (any_kind) {
        BindingValue converted;
        const gf_status read = read_value(&copy, slot, converted);
        if (read != GF_STATUS_OK) {
            return read;
        }
    } else if (!absent) {
        if (copy.kind != target.kind) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the value's kind is not the field's");
        }
        const gf_status checked = check_written(copy, slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        if (target.accepts != nullptr && !target.accepts(copy)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the value is out of the field's range or choices");
        }
    }
    // The library keeps its own copy of written text.
    if (copy.kind == GF_VALUE_KIND_TEXT || copy.kind == GF_VALUE_KIND_ENUMERATION) {
        const std::string_view text = copy.kind == GF_VALUE_KIND_TEXT ? view_of(copy.text) : std::string_view{};
        const std::string_view kept = keep_text(text);
        copy.text.data = kept.data();
        copy.text.size = kept.size();
    } else {
        copy.text.data = nullptr;
        copy.text.size = 0U;
    }
    values_[static_cast<std::size_t>(field)] = copy;
    target.written = true;
    return GF_STATUS_OK;
}

gf_status EventFields::element(std::uint64_t field, std::uint64_t index, gf_value* out, SlotIdentity slot) {
    if (field >= fields_.size()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the event has no field of that index");
    }
    const Field& source = fields_[static_cast<std::size_t>(field)];
    if (source.reader == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the field is not a collection");
    }
    if (index >= source.count) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the collection has no element of that index");
    }
    const gf_status checked = check_record(out, slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    gf_value element{};
    element.struct_size = value_size_1_0;
    if (!source.reader(*this, source.items, static_cast<std::size_t>(index), element)) {
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot, "the collection's elements are not single values");
    }
    write_record(element, out);
    return GF_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Names.

bool accepts_any(const gf_value& value) {
    static_cast<void>(value);
    return true;
}

std::string_view enumeration_name(EventFields& fields, std::span<const EnumerationChoice> choices, bool flags,
                                  std::int64_t value) {
    for (const EnumerationChoice& choice : choices) {
        if (choice.value == value) {
            return choice.name;
        }
    }
    if (!flags || value <= 0) {
        return std::string_view{};
    }
    // A combination: the single-bit choices it holds, in declaration order.
    std::string joined;
    std::int64_t covered = 0;
    for (const EnumerationChoice& choice : choices) {
        const bool single_bit = choice.value > 0 && (choice.value & (choice.value - 1)) == 0;
        if (single_bit && (value & choice.value) == choice.value) {
            if (!joined.empty()) {
                joined += ", ";
            }
            joined += choice.name;
            covered |= choice.value;
        }
    }
    if (covered != value) {
        return std::string_view{};
    }
    const std::string_view kept = fields.keep_text(joined);
    return kept;
}

std::string argument_name(std::size_t index, std::size_t count) {
    if (count == 1U) {
        return std::string{};
    }
    std::string name(single_name);
    name += '_';
    name += std::to_string(index + 1U);
    return name;
}

// ---------------------------------------------------------------------------
// Writers.

void append_field(EventFields& fields, std::string_view name, bool value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_BOOLEAN, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.boolean = value ? 1U : 0U;
}

void append_field(EventFields& fields, std::string_view name, const std::string& value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_TEXT, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.text.data = value.data();
    record.text.size = value.size();
}

void append_field(EventFields& fields, std::string_view name, std::string_view value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_TEXT, 0U, nullptr);
    record.text.data = value.data();
    record.text.size = value.size();
}

void append_field(EventFields& fields, std::string_view name, Point value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_POINT, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.point = gf_logical_point{value.x, value.y};
}

void append_field(EventFields& fields, std::string_view name, Size value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_SIZE, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.size = gf_logical_size{value.width, value.height};
}

void append_field(EventFields& fields, std::string_view name, const Rect& value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_RECTANGLE, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.rectangle = gf_logical_rect{value.x, value.y, value.width, value.height};
}

void append_field(EventFields& fields, std::string_view name, const Insets& value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_INSETS, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.insets = gf_logical_insets{value.left, value.top, value.right, value.bottom};
}

void append_field(EventFields& fields, std::string_view name, Color value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_COLOR, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.color = packed_color(value);
}

void append_field(EventFields& fields, std::string_view name, const BindingValue& value) {
    gf_value described{};
    described.struct_size = value_size_1_0;
    std::string_view text;
    if (!record_binding_value(value, described, text)) {
        // An object value has no gf_value form yet: it carries only its
        // kind.
        described.kind = GF_VALUE_KIND_OBJECT;
    }
    gf_value& record = fields.append(name, described.kind,
                                     GF_EVENT_FIELD_WRITABLE | GF_EVENT_FIELD_ANY_KIND, &accepts_any);
    record = described;
    record.text.data = text.data();
    record.text.size = text.size();
}

void append_field(EventFields& fields, std::string_view name, const DockDragOutcome& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

void append_field(EventFields& fields, std::string_view name, const Theme& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

// ---------------------------------------------------------------------------
// Readers.

void read_field(EventFields& fields, std::string_view name, bool& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = written.boolean == 1U;
    }
}

void read_field(EventFields& fields, std::string_view name, std::string& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value.assign(view_of(written.text));
    }
}

void read_field(EventFields& fields, std::string_view name, std::string_view& value) {
    static_cast<void>(fields);
    static_cast<void>(name);
    static_cast<void>(value);
}

void read_field(EventFields& fields, std::string_view name, Point& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = Point{written.point.x, written.point.y};
    }
}

void read_field(EventFields& fields, std::string_view name, Size& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = Size{written.size.width, written.size.height};
    }
}

void read_field(EventFields& fields, std::string_view name, Rect& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = Rect{written.rectangle.x, written.rectangle.y, written.rectangle.width, written.rectangle.height};
    }
}

void read_field(EventFields& fields, std::string_view name, Insets& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = Insets{written.insets.left, written.insets.top, written.insets.right, written.insets.bottom};
    }
}

void read_field(EventFields& fields, std::string_view name, Color& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        value = unpacked_color(written.color);
    }
}

void read_field(EventFields& fields, std::string_view name, BindingValue& value) {
    gf_value written{};
    if (!fields.written(name, written)) {
        return;
    }
    // Validated when it was written.
    BindingValue converted;
    const gf_status read = read_value(&written, lend_slot, converted);
    if (read == GF_STATUS_OK) {
        value = std::move(converted);
    }
}

} // namespace gui_forms::abi1
