// Composite controls through the custom controls table (C ABI 1.5): a
// `.control` document loaded once into a definition, controls made from it,
// their commands performed and their children found. Properties and declared
// events go through the properties and events tables like any control's.
//
// Lifetime: a CompositeDefinition holds one reference to the library's
// definition object; the controls made from it keep what they need, so the
// definition may be disposed while they live. UI thread, as the table is.
using System;
using System.Text;

namespace GuiForms.Interop;

public sealed unsafe class CompositeDefinition : IDisposable
{
    private readonly ObjectHandle native;

    private CompositeDefinition(ObjectHandle native)
    {
        this.native = native;
    }

    /// <summary>The library's definition object.</summary>
    public ObjectHandle handle => native;

    /// <summary>
    /// Reads, checks and compiles document (named file in its diagnostics),
    /// its children's element names resolved among child_types (schema type
    /// ids). A refused document throws GuiFormsException with its
    /// diagnostics in the message.
    /// </summary>
    public static CompositeDefinition load(string file, string document, params string[] child_types)
    {
        gf_custom_controls_table table = Composite.table();
        byte[] file_bytes = Encoding.UTF8.GetBytes(file);
        byte[] document_bytes = Encoding.UTF8.GetBytes(document);
        // The names back to back in one pinned buffer, each view into it.
        int[] starts = new int[child_types.Length];
        int[] lengths = new int[child_types.Length];
        int total = 0;
        for (int index = 0; index < child_types.Length; ++index)
        {
            starts[index] = total;
            lengths[index] = Encoding.UTF8.GetByteCount(child_types[index]);
            total = checked(total + lengths[index]);
        }
        byte[] names = new byte[total];
        for (int index = 0; index < child_types.Length; ++index)
        {
            Encoding.UTF8.GetBytes(child_types[index], 0, child_types[index].Length, names, starts[index]);
        }
        gf_utf8_view[] views = new gf_utf8_view[child_types.Length];
        gf_object made = 0;
        gf_status status;
        fixed (byte* file_data = file_bytes)
        fixed (byte* document_data = document_bytes)
        fixed (byte* names_data = names)
        fixed (gf_utf8_view* views_data = views)
        {
            for (int index = 0; index < child_types.Length; ++index)
            {
                views_data[index] = Utf8.view(names_data + starts[index], lengths[index]);
            }
            gf_composite_spec spec = default;
            spec.struct_size = (uint)sizeof(gf_composite_spec);
            spec.file = Utf8.view(file_data, file_bytes.Length);
            spec.document = Utf8.view(document_data, document_bytes.Length);
            spec.child_types = views_data;
            spec.child_type_count = (ulong)child_types.Length;
            status = table.load_composite(&spec, &made);
        }
        Abi.check(status);
        return new CompositeDefinition(ObjectHandle.adopt(made));
    }

    /// <summary>Makes a composite control from this definition with stable_id.</summary>
    public ObjectHandle create(string stable_id)
    {
        gf_custom_controls_table table = Composite.table();
        byte[] id_bytes = Encoding.UTF8.GetBytes(stable_id);
        gf_object made = 0;
        gf_status status;
        fixed (byte* id_data = id_bytes)
        {
            status = table.create_composite(native.value, Utf8.view(id_data, id_bytes.Length), &made);
        }
        Abi.check(status);
        return ObjectHandle.adopt(made);
    }

    /// <summary>Releases this reference to the definition.</summary>
    public void Dispose()
    {
        native.Dispose();
    }
}

public static unsafe class Composite
{
    private static readonly object table_lock = new object();
    private static gf_custom_controls_table negotiated;
    private static bool held;

    // The custom controls table, negotiated on first use; it must reach
    // composite_child (1.5).
    internal static gf_custom_controls_table table()
    {
        lock (table_lock)
        {
            if (held)
            {
                return negotiated;
            }
            Abi.check(Exports.get_custom_controls_table(out gf_custom_controls_table found));
            if (found.struct_size < (uint)sizeof(gf_custom_controls_table) || found.composite_child == null)
            {
                throw new GuiFormsException(Constants.GF_STATUS_UNSUPPORTED_VERSION,
                                            "the library has no composite controls (C ABI 1.5)");
            }
            negotiated = found;
            held = true;
            return negotiated;
        }
    }

    /// <summary>
    /// Runs the composite's command name: true when it ran, false when it
    /// is not enabled. An undeclared command throws.
    /// </summary>
    public static bool perform(ObjectHandle control, string name)
    {
        gf_custom_controls_table found = table();
        byte[] name_bytes = Encoding.UTF8.GetBytes(name);
        uint performed = 0;
        gf_status status;
        fixed (byte* name_data = name_bytes)
        {
            status = found.perform_composite(control.value, Utf8.view(name_data, name_bytes.Length), &performed);
        }
        Abi.check(status);
        return performed == 1u;
    }

    /// <summary>The child of local ID local_id (title for @title), or null when there is none.</summary>
    public static ObjectHandle? child(ObjectHandle control, string local_id)
    {
        gf_custom_controls_table found = table();
        byte[] id_bytes = Encoding.UTF8.GetBytes(local_id);
        gf_object child = 0;
        gf_status status;
        fixed (byte* id_data = id_bytes)
        {
            status = found.composite_child(control.value, Utf8.view(id_data, id_bytes.Length), &child);
        }
        if (status == Constants.GF_STATUS_NOT_FOUND)
        {
            return null;
        }
        Abi.check(status);
        return ObjectHandle.adopt(child);
    }
}
