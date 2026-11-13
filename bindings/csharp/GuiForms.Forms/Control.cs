// Control's members that are not class-table hooks: where the pointer is
// (windows.pointer_location, since ABI 1.3), which OnMouseEnter and
// OnMouseLeave read for the crossing, since they carry Windows Forms'
// plain EventArgs. The values are the C++ PointerLocation's
// (include/gui_forms/events/mouse_events/mouse_events.hpp).
//
// Lifetime: each read calls the library on the UI thread and copies the
// values; nothing is kept.
using System;
using GuiForms.Interop;

namespace GuiForms.Forms;

/// <summary>
/// Where the pointer is, as a window last saw it: the position of the last
/// pointer event the window dispatched (the event being dispatched, during
/// a hook), in the control's coordinates and the window's, and whether the
/// pointer is in the window. All zero before the window has seen a pointer
/// event.
/// </summary>
public readonly struct PointerLocation
{
    public PointerLocation(gf_logical_point location, gf_logical_point window_position, bool inside)
    {
        this.location = location;
        this.window_position = window_position;
        this.inside = inside;
    }

    /// <summary>The position in the control's coordinates.</summary>
    public gf_logical_point location { get; }

    /// <summary>The position in the window's coordinates.</summary>
    public gf_logical_point window_position { get; }

    /// <summary>Whether the pointer is in the window.</summary>
    public bool inside { get; }
}

public partial class Control
{
    /// <summary>
    /// Where the pointer is, as this control's window last saw it
    /// (windows.pointer_location). During OnMouseLeave and OnMouseEnter it is
    /// where the crossing happened. Throws GuiFormsException with
    /// GF_STATUS_INVALID_STATE for a control not in a window, and with
    /// GF_STATUS_UNSUPPORTED_VERSION for a library older than ABI 1.3.
    /// </summary>
    public unsafe PointerLocation pointer_location
    {
        get
        {
            if (Abi.windows.pointer_location == null)
            {
                Abi.check(Constants.GF_STATUS_UNSUPPORTED_VERSION);
            }
            gf_logical_point location = default;
            gf_logical_point window_position = default;
            uint inside = 0;
            Abi.check(Abi.windows.pointer_location(handle.value, &location, &window_position, &inside));
            PointerLocation read = new PointerLocation(location, window_position, inside != 0);
            return read;
        }
    }
}
