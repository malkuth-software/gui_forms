// Managed callbacks behind gf_callback: an [UnmanagedCallersOnly] thunk, a
// GCHandle context the library releases through the callback's release slot,
// and the no-unwind rule. An exception never crosses into the library: the
// thunk catches it, takes a snapshot of the shadow frames it ran under (read
// before it returns, so the library's frames stay balanced) and keeps it for
// the thread; the call that ran the callback rethrows it on return
// (Abi.check, CallbackFailure.rethrow_pending).
//
// Lifetime: the GCHandle is allocated by to_native and freed by release,
// which the library calls exactly once when it lets go of the callback
// (disconnect, the source's disposal, or after a posted callback ran). A
// to_native whose callback the library refused must be freed by the caller
// with free_unregistered.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;

namespace GuiForms.Interop;

/// <summary>A managed event or posted-callback handler; event is null for a posted callback.</summary>
public unsafe delegate void EventCallback(gf_event* @event);

/// <summary>A managed exception that a callback threw, with the frames it was thrown under.</summary>
public sealed class CallbackFailure : Exception
{
    [ThreadStatic] private static List<CallbackFailure>? pending;

    public ShadowFrame[] frames { get; }

    private CallbackFailure(Exception inner, ShadowFrame[] frames)
        : base("a GUI.Forms callback threw: " + inner.Message, inner)
    {
        this.frames = frames;
    }

    internal static void record(Exception exception)
    {
        ShadowFrame[] frames;
        try
        {
            frames = ShadowStack.snapshot();
        }
        catch (GuiFormsException)
        {
            frames = Array.Empty<ShadowFrame>();
        }
        pending ??= new List<CallbackFailure>();
        pending.Add(new CallbackFailure(exception, frames));
    }

    /// <summary>The calling thread's kept failures, oldest first; the list is cleared.</summary>
    public static CallbackFailure[] take_pending()
    {
        if (pending == null || pending.Count == 0)
        {
            return Array.Empty<CallbackFailure>();
        }
        CallbackFailure[] taken = pending.ToArray();
        pending.Clear();
        return taken;
    }

    /// <summary>Rethrows the oldest kept failure (dropping the rest into its Data), if any.</summary>
    public static void rethrow_pending()
    {
        CallbackFailure[] taken = take_pending();
        if (taken.Length == 0)
        {
            return;
        }
        if (taken.Length > 1)
        {
            taken[0].Data["later_failures"] = taken[1..];
        }
        ExceptionDispatchInfo.Capture(taken[0]).Throw();
    }
}

public static unsafe class Callbacks
{
    /// <summary>A gf_callback that runs handler; the library owns its context from here.</summary>
    public static gf_callback to_native(EventCallback handler)
    {
        GCHandle context = GCHandle.Alloc(handler);
        gf_callback callback = default;
        callback.struct_size = (uint)sizeof(gf_callback);
        callback.invoke = &invoke;
        callback.release = &release;
        callback.context = (void*)GCHandle.ToIntPtr(context);
        return callback;
    }

    /// <summary>Frees the context of a callback the library refused (it never calls release then).</summary>
    public static void free_unregistered(in gf_callback callback)
    {
        GCHandle.FromIntPtr((IntPtr)callback.context).Free();
    }

    // Stepping in from the library lands in the handler, not in the thunk.
    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void invoke(void* context, gf_event* @event)
    {
        try
        {
            EventCallback handler = (EventCallback)GCHandle.FromIntPtr((IntPtr)context).Target!;
            handler(@event);
        }
        catch (Exception exception)
        {
            CallbackFailure.record(exception);
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void release(void* context)
    {
        GCHandle.FromIntPtr((IntPtr)context).Free();
    }
}
