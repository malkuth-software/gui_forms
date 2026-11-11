// Native exceptions reflected into managed code (C ABI 1.2). With exception
// reflection on (NativeException.reflection), an exception inside the
// library that reaches a C ABI function's boundary refuses the call with
// GF_STATUS_NATIVE_EXCEPTION. Abi.check then reads the thread's failure
// snapshot, as copies (the frames, the site and the message), and throws a
// NativeException carrying it, through NativeFrames: one named method per
// shadow frame, outermost first, so the exception's stack trace and a
// debugger's call stack read the library's logical frames between the
// managed caller and the throw. Off (the library's default), the same
// failure is GF_STATUS_INTERNAL, a plain GuiFormsException.
//
// The fail-fast boundary (a native callback entry such as a window
// procedure) ends the process either way; its report prints the frames.
using System;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Text;

namespace GuiForms.Interop;

/// <summary>
/// An exception inside the library that refused a call under exception
/// reflection: its text, the boundary that caught it, and the shadow frames
/// it was thrown under (outermost first, the frames it unwound included).
/// </summary>
public sealed unsafe class NativeException : GuiFormsException
{
    public ShadowFrame[] frames { get; }
    /// <summary>The boundary that caught it: "c_abi" for a function of the tables.</summary>
    public string site { get; }
    /// <summary>The refused function's table and slot.</summary>
    public gf_table_id table_id { get; }
    public uint slot_index { get; }
    /// <summary>The thread's snapshot count; each failure has its own.</summary>
    public ulong sequence { get; }
    /// <summary>The frames open where it was thrown, recorded or not.</summary>
    public uint depth { get; }

    private NativeException(gf_status status, string message, ShadowFrame[] frames, string site, gf_table_id table_id,
                            uint slot_index, ulong sequence, uint depth)
        : base(status, message)
    {
        this.frames = frames;
        this.site = site;
        this.table_id = table_id;
        this.slot_index = slot_index;
        this.sequence = sequence;
        this.depth = depth;
        // Logs that print only Message and Data still show the path.
        Data["shadow_frames"] = describe(frames);
    }

    /// <summary>
    /// The library's exception reflection, a process switch, off at start. On,
    /// a native exception reaches managed callers as a NativeException; off,
    /// as a GuiFormsException with GF_STATUS_INTERNAL.
    /// </summary>
    public static bool reflection
    {
        get
        {
            uint enabled = 0;
            Abi.check_status(Abi.diagnostics.exception_reflection(&enabled));
            return enabled == 1u;
        }
        set
        {
            Abi.check_status(Abi.diagnostics.set_exception_reflection(value ? 1u : 0u));
        }
    }

    /// <summary>
    /// The calling thread's failure snapshot as an exception for status. A
    /// snapshot that cannot be read, or that is not this failure's, gives
    /// the error detail with no frames.
    /// </summary>
    internal static NativeException read(gf_status status)
    {
        // The error detail first: a failed snapshot read would replace it.
        string detail = Abi.error_message();
        const int capacity = (int)Constants.GF_SHADOW_STACK_CAPACITY;
        const int message_capacity = (int)Constants.GF_FAILURE_MESSAGE_CAPACITY;
        gf_shadow_frame* native_frames = stackalloc gf_shadow_frame[capacity];
        byte* message = stackalloc byte[message_capacity];
        gf_failure_snapshot snapshot = default;
        snapshot.struct_size = (uint)sizeof(gf_failure_snapshot);
        ulong frames_required = 0;
        ulong message_required = 0;
        gf_status read = Abi.diagnostics.failure_snapshot(&snapshot, native_frames, (ulong)capacity, &frames_required,
                                                          message, (ulong)message_capacity, &message_required);
        if (read != Constants.GF_STATUS_OK || snapshot.status != status)
        {
            return new NativeException(status, detail, Array.Empty<ShadowFrame>(), string.Empty, 0u, 0u, 0ul, 0u);
        }
        int count = (int)Math.Min(frames_required, (ulong)capacity);
        ShadowFrame[] frames = new ShadowFrame[count];
        for (int index = 0; index < count; ++index)
        {
            frames[index] = ShadowFrame.from_native(native_frames + index);
        }
        int site_size = (int)Math.Min(snapshot.site_size, Constants.GF_FAILURE_SITE_CAPACITY);
        string site = Encoding.UTF8.GetString(snapshot.site, site_size);
        int message_size = (int)Math.Min(message_required, (ulong)message_capacity);
        string text = Encoding.UTF8.GetString(message, message_size);
        return new NativeException(status, text, frames, site, snapshot.table_id, snapshot.slot_index,
                                   snapshot.sequence, snapshot.depth);
    }

    // One line per frame, outermost first.
    private static string describe(ShadowFrame[] frames)
    {
        StringBuilder text = new StringBuilder();
        for (int index = 0; index < frames.Length; ++index)
        {
            text.Append('#').Append(index).Append(' ').Append(frames[index].ToString()).Append('\n');
        }
        return text.ToString();
    }
}

/// <summary>
/// The named thunks a NativeException is thrown through: each shadow frame,
/// outermost first, is one call of the method named for its kind, and the
/// innermost throws. The methods do nothing else; NoInlining and the
/// statement after each call keep every frame on the stack (no inlining, no
/// tail call), so the stack trace lists them innermost first, as frames
/// always are.
/// </summary>
internal static class NativeFrames
{
    [DoesNotReturn]
    [StackTraceHidden]
    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static void raise(NativeException exception)
    {
        enter(exception, 0);
        throw exception;
    }

    // Calls the method named for frame index's kind, or throws past the
    // innermost frame.
    [DebuggerHidden]
    [StackTraceHidden]
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void enter(NativeException exception, int index)
    {
        if (index >= exception.frames.Length)
        {
            throw exception;
        }
        switch (exception.frames[index].kind)
        {
        case Constants.GF_SHADOW_FRAME_KIND_LIBRARY_CALL:
            library_call(exception, index);
            break;
        case Constants.GF_SHADOW_FRAME_KIND_INPUT_DISPATCH:
            input_dispatch(exception, index);
            break;
        case Constants.GF_SHADOW_FRAME_KIND_EVENT_CALLBACK:
            event_callback(exception, index);
            break;
        case Constants.GF_SHADOW_FRAME_KIND_LAYOUT_PASS:
            layout_pass(exception, index);
            break;
        case Constants.GF_SHADOW_FRAME_KIND_PAINT_PASS:
            paint_pass(exception, index);
            break;
        case Constants.GF_SHADOW_FRAME_KIND_POSTED_CALLBACK:
            posted_callback(exception, index);
            break;
        default:
            unknown_frame(exception, index);
            break;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void library_call(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void input_dispatch(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void event_callback(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void layout_pass(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void paint_pass(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void posted_callback(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }

    // A kind this binding does not know (a later library's).
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void unknown_frame(NativeException exception, int index)
    {
        enter(exception, index + 1);
        GC.KeepAlive(exception);
    }
}
