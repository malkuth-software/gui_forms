// The tables this layer uses, negotiated once per process, and the
// translation of a refused call into an exception. A status the caller
// expects (NOT_FOUND, BUFFER_TOO_SMALL) stays a status: only the caller knows
// which alternatives are expected, so check() is called where a failure is
// exceptional.
using System;
using System.Text;

namespace GuiForms.Interop;

public static unsafe class Abi
{
    private static readonly object negotiation_lock = new object();
    // The negotiated tables, published once (N72): a call reads this
    // reference without the lock and borrows the table in place (ref
    // readonly), so it neither locks nor copies eight tables per call. Null
    // until the first negotiation succeeds.
    private static volatile Tables? negotiated;

    public static ref readonly gf_runtime_table runtime => ref tables().runtime;
    public static ref readonly gf_objects_table objects => ref tables().objects;
    public static ref readonly gf_properties_table properties => ref tables().properties;
    public static ref readonly gf_events_table events => ref tables().events;
    public static ref readonly gf_dispatch_table dispatch => ref tables().dispatch;
    public static ref readonly gf_diagnostics_table diagnostics => ref tables().diagnostics;
    public static ref readonly gf_windows_table windows => ref tables().windows;
    public static ref readonly gf_bindings_table bindings => ref tables().bindings;

    // Immutable once built: the published reference is the only writer's
    // hand-off, so readers need no lock.
    private sealed class Tables
    {
        public readonly gf_runtime_table runtime;
        public readonly gf_objects_table objects;
        public readonly gf_properties_table properties;
        public readonly gf_events_table events;
        public readonly gf_dispatch_table dispatch;
        public readonly gf_diagnostics_table diagnostics;
        public readonly gf_windows_table windows;
        public readonly gf_bindings_table bindings;

        public Tables(gf_runtime_table runtime, gf_objects_table objects, gf_properties_table properties,
                      gf_events_table events, gf_dispatch_table dispatch, gf_diagnostics_table diagnostics,
                      gf_windows_table windows, gf_bindings_table bindings)
        {
            this.runtime = runtime;
            this.objects = objects;
            this.properties = properties;
            this.events = events;
            this.dispatch = dispatch;
            this.diagnostics = diagnostics;
            this.windows = windows;
            this.bindings = bindings;
        }
    }

    // The published tables, negotiating every table on first use under the
    // lock; a refusal throws and leaves nothing published, so a later call
    // tries again.
    private static Tables tables()
    {
        Tables? published = negotiated;
        if (published != null)
        {
            return published;
        }
        lock (negotiation_lock)
        {
            published = negotiated;
            if (published != null)
            {
                return published;
            }
            // The C ABI supports only 64-bit targets (gf_abi.h), and
            // ObjectHandle keeps a gf_object, a 64-bit identity, in its
            // IntPtr (N73): a 32-bit process is refused before any call.
            if (IntPtr.Size != 8)
            {
                throw new PlatformNotSupportedException("GUI.Forms supports 64-bit processes only");
            }
            negotiate(Exports.get_runtime_table(out gf_runtime_table runtime), "runtime");
            negotiate(Exports.get_objects_table(out gf_objects_table objects), "objects");
            negotiate(Exports.get_properties_table(out gf_properties_table properties), "properties");
            negotiate(Exports.get_events_table(out gf_events_table events), "events");
            negotiate(Exports.get_dispatch_table(out gf_dispatch_table dispatch), "dispatch");
            negotiate(Exports.get_diagnostics_table(out gf_diagnostics_table diagnostics), "diagnostics");
            negotiate(Exports.get_windows_table(out gf_windows_table windows), "windows");
            negotiate(Exports.get_bindings_table(out gf_bindings_table bindings), "bindings");
            Tables made = new Tables(runtime, objects, properties, events, dispatch, diagnostics, windows, bindings);
            negotiated = made;
            return made;
        }
    }

    private static void negotiate(gf_status status, string table)
    {
        if (status != Constants.GF_STATUS_OK)
        {
            throw new GuiFormsException(status, "the " + table + " table did not negotiate at ABI " +
                                                    Constants.GF_ABI_MAJOR + "." + Constants.GF_ABI_MINOR);
        }
    }

    /// <summary>
    /// Throws when status is not OK, with the failing thread's error detail
    /// and, in its Data["callback_failures"], any managed callback failures
    /// the call carried through, so they do not attach to a later, unrelated
    /// call; otherwise rethrows a managed callback's exception that the
    /// library's call carried through (CallbackFailure), so it surfaces at
    /// the call that ran the callback.
    /// </summary>
    public static void check(gf_status status)
    {
        if (status != Constants.GF_STATUS_OK)
        {
            GuiFormsException refused = new GuiFormsException(status, error_message());
            CallbackFailure[] carried = CallbackFailure.take_pending();
            if (carried.Length != 0)
            {
                refused.Data["callback_failures"] = carried;
            }
            throw refused;
        }
        CallbackFailure.rethrow_pending();
    }

    /// <summary>
    /// Throws when status is not OK, and leaves kept callback failures where
    /// they are: for reads (the shadow stack) that a failure must not interrupt.
    /// </summary>
    public static void check_status(gf_status status)
    {
        if (status != Constants.GF_STATUS_OK)
        {
            throw new GuiFormsException(status, error_message());
        }
    }

    /// <summary>The calling thread's last error message, or empty.</summary>
    public static string error_message()
    {
        gf_runtime_table runtime = tables().runtime;
        gf_error_detail detail = default;
        detail.struct_size = (uint)sizeof(gf_error_detail);
        ulong required = 0;
        gf_status status = runtime.error_detail(&detail, null, 0, &required);
        if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
        {
            return string.Empty;
        }
        if (required == 0)
        {
            return string.Empty;
        }
        byte[] message = new byte[checked((int)required)];
        fixed (byte* buffer = message)
        {
            status = runtime.error_detail(&detail, buffer, (ulong)message.Length, &required);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            return string.Empty;
        }
        int length = Array.IndexOf(message, (byte)0);
        if (length < 0)
        {
            length = message.Length;
        }
        return Encoding.UTF8.GetString(message, 0, length);
    }
}

/// <summary>A call the library refused: its status and the thread's error detail.</summary>
public sealed class GuiFormsException : Exception
{
    public gf_status status { get; }

    public GuiFormsException(gf_status status, string message)
        : base(message.Length == 0 ? "GUI.Forms status " + status : message)
    {
        this.status = status;
    }
}
