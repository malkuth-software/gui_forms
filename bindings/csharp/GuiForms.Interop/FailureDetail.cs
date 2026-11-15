// The native detail of a refused call (C ABI 1.5, survey S4 = 1): the C++
// exception's type and the native trace where it was thrown, which a
// development build of the library keeps for the thread's failure snapshot.
// Abi.check attaches it to the GuiFormsException (or NativeException) it
// throws, so the parent developer reads the whole record where the failure
// surfaces. A release build of the library keeps neither, and an older
// library (before 1.5) has no detail to read: native_detail is then null.
using System;
using System.Text;

namespace GuiForms.Interop;

/// <summary>
/// What a development build of the library knew of a failure beyond its
/// frames: the exception's C++ type (demangled where the runtime can) and
/// the return addresses of the native stack at the throw, innermost first.
/// </summary>
public sealed class FailureDetail
{
    /// <summary>The snapshot's count; the detail of this failure only.</summary>
    public ulong sequence { get; }
    /// <summary>The C++ type, such as "std::runtime_error"; empty when not kept.</summary>
    public string exception_type { get; }
    /// <summary>Addresses in the library's process, innermost first.</summary>
    public ulong[] trace { get; }
    /// <summary>
    /// The library parked the failure and rethrew it later, and the park
    /// could not keep its own copy of the frames (its allocation was refused,
    /// out of memory included): the frames are what the thread still held,
    /// possibly none. Provisional, as gf_failure_detail's flags.
    /// </summary>
    public bool frames_lost_at_park { get; }

    private FailureDetail(ulong sequence, string exception_type, ulong[] trace, bool frames_lost_at_park)
    {
        this.sequence = sequence;
        this.exception_type = exception_type;
        this.trace = trace;
        this.frames_lost_at_park = frames_lost_at_park;
    }

    // The diagnostics table's size once it has failure_detail (1.5).
    private const uint diagnostics_size_1_5 = 80u;

    /// <summary>
    /// The calling thread's failure detail when it is the detail of a failure
    /// refused with status and holds a type, a trace or a flag; otherwise
    /// null. An older library leaves flags zero (the record starts zeroed).
    /// </summary>
    internal static unsafe FailureDetail? read(gf_status status)
    {
        gf_diagnostics_table diagnostics = Abi.diagnostics;
        if (diagnostics.struct_size < diagnostics_size_1_5)
        {
            return null;
        }
        gf_failure_detail detail = default;
        detail.struct_size = (uint)sizeof(gf_failure_detail);
        if (diagnostics.failure_detail(&detail) != Constants.GF_STATUS_OK || detail.status != status ||
            detail.sequence == 0ul)
        {
            return null;
        }
        bool frames_lost_at_park = (detail.flags & Constants.GF_FAILURE_DETAIL_FLAGS_FRAMES_LOST_AT_PARK) != 0u;
        if (detail.type_size == 0u && detail.trace_count == 0u && !frames_lost_at_park)
        {
            return null;
        }
        int type_size = (int)Math.Min(detail.type_size, Constants.GF_FAILURE_TYPE_CAPACITY);
        string type = Encoding.UTF8.GetString(detail.type, type_size);
        int count = (int)Math.Min(detail.trace_count, Constants.GF_FAILURE_TRACE_CAPACITY);
        ulong[] trace = new ulong[count];
        for (int index = 0; index < count; ++index)
        {
            trace[index] = detail.trace[index];
        }
        return new FailureDetail(detail.sequence, type, trace, frames_lost_at_park);
    }

    /// <summary>
    /// Attaches the thread's detail for status to refused, also as Data
    /// entries, so logs that print only Message and Data show it.
    /// </summary>
    internal static void attach(GuiFormsException refused, gf_status status)
    {
        FailureDetail? detail = read(status);
        if (detail == null)
        {
            return;
        }
        refused.native_detail = detail;
        if (detail.exception_type.Length != 0)
        {
            refused.Data["native_exception_type"] = detail.exception_type;
        }
        if (detail.trace.Length != 0)
        {
            refused.Data["native_trace"] = describe(detail.trace);
        }
        if (detail.frames_lost_at_park)
        {
            refused.Data["native_frames_lost_at_park"] = true;
        }
    }

    // One address per line, innermost first.
    private static string describe(ulong[] trace)
    {
        StringBuilder text = new StringBuilder();
        for (int index = 0; index < trace.Length; ++index)
        {
            text.Append('#').Append(index).Append(" 0x").Append(trace[index].ToString("x")).Append('\n');
        }
        return text.ToString();
    }
}
