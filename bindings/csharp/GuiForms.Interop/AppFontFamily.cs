// An application font family (GF_TABLE_FONTS load_family and unload_family,
// C ABI 1.5 provisional; owner decision of 2026-11-15, "App-loaded fonts:
// inside composites and subsurfaces"). The program's own font bytes, loaded
// as a family of its own whose id a font carries like a bundled family's
// (FontFamilies.with_family). It draws only inside composite controls (a
// composite document's look names it by its name) and sub-surfaces; stock
// controls and GUI.Forms' own interface draw their bundled faces instead.
//
// The library validates and copies the bytes on load, so the arrays may be
// dropped afterwards. The handle owns the family: dispose (or unload) takes
// it back, and the finalizer does when neither ran. Unloading stops new text
// from resolving the family at once; the library frees the bytes when the
// last text engine that opened them goes. Any thread may load or unload.
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading;

namespace GuiForms.Interop;

/// <summary>One face of an application family to load: its bytes, weight (1 to 1000) and slant.</summary>
public readonly struct AppFontFace
{
    public readonly byte[] bytes;
    public readonly uint weight;
    public readonly bool italic;

    public AppFontFace(byte[] bytes, uint weight = 400u, bool italic = false)
    {
        this.bytes = bytes ?? throw new ArgumentNullException(nameof(bytes));
        this.weight = weight;
        this.italic = italic;
    }
}

public sealed unsafe class AppFontFamily : IDisposable
{
    // The family's id while this handle owns it; zero once unloaded. Swapped
    // to zero exactly once, by unload, Dispose or the finalizer.
    private uint family;

    private AppFontFamily(uint family)
    {
        this.family = family;
    }

    /// <summary>The family's id (256 and up), zero once unloaded.</summary>
    public uint id => Volatile.Read(ref family);

    /// <summary>
    /// Loads faces (1 to GF_APP_FONT_MAXIMUM_FACES) as one family called
    /// name. A refusal (a face that fails the library's table checks, a
    /// taken or unnameable name) throws GuiFormsException with the reason.
    /// </summary>
    public static AppFontFamily load(string name, ReadOnlySpan<AppFontFace> faces)
    {
        ArgumentNullException.ThrowIfNull(name);
        if (faces.Length > (int)Constants.GF_APP_FONT_MAXIMUM_FACES)
        {
            throw new ArgumentOutOfRangeException(nameof(faces), "at most GF_APP_FONT_MAXIMUM_FACES faces");
        }
        byte[] name_bytes = Encoding.UTF8.GetBytes(name);
        gf_fonts_table table = FontFamilies.fonts();
        gf_app_font_face* records = stackalloc gf_app_font_face[(int)Constants.GF_APP_FONT_MAXIMUM_FACES];
        // Every face's bytes stay pinned for the call: the library copies them.
        GCHandle[] pins =
            new GCHandle[faces.Length];
        uint family = 0;
        gf_status status;
        try
        {
            for (int index = 0; index < faces.Length; index++)
            {
                AppFontFace face = faces[index];
                pins[index] = GCHandle.Alloc(
                    face.bytes, GCHandleType.Pinned);
                gf_app_font_face record = default;
                record.struct_size = (uint)sizeof(gf_app_font_face);
                record.weight = face.weight;
                record.italic = face.italic ? 1u : 0u;
                record.bytes.data = (byte*)pins[index].AddrOfPinnedObject();
                record.bytes.size = (ulong)face.bytes.Length;
                records[index] = record;
            }
            fixed (byte* text = name_bytes)
            {
                status = table.load_family(Utf8.view(text, name_bytes.Length), records, (ulong)faces.Length, &family);
            }
        }
        finally
        {
            foreach (GCHandle pin in pins)
            {
                if (pin.IsAllocated)
                {
                    pin.Free();
                }
            }
        }
        Abi.check(status);
        return new AppFontFamily(family);
    }

    /// <summary>
    /// Unloads the family now. True when the library deferred freeing its
    /// bytes (text it shaped still holds them), false when it freed them or
    /// the family was already unloaded.
    /// </summary>
    public bool unload()
    {
        uint taken = Interlocked.Exchange(ref family, 0u);
        if (taken == 0u)
        {
            return false;
        }
        GC.SuppressFinalize(this);
        uint deferred = 0u;
        Abi.check(FontFamilies.fonts().unload_family(taken, &deferred));
        return deferred == 1u;
    }

    public void Dispose()
    {
        unload();
    }

    ~AppFontFamily()
    {
        uint taken = Interlocked.Exchange(ref family, 0u);
        if (taken != 0u)
        {
            // The finalizer thread may not throw: a refusal leaves the family to the library's unload.
            FontFamilies.fonts().unload_family(taken, null);
        }
    }
}
