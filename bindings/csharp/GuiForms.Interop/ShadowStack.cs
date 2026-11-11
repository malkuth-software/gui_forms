// The library's shadow frames (GF_TABLE_DIAGNOSTICS) as managed records: the
// logical calls, events and posted callbacks on the calling thread, outermost
// first. A thunk reads them before its callback returns, while the frames it
// runs under are still pushed.
using System;
using System.Diagnostics;
using System.Text;

namespace GuiForms.Interop;

/// <summary>One shadow frame, its fixed-size texts decoded.</summary>
[DebuggerDisplay("{kind_name,nq} {subject,nq} {name,nq}")]
public sealed unsafe class ShadowFrame
{
    public gf_shadow_frame_kind kind { get; }
    public gf_shadow_frame_flags flags { get; }
    public gf_table_id table_id { get; }
    public uint slot_index { get; }
    public ulong sequence { get; }
    public string subject { get; }
    public string name { get; }

    internal ShadowFrame(gf_shadow_frame_kind kind, gf_shadow_frame_flags flags, gf_table_id table_id,
                         uint slot_index, ulong sequence, string subject, string name)
    {
        this.kind = kind;
        this.flags = flags;
        this.table_id = table_id;
        this.slot_index = slot_index;
        this.sequence = sequence;
        this.subject = subject;
        this.name = name;
    }

    /// <summary>The frame kind's name, as gf_abi.h spells it after GF_SHADOW_FRAME_KIND_.</summary>
    public string kind_name
    {
        get
        {
            switch (kind)
            {
            case Constants.GF_SHADOW_FRAME_KIND_LIBRARY_CALL:
                return "library_call";
            case Constants.GF_SHADOW_FRAME_KIND_INPUT_DISPATCH:
                return "input_dispatch";
            case Constants.GF_SHADOW_FRAME_KIND_EVENT_CALLBACK:
                return "event_callback";
            case Constants.GF_SHADOW_FRAME_KIND_LAYOUT_PASS:
                return "layout_pass";
            case Constants.GF_SHADOW_FRAME_KIND_PAINT_PASS:
                return "paint_pass";
            case Constants.GF_SHADOW_FRAME_KIND_POSTED_CALLBACK:
                return "posted_callback";
            default:
                return "kind " + kind;
            }
        }
    }

    public override string ToString()
    {
        return kind_name + " " + subject + " " + name;
    }

    /// <summary>A managed copy of one library record, its texts decoded within their capacities.</summary>
    internal static ShadowFrame from_native(gf_shadow_frame* frame)
    {
        int subject_size = (int)Math.Min((*frame).subject_size, Constants.GF_SHADOW_FRAME_SUBJECT_CAPACITY);
        int name_size = (int)Math.Min((*frame).name_size, Constants.GF_SHADOW_FRAME_NAME_CAPACITY);
        string subject = Encoding.UTF8.GetString((*frame).subject, subject_size);
        string name = Encoding.UTF8.GetString((*frame).name, name_size);
        return new ShadowFrame((*frame).kind, (*frame).flags, (*frame).table_id, (*frame).slot_index,
                               (*frame).sequence, subject, name);
    }
}

public static unsafe class ShadowStack
{
    /// <summary>The calling thread's depth (the frames pushed and not yet popped).</summary>
    public static uint depth()
    {
        gf_shadow_stack_info info = default;
        info.struct_size = (uint)sizeof(gf_shadow_stack_info);
        ulong required = 0;
        Abi.check_status(Abi.diagnostics.shadow_frames(null, 0, &required, &info));
        return info.depth;
    }

    /// <summary>
    /// The calling thread's frames, outermost first. The stack holds at most
    /// GF_SHADOW_STACK_CAPACITY frames, so one fixed buffer always suffices.
    /// </summary>
    public static ShadowFrame[] snapshot()
    {
        const int capacity = (int)Constants.GF_SHADOW_STACK_CAPACITY;
        gf_shadow_frame* frames = stackalloc gf_shadow_frame[capacity];
        gf_shadow_stack_info info = default;
        info.struct_size = (uint)sizeof(gf_shadow_stack_info);
        ulong required = 0;
        Abi.check_status(Abi.diagnostics.shadow_frames(frames, (ulong)capacity, &required, &info));
        int count = (int)Math.Min(required, (ulong)capacity);
        ShadowFrame[] result = new ShadowFrame[count];
        for (int index = 0; index < count; ++index)
        {
            result[index] = ShadowFrame.from_native(frames + index);
        }
        return result;
    }
}
