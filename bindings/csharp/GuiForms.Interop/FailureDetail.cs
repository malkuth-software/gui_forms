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

    private FailureDetail(ulong sequence, string exception_type, ulong[] trace)
    {
        this.sequence = sequence;
        this.exception_type = exception_type;
        this.trace = trace;
    }

    // The diagnostics table's size once it has failure_detail (1.5).
    private const uint diagnostics_size_1_5 = 80u;

    /// <summary>
    /// The calling thread's failure detail when it is the detail of a failure
    /// refused with status and holds a type or a trace; otherwise null.
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
            detail.sequence == 0ul || (detail.type_size == 0u && detail.trace_count == 0u))
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
        return new FailureDetail(detail.sequence, type, trace);
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
