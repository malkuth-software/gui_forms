// Relative pointer mode from managed code (C++ gui_forms/events/
// relative_pointer/relative_pointer.hpp; the C ABI's gf_controls_table slots
// since 1.5). RelativePointer asks a control's window for the pointer's
// motion as deltas; RelativePointerEventArgs and RelativePointerEnded read
// the control's relative_pointer and relative_pointer_ended events, whose
// fields the interaction catalog names (manifests/abi/interaction_fields.json).
//
// Lifetime: the native event is lent for the handler's call. from_native
// reads every value at once, so the object may outlive the call; handled
// writes the native field only while the entry runs, as MouseEventArgs.
using System;
using GuiForms.Interop;

namespace GuiForms.Forms;

/// <summary>The modes, as gf_relative_pointer_mode.</summary>
public enum RelativePointerMode : uint
{
    /// <summary>The ordinary pointer, shown and clicking, with every motion sample as a delta besides.</summary>
    free = Constants.GF_RELATIVE_POINTER_MODE_FREE,
    /// <summary>Kept inside the control and the window's release regions; buttons and the wheel are samples.</summary>
    confined = Constants.GF_RELATIVE_POINTER_MODE_CONFINED,
    /// <summary>Hidden and held still; only deltas arrive.</summary>
    locked = Constants.GF_RELATIVE_POINTER_MODE_LOCKED,
}

/// <summary>What a request got: gf_relative_pointer_grant.</summary>
public readonly struct RelativePointerGrant
{
    public RelativePointerGrant(uint result, RelativePointerMode mode, bool raw, bool cursor_hidden, bool host_relative)
    {
        this.result = result;
        this.mode = mode;
        this.raw = raw;
        this.cursor_hidden = cursor_hidden;
        this.host_relative = host_relative;
    }

    /// <summary>A gf_relative_pointer_result value.</summary>
    public uint result { get; }
    public RelativePointerMode mode { get; }
    public bool raw { get; }
    public bool cursor_hidden { get; }
    public bool host_relative { get; }

    /// <summary>Granted, as asked or below it.</summary>
    public bool accepted => result == Constants.GF_RELATIVE_POINTER_RESULT_GRANTED ||
                            result == Constants.GF_RELATIVE_POINTER_RESULT_DEGRADED;
}

/// <summary>The control-side calls of relative pointer mode.</summary>
public static unsafe class RelativePointer
{
    /// <summary>
    /// Asks control's window for relative pointer mode. Escape, focus leaving
    /// the control, the window's deactivation, a dialog or menu and the
    /// control's disposal always end it.
    /// </summary>
    public static RelativePointerGrant request(ObjectHandle control, RelativePointerMode mode,
                                               bool hide_cursor = false, bool raw_motion = false,
                                               bool until_release = false)
    {
        gf_relative_pointer_request asked = default;
        asked.struct_size = (uint)sizeof(gf_relative_pointer_request);
        asked.mode = (uint)mode;
        asked.hide_cursor = hide_cursor ? 1u : 0u;
        asked.raw_motion = raw_motion ? 1u : 0u;
        asked.until_release = until_release ? 1u : 0u;
        gf_relative_pointer_grant granted = default;
        granted.struct_size = (uint)sizeof(gf_relative_pointer_grant);
        Abi.check(Abi.controls.request_relative_pointer(control.value, &asked, &granted));
        RelativePointerGrant result = new RelativePointerGrant(granted.result, (RelativePointerMode)granted.mode,
                                                               granted.raw != 0u, granted.cursor_hidden != 0u,
                                                               granted.host_relative != 0u);
        return result;
    }

    /// <summary>Ends control's own mode; false when it had none.</summary>
    public static bool release(ObjectHandle control)
    {
        uint released = 0;
        Abi.check(Abi.controls.release_relative_pointer(control.value, &released));
        return released != 0u;
    }

    /// <summary>Moves the pointer to position, in control's units, where the host can.</summary>
    public static bool warp(ObjectHandle control, gf_logical_point position)
    {
        uint warped = 0;
        Abi.check(Abi.controls.warp_pointer(control.value, position, &warped));
        return warped != 0u;
    }

    /// <summary>
    /// Sets the release regions of control's window (window units, at most
    /// 8): over one, a confined or locked mode is suspended and the ordinary
    /// pointer reaches it. The shell's capsule is one.
    /// </summary>
    public static void set_release_regions(ObjectHandle control, ReadOnlySpan<gf_logical_rect> regions)
    {
        fixed (gf_logical_rect* first = regions)
        {
            Abi.check(Abi.controls.set_pointer_release_regions(control.value, first, (uint)regions.Length));
        }
    }

    /// <summary>The window's relative pointer state as control sees it.</summary>
    public static gf_relative_pointer_state state(ObjectHandle control)
    {
        gf_relative_pointer_state state = default;
        state.struct_size = (uint)sizeof(gf_relative_pointer_state);
        Abi.check(Abi.controls.relative_pointer_state(control.value, &state));
        return state;
    }
}

/// <summary>One sample: the C++ RelativePointerEventArgs.</summary>
public sealed unsafe class RelativePointerEventArgs : EventArgs
{
    private readonly gf_event* native;
    private readonly ulong handled_field;
    private readonly bool has_handled_field;
    private bool handled_value;

    private RelativePointerEventArgs(gf_event* native)
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
            string name = EventFields.last_segment((*field).name);
            switch (name)
            {
            case "kind":
                kind = EventFields.integer_of(value);
                break;
            case "delta":
                delta = (*value).point;
                break;
            case "position":
                position = (*value).point;
                break;
            case "button":
                button = EventFields.integer_of(value);
                break;
            case "wheel_delta":
                wheel_delta = (*value).point;
                break;
            case "wheel_unit":
                wheel_unit = EventFields.integer_of(value);
                break;
            case "modifiers":
                modifiers = EventFields.integer_of(value);
                break;
            case "mode":
                mode = EventFields.integer_of(value);
                break;
            case "timestamp_nanoseconds":
                timestamp_nanoseconds = (ulong)EventFields.integer_of(value);
                break;
            case "unaccelerated":
                unaccelerated = (*value).boolean != 0u;
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

    /// <summary>Reads the sample from the event a handler received.</summary>
    public static RelativePointerEventArgs from_native(gf_event* native)
    {
        return new RelativePointerEventArgs(native);
    }

    /// <summary>The C++ RelativePointerSampleKind's number: motion, button_down, button_up, wheel.</summary>
    public long kind { get; }
    /// <summary>The motion in logical units, fractions kept; zero for a button or wheel sample.</summary>
    public gf_logical_point delta { get; }
    /// <summary>The pointer in the control's units (a locked pointer's virtual position).</summary>
    public gf_logical_point position { get; }
    public long button { get; }
    public gf_logical_point wheel_delta { get; }
    public long wheel_unit { get; }
    public long modifiers { get; }
    /// <summary>The mode in force (the C++ RelativePointerMode's number).</summary>
    public long mode { get; }
    /// <summary>The host's time for the sample, as MouseEventArgs.timestamp_nanoseconds.</summary>
    public ulong timestamp_nanoseconds { get; }
    /// <summary>The delta is the device's raw motion, not the accelerated one.</summary>
    public bool unaccelerated { get; }

    /// <summary>Set by a handler; read back by the library. Ends nothing.</summary>
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
}

/// <summary>Why a mode ended: the C++ RelativePointerEnded.</summary>
public sealed unsafe class RelativePointerEnded : EventArgs
{
    private RelativePointerEnded(gf_event* native)
    {
        if (native == null)
        {
            return;
        }
        for (ulong index = 0; index < (*native).field_count; ++index)
        {
            gf_event_field* field = &(*native).fields[index];
            if ((*field).value != null && EventFields.last_segment((*field).name) == "reason")
            {
                reason = (uint)EventFields.integer_of((*field).value);
            }
        }
    }

    public static RelativePointerEnded from_native(gf_event* native)
    {
        return new RelativePointerEnded(native);
    }

    /// <summary>A gf_relative_pointer_end value.</summary>
    public uint reason { get; }
}

// The readers MouseEventArgs keeps privately, for these events' fields.
internal static unsafe class EventFields
{
    public static long integer_of(gf_value* value)
    {
        if ((*value).kind == Constants.GF_VALUE_KIND_UNSIGNED)
        {
            return (long)(*value).unsigned_integer;
        }
        return (*value).signed_integer;
    }

    public static string last_segment(gf_utf8_view name)
    {
        string text = name.data == null ? "" : System.Text.Encoding.UTF8.GetString(name.data, (int)name.size);
        int dot = text.LastIndexOf('.');
        return dot < 0 ? text : text.Substring(dot + 1);
    }
}
