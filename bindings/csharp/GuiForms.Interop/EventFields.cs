// The fields of a lent event read by name, and a fixed-size array field's
// elements (gf_abi.h, "Event fields", since 1.5): a COLLECTION whose N
// elements the library lends in place, N contiguous gf_value records of the
// element kind listed in elements and element_count. Like the event, the
// fields and their records are valid only during the callback's invoke:
// what these readers copy out may be kept, the pointers may not.
using System;

namespace GuiForms.Interop;

public static unsafe class EventFields
{
    // The field of the event whose full (dotted) name is name, or null.
    public static gf_event_field* named(gf_event* @event, ReadOnlySpan<byte> name)
    {
        if (@event == null || (*@event).fields == null)
        {
            return null;
        }
        for (ulong index = 0; index < (*@event).field_count; ++index)
        {
            gf_event_field* field = &(*@event).fields[index];
            ReadOnlySpan<byte> held = new ReadOnlySpan<byte>((*field).name.data, checked((int)(*field).name.size));
            if (held.SequenceEqual(name))
            {
                return field;
            }
        }
        return null;
    }

    // The element count of a fixed-size array field, or -1 when the field is
    // not one (not a COLLECTION, or one whose elements are not lent in place,
    // which events.field_element reads instead).
    public static long fixed_count(gf_event_field* field)
    {
        if (field == null || (*field).kind != Constants.GF_VALUE_KIND_COLLECTION || (*field).value == null)
        {
            return -1;
        }
        gf_value* value = (*field).value;
        if ((*value).kind != Constants.GF_VALUE_KIND_COLLECTION)
        {
            return -1;
        }
        if ((*value).element_count != 0u && (*value).elements == null)
        {
            return -1;
        }
        if ((*value).elements == null)
        {
            // No lent elements: an array of none, or an ordinary collection
            // with a count of its own.
            return (*value).unsigned_integer == 0u ? 0 : -1;
        }
        return checked((long)(*value).element_count);
    }

    // Copies a fixed-size array field of NUMBER elements into into: true
    // when the field lends exactly into.Length elements, all NUMBER; into is
    // left unchanged otherwise.
    public static bool read_numbers(gf_event_field* field, Span<double> into)
    {
        long count = fixed_count(field);
        if (count != into.Length)
        {
            return false;
        }
        gf_value* elements = (*(*field).value).elements;
        for (int index = 0; index < into.Length; ++index)
        {
            if (elements[index].kind != Constants.GF_VALUE_KIND_NUMBER)
            {
                return false;
            }
        }
        for (int index = 0; index < into.Length; ++index)
        {
            into[index] = elements[index].number;
        }
        return true;
    }
}
