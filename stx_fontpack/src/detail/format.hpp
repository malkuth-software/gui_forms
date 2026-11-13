// stx_fontpack: the pack's stream layout and the token grammars the
// packer and the loader must agree on. Anything both sides compute lives here
// so there is one definition.
#pragma once

#include "detail/base.hpp"
#include "stx_fontpack/limits.hpp"

namespace stx_fontpack {

// ---- container ---------------------------------------------------------------
//
// A pack is a container of groups. Each group is a complete, independent set
// of streams over its own fonts (a "body"); no match, context or shared table
// reaches from one group into another, so any group decodes alone and a
// later group's decode never touches an earlier one.
//
//   "FPK2"
//   varint group count                      1..kMaxGroups
//   per group:
//     varint name length, name bytes        1..kMaxNameBytes
//     u8 flags                              kGroupEager or 0
//     varint font count                     1..kMaxFonts (all groups together)
//     varint font bytes                     sum of the group's font sizes
//     varint stream bytes                   sum of the group's decoded stream sizes (a number counts 2 bytes)
//     varint body size                      coded bytes of the body
//     per font:
//       varint name length, name bytes      1..kMaxNameBytes
//       varint size                         1..kMaxFontBytes
//       u32 little-endian CRC-32 of the font (stx_zlib::crc32)
//   per group, in the same order: body, then kStreamPadding bytes
//
// A body is, for the group's fonts in order, kStreamCount varints each (the
// font's start in every stream), then kStreamCount varints (each stream's
// coded size), then the coded streams.
//
// The limits a loader enforces before it allocates anything for a group are
// in stx_fontpack/limits.hpp; the packer enforces each of them too, so every
// pack the packer writes loads.

// Every font contributes to the same set of streams, in font order, so a
// stream is "solid" across fonts and matches reach into earlier fonts.
enum StreamId : std::uint32_t {
    kStreamMeta = 0,        // per-font recipes: varints and small fields
    kStreamDirectory,       // sfnt header and table records, verbatim
    kStreamGap,             // non-zero bytes between tables (rare)
    kStreamRawOther,        // untransformed tables by family
    kStreamRawLayout,
    kStreamRawCmap,
    kStreamRawPost,
    kStreamRawName,
    kStreamRawMetrics,
    kStreamGlyphKind,       // one byte per glyph, see GlyphKind
    kStreamGlyphContours,   // contour count per simple glyph
    kStreamGlyphPoints,     // points per contour
    kStreamGlyphWide,       // escapes of the two streams above and raw glyph lengths (varints)
    kStreamGlyphBbox,       // explicit bounding boxes, 8 bytes each
    kStreamGlyphFlags,      // one triplet flag per point
    kStreamGlyphCoords,     // triplet payload bytes
    kStreamGlyphComposite,  // component records
    kStreamGlyphRaw,        // glyphs kept verbatim
    kStreamInstrLength,     // instruction byte counts (varints)
    kStreamInstrCode,       // opcodes and push counts
    kStreamInstrPush,       // pushed bytes and words
    kStreamCffResidual,     // CFF bytes outside the charstring INDEXes
    kStreamCffOps,          // one (operator, argument count) pair symbol per group, see kCffPairEscape
    kStreamCffPairTable,    // the pairs symbols 0..254 stand for: operator and count bytes
    kStreamCffNumbers,      // number tokens; a number stream (detail/codec.hpp), not a byte stream
    kStreamCffNumWide,      // payload of 3-byte and 5-byte numbers
    kStreamCffMask,         // hintmask and cntrmask bytes
    kStreamCffMaskLength,   // mask byte count, one per item that has a mask
    kStreamCffMisc,         // escaped operators' second bytes and raw tails
    kStreamCffIndex,        // per-chunk stream positions for parallel reconstruction, see CffIndexField
    kStreamCffNumMap,       // fine number context -> class
    kStreamHmtxAdvance,     // hmtx advance widths, two bytes each
    kStreamHmtxLsb,         // hmtx left side bearings minus the glyph's xMin, two bytes each
    kStreamCount
};

enum TableKind : std::uint32_t {
    kTableRaw = 0,
    kTableGlyf = 1,
    kTableLoca = 2,     // rebuilt from glyf
    kTableCff = 3,
    kTableInstr = 4,    // fpgm, prep: split like glyph instructions
    kTableHmtx = 5      // advances and bearings apart, bearings relative to the rebuilt glyf
};

// A raw table may be stored through a stride filter: with stride s > 0 every
// byte from the s-th on is stored as its difference from the byte s places
// before it. Tables of fixed-size records with slowly changing fields (cmap
// groups, metrics) become mostly zeros.
inline void stride_filter(std::uint8_t* bytes, std::size_t size, std::size_t stride) {
    if (stride == 0) {
        return;
    }
    for (std::size_t k = size; k > stride; k -= 1) {
        bytes[k - 1] = static_cast<std::uint8_t>(bytes[k - 1] - bytes[k - 1 - stride]);
    }
}

inline void stride_unfilter(std::uint8_t* bytes, std::size_t size, std::size_t stride) {
    if (stride == 0) {
        return;
    }
    for (std::size_t k = stride; k < size; k += 1) {
        bytes[k] = static_cast<std::uint8_t>(bytes[k] + bytes[k - stride]);
    }
}

// What an hmtx left side bearing is stored relative to: the xMin in the
// glyph's header, or 0 for a slot too short to have one. start and end are
// the glyph's loca offsets.
inline std::int32_t glyph_bearing_reference(const std::uint8_t* glyf, std::uint32_t start, std::uint32_t end) {
    if (end - start < 10) {
        return 0;
    }
    return be16s(glyf + start + 2);
}

// kStreamGlyphKind byte.
enum GlyphKind : std::uint32_t {
    kGlyphEmpty = 0,        // zero-length slot
    kGlyphSimple = 1,
    kGlyphComposite = 2,
    kGlyphVerbatim = 3,     // length in kStreamGlyphWide, bytes in kStreamGlyphRaw
    kGlyphKindMask = 3,
    kGlyphExplicitBbox = 4, // simple glyph whose stored box is not the points' box
    kGlyphOverlapBit = 8    // first flag carries the overlap bit
};

// TrueType simple-glyph flag bits.
enum : std::uint32_t {
    kTtOnCurve = 1,
    kTtXShort = 2,
    kTtYShort = 4,
    kTtRepeat = 8,
    kTtXSame = 16,
    kTtYSame = 32,
    kTtOverlap = 64
};

// CFF operator symbols beyond the real one-byte operators 0..31.
enum : std::uint32_t {
    kCffOpEnd = 32,       // end of item; its arguments are trailing numbers
    kCffOpRawTail = 33,   // rest of the item verbatim: varint length and bytes in kStreamCffMisc
    kCffOpMoreArgs = 34,  // 255 arguments with no operator; the group continues
    kCffOpCount = 35
};

// A group is an operator symbol and the count of numbers before it. The
// kCffPairCount most frequent (operator, count) pairs of a pack are one byte
// in kStreamCffOps; any other pair is kCffPairEscape, then the operator
// symbol, then the count.
enum : std::uint32_t {
    kCffPairCount = 255,
    kCffPairEscape = 255
};

// One kStreamCffIndex entry: where a chunk of kCffChunkItems items starts in
// each CFF stream and in its INDEX's data, as little-endian 32-bit fields.
enum CffIndexField : std::uint32_t {
    kCffIndexOps = 0,
    kCffIndexNumbers,
    kCffIndexNumWide,
    kCffIndexMask,
    kCffIndexMaskLength,
    kCffIndexMisc,
    kCffIndexData,
    kCffIndexFieldCount,
    kCffIndexEntryBytes = kCffIndexFieldCount * 4
};

enum : std::uint32_t {
    kCffChunkItems = 256,        // items per reconstruction chunk
    kCffNumFineCount = kCffOpCount * 25 * 24
};

// Fine context of argument index among count arguments of operator symbol op.
inline std::uint32_t cff_number_context_base(std::uint32_t op, std::uint32_t count) {
    std::uint32_t count_class = count;
    if (count > 18) {
        count_class = 19 + (count % 6);
    }
    return (op * 25 + count_class) * 24;
}

inline std::uint32_t cff_number_context_index(std::uint32_t index) {
    if (index < 12) {
        return index;
    }
    return 12 + (index % 12);
}

// Triplet coding of one point delta (the WOFF2 glyf transform's scheme).
// The flag's top bit is clear for an on-curve point.
struct Triplet {
    std::uint8_t flag;
    std::uint8_t count;
    std::uint8_t bytes[4];
};

inline Triplet triplet_encode(bool on_curve, std::int32_t dx, std::int32_t dy) {
    Triplet t;
    const std::uint32_t ax = static_cast<std::uint32_t>(dx < 0 ? -dx : dx);
    const std::uint32_t ay = static_cast<std::uint32_t>(dy < 0 ? -dy : dy);
    const std::uint32_t on_bit = on_curve ? 0u : 128u;
    const std::uint32_t xs = dx < 0 ? 0u : 1u;
    const std::uint32_t ys = dy < 0 ? 0u : 1u;
    const std::uint32_t xy = xs + 2 * ys;
    if (dx == 0 && ay < 1280) {
        t.flag = static_cast<std::uint8_t>(on_bit + ((ay & 0xF00u) >> 7) + ys);
        t.count = 1;
        t.bytes[0] = static_cast<std::uint8_t>(ay);
    } else if (dy == 0 && ax < 1280) {
        t.flag = static_cast<std::uint8_t>(on_bit + 10 + ((ax & 0xF00u) >> 7) + xs);
        t.count = 1;
        t.bytes[0] = static_cast<std::uint8_t>(ax);
    } else if (ax < 65 && ay < 65) {
        t.flag = static_cast<std::uint8_t>(on_bit + 20 + ((ax - 1) & 0x30u) + (((ay - 1) & 0x30u) >> 2) + xy);
        t.count = 1;
        t.bytes[0] = static_cast<std::uint8_t>((((ax - 1) & 0xFu) << 4) | ((ay - 1) & 0xFu));
    } else if (ax < 769 && ay < 769) {
        t.flag = static_cast<std::uint8_t>(on_bit + 84 + 12 * (((ax - 1) & 0x300u) >> 8) + (((ay - 1) & 0x300u) >> 6) + xy);
        t.count = 2;
        t.bytes[0] = static_cast<std::uint8_t>(ax - 1);
        t.bytes[1] = static_cast<std::uint8_t>(ay - 1);
    } else if (ax < 4096 && ay < 4096) {
        t.flag = static_cast<std::uint8_t>(on_bit + 120 + xy);
        t.count = 3;
        t.bytes[0] = static_cast<std::uint8_t>(ax >> 4);
        t.bytes[1] = static_cast<std::uint8_t>(((ax & 0xFu) << 4) | (ay >> 8));
        t.bytes[2] = static_cast<std::uint8_t>(ay);
    } else {
        t.flag = static_cast<std::uint8_t>(on_bit + 124 + xy);
        t.count = 4;
        t.bytes[0] = static_cast<std::uint8_t>(ax >> 8);
        t.bytes[1] = static_cast<std::uint8_t>(ax);
        t.bytes[2] = static_cast<std::uint8_t>(ay >> 8);
        t.bytes[3] = static_cast<std::uint8_t>(ay);
    }
    return t;
}

// Payload byte count for a triplet flag.
inline std::uint32_t triplet_bytes(std::uint32_t flag) {
    const std::uint32_t f = flag & 127u;
    if (f < 84) {
        return 1;
    }
    if (f < 120) {
        return 2;
    }
    if (f < 124) {
        return 3;
    }
    return 4;
}

// Class of the first payload byte of a triplet; later bytes follow consecutively.
// 0: dy only, 1: dx only, 2: both small, 3..4: medium, 5..7: 12-bit, 8..11: 16-bit.
inline std::uint32_t triplet_class(std::uint32_t flag) {
    const std::uint32_t f = flag & 127u;
    if (f < 10) {
        return 0;
    }
    if (f < 20) {
        return 1;
    }
    if (f < 84) {
        return 2;
    }
    if (f < 120) {
        return 3;
    }
    if (f < 124) {
        return 5;
    }
    return 8;
}

enum : std::uint32_t { kTripletClassCount = 12 };

inline void triplet_decode(std::uint32_t flag, const std::uint8_t* in, std::int32_t& dx, std::int32_t& dy) {
    const std::uint32_t f = flag & 127u;
    std::int32_t x;
    std::int32_t y;
    if (f < 10) {
        x = 0;
        y = static_cast<std::int32_t>(((f & 14u) << 7) + in[0]);
        dx = 0;
        dy = (f & 1u) != 0 ? y : -y;
        return;
    }
    if (f < 20) {
        x = static_cast<std::int32_t>((((f - 10) & 14u) << 7) + in[0]);
        dx = (f & 1u) != 0 ? x : -x;
        dy = 0;
        return;
    }
    if (f < 84) {
        const std::uint32_t b0 = f - 20;
        const std::uint32_t b1 = in[0];
        x = static_cast<std::int32_t>(1 + (b0 & 0x30u) + (b1 >> 4));
        y = static_cast<std::int32_t>(1 + ((b0 & 0x0Cu) << 2) + (b1 & 0x0Fu));
    } else if (f < 120) {
        const std::uint32_t b0 = f - 84;
        x = static_cast<std::int32_t>(1 + ((b0 / 12) << 8) + in[0]);
        y = static_cast<std::int32_t>(1 + (((b0 % 12) >> 2) << 8) + in[1]);
    } else if (f < 124) {
        const std::uint32_t b2 = in[1];
        x = static_cast<std::int32_t>((static_cast<std::uint32_t>(in[0]) << 4) + (b2 >> 4));
        y = static_cast<std::int32_t>(((b2 & 0x0Fu) << 8) + in[2]);
    } else {
        x = static_cast<std::int32_t>((static_cast<std::uint32_t>(in[0]) << 8) + in[1]);
        y = static_cast<std::int32_t>((static_cast<std::uint32_t>(in[2]) << 8) + in[3]);
    }
    dx = (f & 1u) != 0 ? x : -x;
    dy = (f & 2u) != 0 ? y : -y;
}

// TrueType instructions: bytes of pushed data that follow opcode at code[0],
// given the bytes remaining after the opcode. count_byte is 1 when a count
// byte follows the opcode (NPUSHB, NPUSHW) and is present.
inline std::uint32_t instr_push_bytes(const std::uint8_t* code, std::uint32_t remaining_after, std::uint32_t& count_byte) {
    const std::uint32_t op = code[0];
    std::uint32_t wanted = 0;
    count_byte = 0;
    if (op == 0x40 || op == 0x41) {
        if (remaining_after == 0) {
            return 0;
        }
        count_byte = 1;
        wanted = code[1];
        if (op == 0x41) {
            wanted *= 2;
        }
        remaining_after -= 1;
    } else if (op >= 0xB0 && op <= 0xB7) {
        wanted = op - 0xB0 + 1;
    } else if (op >= 0xB8 && op <= 0xBF) {
        wanted = (op - 0xB8 + 1) * 2;
    } else {
        return 0;
    }
    return wanted < remaining_after ? wanted : remaining_after;
}

}  // namespace stx_fontpack

