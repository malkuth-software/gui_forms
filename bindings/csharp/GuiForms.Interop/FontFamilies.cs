// The bundled font families a gf_font names (GF_TABLE_FONTS, gf_abi.h; the
// family registry, owner decisions of 2026-11-13). Interface text draws only
// families bundled with the library: a name resolves once to an id, through
// the library's substitution table for a Windows Forms name (Segoe UI to
// Selawik), and the id is what a font carries. Font.FontFamily.Name reads an
// id's name back. Both calls read constant tables, so any thread may call
// them.
using System;
using System.Text;

namespace GuiForms.Interop;

public static unsafe class FontFamilies
{
    // The negotiated fonts table, published once; null until the first
    // negotiation succeeds. A refusal throws and publishes nothing.
    private static readonly object negotiation_lock = new object();
    private static volatile FontsTable? negotiated;

    private sealed class FontsTable
    {
        public readonly gf_fonts_table table;

        public FontsTable(gf_fonts_table table)
        {
            this.table = table;
        }
    }

    private static gf_fonts_table fonts()
    {
        FontsTable? published = negotiated;
        if (published != null)
        {
            return published.table;
        }
        lock (negotiation_lock)
        {
            published = negotiated;
            if (published == null)
            {
                gf_status status = Exports.get_fonts_table(out gf_fonts_table table);
                if (status != Constants.GF_STATUS_OK)
                {
                    throw new GuiFormsException(status, "the fonts table did not negotiate at ABI " +
                                                            Constants.GF_ABI_MAJOR + "." + Constants.GF_ABI_MINOR);
                }
                published = new FontsTable(table);
                negotiated = published;
            }
            return published.table;
        }
    }

    /// <summary>
    /// The id of the bundled family a name resolves to, or 0 when the name is
    /// unmapped (no bundled family answers it, and the role's faces draw).
    /// </summary>
    public static uint resolve(string name)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(name);
        uint family = 0;
        gf_status status;
        fixed (byte* data = bytes)
        {
            status = fonts().family_id(Utf8.view(data, bytes.Length), &family);
        }
        if (status == Constants.GF_STATUS_NOT_FOUND)
        {
            return 0;
        }
        Abi.check(status);
        return family;
    }

    /// <summary>The name of the family id, or null for 0 or an id the library does not have.</summary>
    public static string? name(uint family)
    {
        gf_fonts_table table = fonts();
        ulong required = 0;
        gf_status status = table.family_name(family, null, 0, &required);
        if (status == Constants.GF_STATUS_NOT_FOUND)
        {
            return null;
        }
        if (status != Constants.GF_STATUS_OK && status != Constants.GF_STATUS_BUFFER_TOO_SMALL)
        {
            Abi.check(status);
        }
        byte[] text = new byte[checked((int)required)];
        fixed (byte* buffer = text)
        {
            status = table.family_name(family, buffer, (ulong)text.Length, &required);
        }
        Abi.check(status);
        return Encoding.UTF8.GetString(text, 0, checked((int)required));
    }

    /// <summary>The family id a font carries: the low half of its reserved word.</summary>
    public static uint family_of(in gf_font font)
    {
        return font.reserved & 0xFFFFu;
    }

    /// <summary>
    /// The font with its family set to a family id, keeping its family stack
    /// (the high half, a number valid in this process only).
    /// </summary>
    public static gf_font with_family(gf_font font, uint family)
    {
        if (family > 0xFFFFu)
        {
            throw new ArgumentOutOfRangeException(nameof(family), "a family id fits in 16 bits");
        }
        font.reserved = (font.reserved & 0xFFFF0000u) | family;
        return font;
    }
}
