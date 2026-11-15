// Composite controls through the custom controls table (C ABI 1.5): a
// `.control` document loaded once into a definition, controls made from it,
// their commands performed and their children found. Properties and declared
// events go through the properties and events tables like any control's.
//
// Lifetime: a CompositeDefinition holds one reference to the library's
// definition object; the controls made from it keep what they need, so the
// definition may be disposed while they live. UI thread, as the table is.
//
// CompositeSchema lists the definitions loaded now through the schema table
// (provisional 1.5 slots): each listing is copied into managed strings while
// its load is held, since the library's text lasts only that long.
using System;
using System.Collections.Generic;
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

/// <summary>A part of a listed composite's anatomy.</summary>
public sealed class CompositeSchemaPart
{
    public string name = "";
    public string role = "";
    public string composed_from = "";
    public string[] states = Array.Empty<string>();
}

/// <summary>A declared property of a listed composite (gf_schema_property, copied).</summary>
public sealed class CompositeSchemaProperty
{
    public string name = "";
    public gf_value_kind kind;
    public string category = "";
    public string enumeration = "";
    public string[] choices = Array.Empty<string>();
    public gf_schema_property_flags flags;
    public gf_schema_serialization serialization;
}

/// <summary>A declared event: its payload in the ABI's interaction words, empty for none.</summary>
public sealed class CompositeSchemaEvent
{
    public string name = "";
    public string payload = "";
}

/// <summary>
/// One loaded composite definition as the schema table lists it, copied
/// while its load was held: identity is the load's, which stays the same
/// while the load is held and is never reused.
/// </summary>
public sealed class CompositeSchemaEntry
{
    public ulong identity;
    public string id = "";
    public string name = "";
    public string version = "";
    public string slot = "";
    public CompositeSchemaPart[] parts = Array.Empty<CompositeSchemaPart>();
    public string[] states = Array.Empty<string>();
    public string[] custom_states = Array.Empty<string>();
    public CompositeSchemaProperty[] properties = Array.Empty<CompositeSchemaProperty>();
    public CompositeSchemaEvent[] events = Array.Empty<CompositeSchemaEvent>();
    public string[] commands = Array.Empty<string>();
}

public static unsafe class CompositeSchema
{
    private static readonly object table_lock = new object();
    private static gf_schema_table negotiated;
    private static bool held;

    // The schema table, negotiated on first use; it must reach
    // composite_command_at (provisional 1.5).
    private static gf_schema_table table()
    {
        lock (table_lock)
        {
            if (held)
            {
                return negotiated;
            }
            Abi.check(Exports.get_schema_table(out gf_schema_table found));
            if (found.struct_size < (uint)sizeof(gf_schema_table) || found.composite_command_at == null)
            {
                throw new GuiFormsException(Constants.GF_STATUS_UNSUPPORTED_VERSION,
                                            "the library does not list composites (C ABI 1.5)");
            }
            negotiated = found;
            held = true;
            return negotiated;
        }
    }

    private static string text(gf_utf8_view view)
    {
        string copied = view.size == 0u ? "" : Encoding.UTF8.GetString(view.data, checked((int)view.size));
        return copied;
    }

    /// <summary>The composite definitions loaded now, in load order. UI thread.</summary>
    public static CompositeSchemaEntry[] list()
    {
        gf_schema_table found = table();
        ulong count = 0;
        Abi.check(found.composite_count(&count));
        List<CompositeSchemaEntry> entries = new List<CompositeSchemaEntry>(checked((int)count));
        for (ulong index = 0; index < count; ++index)
        {
            gf_schema_composite record = default;
            record.struct_size = (uint)sizeof(gf_schema_composite);
            Abi.check(found.composite_at(index, &record));
            entries.Add(read(found, record));
        }
        return entries.ToArray();
    }

    /// <summary>
    /// The listing of the load identity; a released load throws
    /// GuiFormsException with GF_STATUS_REMOVED. UI thread.
    /// </summary>
    public static CompositeSchemaEntry find(ulong identity)
    {
        gf_schema_table found = table();
        ulong count = 0;
        Abi.check(found.composite_count(&count));
        for (ulong index = 0; index < count; ++index)
        {
            gf_schema_composite record = default;
            record.struct_size = (uint)sizeof(gf_schema_composite);
            Abi.check(found.composite_at(index, &record));
            if (record.composite == identity)
            {
                return read(found, record);
            }
        }
        // Not listed: ask the library why, so a released load throws its
        // GF_STATUS_REMOVED with the library's detail.
        gf_utf8_view probe = default;
        gf_status status = found.composite_command_at(identity, 0, &probe);
        if (status == Constants.GF_STATUS_REMOVED)
        {
            Abi.check(status);
        }
        throw new GuiFormsException(Constants.GF_STATUS_INVALID_ARGUMENT, "no composite load had that identity");
    }

    private static CompositeSchemaEntry read(in gf_schema_table found, in gf_schema_composite record)
    {
        ulong composite = record.composite;
        CompositeSchemaEntry entry = new CompositeSchemaEntry();
        entry.identity = composite;
        entry.id = text(record.id);
        entry.name = text(record.name);
        entry.version = text(record.version);
        entry.slot = text(record.slot);
        entry.parts = new CompositeSchemaPart[checked((int)record.part_count)];
        for (ulong part = 0; part < record.part_count; ++part)
        {
            gf_schema_part declared = default;
            declared.struct_size = (uint)sizeof(gf_schema_part);
            Abi.check(found.composite_part_at(composite, part, &declared));
            CompositeSchemaPart copied = new CompositeSchemaPart();
            copied.name = text(declared.name);
            copied.role = text(declared.role);
            copied.composed_from = text(declared.composed_from);
            copied.states = new string[checked((int)declared.state_count)];
            for (ulong state = 0; state < declared.state_count; ++state)
            {
                gf_utf8_view name = default;
                Abi.check(found.composite_state_at(composite, part, state, &name));
                copied.states[state] = text(name);
            }
            entry.parts[part] = copied;
        }
        entry.states = new string[checked((int)record.state_count)];
        for (ulong state = 0; state < record.state_count; ++state)
        {
            gf_utf8_view name = default;
            Abi.check(found.composite_state_at(composite, Constants.GF_SCHEMA_NONE, state, &name));
            entry.states[state] = text(name);
        }
        entry.custom_states = new string[checked((int)record.custom_state_count)];
        for (ulong state = 0; state < record.custom_state_count; ++state)
        {
            gf_utf8_view name = default;
            Abi.check(found.composite_custom_state_at(composite, state, &name));
            entry.custom_states[state] = text(name);
        }
        entry.properties = new CompositeSchemaProperty[checked((int)record.property_count)];
        for (ulong property = 0; property < record.property_count; ++property)
        {
            gf_schema_property declared = default;
            declared.struct_size = (uint)sizeof(gf_schema_property);
            Abi.check(found.composite_property_at(composite, property, &declared));
            CompositeSchemaProperty copied = new CompositeSchemaProperty();
            copied.name = text(declared.name);
            copied.kind = declared.kind;
            copied.category = text(declared.category);
            copied.enumeration = text(declared.enumeration);
            copied.flags = declared.flags;
            copied.serialization = declared.serialization;
            copied.choices = new string[checked((int)declared.choice_count)];
            for (ulong choice = 0; choice < declared.choice_count; ++choice)
            {
                gf_schema_choice value = default;
                value.struct_size = (uint)sizeof(gf_schema_choice);
                Abi.check(found.composite_choice_at(composite, property, choice, &value));
                copied.choices[choice] = text(value.name);
            }
            entry.properties[property] = copied;
        }
        entry.events = new CompositeSchemaEvent[checked((int)record.event_count)];
        for (ulong @event = 0; @event < record.event_count; ++@event)
        {
            gf_schema_interaction declared = default;
            declared.struct_size = (uint)sizeof(gf_schema_interaction);
            Abi.check(found.composite_event_at(composite, @event, &declared));
            CompositeSchemaEvent copied = new CompositeSchemaEvent();
            copied.name = text(declared.name);
            copied.payload = text(declared.payload);
            entry.events[@event] = copied;
        }
        entry.commands = new string[checked((int)record.command_count)];
        for (ulong command = 0; command < record.command_count; ++command)
        {
            gf_utf8_view name = default;
            Abi.check(found.composite_command_at(composite, command, &name));
            entry.commands[command] = text(name);
        }
        return entry;
    }
}
