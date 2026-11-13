// The managed side of an object made from a class table: the native object's
// reference, the instance the library hands back to every entry, and the
// release that frees it.
//
// Lifetime: create_native allocates a normal GCHandle to the managed object
// and passes it as the class table's instance. The library calls the table's
// release exactly once, on the UI thread, when the native object is
// destroyed; release_instance frees the handle there. Until then the native
// object keeps the managed one alive, as a Windows Forms control lives until
// it is disposed: Dispose (or a disposing parent) ends both. A table the
// library refuses calls no release, so create_native frees the handle itself.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using GuiForms.Interop;

namespace GuiForms.Forms;

/// <summary>Passed up a constructor chain whose most derived generated constructor makes the native object.</summary>
public readonly struct ClassCreation
{
    /// <summary>The native object is made by the constructor that passed this.</summary>
    public static readonly ClassCreation deferred = default;
}

/// <summary>The root of the classes made from class tables.</summary>
public abstract unsafe class Component : IDisposable
{
    private ObjectHandle? native;

    /// <summary>For a derived class's constructor, which makes the native object.</summary>
    protected Component(ClassCreation creation)
    {
        _ = creation;
    }

    /// <summary>The native object; throws before the constructor made it.</summary>
    public ObjectHandle handle => native ?? throw new InvalidOperationException("the native object is not made yet");

    /// <summary>
    /// Makes the native object of class_id from this managed type's class
    /// table (ClassTables), with stable_id. Called once, by the most derived
    /// generated constructor.
    /// </summary>
    protected void create_native(gf_class_id class_id, string stable_id)
    {
        if (native != null)
        {
            throw new InvalidOperationException("the native object is made once");
        }
        gf_class_header* table = ClassTables.table_for(GetType(), class_id);
        GCHandle instance = GCHandle.Alloc(this, GCHandleType.Normal);
        byte[] id_bytes = System.Text.Encoding.UTF8.GetBytes(stable_id);
        gf_object created = 0;
        gf_status status;
        fixed (byte* id_data = id_bytes)
        {
            gf_utf8_view id_view = Utf8.view(id_data, id_bytes.Length);
            status = Abi.classes.create(table, id_view, (void*)GCHandle.ToIntPtr(instance), &created);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            // Refused: the library calls no release, so the handle is ours to free.
            instance.Free();
            Abi.check(status);
        }
        native = ObjectHandle.adopt(created);
    }

    /// <summary>Disposes the native object and releases this reference to it.</summary>
    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!disposing || native == null)
        {
            return;
        }
        ObjectHandle owned = native;
        native = null;
        gf_status disposed = Abi.objects.dispose(owned.value);
        owned.Dispose();
        Abi.check(disposed);
    }
}

/// <summary>The instance pointer of a class table's object: a GCHandle to its managed object.</summary>
internal static unsafe class Instances
{
    internal static object from(void* instance)
    {
        object? target = GCHandle.FromIntPtr((IntPtr)instance).Target;
        return target ?? throw new InvalidOperationException("the instance's managed object is gone");
    }
}

/// <summary>The class tables' release entry.</summary>
internal static unsafe class ClassObjects
{
    /// <summary>The library calls this once per object, on the UI thread, when it is destroyed.</summary>
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static void release_instance(void* instance)
    {
        // No exception crosses into the library.
        try
        {
            GCHandle.FromIntPtr((IntPtr)instance).Free();
        }
        catch (Exception failure)
        {
            CallbackFailure.record(failure);
        }
    }
}
