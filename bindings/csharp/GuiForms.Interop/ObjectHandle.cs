// One reference to a library object. The library allows release from any
// thread (destruction is deferred to the owning UI thread), so the finalizer
// thread may release a handle the program dropped. The handle keeps the
// 64-bit gf_object in its IntPtr, so the binding runs in 64-bit processes
// only; Abi refuses a 32-bit one at its first negotiation (N73).
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace GuiForms.Interop;

[DebuggerTypeProxy(typeof(ObjectHandleView))]
[DebuggerDisplay("gf_object {value}")]
public sealed unsafe class ObjectHandle : SafeHandle
{
    private ObjectHandle(gf_object value)
        : base(IntPtr.Zero, true)
    {
        SetHandle((IntPtr)(long)value);
    }

    /// <summary>The gf_object this handle holds a reference to.</summary>
    public gf_object value => (gf_object)(long)handle;

    public override bool IsInvalid => handle == IntPtr.Zero;

    /// <summary>Takes ownership of one reference the caller holds.</summary>
    public static ObjectHandle adopt(gf_object value)
    {
        return new ObjectHandle(value);
    }

    /// <summary>Creates a control of type_name with stable_id (objects.create).</summary>
    public static ObjectHandle create(ReadOnlySpan<byte> type_name, ReadOnlySpan<byte> stable_id)
    {
        gf_object created = 0;
        fixed (byte* type_bytes = type_name)
        fixed (byte* id_bytes = stable_id)
        {
            gf_utf8_view type_view = Utf8.view(type_bytes, type_name.Length);
            gf_utf8_view id_view = Utf8.view(id_bytes, stable_id.Length);
            Abi.check(Abi.objects.create(type_view, id_view, &created));
        }
        return new ObjectHandle(created);
    }

    protected override bool ReleaseHandle()
    {
        return Abi.objects.release((gf_object)(long)handle) == Constants.GF_STATUS_OK;
    }
}

public static unsafe class Utf8
{
    /// <summary>A view of length bytes at data; the caller keeps them pinned through the call.</summary>
    public static gf_utf8_view view(byte* data, int length)
    {
        gf_utf8_view result = default;
        result.data = data;
        result.size = (ulong)length;
        return result;
    }
}

/// <summary>
/// The debugger's view of an ObjectHandle: what the library says the object
/// is, read through the ABI (type_id, stable_id, state) when the view opens.
/// </summary>
public sealed unsafe class ObjectHandleView
{
    private readonly ObjectHandle handle;

    public ObjectHandleView(ObjectHandle handle)
    {
        this.handle = handle;
    }

    public gf_object value => handle.value;

    public string type_id => read_text(true);

    public string stable_id => read_text(false);

    public string state
    {
        get
        {
            gf_object_state state = 0;
            gf_status status = Abi.objects.state(handle.value, &state);
            if (status != Constants.GF_STATUS_OK)
            {
                return "status " + status;
            }
            switch (state)
            {
            case Constants.GF_OBJECT_STATE_ALIVE:
                return "alive";
            case Constants.GF_OBJECT_STATE_DISPOSING:
                return "disposing";
            case Constants.GF_OBJECT_STATE_DISPOSED:
                return "disposed";
            default:
                return "state " + state;
            }
        }
    }

    private string read_text(bool type)
    {
        const int capacity = 256;
        byte* buffer = stackalloc byte[capacity];
        ulong required = 0;
        gf_objects_table objects = Abi.objects;
        gf_status status = type ? objects.type_id(handle.value, buffer, capacity, &required)
                                : objects.stable_id(handle.value, buffer, capacity, &required);
        if (status != Constants.GF_STATUS_OK)
        {
            return "status " + status;
        }
        int length = (int)Math.Min(required, (ulong)capacity);
        while (length > 0 && buffer[length - 1] == 0)
        {
            --length;
        }
        return Encoding.UTF8.GetString(buffer, length);
    }
}
