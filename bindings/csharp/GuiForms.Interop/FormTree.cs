// A converted form's objects for C# (stage MCP G7): what the bindings the
// Windows Forms converter writes (<Form>.GuiForms.g.cs) call to find each
// object of a loaded form by its stable ID and attach the form class's
// handlers. Only the objects and events tables are used; the form itself
// is loaded by the program (the C ABI has no form loader before stage D).
// Every call is the UI thread's; a refusal throws GuiFormsException.
using System;
using System.Collections.Generic;
using System.Text;

namespace GuiForms.Interop;

/// <summary>Finding a form's objects and subscribing to their interactions.</summary>
public static unsafe class FormTree
{
    /// <summary>The most objects find visits; a deeper or larger tree is refused.</summary>
    public const int maximum_visited = 100000;

    /// <summary>
    /// The object under root (root included) whose stable ID is stable_id, as
    /// a new reference. Throws InvalidOperationException when there is none.
    /// </summary>
    public static ObjectHandle find(ObjectHandle root, string stable_id)
    {
        ObjectHandle? found = try_find(root, stable_id);
        if (found == null)
        {
            throw new InvalidOperationException("the form has no object " + stable_id);
        }
        return found;
    }

    /// <summary>The object under root with stable_id, or null.</summary>
    public static ObjectHandle? try_find(ObjectHandle root, string stable_id)
    {
        if (new ObjectHandleView(root).stable_id == stable_id)
        {
            // A second reference, so the caller may dispose either.
            Abi.check(Abi.objects.retain(root.value));
            return ObjectHandle.adopt(root.value);
        }
        // Breadth first in explicit storage; each handle visited is released
        // unless it is the one returned.
        Queue<ObjectHandle> pending = new Queue<ObjectHandle>();
        enqueue_children(root, pending);
        int visited = 0;
        while (pending.Count != 0)
        {
            ObjectHandle current = pending.Dequeue();
            visited = visited + 1;
            if (visited > maximum_visited)
            {
                current.Dispose();
                release_all(pending);
                throw new InvalidOperationException("the form has more objects than find visits");
            }
            if (new ObjectHandleView(current).stable_id == stable_id)
            {
                release_all(pending);
                return current;
            }
            enqueue_children(current, pending);
            current.Dispose();
        }
        return null;
    }

    /// <summary>
    /// Runs handler for each of source's interaction (its registered name,
    /// "click"); the subscription ends at FormSubscriptions.Dispose.
    /// </summary>
    public static gf_subscription subscribe(ObjectHandle source, string interaction, EventCallback handler)
    {
        byte[] name = Encoding.UTF8.GetBytes(interaction);
        gf_callback callback = Callbacks.to_native(handler);
        gf_subscription subscription = 0;
        gf_status status;
        fixed (byte* bytes = name)
        {
            status = Abi.events.subscribe(source.value, Utf8.view(bytes, name.Length), &callback, &subscription);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(callback);
            Abi.check(status);
        }
        return subscription;
    }

    private static void enqueue_children(ObjectHandle parent, Queue<ObjectHandle> pending)
    {
        ulong count = 0;
        Abi.check(Abi.objects.child_count(parent.value, &count));
        for (ulong index = 0; index < count; ++index)
        {
            gf_object child = 0;
            Abi.check(Abi.objects.child_at(parent.value, index, &child));
            pending.Enqueue(ObjectHandle.adopt(child));
        }
    }

    private static void release_all(Queue<ObjectHandle> pending)
    {
        while (pending.Count != 0)
        {
            pending.Dequeue().Dispose();
        }
    }
}

/// <summary>The subscriptions a converted form's binding made; Dispose ends them all.</summary>
public sealed class FormSubscriptions : IDisposable
{
    private readonly List<gf_subscription> subscriptions = new List<gf_subscription>();

    public void add(gf_subscription subscription)
    {
        subscriptions.Add(subscription);
    }

    public unsafe void Dispose()
    {
        foreach (gf_subscription subscription in subscriptions)
        {
            Abi.events.disconnect(subscription);
        }
        subscriptions.Clear();
    }
}
