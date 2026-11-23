// The multiple document interface for C# (C ABI 1.6; DK decisions 60 to
// 63): Windows Forms' Form MDI calls over a window's docking document area
// (the windows table's MDI slots), and the dock workspace's frames
// presentation (the docking table's 1.6 slots and edit kinds). An MDI child
// is a document of the area, named by its panel id, never a second window.
// Every call is the UI thread's; a refusal throws GuiFormsException.
using System;
using System.Text;

namespace GuiForms.Interop;

/// <summary>
/// A window's MDI calls (Form.IsMdiContainer, MdiChildren, ActiveMdiChild,
/// ActivateMdiChild, LayoutMdi, MdiChildrenMinimizedAnchorBottom,
/// MainMenuStrip). The window is one from windows.create, or a control in
/// one. Without a bound document area the window keeps its settings, has no
/// children and arranges nothing.
/// </summary>
public static unsafe class Mdi
{
    public static bool is_mdi_container(ObjectHandle window)
    {
        uint container = 0;
        Abi.check(Abi.windows.mdi_container(window.value, &container));
        return container == 1u;
    }

    public static void set_mdi_container(ObjectHandle window, bool container)
    {
        Abi.check(Abi.windows.set_mdi_container(window.value, container ? 1u : 0u));
    }

    /// <summary>The children's ids, in tree and tab order.</summary>
    public static string[] mdi_children(ObjectHandle window)
    {
        gf_windows_table table = Abi.windows;
        ulong count = 0;
        Abi.check(table.mdi_child_count(window.value, &count));
        string[] children = new string[checked((int)count)];
        for (int index = 0; index < children.Length; ++index)
        {
            ulong required = 0;
            gf_status status = table.mdi_child_at(window.value, (ulong)index, null, 0, &required);
            if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
            {
                Abi.check(status);
            }
            byte[] text = new byte[checked((int)required)];
            fixed (byte* buffer = text)
            {
                status = table.mdi_child_at(window.value, (ulong)index, buffer, (ulong)text.Length, &required);
            }
            Abi.check(status);
            children[index] = Encoding.UTF8.GetString(text, 0, checked((int)required));
        }
        return children;
    }

    /// <summary>The active child's id, or null when there is none.</summary>
    public static string? active_mdi_child(ObjectHandle window)
    {
        gf_windows_table table = Abi.windows;
        ulong required = 0;
        gf_status status = table.active_mdi_child(window.value, null, 0, &required);
        if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
        {
            Abi.check(status);
        }
        if (required == 0)
        {
            return null;
        }
        byte[] text = new byte[checked((int)required)];
        fixed (byte* buffer = text)
        {
            status = table.active_mdi_child(window.value, buffer, (ulong)text.Length, &required);
        }
        Abi.check(status);
        return Encoding.UTF8.GetString(text, 0, checked((int)required));
    }

    /// <summary>Activates a child; false for one the area does not hold.</summary>
    public static bool activate_mdi_child(ObjectHandle window, string child)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(child);
        uint activated = 0;
        fixed (byte* data = bytes)
        {
            Abi.check(Abi.windows.activate_mdi_child(window.value, Utf8.view(data, bytes.Length), &activated));
        }
        return activated == 1u;
    }

    /// <summary>Arranges the children's frames; false when there was nothing to arrange.</summary>
    public static bool layout_mdi(ObjectHandle window, gf_mdi_layout layout)
    {
        uint arranged = 0;
        Abi.check(Abi.windows.layout_mdi(window.value, layout, &arranged));
        return arranged == 1u;
    }

    public static bool minimized_anchor_bottom(ObjectHandle window)
    {
        uint bottom = 0;
        Abi.check(Abi.windows.mdi_children_minimized_anchor_bottom(window.value, &bottom));
        return bottom == 1u;
    }

    public static void set_minimized_anchor_bottom(ObjectHandle window, bool bottom)
    {
        Abi.check(Abi.windows.set_mdi_children_minimized_anchor_bottom(window.value, bottom ? 1u : 0u));
    }

    /// <summary>
    /// Sets the window's main menu strip (a menu_strip object), or clears it
    /// with null. The window holds it weakly: the caller's handle keeps it.
    /// </summary>
    public static void set_main_menu_strip(ObjectHandle window, ObjectHandle? strip)
    {
        gf_object value = strip != null ? strip.value : 0;
        Abi.check(Abi.windows.set_main_menu_strip(window.value, value));
    }

    /// <summary>The window's main menu strip as a new handle, or null when none is set or it is gone.</summary>
    public static ObjectHandle? main_menu_strip(ObjectHandle window)
    {
        gf_object strip = 0;
        Abi.check(Abi.windows.main_menu_strip(window.value, &strip));
        return strip != 0 ? ObjectHandle.adopt(strip) : null;
    }
}

/// <summary>
/// A dock workspace's frames presentation (the docking table's 1.6 slots
/// and edit kinds): the presentation, each document's frame, the
/// arrangements, the area's documents and a panel's menu strip; and its
/// tool strip and ribbon merging (the 1.7 slots).
/// </summary>
public static unsafe class DockFrames
{
    // The negotiated docking table, published once; null until the first
    // negotiation succeeds. A refusal throws and publishes nothing.
    private static readonly object negotiation_lock = new object();
    private static volatile DockingTable? negotiated;

    private sealed class DockingTable
    {
        public readonly gf_docking_table table;

        public DockingTable(gf_docking_table table)
        {
            this.table = table;
        }
    }

    /// <summary>The docking table, negotiated once at the bindings' ABI version.</summary>
    public static gf_docking_table docking()
    {
        DockingTable? published = negotiated;
        if (published != null)
        {
            return published.table;
        }
        lock (negotiation_lock)
        {
            published = negotiated;
            if (published == null)
            {
                gf_status status = Exports.get_docking_table(out gf_docking_table table);
                if (status != Constants.GF_STATUS_OK)
                {
                    throw new GuiFormsException(status, "the docking table did not negotiate at ABI " +
                                                            Constants.GF_ABI_MAJOR + "." + Constants.GF_ABI_MINOR);
                }
                published = new DockingTable(table);
                negotiated = published;
            }
            return published.table;
        }
    }

    public static gf_dock_document_presentation presentation(ObjectHandle workspace)
    {
        gf_dock_document_presentation shown = 0;
        Abi.check(docking().document_presentation(workspace.value, &shown));
        return shown;
    }

    /// <summary>Shows the area's documents as tabs or frames (one program edit).</summary>
    public static void set_presentation(ObjectHandle workspace, gf_dock_document_presentation presentation)
    {
        gf_dock_edit edit = new_edit(Constants.GF_DOCK_EDIT_SET_DOCUMENT_PRESENTATION);
        edit.presentation = presentation;
        apply(workspace, &edit);
    }

    /// <summary>A document frame's normal bounds, in the document area's coordinates.</summary>
    public static void set_frame_bounds(ObjectHandle workspace, string document, gf_logical_rect bounds)
    {
        gf_dock_edit edit = new_edit(Constants.GF_DOCK_EDIT_SET_FRAME_BOUNDS);
        edit.frame = bounds;
        apply_to(workspace, document, &edit);
    }

    /// <summary>A document frame's state; maximizing one restores a maximized other.</summary>
    public static void set_frame_state(ObjectHandle workspace, string document, gf_dock_frame_state state)
    {
        gf_dock_edit edit = new_edit(Constants.GF_DOCK_EDIT_SET_FRAME_STATE);
        edit.frame_state = state;
        apply_to(workspace, document, &edit);
    }

    /// <summary>A document's frame; present is 0 when it has none.</summary>
    public static gf_dock_frame_memory frame(ObjectHandle workspace, string document)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(document);
        gf_dock_frame_memory memory = default;
        memory.struct_size = (uint)sizeof(gf_dock_frame_memory);
        fixed (byte* data = bytes)
        {
            Abi.check(docking().frame_memory(workspace.value, Utf8.view(data, bytes.Length), &memory));
        }
        return memory;
    }

    /// <summary>Arranges the frames as last laid out; a refusal throws.</summary>
    public static void arrange(ObjectHandle workspace, gf_dock_frame_arrangement arrangement)
    {
        gf_dock_refusal refusal = 0;
        Abi.check(docking().arrange_frames(workspace.value, arrangement, &refusal));
    }

    public static void set_icons_at_bottom(ObjectHandle workspace, bool bottom)
    {
        Abi.check(docking().set_frame_icons_at_bottom(workspace.value, bottom ? 1u : 0u));
    }

    /// <summary>The documents of the main site's document area, in tree and tab order.</summary>
    public static string[] area_documents(ObjectHandle workspace)
    {
        gf_docking_table table = docking();
        ulong count = 0;
        Abi.check(table.area_document_count(workspace.value, &count));
        string[] documents = new string[checked((int)count)];
        for (int index = 0; index < documents.Length; ++index)
        {
            ulong required = 0;
            gf_status status = table.area_document_at(workspace.value, (ulong)index, null, 0, &required);
            if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
            {
                Abi.check(status);
            }
            byte[] text = new byte[checked((int)required)];
            fixed (byte* buffer = text)
            {
                status = table.area_document_at(workspace.value, (ulong)index, buffer, (ulong)text.Length, &required);
            }
            Abi.check(status);
            documents[index] = Encoding.UTF8.GetString(text, 0, checked((int)required));
        }
        return documents;
    }

    /// <summary>The area's active document, or null when the area is empty.</summary>
    public static string? active_document(ObjectHandle workspace)
    {
        gf_docking_table table = docking();
        ulong required = 0;
        gf_status status = table.area_active_document(workspace.value, null, 0, &required);
        if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
        {
            Abi.check(status);
        }
        if (required == 0)
        {
            return null;
        }
        byte[] text = new byte[checked((int)required)];
        fixed (byte* buffer = text)
        {
            status = table.area_active_document(workspace.value, buffer, (ulong)text.Length, &required);
        }
        Abi.check(status);
        return Encoding.UTF8.GetString(text, 0, checked((int)required));
    }

    /// <summary>
    /// Gives a panel a menu strip that merges into the main window's main
    /// menu strip while its document is active, or takes it away with null.
    /// </summary>
    public static void set_panel_menu_strip(ObjectHandle workspace, string panel, ObjectHandle? strip)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(panel);
        gf_object value = strip != null ? strip.value : 0;
        fixed (byte* data = bytes)
        {
            Abi.check(docking().set_panel_menu_strip(workspace.value, Utf8.view(data, bytes.Length), value));
        }
    }

    /// <summary>The document whose menu strip is merged into the main menu strip, or null.</summary>
    public static string? merged_document(ObjectHandle workspace)
    {
        return optional_document(workspace, docking().merged_document);
    }

    /// <summary>
    /// Names the workspace's merge tool strip (the main window's tool
    /// strip), into which the active document's tool strip merges, or
    /// clears it with null.
    /// </summary>
    public static void set_merge_tool_strip(ObjectHandle workspace, ObjectHandle? strip)
    {
        gf_object value = strip != null ? strip.value : 0;
        Abi.check(docking().set_merge_tool_strip(workspace.value, value));
    }

    /// <summary>The merge tool strip as a new handle, or null when none is set.</summary>
    public static ObjectHandle? merge_tool_strip(ObjectHandle workspace)
    {
        gf_object strip = 0;
        Abi.check(docking().merge_tool_strip(workspace.value, &strip));
        return strip != 0 ? ObjectHandle.adopt(strip) : null;
    }

    /// <summary>
    /// Gives a panel a tool strip that merges into the merge tool strip
    /// while its document is active, or takes it away with null.
    /// </summary>
    public static void set_panel_tool_strip(ObjectHandle workspace, string panel, ObjectHandle? strip)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(panel);
        gf_object value = strip != null ? strip.value : 0;
        fixed (byte* data = bytes)
        {
            Abi.check(docking().set_panel_tool_strip(workspace.value, Utf8.view(data, bytes.Length), value));
        }
    }

    /// <summary>The document whose tool strip is merged, or null.</summary>
    public static string? merged_tool_strip_document(ObjectHandle workspace)
    {
        return optional_document(workspace, docking().merged_tool_strip_document);
    }

    /// <summary>
    /// Names the workspace's merge ribbon, into which the active
    /// document's ribbon merges its tabs, or clears it with null.
    /// </summary>
    public static void set_merge_ribbon(ObjectHandle workspace, ObjectHandle? ribbon)
    {
        gf_object value = ribbon != null ? ribbon.value : 0;
        Abi.check(docking().set_merge_ribbon(workspace.value, value));
    }

    /// <summary>The merge ribbon as a new handle, or null when none is set.</summary>
    public static ObjectHandle? merge_ribbon(ObjectHandle workspace)
    {
        gf_object ribbon = 0;
        Abi.check(docking().merge_ribbon(workspace.value, &ribbon));
        return ribbon != 0 ? ObjectHandle.adopt(ribbon) : null;
    }

    /// <summary>
    /// Gives a panel a ribbon whose tabs merge into the merge ribbon while
    /// its document is active, or takes it away with null.
    /// </summary>
    public static void set_panel_ribbon(ObjectHandle workspace, string panel, ObjectHandle? ribbon)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(panel);
        gf_object value = ribbon != null ? ribbon.value : 0;
        fixed (byte* data = bytes)
        {
            Abi.check(docking().set_panel_ribbon(workspace.value, Utf8.view(data, bytes.Length), value));
        }
    }

    /// <summary>The document whose ribbon is merged, or null.</summary>
    public static string? merged_ribbon_document(ObjectHandle workspace)
    {
        return optional_document(workspace, docking().merged_ribbon_document);
    }

    // A document id a slot writes as area_active_document does, or null
    // when it writes none.
    private static string? optional_document(ObjectHandle workspace,
                                             delegate* unmanaged[Cdecl]<gf_object, byte*, ulong, ulong*, gf_status> read)
    {
        ulong required = 0;
        gf_status status = read(workspace.value, null, 0, &required);
        if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
        {
            Abi.check(status);
        }
        if (required == 0)
        {
            return null;
        }
        byte[] text = new byte[checked((int)required)];
        fixed (byte* buffer = text)
        {
            status = read(workspace.value, buffer, (ulong)text.Length, &required);
        }
        Abi.check(status);
        return Encoding.UTF8.GetString(text, 0, checked((int)required));
    }

    private static gf_dock_edit new_edit(gf_dock_edit_kind kind)
    {
        gf_dock_edit edit = default;
        edit.struct_size = (uint)sizeof(gf_dock_edit);
        edit.kind = kind;
        return edit;
    }

    private static void apply(ObjectHandle workspace, gf_dock_edit* edit)
    {
        gf_dock_refusal refusal = 0;
        Abi.check(docking().apply_edit(workspace.value, edit, &refusal));
    }

    private static void apply_to(ObjectHandle workspace, string document, gf_dock_edit* edit)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(document);
        fixed (byte* data = bytes)
        {
            (*edit).panel = Utf8.view(data, bytes.Length);
            apply(workspace, edit);
        }
    }
}
