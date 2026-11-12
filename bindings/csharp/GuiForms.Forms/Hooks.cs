// Hooks: which managed class overrides which raiser, the class table each
// managed type gets, and the base call an override makes.
//
// Which hooks a class overrides is decided at compile time: the
// GuiForms.Forms.Generators source generator emits a module initializer per
// assembly that registers every class's overridden hooks (ClassOverrides).
// A class table carries an entry only for those, so a raise whose hook is
// not overridden never leaves native code. A type the generator did not see
// (an assembly built without it) is read once by reflection instead.
//
// The base call: inside an entry (HookFrame), base runs the base function the
// library passed with the event it passed; called directly (an OnX called by
// managed code, as Windows Forms allows), it asks classes.base_raiser for the
// entry's base function and calls it with a new event.
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using GuiForms.Interop;

namespace GuiForms.Forms;

/// <summary>One raiser: its class's segment and its entry's position there.</summary>
public readonly struct HookKey : IEquatable<HookKey>
{
    public readonly gf_class_id class_id;
    public readonly uint entry;

    public HookKey(gf_class_id class_id, uint entry)
    {
        this.class_id = class_id;
        this.entry = entry;
    }

    public bool Equals(HookKey other)
    {
        return class_id == other.class_id && entry == other.entry;
    }

    public override bool Equals(object? other)
    {
        return other is HookKey key && Equals(key);
    }

    public override int GetHashCode()
    {
        return (int)(class_id * 397u ^ entry);
    }
}

/// <summary>Marks a generated OnX virtual with its segment entry; the source generator reads it.</summary>
[AttributeUsage(AttributeTargets.Method, Inherited = false)]
public sealed class HookAttribute : Attribute
{
    public gf_class_id class_id { get; }
    public uint entry { get; }

    public HookAttribute(gf_class_id class_id, uint entry)
    {
        this.class_id = class_id;
        this.entry = entry;
    }
}

/// <summary>The hooks one managed class overrides, its bases' included.</summary>
public sealed class HookSet
{
    private readonly HashSet<HookKey> keys;

    internal HookSet(HashSet<HookKey> keys)
    {
        this.keys = keys;
    }

    public bool contains(HookKey key)
    {
        return keys.Contains(key);
    }
}

/// <summary>The overridden hooks of each managed class, registered by the source generator.</summary>
public static class ClassOverrides
{
    private static readonly object registry_lock = new object();
    private static readonly Dictionary<Type, HookKey[]> registered = new Dictionary<Type, HookKey[]>();

    /// <summary>Called by the generated module initializers: type's own overrides (not its bases').</summary>
    public static void register(Type type, HookKey[] overridden)
    {
        lock (registry_lock)
        {
            registered[type] = overridden;
        }
    }

    /// <summary>The hooks type and its bases override, up to this assembly's classes.</summary>
    internal static HookSet hooks_of(Type type)
    {
        HashSet<HookKey> keys = new HashSet<HookKey>();
        Assembly own = typeof(ClassOverrides).Assembly;
        Type? current = type;
        while (current != null && current.Assembly != own)
        {
            HookKey[] own_keys = keys_of(current);
            foreach (HookKey key in own_keys)
            {
                keys.Add(key);
            }
            current = current.BaseType;
        }
        return new HookSet(keys);
    }

    private static HookKey[] keys_of(Type type)
    {
        lock (registry_lock)
        {
            if (registered.TryGetValue(type, out HookKey[]? found))
            {
                return found;
            }
        }
        HookKey[] read = read_by_reflection(type);
        lock (registry_lock)
        {
            registered[type] = read;
        }
        return read;
    }

    // The fallback for an assembly built without the generator: type's own
    // overrides of a [Hook] virtual.
    private static HookKey[] read_by_reflection(Type type)
    {
        List<HookKey> keys = new List<HookKey>();
        const BindingFlags declared = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                      BindingFlags.DeclaredOnly;
        foreach (MethodInfo method in type.GetMethods(declared))
        {
            MethodInfo definition = method.GetBaseDefinition();
            if (definition == method)
            {
                continue;
            }
            HookAttribute? hook = definition.GetCustomAttribute<HookAttribute>();
            if (hook != null)
            {
                keys.Add(new HookKey(hook.class_id, hook.entry));
            }
        }
        return keys.ToArray();
    }
}

/// <summary>The class table of each managed type, built once and kept for the process.</summary>
public static unsafe class ClassTables
{
    private static readonly object tables_lock = new object();
    // The tables are native memory never freed: the library borrows a table
    // for as long as any object made from it lives (gf_classes_table.create).
    private static readonly Dictionary<Type, IntPtr> tables = new Dictionary<Type, IntPtr>();

    /// <summary>The class table of type, whose native class is class_id.</summary>
    internal static gf_class_header* table_for(Type type, gf_class_id class_id)
    {
        lock (tables_lock)
        {
            if (tables.TryGetValue(type, out IntPtr found))
            {
                return (gf_class_header*)found;
            }
            gf_class_header* built = ClassShapes.build(class_id, ClassOverrides.hooks_of(type));
            tables[type] = (IntPtr)built;
            return built;
        }
    }

    /// <summary>Whether type's class table has an entry for key (tests and diagnostics).</summary>
    public static bool has_entry(Type type, HookKey key)
    {
        return ClassOverrides.hooks_of(type).contains(key);
    }
}

/// <summary>The entry a thread is running, innermost last: what a base call inside it uses.</summary>
public unsafe ref struct HookFrame
{
    private struct Record
    {
        public object target;
        public HookKey key;
        public gf_object self;
        public gf_event* native;
        public delegate* unmanaged[Cdecl]<gf_object, gf_event*, gf_status> base_raiser;
    }

    [ThreadStatic] private static List<Record>? frames;

    private readonly int depth;

    private HookFrame(int depth)
    {
        this.depth = depth;
    }

    /// <summary>Pushes the frame of one entry call; Dispose pops it.</summary>
    internal static HookFrame enter(object target, HookKey key, gf_object self, gf_event* native,
                                    delegate* unmanaged[Cdecl]<gf_object, gf_event*, gf_status> base_raiser)
    {
        List<Record> stack = frames ??= new List<Record>();
        Record record = default;
        record.target = target;
        record.key = key;
        record.self = self;
        record.native = native;
        record.base_raiser = base_raiser;
        stack.Add(record);
        return new HookFrame(stack.Count);
    }

    public void Dispose()
    {
        List<Record>? stack = frames;
        if (stack != null && stack.Count == depth)
        {
            stack.RemoveAt(depth - 1);
        }
    }

    /// <summary>Runs the base of target's hook key in the innermost frame for them; false when none.</summary>
    internal static bool call_framed(object target, HookKey key)
    {
        List<Record>? stack = frames;
        if (stack == null || stack.Count == 0)
        {
            return false;
        }
        Record top = stack[stack.Count - 1];
        if (!ReferenceEquals(top.target, target) || !top.key.Equals(key))
        {
            return false;
        }
        gf_status status = top.base_raiser(top.self, top.native);
        if (status != Constants.GF_STATUS_OK)
        {
            Abi.check(status);
        }
        return true;
    }
}

/// <summary>The base call of the generated OnX virtuals.</summary>
public static unsafe class Hooks
{
    /// <summary>Runs the C++ raiser below the class table for self's hook key: the subscribers run here.</summary>
    public static void call_base(Component self, HookKey key, ReadOnlySpan<byte> property)
    {
        if (HookFrame.call_framed(self, key))
        {
            return;
        }
        delegate* unmanaged[Cdecl]<gf_object, gf_event*, gf_status> raiser = null;
        Abi.check(Abi.classes.base_raiser(key.class_id, key.entry, &raiser));
        gf_object native = self.handle.value;
        fixed (byte* name = property)
        {
            gf_event created = default;
            created.struct_size = (uint)sizeof(gf_event);
            created.sender = native;
            created.name = Utf8.view(name, property.Length);
            gf_status status = raiser(native, &created);
            if (status != Constants.GF_STATUS_OK)
            {
                Abi.check(status);
            }
        }
    }
}
