// Managed callbacks behind gf_callback: an [UnmanagedCallersOnly] thunk, a
// GCHandle context the library releases through the callback's release slot,
// and the no-unwind rule. An exception never crosses into the library: the
// thunk catches it and takes a snapshot of the shadow frames it ran under
// (read before it returns, so the library's frames stay balanced). Where it
// goes depends on the innermost library call below the callback:
// - an event loop (windows.run, windows.show_modal), or none: no managed
//   caller gets control back until the loop ends, so the failure is raised
//   at once through CallbackFailure.unhandled, as Windows Forms raises
//   Application.ThreadException; unhandled there, unhandled_policy decides;
// - any other call (properties.set, dispatch.run_pending, ...): the failure
//   is kept for the thread and the managed code that made that call
//   rethrows it on return (Abi.check, CallbackFailure.rethrow_pending).
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

/// <summary>What a callback failure nobody handled does (CallbackFailure.unhandled_policy).</summary>
public enum UnhandledCallbackPolicy
{
    /// <summary>Ends the process at once (Environment.FailFast) with the failure.</summary>
    fail_fast,
    /// <summary>Drops the failure and lets the event loop run on.</summary>
    continue_running,
}

/// <summary>A callback failure raised from an event loop; set handled to stop the policy.</summary>
public sealed class CallbackFailureEventArgs : EventArgs
{
    public CallbackFailure failure { get; }
    public bool handled { get; set; }

    internal CallbackFailureEventArgs(CallbackFailure failure)
    {
        this.failure = failure;
    }
}

/// <summary>A managed exception that a callback threw, with the frames it was thrown under.</summary>
public sealed class CallbackFailure : Exception
{
    [ThreadStatic] private static List<CallbackFailure>? pending;

    // The slots of the windows table's event loops (gf_windows_table: run is
    // its first function pointer, show_modal its eighth), as
    // src/abi/stable/tables/abi_windows.cpp numbers them.
    private const uint windows_run_slot = 0u;
    private const uint windows_show_modal_slot = 7u;

    /// <summary>
    /// Raised on the callback's thread for a failure no managed caller will
    /// receive soon: one thrown under an event loop (windows.run or
    /// windows.show_modal) or under no library call. Unhandled, the failure
    /// goes to unhandled_policy. A handler that throws is itself unhandled.
    /// </summary>
    public static event EventHandler<CallbackFailureEventArgs>? unhandled;

    /// <summary>
    /// What a failure raised through unhandled and not handled does. The
    /// default is fail_fast, so a failing handler is never silent; a program
    /// that wants Windows Forms' carry-on behaviour handles unhandled or sets
    /// continue_running. Set it before the event loop runs.
    /// </summary>
    public static UnhandledCallbackPolicy unhandled_policy { get; set; } = UnhandledCallbackPolicy.fail_fast;

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
        CallbackFailure failure = new CallbackFailure(exception, frames);
        if (reaches_a_caller(frames))
        {
            pending ??= new List<CallbackFailure>();
            pending.Add(failure);
            return;
        }
        raise_unhandled(failure);
    }

    // Whether managed code gets control back soon to receive the failure: the
    // innermost library call below the callback is one that returns when the
    // callback does, not an event loop.
    private static bool reaches_a_caller(ShadowFrame[] frames)
    {
        for (int index = frames.Length - 1; index >= 0; --index)
        {
            ShadowFrame frame = frames[index];
            if (frame.kind != Constants.GF_SHADOW_FRAME_KIND_LIBRARY_CALL)
            {
                continue;
            }
            bool event_loop = frame.table_id == Constants.GF_TABLE_WINDOWS &&
                              (frame.slot_index == windows_run_slot || frame.slot_index == windows_show_modal_slot);
            return !event_loop;
        }
        return false;
    }

    // Runs on the callback's thread, inside the thunk: nothing may escape.
    private static void raise_unhandled(CallbackFailure failure)
    {
        CallbackFailureEventArgs arguments = new CallbackFailureEventArgs(failure);
        EventHandler<CallbackFailureEventArgs>? handlers = unhandled;
        CallbackFailure reported = failure;
        if (handlers != null)
        {
            try
            {
                handlers(null, arguments);
            }
            catch (Exception thrown)
            {
                // The handler's own exception is the one reported now.
                arguments.handled = false;
                reported = new CallbackFailure(thrown, failure.frames);
            }
        }
        if (arguments.handled || unhandled_policy == UnhandledCallbackPolicy.continue_running)
        {
            return;
        }
        Environment.FailFast(reported.Message, reported);
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
