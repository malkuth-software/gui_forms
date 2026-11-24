// A program's own model tools and inspect sections (GF_TABLE_AUTOMATION,
// since 1.6; gf_abi.h, and C++ gui_forms/automation/tools/model_tools.hpp):
// served beside the library's built-in tools when the program runs with
// --mcp, and run by --mcp-script. The safety rules hold for every tool: a
// destructive one asks the user's one question of the launch first; an
// acting one runs on the UI thread and is refused while a dialog the
// program did not instrument is open; a handler must not put a password
// field's text in its reply.
//
//     ModelTools.register(new ModelToolSpec("convert.form", "Converts a Windows Forms designer file.")
//         { input_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}" },
//         convert_form);
//
// Lifetime: register hands the library a GCHandle to the registration; the
// library calls release once, after unregister and the tool's last call,
// and release frees the handle. Failures: an exception in a handler never
// crosses into the library: the call fails as internal with its message.
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace GuiForms.Interop;

/// <summary>A model tool's declaration.</summary>
public sealed class ModelToolSpec
{
    public ModelToolSpec(string name, string description)
    {
        this.name = name;
        this.description = description;
    }

    /// <summary>1 to 64 ASCII letters, digits, '_', '-' and '.', starting with a letter.</summary>
    public string name { get; }
    public string description { get; }
    public string title { get; init; } = "";
    /// <summary>The arguments' JSON Schema, one object; empty takes any object.</summary>
    public string input_schema { get; init; } = "";
    /// <summary>The tool changes the program (an acting tool runs on the UI thread).</summary>
    public bool acting { get; init; }
    /// <summary>Every call changes something the user cannot take back: the launch's question is asked first.</summary>
    public bool destructive { get; init; }
    /// <summary>The call runs off the UI thread and touches no control (read-only tools only).</summary>
    public bool any_thread { get; init; }
    /// <summary>An acting call runs as an act, answered with what changed once the window is idle.</summary>
    public bool settles { get; init; }
}

/// <summary>One call of a tool, read before the handler runs.</summary>
public sealed class ModelToolCall
{
    internal ModelToolCall(string arguments_json, string client_name, string model_name, string window_id)
    {
        this.arguments_json = arguments_json;
        this.client_name = client_name;
        this.model_name = model_name;
        this.window_id = window_id;
    }

    /// <summary>The arguments: one JSON object.</summary>
    public string arguments_json { get; }
    /// <summary>Who is calling, as the client states it (not verified).</summary>
    public string client_name { get; }
    public string model_name { get; }
    /// <summary>The stable ID of the window the call names; empty for an any-thread tool.</summary>
    public string window_id { get; }
}

/// <summary>The reply a handler fills, valid only while the handler runs.</summary>
public sealed unsafe class ModelToolReply
{
    private void* token;

    internal ModelToolReply(void* token)
    {
        this.token = token;
    }

    internal void end() => token = null;

    private void* live()
    {
        if (token == null)
        {
            throw new InvalidOperationException("a reply is valid only while its handler runs");
        }
        return token;
    }

    /// <summary>Appends text to the reply.</summary>
    public void text(string text)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* data = bytes)
        {
            Abi.check(ModelTools.table().reply_text(live(), Utf8.view(data, bytes.Length)));
        }
    }

    /// <summary>Sets the structured content: one JSON object matching the tool's declared output.</summary>
    public void structured(string json)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(json);
        fixed (byte* data = bytes)
        {
            Abi.check(ModelTools.table().reply_structured(live(), Utf8.view(data, bytes.Length)));
        }
    }

    /// <summary>Adds an image (PNG unless mime_type names another image/ type).</summary>
    public void image(ReadOnlySpan<byte> bytes, string mime_type = "image/png")
    {
        byte[] mime = Encoding.UTF8.GetBytes(mime_type);
        fixed (byte* data = bytes)
        fixed (byte* type = mime)
        {
            Abi.check(ModelTools.table().reply_image(live(), data, (ulong)bytes.Length, Utf8.view(type, mime.Length)));
        }
    }

    /// <summary>Fails the call with a named error (Constants.GF_AUTOMATION_TOOL_ERROR_*), a message and a hint.</summary>
    public void fail(gf_automation_tool_error error, string message, string hint = "")
    {
        byte[] message_bytes = Encoding.UTF8.GetBytes(message);
        byte[] hint_bytes = Encoding.UTF8.GetBytes(hint);
        fixed (byte* message_data = message_bytes)
        fixed (byte* hint_data = hint_bytes)
        {
            Abi.check(ModelTools.table().reply_fail(live(), error, Utf8.view(message_data, message_bytes.Length),
                                                    Utf8.view(hint_data, hint_bytes.Length)));
        }
    }
}

/// <summary>One inspect request, read before the section runs.</summary>
public sealed class ModelInspectRequest
{
    internal ModelInspectRequest(string window_id, string node_id, string arguments_json)
    {
        this.window_id = window_id;
        this.node_id = node_id;
        this.arguments_json = arguments_json;
    }

    public string window_id { get; }
    /// <summary>The stable ID of the node the call named; empty for none.</summary>
    public string node_id { get; }
    public string arguments_json { get; }
}

public delegate void ModelToolHandler(ModelToolCall call, ModelToolReply reply);

/// <summary>An inspect section: returns its text.</summary>
public delegate string ModelInspectHandler(ModelInspectRequest request);

public static unsafe class ModelTools
{
    private static readonly object negotiation_lock = new object();
    private static volatile AutomationTable? negotiated;

    private sealed class AutomationTable
    {
        public readonly gf_automation_table table;

        public AutomationTable(gf_automation_table table)
        {
            this.table = table;
        }
    }

    internal static gf_automation_table table()
    {
        AutomationTable? published = negotiated;
        if (published != null)
        {
            return published.table;
        }
        lock (negotiation_lock)
        {
            published = negotiated;
            if (published == null)
            {
                gf_status status = Exports.get_automation_table(out gf_automation_table made);
                if (status != Constants.GF_STATUS_OK)
                {
                    throw new GuiFormsException(status, "the automation table did not negotiate at ABI " +
                                                            Constants.GF_ABI_MAJOR + "." + Constants.GF_ABI_MINOR);
                }
                published = new AutomationTable(made);
                negotiated = published;
            }
            return published.table;
        }
    }

    // The library's handle to one registration, freed by release.
    private sealed class ToolRegistration
    {
        public readonly ModelToolHandler handler;
        public GCHandle self;

        public ToolRegistration(ModelToolHandler handler)
        {
            this.handler = handler;
        }
    }

    private sealed class SectionRegistration
    {
        public readonly ModelInspectHandler handler;
        public GCHandle self;

        public SectionRegistration(ModelInspectHandler handler)
        {
            this.handler = handler;
        }
    }

    /// <summary>Whether this library serves MCP automation at all.</summary>
    public static bool capability_built
    {
        get
        {
            uint built = 0;
            Abi.check(table().capability_built(&built));
            return built != 0;
        }
    }

    /// <summary>Declares a tool; throws GuiFormsException on a refusal (a name taken, a record that breaks its rules).</summary>
    public static void register(ModelToolSpec spec, ModelToolHandler handler)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(handler);
        ToolRegistration registration = new ToolRegistration(handler);
        registration.self = GCHandle.Alloc(registration);
        byte[] name = Encoding.UTF8.GetBytes(spec.name);
        byte[] title = Encoding.UTF8.GetBytes(spec.title);
        byte[] description = Encoding.UTF8.GetBytes(spec.description);
        byte[] schema = Encoding.UTF8.GetBytes(spec.input_schema);
        gf_status status;
        fixed (byte* name_data = name)
        fixed (byte* title_data = title)
        fixed (byte* description_data = description)
        fixed (byte* schema_data = schema)
        {
            gf_automation_tool tool = default;
            tool.struct_size = (uint)sizeof(gf_automation_tool);
            tool.access = spec.acting ? Constants.GF_AUTOMATION_TOOL_ACTING : Constants.GF_AUTOMATION_TOOL_READ_ONLY;
            tool.flags = (spec.destructive ? Constants.GF_AUTOMATION_TOOL_DESTRUCTIVE : 0u) |
                         (spec.any_thread ? Constants.GF_AUTOMATION_TOOL_ANY_THREAD : 0u) |
                         (spec.settles ? Constants.GF_AUTOMATION_TOOL_SETTLES : 0u);
            tool.name = Utf8.view(name_data, name.Length);
            tool.title = Utf8.view(title_data, title.Length);
            tool.description = Utf8.view(description_data, description.Length);
            tool.input_schema = Utf8.view(schema_data, schema.Length);
            tool.context = (void*)GCHandle.ToIntPtr(registration.self);
            tool.call = &call_thunk;
            tool.release = &release_tool_thunk;
            status = table().register_tool(&tool);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            // A refused registration is never released by the library.
            registration.self.Free();
            Abi.check(status);
        }
    }

    /// <summary>Withdraws a tool; false when none has the name.</summary>
    public static bool unregister(string name)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(name);
        gf_status status;
        fixed (byte* data = bytes)
        {
            status = table().unregister_tool(Utf8.view(data, bytes.Length));
        }
        if (status == Constants.GF_STATUS_NOT_FOUND)
        {
            return false;
        }
        Abi.check(status);
        return true;
    }

    /// <summary>Declares an inspect section (inspect's what); throws on a refusal.</summary>
    public static void register_inspect_section(string name, string description, ModelInspectHandler handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        SectionRegistration registration = new SectionRegistration(handler);
        registration.self = GCHandle.Alloc(registration);
        byte[] name_bytes = Encoding.UTF8.GetBytes(name);
        byte[] description_bytes = Encoding.UTF8.GetBytes(description);
        gf_status status;
        fixed (byte* name_data = name_bytes)
        fixed (byte* description_data = description_bytes)
        {
            gf_automation_inspect_section section = default;
            section.struct_size = (uint)sizeof(gf_automation_inspect_section);
            section.name = Utf8.view(name_data, name_bytes.Length);
            section.description = Utf8.view(description_data, description_bytes.Length);
            section.context = (void*)GCHandle.ToIntPtr(registration.self);
            section.inspect = &inspect_thunk;
            section.release = &release_section_thunk;
            status = table().register_inspect_section(&section);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            registration.self.Free();
            Abi.check(status);
        }
    }

    /// <summary>Withdraws an inspect section; false when none has the name.</summary>
    public static bool unregister_inspect_section(string name)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(name);
        gf_status status;
        fixed (byte* data = bytes)
        {
            status = table().unregister_inspect_section(Utf8.view(data, bytes.Length));
        }
        if (status == Constants.GF_STATUS_NOT_FOUND)
        {
            return false;
        }
        Abi.check(status);
        return true;
    }

    // ---------------------------------------------------------------------
    // Thunks: the library's entry points. No exception crosses back.

    private static string text_of(gf_utf8_view view) =>
        view.size == 0 ? "" : Encoding.UTF8.GetString(view.data, checked((int)view.size));

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void call_thunk(void* context, gf_automation_tool_call* call)
    {
        ModelToolReply reply = new ModelToolReply((*call).reply);
        try
        {
            ToolRegistration registration = (ToolRegistration)GCHandle.FromIntPtr((IntPtr)context).Target!;
            ModelToolCall read = new ModelToolCall(text_of((*call).arguments_json), text_of((*call).client_name),
                                                   text_of((*call).model_name), text_of((*call).window_id));
            registration.handler(read, reply);
        }
        catch (Exception exception)
        {
            byte[] message = Encoding.UTF8.GetBytes(exception.GetType().Name + ": " + exception.Message);
            fixed (byte* data = message)
            {
                gf_utf8_view none = default;
                // The status is not reported: the call fails either way.
                _ = table().reply_fail((*call).reply, Constants.GF_AUTOMATION_TOOL_ERROR_INTERNAL,
                                       Utf8.view(data, message.Length), none);
            }
        }
        finally
        {
            reply.end();
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void inspect_thunk(void* context, gf_automation_inspect_call* call)
    {
        string text;
        try
        {
            SectionRegistration registration = (SectionRegistration)GCHandle.FromIntPtr((IntPtr)context).Target!;
            text = registration.handler(new ModelInspectRequest(text_of((*call).window_id), text_of((*call).node_id),
                                                                text_of((*call).arguments_json)));
        }
        catch (Exception exception)
        {
            text = "(the section failed: " + exception.Message + ")\n";
        }
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        fixed (byte* data = bytes)
        {
            _ = table().inspect_append((*call).text, Utf8.view(data, bytes.Length));
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void release_tool_thunk(void* context)
    {
        GCHandle.FromIntPtr((IntPtr)context).Free();
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void release_section_thunk(void* context)
    {
        GCHandle.FromIntPtr((IntPtr)context).Free();
    }
}
