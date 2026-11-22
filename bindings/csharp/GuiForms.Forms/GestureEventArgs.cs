// The arguments of the OnGesture hook: the C++ GestureEventArgs
// (include/gui_forms/events/gesture_events/gesture_events.hpp), a touchpad
// gesture (pinch to magnify, rotate, smart magnify, swipe) as one transform
// since the gesture's previous event: scale (multiplied), rotation_degrees
// (counter-clockwise, added) and translation (added), about the focal point.
// The values are read from the fields of the native event the library lent
// the hook entry, by their last name segment; a field the event does not
// carry keeps its identity value (scale 1, the rest zero).
//
// Lifetime: the native event is lent for the entry's call. from_native reads
// every value at once, so the object may outlive the call; handled writes the
// native field only while the entry runs (events.write_field, which the
// library applies to the C++ arguments when the hook returns: a gesture
// handled there ends its route). Set after the call, or on an object managed
// code made, it changes only this object.
using System;
using GuiForms.Interop;

namespace GuiForms.Forms;

/// <summary>The gesture hook's arguments: the C++ GestureEventArgs.</summary>
public sealed unsafe class GestureEventArgs : EventArgs
{
    // The lent event and its handled field's index while the entry runs;
    // null and no index for an object managed code made.
    private readonly gf_event* native;
    private readonly ulong handled_field;
    private readonly bool has_handled_field;
    private bool handled_value;

    /// <summary>Identity arguments, for an OnGesture called directly by managed code.</summary>
    public GestureEventArgs()
    {
        native = null;
        scale = 1.0;
    }

    private GestureEventArgs(gf_event* native)
    {
        this.native = native;
        scale = 1.0;
        if (native == null)
        {
            return;
        }
        for (ulong index = 0; index < (*native).field_count; ++index)
        {
            gf_event_field* field = &(*native).fields[index];
            if ((*field).value == null)
            {
                continue;
            }
            gf_value* value = (*field).value;
            string name = last_segment((*field).name);
            switch (name)
            {
            case "kind":
                kind = integer_of(value);
                break;
            case "gesture_phase":
                gesture_phase = integer_of(value);
                break;
            case "location":
                location = (*value).point;
                break;
            case "window_position":
                window_position = (*value).point;
                break;
            case "scale":
                scale = (*value).number;
                break;
            case "rotation_degrees":
                rotation_degrees = (*value).number;
                break;
            case "translation":
                translation = (*value).point;
                break;
            case "modifiers":
                modifiers = integer_of(value);
                break;
            case "phase":
                phase = integer_of(value);
                break;
            case "timestamp_nanoseconds":
                timestamp_nanoseconds = (ulong)integer_of(value);
                break;
            case "handled":
                handled_value = (*value).boolean != 0u;
                handled_field = index;
                has_handled_field = ((*field).flags & Constants.GF_EVENT_FIELD_WRITABLE) != 0u;
                break;
            default:
                // A field a later library adds: not read here.
                break;
            }
        }
    }

    /// <summary>Reads the arguments from the event a hook entry received.</summary>
    public static GestureEventArgs from_native(gf_event* native)
    {
        return new GestureEventArgs(native);
    }

    /// <summary>The gesture (the C++ GestureKind's number): magnify, rotate, smart_magnify, swipe.</summary>
    public long kind { get; }

    /// <summary>
    /// The event's place in its gesture (the C++ GesturePhase's number):
    /// began, changed, ended, cancelled, momentum, momentum_ended.
    /// </summary>
    public long gesture_phase { get; }

    /// <summary>The focal point relative to the control.</summary>
    public gf_logical_point location { get; }

    /// <summary>The focal point in the window's coordinates.</summary>
    public gf_logical_point window_position { get; }

    /// <summary>The scale since the gesture's previous event, multiplied: 1 changes nothing.</summary>
    public double scale { get; }

    /// <summary>The turn since the gesture's previous event, in degrees, counter-clockwise.</summary>
    public double rotation_degrees { get; }

    /// <summary>The travel since the gesture's previous event, in logical units.</summary>
    public gf_logical_point translation { get; }

    /// <summary>The modifier keys held (the C++ Modifier flags).</summary>
    public long modifiers { get; }

    /// <summary>The route's phase (the C++ EventPhase's number): target, then bubble on the ancestors.</summary>
    public long phase { get; }

    /// <summary>The host's time for the event, in nanoseconds of the library's monotonic clock.</summary>
    public ulong timestamp_nanoseconds { get; }

    /// <summary>
    /// Whether the gesture is handled, which ends its route. Written to the
    /// lent event while the entry runs.
    /// </summary>
    public bool handled
    {
        get => handled_value;
        set
        {
            handled_value = value;
            write_boolean(has_handled_field, handled_field, value);
        }
    }

    // Writes a writable boolean field of the lent event while the entry runs;
    // otherwise only this object changes.
    private void write_boolean(bool has_field, ulong field, bool value)
    {
        if (native == null || !has_field || !HookFrame.lends(native))
        {
            return;
        }
        gf_value written = default;
        written.struct_size = (uint)sizeof(gf_value);
        written.kind = Constants.GF_VALUE_KIND_BOOLEAN;
        written.boolean = value ? 1u : 0u;
        Abi.check(Abi.events.write_field(native, field, &written));
    }

    private static long integer_of(gf_value* value)
    {
        if ((*value).kind == Constants.GF_VALUE_KIND_UNSIGNED)
        {
            return (long)(*value).unsigned_integer;
        }
        return (*value).signed_integer;
    }

    private static string last_segment(gf_utf8_view name)
    {
        string text = name.data == null ? "" : System.Text.Encoding.UTF8.GetString(name.data, (int)name.size);
        int dot = text.LastIndexOf('.');
        return dot < 0 ? text : text.Substring(dot + 1);
    }
}
