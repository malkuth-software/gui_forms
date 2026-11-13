// The C# class tables against the built library: for each generated case
// (ClassHookTests.g.cs, from tools/abi_class_tables.py), a managed subclass
// overriding one OnX hook sees the order contract of gf_abi_classes.h: its
// override runs before the subscribers, they run inside base, skipping base
// suppresses them, and the hook called directly runs them through
// classes.base_raiser. A class that overrides nothing gets no entry (the
// source generator's registration).
using System;
using System.Collections.Generic;
using GuiForms.Forms;
using GuiForms.Interop;

namespace GuiForms.Forms.Tests;

/// <summary>Writes a property of a class table's object (properties.set).</summary>
internal static unsafe class PropertyWrites
{
    public static void set(Component target, ReadOnlySpan<byte> property, gf_value value)
    {
        fixed (byte* name = property)
        {
            Abi.check(Abi.properties.set(target.handle.value, Utf8.view(name, property.Length), &value));
        }
    }
}

/// <summary>A subscription that marks 'S' in a log for each event; Dispose disconnects it.</summary>
internal sealed unsafe class SubscriptionScope : IDisposable
{
    private readonly HookLog log;
    private gf_subscription subscription;

    private SubscriptionScope(HookLog log)
    {
        this.log = log;
    }

    public static SubscriptionScope subscribe(Component source, string interaction, HookLog log)
    {
        SubscriptionScope scope = new SubscriptionScope(log);
        gf_callback callback = Callbacks.to_native(scope.mark);
        byte[] name = System.Text.Encoding.UTF8.GetBytes(interaction);
        gf_subscription made = 0;
        gf_status status;
        fixed (byte* bytes = name)
        {
            status = Abi.events.subscribe(source.handle.value, Utf8.view(bytes, name.Length), &callback, &made);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(callback);
            Abi.check(status);
        }
        scope.subscription = made;
        return scope;
    }

    private void mark(gf_event* @event)
    {
        log.marks.Add('S');
    }

    public void Dispose()
    {
        if (subscription != 0)
        {
            _ = Abi.events.disconnect(subscription);
            subscription = 0;
        }
    }
}

internal static class Program
{
    private static int failures;

    private static void expect(bool condition, string what)
    {
        if (!condition)
        {
            Console.WriteLine("FAIL: " + what);
            ++failures;
        }
    }

    private static unsafe int Main()
    {
        ClassHookTests.run(expect);
        ulong invoked = 0;
        Abi.check(Abi.dispatch.run_pending(&invoked));
        Console.WriteLine(failures == 0 ? "GuiForms.Forms: all checks passed" : $"GuiForms.Forms: {failures} failed");
        return failures == 0 ? 0 : 1;
    }
}
