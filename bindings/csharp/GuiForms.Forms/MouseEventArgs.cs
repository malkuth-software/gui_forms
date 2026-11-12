// The arguments of the OnMouseDown, OnMouseUp, OnMouseMove, OnMouseWheel,
// OnMouseClick and OnMouseDoubleClick hooks: Windows Forms' MouseEventArgs
// (Button, Clicks, Location, Delta), with HandledMouseEventArgs' Handled,
// and the routed event's own fields. The values are the C++ MouseEventArgs's
// members (include/gui_forms/events/mouse_events/mouse_events.hpp), read from
// the fields of the native event the library lent the hook entry, by their
// last name segment; a field the event does not carry keeps its zero value.
//
// Lifetime: the native event is lent for the entry's call. from_native reads
// every value at once, so the object may outlive the call; handled writes the
// native field only while the entry runs (events.write_field, which the
// library applies to the C++ arguments when the hook returns: a MouseDown
// handled there ends the pointer event's route). Set after the call, or on an
// object managed code made, it changes only this object.
using System;
using GuiForms.Interop;

namespace GuiForms.Forms;

/// <summary>The mouse hooks' arguments: the C++ MouseEventArgs.</summary>
public sealed unsafe class MouseEventArgs : EventArgs
{
    // The lent event and its handled field's index while the entry runs;
    // null and no index for an object managed code made.
    private readonly gf_event* native;
    private readonly ulong handled_field;
    private readonly bool has_handled_field;
    private bool handled_value;

    /// <summary>Empty arguments, for an OnX called directly by managed code.</summary>
    public MouseEventArgs()
    {
        native = null;
    }

    private MouseEventArgs(gf_event* native)
    {
        this.native = native;
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
            case "button":
                button = integer_of(value);
                break;
            case "clicks":
                clicks = (uint)integer_of(value);
                break;
            case "location":
                location = (*value).point;
                break;
            case "window_position":
                window_position = (*value).point;
                break;
            case "wheel_delta":
                wheel_delta = (*value).point;
                break;
            case "modifiers":
                modifiers = integer_of(value);
                break;
            case "action":
                action = integer_of(value);
                break;
            case "wheel_unit":
                wheel_unit = integer_of(value);
                break;
            case "pointer_id":
                pointer_id = (uint)integer_of(value);
                break;
            case "phase":
                phase = integer_of(value);
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
    public static MouseEventArgs from_native(gf_event* native)
    {
        return new MouseEventArgs(native);
    }

    /// <summary>The button (the C++ PointerButton's number). Windows Forms' Button.</summary>
    public long button { get; }

    /// <summary>The press's click count; zero for a move or a wheel. Windows Forms' Clicks.</summary>
    public uint clicks { get; }

    /// <summary>The position relative to the control. Windows Forms' Location (X, Y).</summary>
    public gf_logical_point location { get; }

    /// <summary>The position in the window's coordinates.</summary>
    public gf_logical_point window_position { get; }

    /// <summary>The wheel's turn on each axis, in wheel_unit. Windows Forms' Delta (y).</summary>
    public gf_logical_point wheel_delta { get; }

    /// <summary>The modifier keys held (the C++ Modifier flags).</summary>
    public long modifiers { get; }

    /// <summary>What the pointer did (the C++ PointerAction's number).</summary>
    public long action { get; }

    /// <summary>The unit of wheel_delta (the C++ WheelDeltaUnit's number).</summary>
    public long wheel_unit { get; }

    /// <summary>The pointer's identity, as the host reported it.</summary>
    public uint pointer_id { get; }

    /// <summary>The route's phase (the C++ EventPhase's number): target for a raiser.</summary>
    public long phase { get; }

    /// <summary>
    /// Whether the event is handled, which ends its route (Windows Forms'
    /// HandledMouseEventArgs.Handled). Written to the lent event while the
    /// entry runs.
    /// </summary>
    public bool handled
    {
        get => handled_value;
        set
        {
            handled_value = value;
            if (native == null || !has_handled_field || !HookFrame.lends(native))
            {
                return;
            }
            gf_value written = default;
            written.struct_size = (uint)sizeof(gf_value);
            written.kind = Constants.GF_VALUE_KIND_BOOLEAN;
            written.boolean = value ? 1u : 0u;
            Abi.check(Abi.events.write_field(native, handled_field, &written));
        }
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
