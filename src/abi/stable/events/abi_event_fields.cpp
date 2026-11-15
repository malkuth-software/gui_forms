// The fields of one event. See abi_event_fields.hpp and gf_abi.h ("Event
// fields").

#include "abi_event_fields.hpp"

#include "../tables/abi_records.hpp"

#include "gui_forms/detail/storage_growth.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <variant>

// The checked build poisons the scratch an invoke lent once it returns
// (abi_event_fields.hpp); AddressSanitizer's interface, when it is present.
#if GUI_FORMS_CHECKED_BORROWS
#if defined(__SANITIZE_ADDRESS__)
#define GUI_FORMS_EVENT_SCRATCH_POISONED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GUI_FORMS_EVENT_SCRATCH_POISONED 1
#endif
#endif
#endif
#if defined(GUI_FORMS_EVENT_SCRATCH_POISONED)
#include <sanitizer/asan_interface.h>
#endif

namespace gui_forms::abi1 {
namespace {

// Lent identities are released with the events table's first slot, the one
// that made the subscription.
constexpr SlotIdentity lend_slot{GF_TABLE_EVENTS, 0U};

constexpr std::string_view single_name = "value";

// The innermost frame whose callback is running on this thread; each frame
// links the one before it (EventFields::previous_frame_). One stack per
// thread (N9): since 1.1 a sub-surface's thread raises its own controls'
// events and calls the events slots for them, beside the UI thread. A
// plain pointer (owner rule 2: every thread_local trivially destructible),
// and the frames are their callers' objects, so pushing never allocates.
thread_local EventFields* top_frame = nullptr;

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
    release();
#if GUI_FORMS_CHECKED_BORROWS
    reclaim_lent_scratch();
#endif
}

bool EventFields::claim() noexcept {
    if (claimed_) {
        return false;
    }
#if GUI_FORMS_CHECKED_BORROWS
    reclaim_lent_scratch();
#endif
    claimed_ = true;
    return true;
}

void EventFields::release() noexcept {
    end_publication();
#if GUI_FORMS_CHECKED_BORROWS
    retire_lent_scratch();
#endif
    for (const gf_object object : lent_) {
        const gf_status released = registry().release(object, lend_slot);
        static_cast<void>(released);
    }
    lent_.clear();
    fields_.clear();
    records_.clear();
    texts_used_ = 0U;
    element_runs_used_ = 0U;
    prefix_.clear();
    writable_ = false;
    claimed_ = false;
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

void EventFields::compose_name(std::string_view name, std::string& out) const {
    if (!name.empty()) {
        out.assign(prefix_);
        out.append(name);
        return;
    }
    if (prefix_.empty()) {
        out.assign(single_name);
        return;
    }
    // A nameless value inside a struct: the struct's own name.
    out.assign(prefix_, 0U, prefix_.size() - 1U);
}

bool EventFields::name_matches(const std::string& stored, std::string_view name) const noexcept {
    const std::string_view held(stored);
    if (!name.empty()) {
        const bool matches = held.size() == prefix_.size() + name.size() && held.starts_with(prefix_) &&
                             held.substr(prefix_.size()) == name;
        return matches;
    }
    if (prefix_.empty()) {
        const bool matches = held == single_name;
        return matches;
    }
    const std::string_view enclosing = std::string_view(prefix_).substr(0U, prefix_.size() - 1U);
    const bool matches = held == enclosing;
    return matches;
}

gf_value& EventFields::append(std::string_view name, gf_value_kind kind, gf_event_field_flags flags,
                              EventFieldAccepts accepts) {
    // The pool's entry for the next field, grown only past the largest
    // event this EventFields has built.
    const std::size_t index = fields_.size();
    if (index == names_.size()) {
        names_.emplace_back();
        values_.emplace_back();
    }
    compose_name(name, names_[index]);
    gf_value& value = values_[index];
    value = gf_value{};
    value.struct_size = value_size_1_0;
    value.kind = kind;
    Field field{};
    field.kind = kind;
    field.flags = writable_ ? flags : (flags & ~static_cast<gf_event_field_flags>(GF_EVENT_FIELD_WRITABLE));
    field.accepts = accepts;
    fields_.push_back(field);
    return value;
}

std::string_view EventFields::lend_text(std::string_view text) {
#if GUI_FORMS_CHECKED_BORROWS
    const std::string_view lent = keep_text(text);
    return lent;
#else
    return text;
#endif
}

std::string_view EventFields::keep_text(std::string_view text) {
    if (texts_used_ == texts_.size()) {
        texts_.emplace_back();
    }
    const std::string_view kept = texts_[texts_used_].assign(text);
    texts_used_ = texts_used_ + 1U;
    return kept;
}

void EventFields::set_collection(const void* items, std::size_t count, EventElementReader reader) noexcept {
    Field& field = fields_.back();
    field.items = items;
    field.count = count;
    field.reader = reader;
}

gf_value* EventFields::lend_elements(std::size_t count, gf_value_kind kind, EventFieldAccepts accepts) {
    // The run is reserved and filled before the field changes: a refused
    // allocation leaves the field a plain collection of no elements.
    if (element_runs_used_ == element_runs_.size()) {
        element_runs_.emplace_back();
    }
    std::vector<gf_value>& run = element_runs_[element_runs_used_];
    gf_value blank{};
    blank.struct_size = value_size_1_0;
    blank.kind = kind;
    run.assign(count, blank);
    Field& field = fields_.back();
    field.count = count;
    field.fixed_count = true;
    field.run = element_runs_used_;
    field.element_kind = kind;
    field.element_accepts = accepts;
    element_runs_used_ = element_runs_used_ + 1U;
    gf_value* first = run.empty() ? nullptr : run.data();
    gf_value& value = values_[fields_.size() - 1U];
    value.unsigned_integer = static_cast<std::uint64_t>(count);
    value.elements = first;
    value.element_count = static_cast<std::uint64_t>(count);
    return first;
}

gf_object EventFields::lend(const Component* object) {
    if (object == nullptr) {
        return 0U;
    }
    detail::reserve_one_more(lent_);
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
    const bool scalar = fields_.size() == mark + 1U && name_matches(names_[mark], name);
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
        field.fixed_count = false;
        gf_value blank{};
        blank.struct_size = value_size_1_0;
        blank.kind = GF_VALUE_KIND_NULL;
        values_[mark] = blank;
        return;
    }
    // An absent struct: one NULL field in place of its members.
    // The members' pool entries stay as spares.
    while (fields_.size() > mark) {
        fields_.pop_back();
    }
    gf_value& null_value = append(name, GF_VALUE_KIND_NULL, GF_EVENT_FIELD_OPTIONAL, nullptr);
    static_cast<void>(null_value);
}

void EventFields::publish(gf_event& event, bool single_argument) {
    records_.clear();
    records_.reserve(fields_.size());
#if GUI_FORMS_CHECKED_BORROWS
    // Room to record every region release() retires: one value per field
    // (names and texts are blocks that keep their own poisoned state, so
    // texts kept after publish need no room here). Reserved before any
    // field is lent; a failure leaves nothing lent.
    std::size_t lent_elements = 0U;
    std::size_t run_index = 0U;
    for (run_index = 0U; run_index < element_runs_used_; ++run_index) {
        lent_elements = lent_elements + element_runs_[run_index].size();
    }
    retired_.reserve(retired_.size() + fields_.size() + lent_elements);
    while (name_blocks_.size() < fields_.size()) {
        name_blocks_.emplace_back();
    }
#endif
    std::size_t index = 0U;
    for (index = 0U; index < fields_.size(); ++index) {
        gf_event_field record{};
#if GUI_FORMS_CHECKED_BORROWS
        const std::string_view name = name_blocks_[index].assign(names_[index]);
#else
        const std::string_view name = names_[index];
#endif
        record.name.data = name.data();
        record.name.size = name.size();
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
    // Published again (a class hook's base): leaves its old place first,
    // so the stack holds each frame once.
    end_publication();
    event_ = &event;
    previous_frame_ = top_frame;
    top_frame = this;
}

void EventFields::end_publication() noexcept {
    if (event_ == nullptr) {
        return;
    }
    // Unlinked wherever it sits; frames end innermost first, so this is
    // the top in practice.
    EventFields** link = &top_frame;
    while (*link != nullptr && *link != this) {
        link = &(**link).previous_frame_;
    }
    if (*link == this) *link = previous_frame_;
    previous_frame_ = nullptr;
    event_ = nullptr;
}

#if GUI_FORMS_CHECKED_BORROWS
namespace {

// What retired scratch reads as when nothing poisons it: an unlikely byte,
// so a kept record shows garbage rather than the old values.
constexpr unsigned char retired_scratch_byte = 0xA5U;

// retire_lent_scratch poisons each gf_value whole: that needs whole
// AddressSanitizer granules, which a pointer-aligned record of 8-byte
// multiples in a deque block is.
static_assert(sizeof(gf_value) % 8U == 0U && alignof(gf_value) % 8U == 0U,
              "a gf_value spans whole 8-byte granules");

} // namespace

void EventFields::retire_region(void* address, std::size_t size) noexcept {
    if (address == nullptr || size == 0U) {
        return;
    }
    std::memset(address, retired_scratch_byte, size);
    if (retired_.size() == retired_.capacity()) {
        return;
    }
    RetiredRegion region{};
    region.address = address;
    region.size = size;
    retired_.push_back(region);
#if defined(GUI_FORMS_EVENT_SCRATCH_POISONED)
    ASAN_POISON_MEMORY_REGION(address, size);
#endif
}

void EventFields::retire_lent_scratch() noexcept {
    std::size_t index = 0U;
    // Names and texts are whole granule blocks, poisonable however short.
    // A gf_value is pointer-aligned and a multiple of 8 bytes in a deque
    // block, so each value is whole granules too.
    for (index = 0U; index < fields_.size() && index < name_blocks_.size(); ++index) {
        name_blocks_[index].retire();
        retire_region(&values_[index], sizeof(gf_value));
    }
    for (index = 0U; index < texts_used_; ++index) {
        texts_[index].retire();
    }
    // A fixed-size array's element records: whole gf_values, contiguous.
    for (index = 0U; index < element_runs_used_ && index < element_runs_.size(); ++index) {
        std::vector<gf_value>& run = element_runs_[index];
        if (!run.empty()) {
            retire_region(run.data(), run.size() * sizeof(gf_value));
        }
    }
    // The records are scribbled only: release() clears records_ next, and
    // the library's own vector annotation then poisons them where it is on.
    if (!records_.empty()) {
        std::memset(static_cast<void*>(records_.data()), retired_scratch_byte,
                    records_.size() * sizeof(gf_event_field));
    }
}

void EventFields::reclaim_lent_scratch() noexcept {
#if defined(GUI_FORMS_EVENT_SCRATCH_POISONED)
    for (const RetiredRegion& region : retired_) {
        ASAN_UNPOISON_MEMORY_REGION(region.address, region.size);
    }
#endif
    retired_.clear();
}
#endif

bool EventFields::written(std::string_view name, gf_value& value) const {
    std::size_t index = 0U;
    for (index = 0U; index < fields_.size(); ++index) {
        if (fields_[index].written && name_matches(names_[index], name)) {
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
    EventFields* frame = top_frame;
    while (frame != nullptr) {
        if ((*frame).event_ == event) {
            return frame;
        }
        frame = (*frame).previous_frame_;
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
        if (target.fixed_count) {
            const gf_status elements_checked = check_written_elements(target, copy, slot);
            if (elements_checked != GF_STATUS_OK) {
                return elements_checked;
            }
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
    if (target.fixed_count) {
        if (copy.kind == GF_VALUE_KIND_COLLECTION) {
            take_written_elements(target, copy);
        } else {
            // Absent: the caller's elements are not kept.
            copy.elements = nullptr;
            copy.element_count = 0U;
        }
    }
    values_[static_cast<std::size_t>(field)] = copy;
    target.written = true;
    return GF_STATUS_OK;
}

gf_status EventFields::check_written_elements(const Field& target, const gf_value& value, SlotIdentity slot) const {
    if (value.element_count != static_cast<std::uint64_t>(target.count)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a fixed-size array is written with exactly its element count");
    }
    // Stepped by the first element's struct_size (gf_abi.h, the array rule),
    // the whole array validated before any element is kept.
    std::uint32_t stride = 0U;
    const gf_status array_checked = check_array(value.elements, value.element_count, value_size_1_0, slot, stride);
    if (array_checked != GF_STATUS_OK) {
        return array_checked;
    }
    std::uint64_t index = 0U;
    for (index = 0U; index < value.element_count; ++index) {
        gf_value element{};
        std::memcpy(&element, &element_at(value.elements, stride, index), value_size_1_0);
        if (element.kind != target.element_kind) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a fixed-size array's element is not of its element kind");
        }
        const gf_status element_checked = check_written(element, slot);
        if (element_checked != GF_STATUS_OK) {
            return element_checked;
        }
        if (target.element_accepts != nullptr && !target.element_accepts(element)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an element is out of the array's range or choices");
        }
    }
    return GF_STATUS_OK;
}

void EventFields::take_written_elements(const Field& target, gf_value& value) noexcept {
    std::vector<gf_value>& run = element_runs_[target.run];
    const std::uint32_t stride = run.empty() ? value_size_1_0 : (*value.elements).struct_size;
    std::size_t index = 0U;
    for (index = 0U; index < run.size(); ++index) {
        // Through a local copy: the caller may pass back the records this
        // field lent, so source and destination can be the same record.
        gf_value element{};
        std::memcpy(&element, &element_at(value.elements, stride, index), value_size_1_0);
        element.struct_size = value_size_1_0;
        element.text.data = nullptr;
        element.text.size = 0U;
        element.elements = nullptr;
        element.element_count = 0U;
        run[index] = element;
    }
    value.unsigned_integer = static_cast<std::uint64_t>(run.size());
    value.elements = run.empty() ? nullptr : run.data();
    value.element_count = static_cast<std::uint64_t>(run.size());
}

gf_status EventFields::element(std::uint64_t field, std::uint64_t index, gf_value* out, SlotIdentity slot) {
    if (field >= fields_.size()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the event has no field of that index");
    }
    const Field& source = fields_[static_cast<std::size_t>(field)];
    if (source.fixed_count) {
        // A fixed-size array: its lent records, as written when written.
        if (index >= source.count) {
            return fail(GF_STATUS_NOT_FOUND, slot, "the collection has no element of that index");
        }
        const gf_status record_checked = check_record(out, slot);
        if (record_checked != GF_STATUS_OK) {
            return record_checked;
        }
        write_record(element_runs_[source.run][static_cast<std::size_t>(index)], out);
        return GF_STATUS_OK;
    }
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

void fill_value(gf_value& record, Point value) noexcept {
    record.point = gf_logical_point{value.x, value.y};
}

void fill_value(gf_value& record, Size value) noexcept {
    record.size = gf_logical_size{value.width, value.height};
}

void fill_value(gf_value& record, const Rect& value) noexcept {
    record.rectangle = gf_logical_rect{value.x, value.y, value.width, value.height};
}

void fill_value(gf_value& record, const Insets& value) noexcept {
    record.insets = gf_logical_insets{value.left, value.top, value.right, value.bottom};
}

void fill_value(gf_value& record, Color value) noexcept {
    record.color = packed_color(value);
}

void append_field(EventFields& fields, std::string_view name, bool value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_BOOLEAN, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    record.boolean = value ? 1U : 0U;
}

void append_field(EventFields& fields, std::string_view name, const std::string& value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_TEXT, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    // Valid only during the callback (EventFields::lend_text).
    const std::string_view lent = fields.lend_text(value);
    record.text.data = lent.data();
    record.text.size = lent.size();
}

void append_field(EventFields& fields, std::string_view name, std::string_view value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_TEXT, 0U, nullptr);
    // Valid only during the callback (EventFields::lend_text).
    const std::string_view lent = fields.lend_text(value);
    record.text.data = lent.data();
    record.text.size = lent.size();
}

void append_field(EventFields& fields, std::string_view name, Point value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_POINT, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    fill_value(record, value);
}

void append_field(EventFields& fields, std::string_view name, Size value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_SIZE, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    fill_value(record, value);
}

void append_field(EventFields& fields, std::string_view name, const Rect& value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_RECTANGLE, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    fill_value(record, value);
}

void append_field(EventFields& fields, std::string_view name, const Insets& value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_INSETS, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    fill_value(record, value);
}

void append_field(EventFields& fields, std::string_view name, Color value) {
    gf_value& record = fields.append(name, GF_VALUE_KIND_COLOR, GF_EVENT_FIELD_WRITABLE, &accepts_any);
    fill_value(record, value);
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
    // Valid only during the callback (EventFields::lend_text).
    const std::string_view lent = fields.lend_text(text);
    record.text.data = lent.data();
    record.text.size = lent.size();
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

void append_field(EventFields& fields, std::string_view name, const ThemeCompileResult& value) {
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

void take_value(const gf_value& record, Point& value) noexcept {
    value = Point{record.point.x, record.point.y};
}

void read_field(EventFields& fields, std::string_view name, Point& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        take_value(written, value);
    }
}

void take_value(const gf_value& record, Size& value) noexcept {
    value = Size{record.size.width, record.size.height};
}

void read_field(EventFields& fields, std::string_view name, Size& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        take_value(written, value);
    }
}

void take_value(const gf_value& record, Rect& value) noexcept {
    value = Rect{record.rectangle.x, record.rectangle.y, record.rectangle.width, record.rectangle.height};
}

void read_field(EventFields& fields, std::string_view name, Rect& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        take_value(written, value);
    }
}

void take_value(const gf_value& record, Insets& value) noexcept {
    value = Insets{record.insets.left, record.insets.top, record.insets.right, record.insets.bottom};
}

void read_field(EventFields& fields, std::string_view name, Insets& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        take_value(written, value);
    }
}

void take_value(const gf_value& record, Color& value) noexcept {
    value = unpacked_color(record.color);
}

void read_field(EventFields& fields, std::string_view name, Color& value) {
    gf_value written{};
    if (fields.written(name, written)) {
        take_value(written, value);
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
