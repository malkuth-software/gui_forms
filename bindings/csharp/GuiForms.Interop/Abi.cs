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
    private static bool negotiated;
    private static gf_runtime_table runtime_table;
    private static gf_objects_table objects_table;
    private static gf_properties_table properties_table;
    private static gf_events_table events_table;
    private static gf_dispatch_table dispatch_table;
    private static gf_diagnostics_table diagnostics_table;
    private static gf_windows_table windows_table;

    public static gf_runtime_table runtime => tables().runtime;
    public static gf_objects_table objects => tables().objects;
    public static gf_properties_table properties => tables().properties;
    public static gf_events_table events => tables().events;
    public static gf_dispatch_table dispatch => tables().dispatch;
    public static gf_diagnostics_table diagnostics => tables().diagnostics;
    public static gf_windows_table windows => tables().windows;

    private readonly struct Tables
    {
        public readonly gf_runtime_table runtime;
        public readonly gf_objects_table objects;
        public readonly gf_properties_table properties;
        public readonly gf_events_table events;
        public readonly gf_dispatch_table dispatch;
        public readonly gf_diagnostics_table diagnostics;
        public readonly gf_windows_table windows;

        public Tables(gf_runtime_table runtime, gf_objects_table objects, gf_properties_table properties,
                      gf_events_table events, gf_dispatch_table dispatch, gf_diagnostics_table diagnostics,
                      gf_windows_table windows)
        {
            this.runtime = runtime;
            this.objects = objects;
            this.properties = properties;
            this.events = events;
            this.dispatch = dispatch;
            this.diagnostics = diagnostics;
            this.windows = windows;
        }
    }

    // Negotiates every table on first use; a refusal throws and leaves nothing negotiated.
    private static Tables tables()
    {
        lock (negotiation_lock)
        {
            if (!negotiated)
            {
                negotiate(Exports.get_runtime_table(out gf_runtime_table runtime), "runtime");
                negotiate(Exports.get_objects_table(out gf_objects_table objects), "objects");
                negotiate(Exports.get_properties_table(out gf_properties_table properties), "properties");
                negotiate(Exports.get_events_table(out gf_events_table events), "events");
                negotiate(Exports.get_dispatch_table(out gf_dispatch_table dispatch), "dispatch");
                negotiate(Exports.get_diagnostics_table(out gf_diagnostics_table diagnostics), "diagnostics");
                negotiate(Exports.get_windows_table(out gf_windows_table windows), "windows");
                runtime_table = runtime;
                objects_table = objects;
                properties_table = properties;
                events_table = events;
                dispatch_table = dispatch;
                diagnostics_table = diagnostics;
                windows_table = windows;
                negotiated = true;
            }
            return new Tables(runtime_table, objects_table, properties_table, events_table, dispatch_table,
                              diagnostics_table, windows_table);
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
