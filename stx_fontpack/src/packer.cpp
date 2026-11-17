// stx_fontpack: the packer. Splits each font into the pack's streams
// and codes every stream. Every transform is checked for exact reproduction
// here; anything that would not reproduce is stored verbatim instead.
#include "stx_fontpack/packer.hpp"

#include "detail/codec.hpp"
#include "detail/glyph.hpp"

#include <stx_zlib/checksum.hpp>

#include <algorithm>
#include <memory>
#include <span>

namespace stx_fontpack {

namespace {

struct Streams {
    Bytes bytes[kStreamCount];
    // CFF groups before pair coding: operator symbols and argument counts, one each per group.
    // kStreamCffOps is built from them once every font has been tokenised.
    Bytes group_ops;
    Bytes group_counts;
    // CFF number tokens; coded as the number stream kStreamCffNumbers.
    std::vector<std::uint16_t> numbers;
};

struct TableRecord {
    std::uint32_t tag;
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t index;   // position in the directory
};

struct ByOffset {
    bool operator()(const TableRecord& a, const TableRecord& b) const {
        if (a.offset != b.offset) {
            return a.offset < b.offset;
        }
        return a.index < b.index;
    }
};

std::uint32_t tag_of(const char* text) {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[3]));
}

StreamId raw_stream_for(std::uint32_t tag) {
    if (tag == tag_of("GPOS") || tag == tag_of("GSUB") || tag == tag_of("GDEF") || tag == tag_of("BASE") ||
        tag == tag_of("kern") || tag == tag_of("JSTF")) {
        return kStreamRawLayout;
    }
    if (tag == tag_of("cmap")) {
        return kStreamRawCmap;
    }
    if (tag == tag_of("post")) {
        return kStreamRawPost;
    }
    if (tag == tag_of("name")) {
        return kStreamRawName;
    }
    if (tag == tag_of("hmtx") || tag == tag_of("vmtx") || tag == tag_of("hdmx") || tag == tag_of("VORG") ||
        tag == tag_of("LTSH") || tag == tag_of("cvt ")) {
        return kStreamRawMetrics;
    }
    return kStreamRawOther;
}

void append_raw_table(Bytes& stream, const std::uint8_t* data, std::size_t size) {
    stream.insert(stream.end(), data, data + size);
    // Raw streams keep every table 4-aligned so position-parity contexts line up.
    while ((stream.size() & 3u) != 0) {
        stream.push_back(0);
    }
}

// ---- TrueType instructions -------------------------------------------------

void split_instructions(const std::uint8_t* code, std::uint32_t size, Streams& streams) {
    std::uint32_t at = 0;
    while (at < size) {
        std::uint32_t count_byte = 0;
        const std::uint32_t push = instr_push_bytes(code + at, size - at - 1, count_byte);
        streams.bytes[kStreamInstrCode].push_back(code[at]);
        at += 1;
        if (count_byte != 0) {
            streams.bytes[kStreamInstrCode].push_back(code[at]);
            at += 1;
        }
        streams.bytes[kStreamInstrPush].insert(streams.bytes[kStreamInstrPush].end(), code + at, code + at + push);
        at += push;
    }
}

// ---- glyf ------------------------------------------------------------------

struct ParsedGlyph {
    std::uint32_t kind;                   // GlyphKind type bits
    std::uint32_t length;                 // structural byte length (without padding)
    std::int32_t contour_count;
    std::vector<std::uint32_t> contour_points;
    std::uint32_t instruction_offset;
    std::uint32_t instruction_length;
    std::uint32_t point_count;
    std::uint32_t points_offset;          // where the flag array starts
    std::vector<std::uint8_t> triplet_flags;   // top bit clear = on curve; low bits unused here
    std::vector<std::int16_t> dx;
    std::vector<std::int16_t> dy;
    bool overlap_first;
    bool explicit_bbox;
    std::uint32_t composite_length;       // component records' bytes
    bool composite_instructions;
};

// Parses a glyph slot. Returns false when the slot cannot be described by
// the simple or composite grammar (it is then kept verbatim).
bool glyph_parse(const std::uint8_t* data, std::uint32_t size, ParsedGlyph& glyph) {
    if (size < 10) {
        return false;
    }
    glyph.contour_count = be16s(data);
    glyph.overlap_first = false;
    glyph.explicit_bbox = true;
    if (glyph.contour_count < 0) {
        if (glyph.contour_count != -1) {
            return false;
        }
        bool has_instructions = false;
        const std::size_t records = tt_composite_length(data + 10, size - 10, has_instructions);
        if (records == 0) {
            return false;
        }
        glyph.kind = kGlyphComposite;
        glyph.composite_length = static_cast<std::uint32_t>(records);
        glyph.composite_instructions = has_instructions;
        glyph.instruction_offset = 0;
        glyph.instruction_length = 0;
        std::uint32_t at = 10 + glyph.composite_length;
        if (has_instructions) {
            if (size - at < 2) {
                return false;
            }
            glyph.instruction_length = be16(data + at);
            at += 2;
            if (size - at < glyph.instruction_length) {
                return false;
            }
            glyph.instruction_offset = at;
            at += glyph.instruction_length;
        }
        glyph.length = at;
        return true;
    }
    glyph.kind = kGlyphSimple;
    const std::uint32_t contours = static_cast<std::uint32_t>(glyph.contour_count);
    std::uint32_t at = 10;
    if (size - at < contours * 2 + 2) {
        return false;
    }
    glyph.contour_points.clear();
    std::uint32_t previous_end = 0;
    for (std::uint32_t c = 0; c < contours; c += 1) {
        const std::uint32_t end = be16(data + at) + 1;
        at += 2;
        if (end <= previous_end) {
            return false;
        }
        glyph.contour_points.push_back(end - previous_end);
        previous_end = end;
    }
    glyph.point_count = previous_end;
    glyph.instruction_length = be16(data + at);
    at += 2;
    if (size - at < glyph.instruction_length) {
        return false;
    }
    glyph.instruction_offset = at;
    at += glyph.instruction_length;
    glyph.points_offset = at;

    std::vector<std::uint8_t> flags;
    while (flags.size() < glyph.point_count) {
        if (at >= size) {
            return false;
        }
        const std::uint8_t flag = data[at];
        at += 1;
        std::uint32_t repeat = 1;
        if ((flag & kTtRepeat) != 0) {
            if (at >= size) {
                return false;
            }
            repeat += data[at];
            at += 1;
        }
        if (flags.size() + repeat > glyph.point_count) {
            return false;
        }
        flags.insert(flags.end(), repeat, static_cast<std::uint8_t>(flag & ~kTtRepeat));
    }
    glyph.triplet_flags.resize(glyph.point_count);
    glyph.dx.resize(glyph.point_count);
    glyph.dy.resize(glyph.point_count);
    for (std::uint32_t k = 0; k < glyph.point_count; k += 1) {
        const std::uint32_t flag = flags[k];
        std::int32_t d = 0;
        if ((flag & kTtXShort) != 0) {
            if (at >= size) {
                return false;
            }
            d = data[at];
            at += 1;
            if ((flag & kTtXSame) == 0) {
                d = -d;
            }
        } else if ((flag & kTtXSame) == 0) {
            if (size - at < 2) {
                return false;
            }
            d = be16s(data + at);
            at += 2;
        }
        glyph.dx[k] = static_cast<std::int16_t>(d);
    }
    for (std::uint32_t k = 0; k < glyph.point_count; k += 1) {
        const std::uint32_t flag = flags[k];
        std::int32_t d = 0;
        if ((flag & kTtYShort) != 0) {
            if (at >= size) {
                return false;
            }
            d = data[at];
            at += 1;
            if ((flag & kTtYSame) == 0) {
                d = -d;
            }
        } else if ((flag & kTtYSame) == 0) {
            if (size - at < 2) {
                return false;
            }
            d = be16s(data + at);
            at += 2;
        }
        glyph.dy[k] = static_cast<std::int16_t>(d);
        glyph.triplet_flags[k] = (flag & kTtOnCurve) != 0 ? 0 : 128;
    }
    glyph.length = at;
    if (glyph.point_count != 0 && (flags[0] & kTtOverlap) != 0) {
        glyph.overlap_first = true;
    }

    // The box the loader would compute.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t x_min = 0;
    std::int32_t y_min = 0;
    std::int32_t x_max = 0;
    std::int32_t y_max = 0;
    for (std::uint32_t k = 0; k < glyph.point_count; k += 1) {
        x += glyph.dx[k];
        y += glyph.dy[k];
        if (k == 0) {
            x_min = x;
            x_max = x;
            y_min = y;
            y_max = y;
        } else {
            x_min = std::min(x_min, x);
            x_max = std::max(x_max, x);
            y_min = std::min(y_min, y);
            y_max = std::max(y_max, y);
        }
    }
    glyph.explicit_bbox = !(be16s(data + 2) == x_min && be16s(data + 4) == y_min && be16s(data + 6) == x_max &&
                            be16s(data + 8) == y_max);
    return true;
}

// Whether the loader's point writer reproduces a parsed simple glyph's point bytes.
bool glyph_points_reproduce(const std::uint8_t* data, const ParsedGlyph& glyph, std::uint32_t repeat_policy,
                            std::vector<std::uint8_t>& scratch) {
    // The first point_count bytes hold the flags, the rest the written points.
    scratch.resize(static_cast<std::size_t>(glyph.point_count) * 6 + 8);
    std::uint8_t* written_bytes = scratch.data() + glyph.point_count;
    tt_flags_fill(scratch.data(), glyph.triplet_flags.data(), glyph.dx.data(), glyph.dy.data(), glyph.point_count,
                  glyph.overlap_first);
    const std::size_t written =
        tt_points_write(written_bytes, scratch.data(), glyph.dx.data(), glyph.dy.data(), glyph.point_count, repeat_policy);
    if (written != glyph.length - glyph.points_offset) {
        return false;
    }
    return std::memcmp(written_bytes, data + glyph.points_offset, written) == 0;
}

void put_small(Bytes& stream, Bytes& wide, std::uint32_t value) {
    if (value < 255) {
        stream.push_back(static_cast<std::uint8_t>(value));
    } else {
        stream.push_back(255);
        put_varint(wide, value);
    }
}

// Transforms glyf and loca. On success appends the table parameters to meta
// and the glyph data to the streams and returns true; on failure touches nothing.
bool glyf_transform(const std::uint8_t* glyf, std::uint32_t glyf_size, const std::uint8_t* loca, std::uint32_t loca_size,
                    std::uint32_t glyph_count, std::uint32_t loca_long, Streams& streams, Bytes& meta) {
    const std::uint32_t entry = loca_long != 0 ? 4 : 2;
    if (loca_size != (glyph_count + 1) * entry) {
        return false;
    }
    std::vector<std::uint32_t> offsets(glyph_count + 1);
    for (std::uint32_t g = 0; g <= glyph_count; g += 1) {
        offsets[g] = loca_long != 0 ? be32(loca + 4 * g) : 2 * be16(loca + 2 * g);
        if (offsets[g] > glyf_size || (g != 0 && offsets[g] < offsets[g - 1])) {
            return false;
        }
    }
    if (offsets[0] != 0) {
        return false;
    }
    std::vector<ParsedGlyph> glyphs(glyph_count);
    std::vector<bool> parsed(glyph_count, false);
    for (std::uint32_t g = 0; g < glyph_count; g += 1) {
        const std::uint32_t slot = offsets[g + 1] - offsets[g];
        if (slot == 0) {
            glyphs[g].kind = kGlyphEmpty;
            glyphs[g].length = 0;
            parsed[g] = true;
            continue;
        }
        parsed[g] = glyph_parse(glyf + offsets[g], slot, glyphs[g]);
    }
    // Choose the padding and flag-repeat policies that reproduce the most glyphs.
    static const std::uint32_t pad_choices[3] = {1, 2, 4};
    static const std::uint32_t repeat_choices[4] = {3, 2, 0, 4};
    std::uint32_t best_pad = 1;
    std::uint32_t best_pad_hits = 0;
    for (std::uint32_t p = 0; p < 3; p += 1) {
        const std::uint32_t pad = pad_choices[p];
        std::uint32_t hits = 0;
        for (std::uint32_t g = 0; g < glyph_count; g += 1) {
            const std::uint32_t slot = offsets[g + 1] - offsets[g];
            if (parsed[g] && slot == ((glyphs[g].length + pad - 1) & ~(pad - 1))) {
                hits += 1;
            }
        }
        if (hits > best_pad_hits) {
            best_pad_hits = hits;
            best_pad = pad;
        }
    }
    std::vector<std::uint8_t> scratch;
    std::uint32_t best_repeat = 0;
    std::uint32_t best_repeat_hits = 0;
    for (std::uint32_t r = 0; r < 4; r += 1) {
        std::uint32_t hits = 0;
        for (std::uint32_t g = 0; g < glyph_count; g += 1) {
            if (parsed[g] && glyphs[g].kind == kGlyphSimple &&
                glyph_points_reproduce(glyf + offsets[g], glyphs[g], repeat_choices[r], scratch)) {
                hits += 1;
            }
        }
        if (r == 0 || hits > best_repeat_hits) {
            best_repeat_hits = hits;
            best_repeat = repeat_choices[r];
        }
    }

    put_varint(meta, glyph_count);
    meta.push_back(static_cast<std::uint8_t>(loca_long));
    meta.push_back(static_cast<std::uint8_t>(best_pad));
    meta.push_back(static_cast<std::uint8_t>(best_repeat));
    put_varint(meta, glyf_size - offsets[glyph_count]);

    for (std::uint32_t g = 0; g < glyph_count; g += 1) {
        const std::uint8_t* data = glyf + offsets[g];
        const std::uint32_t slot = offsets[g + 1] - offsets[g];
        ParsedGlyph& glyph = glyphs[g];
        bool verbatim = !parsed[g];
        if (!verbatim && glyph.kind != kGlyphEmpty) {
            if (slot != ((glyph.length + best_pad - 1) & ~(best_pad - 1))) {
                verbatim = true;
            }
            for (std::uint32_t k = glyph.length; k < slot && !verbatim; k += 1) {
                if (data[k] != 0) {
                    verbatim = true;
                }
            }
            if (!verbatim && glyph.kind == kGlyphSimple &&
                !glyph_points_reproduce(data, glyph, best_repeat, scratch)) {
                verbatim = true;
            }
        }
        if (verbatim) {
            streams.bytes[kStreamGlyphKind].push_back(kGlyphVerbatim);
            put_varint(streams.bytes[kStreamGlyphWide], slot);
            streams.bytes[kStreamGlyphRaw].insert(streams.bytes[kStreamGlyphRaw].end(), data, data + slot);
            continue;
        }
        if (glyph.kind == kGlyphEmpty) {
            streams.bytes[kStreamGlyphKind].push_back(kGlyphEmpty);
            continue;
        }
        if (glyph.kind == kGlyphComposite) {
            streams.bytes[kStreamGlyphKind].push_back(kGlyphComposite);
            streams.bytes[kStreamGlyphBbox].insert(streams.bytes[kStreamGlyphBbox].end(), data + 2, data + 10);
            streams.bytes[kStreamGlyphComposite].insert(streams.bytes[kStreamGlyphComposite].end(), data + 10,
                                                        data + 10 + glyph.composite_length);
            if (glyph.composite_instructions) {
                put_varint(streams.bytes[kStreamInstrLength], glyph.instruction_length);
                split_instructions(data + glyph.instruction_offset, glyph.instruction_length, streams);
            }
            continue;
        }
        std::uint32_t kind = kGlyphSimple;
        if (glyph.explicit_bbox) {
            kind |= kGlyphExplicitBbox;
        }
        if (glyph.overlap_first) {
            kind |= kGlyphOverlapBit;
        }
        streams.bytes[kStreamGlyphKind].push_back(static_cast<std::uint8_t>(kind));
        if (glyph.explicit_bbox) {
            streams.bytes[kStreamGlyphBbox].insert(streams.bytes[kStreamGlyphBbox].end(), data + 2, data + 10);
        }
        put_small(streams.bytes[kStreamGlyphContours], streams.bytes[kStreamGlyphWide],
                  static_cast<std::uint32_t>(glyph.contour_count));
        for (std::size_t c = 0; c < glyph.contour_points.size(); c += 1) {
            put_small(streams.bytes[kStreamGlyphPoints], streams.bytes[kStreamGlyphWide], glyph.contour_points[c]);
        }
        put_varint(streams.bytes[kStreamInstrLength], glyph.instruction_length);
        split_instructions(data + glyph.instruction_offset, glyph.instruction_length, streams);
        for (std::uint32_t k = 0; k < glyph.point_count; k += 1) {
            const Triplet t = triplet_encode(glyph.triplet_flags[k] == 0, glyph.dx[k], glyph.dy[k]);
            streams.bytes[kStreamGlyphFlags].push_back(t.flag);
            streams.bytes[kStreamGlyphCoords].insert(streams.bytes[kStreamGlyphCoords].end(), t.bytes, t.bytes + t.count);
        }
    }
    streams.bytes[kStreamGlyphRaw].insert(streams.bytes[kStreamGlyphRaw].end(), glyf + offsets[glyph_count], glyf + glyf_size);
    return true;
}

// ---- CFF -------------------------------------------------------------------

struct CffIndex {
    std::uint32_t start;        // offset of the INDEX in the table
    std::uint32_t count;
    std::uint32_t off_size;
    std::uint32_t data_start;   // offset of the first item's bytes
    std::uint32_t end;          // one past the INDEX
    std::vector<std::uint32_t> item_offsets;   // count + 1, relative to data_start
};

// Reads an INDEX whose offsets are contiguous and increasing.
bool cff_index_read(const std::uint8_t* table, std::uint32_t size, std::uint32_t start, CffIndex& index) {
    if (start > size || size - start < 2) {
        return false;
    }
    index.start = start;
    index.count = be16(table + start);
    index.item_offsets.clear();
    if (index.count == 0) {
        index.off_size = 0;
        index.data_start = start + 2;
        index.end = start + 2;
        index.item_offsets.push_back(0);
        return true;
    }
    if (size - start < 3) {
        return false;
    }
    index.off_size = table[start + 2];
    if (index.off_size < 1 || index.off_size > 4) {
        return false;
    }
    const std::uint64_t array_bytes = static_cast<std::uint64_t>(index.count + 1) * index.off_size;
    if (size - start - 3 < array_bytes) {
        return false;
    }
    index.data_start = start + 3 + static_cast<std::uint32_t>(array_bytes);
    std::uint32_t previous = 1;
    for (std::uint32_t k = 0; k <= index.count; k += 1) {
        const std::uint8_t* p = table + start + 3 + k * index.off_size;
        std::uint32_t value = 0;
        for (std::uint32_t b = 0; b < index.off_size; b += 1) {
            value = (value << 8) | p[b];
        }
        if ((k == 0 && value != 1) || value < previous) {
            return false;
        }
        previous = value;
        index.item_offsets.push_back(value - 1);
    }
    if (index.item_offsets[index.count] > size - index.data_start) {
        return false;
    }
    index.end = index.data_start + index.item_offsets[index.count];
    return true;
}

struct DictEntry {
    std::uint32_t op;                    // two-byte operators are 0x0C00 | second byte
    std::vector<std::int32_t> operands;  // reals read as 0
};

bool cff_dict_read(const std::uint8_t* data, std::uint32_t size, std::vector<DictEntry>& entries) {
    std::vector<std::int32_t> operands;
    std::uint32_t at = 0;
    while (at < size) {
        const std::uint32_t b = data[at];
        if (b == 28) {
            if (size - at < 3) {
                return false;
            }
            operands.push_back(be16s(data + at + 1));
            at += 3;
        } else if (b == 29) {
            if (size - at < 5) {
                return false;
            }
            operands.push_back(static_cast<std::int32_t>(be32(data + at + 1)));
            at += 5;
        } else if (b == 30) {
            at += 1;
            for (;;) {
                if (at >= size) {
                    return false;
                }
                const std::uint32_t nibbles = data[at];
                at += 1;
                if ((nibbles & 0x0Fu) == 0x0Fu || (nibbles >> 4) == 0x0Fu) {
                    break;
                }
            }
            operands.push_back(0);
        } else if (b >= 32 && b <= 246) {
            operands.push_back(static_cast<std::int32_t>(b) - 139);
            at += 1;
        } else if (b >= 247 && b <= 250) {
            if (size - at < 2) {
                return false;
            }
            operands.push_back(static_cast<std::int32_t>((b - 247) * 256 + data[at + 1] + 108));
            at += 2;
        } else if (b >= 251 && b <= 254) {
            if (size - at < 2) {
                return false;
            }
            operands.push_back(-static_cast<std::int32_t>((b - 251) * 256 + data[at + 1] + 108));
            at += 2;
        } else if (b <= 21) {
            DictEntry entry;
            entry.op = b;
            at += 1;
            if (b == 12) {
                if (at >= size) {
                    return false;
                }
                entry.op = 0x0C00u | data[at];
                at += 1;
            }
            entry.operands = operands;
            operands.clear();
            entries.push_back(entry);
        } else {
            return false;
        }
    }
    return true;
}

const DictEntry* dict_find(std::vector<DictEntry>&& entries, std::uint32_t op) = delete;

const DictEntry* dict_find(const std::vector<DictEntry>& entries, std::uint32_t op) {
    for (std::size_t k = 0; k < entries.size(); k += 1) {
        if (entries[k].op == op) {
            return &entries[k];
        }
    }
    return nullptr;
}

struct CffFont {
    CffIndex global_subrs;
    CffIndex char_strings;
    std::vector<CffIndex> local_subrs;       // distinct local subr INDEXes
    std::vector<std::int32_t> fd_local;      // per font dict: index into local_subrs or -1
    std::vector<std::uint8_t> glyph_fd;      // per glyph font dict
};

// Finds the local subr INDEX of a Private DICT. local is -1 when there is none.
bool cff_private_subrs(const std::uint8_t* table, std::uint32_t size, const DictEntry* private_entry, CffFont& font,
                       std::int32_t& local) {
    local = -1;
    if (private_entry == nullptr || (*private_entry).operands.size() != 2) {
        return true;
    }
    const std::int32_t private_size = (*private_entry).operands[0];
    const std::int32_t private_offset = (*private_entry).operands[1];
    if (private_size < 0 || private_offset < 0 || static_cast<std::uint32_t>(private_offset) > size ||
        static_cast<std::uint32_t>(private_size) > size - static_cast<std::uint32_t>(private_offset)) {
        return false;
    }
    std::vector<DictEntry> entries;
    if (!cff_dict_read(table + private_offset, static_cast<std::uint32_t>(private_size), entries)) {
        return false;
    }
    const DictEntry* subrs = dict_find(entries, 19);
    if (subrs == nullptr || (*subrs).operands.size() != 1) {
        return true;
    }
    const std::int64_t start = static_cast<std::int64_t>(private_offset) + (*subrs).operands[0];
    if (start < 0 || start > size) {
        return false;
    }
    for (std::size_t k = 0; k < font.local_subrs.size(); k += 1) {
        if (font.local_subrs[k].start == static_cast<std::uint32_t>(start)) {
            local = static_cast<std::int32_t>(k);
            return true;
        }
    }
    CffIndex index;
    if (!cff_index_read(table, size, static_cast<std::uint32_t>(start), index)) {
        return false;
    }
    font.local_subrs.push_back(index);
    local = static_cast<std::int32_t>(font.local_subrs.size() - 1);
    return true;
}

bool cff_parse(const std::uint8_t* table, std::uint32_t size, CffFont& font) {
    if (size < 4 || table[0] != 1) {
        return false;
    }
    CffIndex names;
    CffIndex tops;
    CffIndex strings;
    if (!cff_index_read(table, size, table[2], names) || !cff_index_read(table, size, names.end, tops) ||
        !cff_index_read(table, size, tops.end, strings) || !cff_index_read(table, size, strings.end, font.global_subrs)) {
        return false;
    }
    if (tops.count != 1) {
        return false;
    }
    std::vector<DictEntry> top;
    if (!cff_dict_read(table + tops.data_start, tops.item_offsets[1], top)) {
        return false;
    }
    const DictEntry* type = dict_find(top, 0x0C06);
    if (type != nullptr && ((*type).operands.size() != 1 || (*type).operands[0] != 2)) {
        return false;
    }
    const DictEntry* char_strings = dict_find(top, 17);
    if (char_strings == nullptr || (*char_strings).operands.size() != 1 || (*char_strings).operands[0] < 0) {
        return false;
    }
    if (!cff_index_read(table, size, static_cast<std::uint32_t>((*char_strings).operands[0]), font.char_strings)) {
        return false;
    }
    const std::uint32_t glyph_count = font.char_strings.count;
    font.glyph_fd.assign(glyph_count, 0);
    const DictEntry* fd_array = dict_find(top, 0x0C24);
    if (fd_array == nullptr) {
        std::int32_t local = -1;
        if (!cff_private_subrs(table, size, dict_find(top, 18), font, local)) {
            return false;
        }
        font.fd_local.push_back(local);
        return true;
    }
    if ((*fd_array).operands.size() != 1 || (*fd_array).operands[0] < 0) {
        return false;
    }
    CffIndex dicts;
    if (!cff_index_read(table, size, static_cast<std::uint32_t>((*fd_array).operands[0]), dicts)) {
        return false;
    }
    for (std::uint32_t d = 0; d < dicts.count; d += 1) {
        std::vector<DictEntry> entries;
        if (!cff_dict_read(table + dicts.data_start + dicts.item_offsets[d], dicts.item_offsets[d + 1] - dicts.item_offsets[d],
                           entries)) {
            return false;
        }
        std::int32_t local = -1;
        if (!cff_private_subrs(table, size, dict_find(entries, 18), font, local)) {
            return false;
        }
        font.fd_local.push_back(local);
    }
    const DictEntry* fd_select = dict_find(top, 0x0C25);
    if (fd_select == nullptr || (*fd_select).operands.size() != 1 || (*fd_select).operands[0] < 0 ||
        static_cast<std::uint32_t>((*fd_select).operands[0]) >= size) {
        return false;
    }
    const std::uint32_t select = static_cast<std::uint32_t>((*fd_select).operands[0]);
    if (table[select] == 0) {
        if (size - select - 1 < glyph_count) {
            return false;
        }
        for (std::uint32_t g = 0; g < glyph_count; g += 1) {
            font.glyph_fd[g] = table[select + 1 + g];
        }
    } else if (table[select] == 3) {
        if (size - select < 5) {
            return false;
        }
        const std::uint32_t ranges = be16(table + select + 1);
        if (size - select - 3 < ranges * 3 + 2) {
            return false;
        }
        for (std::uint32_t r = 0; r < ranges; r += 1) {
            const std::uint32_t first = be16(table + select + 3 + r * 3);
            const std::uint32_t fd = table[select + 5 + r * 3];
            const std::uint32_t next = be16(table + select + 3 + (r + 1) * 3);
            for (std::uint32_t g = first; g < next && g < glyph_count; g += 1) {
                font.glyph_fd[g] = static_cast<std::uint8_t>(fd);
            }
        }
    } else {
        return false;
    }
    for (std::uint32_t g = 0; g < glyph_count; g += 1) {
        if (font.glyph_fd[g] >= font.fd_local.size()) {
            return false;
        }
    }
    return true;
}

std::int32_t subr_bias(std::uint32_t count) {
    if (count < 1240) {
        return 107;
    }
    if (count < 33900) {
        return 1131;
    }
    return 32768;
}

// Mask byte counts found by running the charstrings: one slot per item of
// each INDEX, -1 while unknown.
struct MaskLengths {
    std::vector<std::int32_t> global;
    std::vector<std::vector<std::int32_t> > local;
    std::vector<std::int32_t> glyph;
};

struct RunState {
    std::int32_t stack[520];
    std::uint32_t depth;
    std::uint32_t stems;
    bool done;
};

// Follows one charstring or subroutine far enough to count stem hints,
// recording the mask length each item with a mask operator uses.
void cff_run(const std::uint8_t* table, const CffFont& font, std::int32_t local, const std::uint8_t* code,
             std::uint32_t size, std::int32_t& mask_length, MaskLengths& lengths, RunState& state, std::uint32_t nesting) {
    std::uint32_t at = 0;
    while (at < size && !state.done) {
        const std::uint32_t b = code[at];
        if (b >= 32 || b == 28) {
            std::int32_t value = 0;
            std::uint32_t width = 1;
            if (b == 28) {
                width = 3;
                if (size - at >= 3) {
                    value = be16s(code + at + 1);
                }
            } else if (b < 247) {
                value = static_cast<std::int32_t>(b) - 139;
            } else if (b < 251) {
                width = 2;
                if (size - at >= 2) {
                    value = static_cast<std::int32_t>((b - 247) * 256 + code[at + 1] + 108);
                }
            } else if (b < 255) {
                width = 2;
                if (size - at >= 2) {
                    value = -static_cast<std::int32_t>((b - 251) * 256 + code[at + 1] + 108);
                }
            } else {
                width = 5;
            }
            if (size - at < width) {
                state.done = true;
                return;
            }
            at += width;
            if (state.depth < 520) {
                state.stack[state.depth] = value;
                state.depth += 1;
            }
            continue;
        }
        at += 1;
        if (b == 1 || b == 3 || b == 18 || b == 23) {
            state.stems += state.depth / 2;
            state.depth = 0;
        } else if (b == 19 || b == 20) {
            state.stems += state.depth / 2;
            state.depth = 0;
            const std::uint32_t bytes = (state.stems + 7) / 8;
            if (mask_length < 0) {
                mask_length = static_cast<std::int32_t>(bytes);
            }
            // The item is tokenised with the first length seen; follow it with that too.
            at += static_cast<std::uint32_t>(mask_length);
        } else if (b == 10 || b == 29) {
            if (state.depth == 0 || nesting >= 10) {
                state.done = true;
                return;
            }
            state.depth -= 1;
            std::int32_t number = state.stack[state.depth];
            if (b == 10) {
                if (local < 0) {
                    state.done = true;
                    return;
                }
                const CffIndex& index = font.local_subrs[static_cast<std::size_t>(local)];
                number += subr_bias(index.count);
                if (number < 0 || static_cast<std::uint32_t>(number) >= index.count) {
                    state.done = true;
                    return;
                }
                // Non-negative and below index.count (checked above).
                const std::size_t item = static_cast<std::size_t>(number);
                cff_run(table, font, local, table + index.data_start + index.item_offsets[item],
                        index.item_offsets[item + 1] - index.item_offsets[item],
                        lengths.local[static_cast<std::size_t>(local)][item], lengths, state, nesting + 1);
            } else {
                const CffIndex& index = font.global_subrs;
                number += subr_bias(index.count);
                if (number < 0 || static_cast<std::uint32_t>(number) >= index.count) {
                    state.done = true;
                    return;
                }
                // Non-negative and below index.count (checked above).
                const std::size_t item = static_cast<std::size_t>(number);
                cff_run(table, font, local, table + index.data_start + index.item_offsets[item],
                        index.item_offsets[item + 1] - index.item_offsets[item], lengths.global[item], lengths, state,
                        nesting + 1);
            }
        } else if (b == 11) {
            return;
        } else if (b == 14) {
            state.done = true;
            return;
        } else if (b == 12) {
            at += 1;
            state.depth = 0;
        } else {
            state.depth = 0;
        }
    }
}

// Tokenises one item into the CFF streams. mask_length is -1 when unknown.
void cff_tokenise(const std::uint8_t* code, std::uint32_t size, std::int32_t mask_length, Streams& streams) {
    Bytes& ops = streams.group_ops;
    Bytes& counts = streams.group_counts;
    std::vector<std::uint16_t>& numbers = streams.numbers;
    std::uint32_t at = 0;
    std::uint32_t pending = 0;
    bool mask_written = false;
    bool raw_tail = false;
    while (at < size) {
        const std::uint32_t b = code[at];
        if (b >= 32 || b == 28) {
            std::uint32_t width = 1;
            if (b == 28) {
                width = 3;
            } else if (b >= 247 && b < 255) {
                width = 2;
            } else if (b == 255) {
                width = 5;
            }
            if (size - at < width) {
                raw_tail = true;
                break;
            }
            if (width == 2) {
                numbers.push_back(static_cast<std::uint16_t>(b | (static_cast<std::uint32_t>(code[at + 1]) << 8)));
            } else {
                numbers.push_back(static_cast<std::uint16_t>(b));
            }
            if (width > 2) {
                streams.bytes[kStreamCffNumWide].insert(streams.bytes[kStreamCffNumWide].end(), code + at + 1, code + at + width);
            }
            at += width;
            pending += 1;
            if (pending == 255) {
                ops.push_back(kCffOpMoreArgs);
                counts.push_back(255);
                pending = 0;
            }
            continue;
        }
        if (b == 12) {
            if (size - at < 2) {
                raw_tail = true;
                break;
            }
            ops.push_back(12);
            counts.push_back(static_cast<std::uint8_t>(pending));
            streams.bytes[kStreamCffMisc].push_back(code[at + 1]);
            at += 2;
        } else if (b == 19 || b == 20) {
            if (mask_length < 0 || mask_length > 255 || size - at - 1 < static_cast<std::uint32_t>(mask_length)) {
                raw_tail = true;
                break;
            }
            ops.push_back(static_cast<std::uint8_t>(b));
            counts.push_back(static_cast<std::uint8_t>(pending));
            if (!mask_written) {
                streams.bytes[kStreamCffMaskLength].push_back(static_cast<std::uint8_t>(mask_length));
                mask_written = true;
            }
            streams.bytes[kStreamCffMask].insert(streams.bytes[kStreamCffMask].end(), code + at + 1,
                                                 code + at + 1 + mask_length);
            at += 1 + static_cast<std::uint32_t>(mask_length);
        } else {
            ops.push_back(static_cast<std::uint8_t>(b));
            counts.push_back(static_cast<std::uint8_t>(pending));
            at += 1;
        }
        pending = 0;
    }
    if (raw_tail) {
        ops.push_back(kCffOpRawTail);
        counts.push_back(static_cast<std::uint8_t>(pending));
        put_varint(streams.bytes[kStreamCffMisc], size - at);
        streams.bytes[kStreamCffMisc].insert(streams.bytes[kStreamCffMisc].end(), code + at, code + size);
    } else {
        ops.push_back(kCffOpEnd);
        counts.push_back(static_cast<std::uint8_t>(pending));
    }
}

void cff_index_positions(const Streams& streams, std::uint32_t data_offset, Bytes& index_stream) {
    // The group index here becomes a kStreamCffOps position in cff_pair_stream.
    put_le32(index_stream, static_cast<std::uint32_t>(streams.group_ops.size()));
    put_le32(index_stream, static_cast<std::uint32_t>(streams.numbers.size()));
    put_le32(index_stream, static_cast<std::uint32_t>(streams.bytes[kStreamCffNumWide].size()));
    put_le32(index_stream, static_cast<std::uint32_t>(streams.bytes[kStreamCffMask].size()));
    put_le32(index_stream, static_cast<std::uint32_t>(streams.bytes[kStreamCffMaskLength].size()));
    put_le32(index_stream, static_cast<std::uint32_t>(streams.bytes[kStreamCffMisc].size()));
    put_le32(index_stream, data_offset);
}

struct RegionRef {
    const CffIndex* index;
    const std::vector<std::int32_t>* mask_lengths;
};

struct ByStart {
    bool operator()(const RegionRef& a, const RegionRef& b) const {
        return (*a.index).start < (*b.index).start;
    }
};

bool cff_transform(const std::uint8_t* table, std::uint32_t size, Streams& streams, Bytes& meta) {
    CffFont font;
    if (!cff_parse(table, size, font)) {
        return false;
    }
    MaskLengths lengths;
    lengths.global.assign(font.global_subrs.count, -1);
    lengths.local.resize(font.local_subrs.size());
    for (std::size_t k = 0; k < font.local_subrs.size(); k += 1) {
        lengths.local[k].assign(font.local_subrs[k].count, -1);
    }
    lengths.glyph.assign(font.char_strings.count, -1);
    // Large: owned on the heap for the charstring walk.
    std::unique_ptr<RunState> state_owner(new RunState);
    RunState* state = state_owner.get();
    for (std::uint32_t g = 0; g < font.char_strings.count; g += 1) {
        (*state).depth = 0;
        (*state).stems = 0;
        (*state).done = false;
        cff_run(table, font, font.fd_local[font.glyph_fd[g]], table + font.char_strings.data_start + font.char_strings.item_offsets[g],
                font.char_strings.item_offsets[g + 1] - font.char_strings.item_offsets[g], lengths.glyph[g], lengths, *state, 0);
    }

    std::vector<RegionRef> regions;
    RegionRef ref;
    ref.index = &font.global_subrs;
    ref.mask_lengths = &lengths.global;
    regions.push_back(ref);
    ref.index = &font.char_strings;
    ref.mask_lengths = &lengths.glyph;
    regions.push_back(ref);
    for (std::size_t k = 0; k < font.local_subrs.size(); k += 1) {
        ref.index = &font.local_subrs[k];
        ref.mask_lengths = &lengths.local[k];
        regions.push_back(ref);
    }
    std::sort(regions.begin(), regions.end(), ByStart());
    for (std::size_t r = 1; r < regions.size(); r += 1) {
        if ((*regions[r].index).start < (*regions[r - 1].index).end) {
            return false;
        }
    }

    put_varint(meta, regions.size());
    std::uint32_t residual_from = 0;
    for (std::size_t r = 0; r < regions.size(); r += 1) {
        const CffIndex& index = *regions[r].index;
        put_varint(meta, index.start);
        put_varint(meta, index.count);
        meta.push_back(static_cast<std::uint8_t>(index.off_size));
        put_varint(meta, index.item_offsets[index.count]);
        streams.bytes[kStreamCffResidual].insert(streams.bytes[kStreamCffResidual].end(), table + residual_from,
                                                 table + index.start);
        residual_from = index.end;
        for (std::uint32_t k = 0; k < index.count; k += 1) {
            if (k % kCffChunkItems == 0) {
                cff_index_positions(streams, index.item_offsets[k], streams.bytes[kStreamCffIndex]);
            }
            cff_tokenise(table + index.data_start + index.item_offsets[k], index.item_offsets[k + 1] - index.item_offsets[k],
                         (*regions[r].mask_lengths)[k], streams);
        }
    }
    streams.bytes[kStreamCffResidual].insert(streams.bytes[kStreamCffResidual].end(), table + residual_from, table + size);
    return true;
}

// ---- sfnt ------------------------------------------------------------------

const TableRecord* find_table(std::vector<TableRecord>&& records, std::uint32_t tag) = delete;

const TableRecord* find_table(const std::vector<TableRecord>& records, std::uint32_t tag) {
    for (std::size_t k = 0; k < records.size(); k += 1) {
        if (records[k].tag == tag) {
            return &records[k];
        }
    }
    return nullptr;
}

// The stride filter that makes a raw table code smallest, 0 for none.
// Judged by coding the table alone with a quick parse.
std::uint32_t choose_stride(const std::uint8_t* table, std::uint32_t size) {
    if (size < 256) {
        return 0;
    }
    static const std::uint32_t strides[8] = {0, 1, 2, 4, 6, 8, 12, 16};
    EncodeOptions options = encode_options_default();
    options.iterations = 0;
    options.chain_depth = 16;
    std::uint32_t best_stride = 0;
    std::size_t best_size = 0;
    std::size_t plain_size = 0;
    for (std::uint32_t k = 0; k < 8; k += 1) {
        Bytes filtered(table, table + size);
        stride_filter(filtered.data(), filtered.size(), strides[k]);
        Bytes coded;
        stream_encode(filtered.data(), filtered.size(), options, coded);
        if (k == 0) {
            plain_size = coded.size();
            best_size = coded.size();
        } else if (coded.size() < best_size) {
            best_size = coded.size();
            best_stride = strides[k];
        }
    }
    // A filter must earn its keep clearly: it hides the table from matches against other fonts' copies.
    if (best_size * 100 > plain_size * 95) {
        return 0;
    }
    return best_stride;
}

void gap_put(const std::uint8_t* data, std::uint32_t size, Streams& streams, Bytes& meta) {
    put_varint(meta, size);
    bool zeros = true;
    for (std::uint32_t k = 0; k < size; k += 1) {
        if (data[k] != 0) {
            zeros = false;
        }
    }
    meta.push_back(zeros ? 0 : 1);
    if (!zeros) {
        streams.bytes[kStreamGap].insert(streams.bytes[kStreamGap].end(), data, data + size);
    }
}

void font_verbatim(const Bytes& file, Streams& streams) {
    streams.bytes[kStreamMeta].push_back(0);
    append_raw_table(streams.bytes[kStreamRawOther], file.data(), file.size());
}

void font_transform(const Bytes& file, Streams& streams) {
    const std::uint8_t* data = file.data();
    const std::size_t size = file.size();
    if (size < 12 || size > 0x7FFFFFFFu) {
        font_verbatim(file, streams);
        return;
    }
    const std::uint32_t table_count = be16(data + 4);
    const std::size_t directory_size = 12 + static_cast<std::size_t>(table_count) * 16;
    if (table_count == 0 || directory_size > size) {
        font_verbatim(file, streams);
        return;
    }
    std::vector<TableRecord> records(table_count);
    for (std::uint32_t t = 0; t < table_count; t += 1) {
        const std::uint8_t* record = data + 12 + t * 16;
        records[t].tag = be32(record);
        records[t].offset = be32(record + 8);
        records[t].length = be32(record + 12);
        records[t].index = t;
    }
    std::vector<TableRecord> physical = records;
    std::sort(physical.begin(), physical.end(), ByOffset());
    std::size_t previous_end = directory_size;
    for (std::uint32_t t = 0; t < table_count; t += 1) {
        // offset is tested against size first, so size - offset cannot wrap
        // (A-gf-typography-01-F4; the loader's plan has the same test).
        if (physical[t].offset > size || physical[t].offset < previous_end ||
            physical[t].length > size - physical[t].offset) {
            font_verbatim(file, streams);
            return;
        }
        previous_end = static_cast<std::size_t>(physical[t].offset) + physical[t].length;
    }

    // Decide the transforms before writing anything, so a refusal leaves the streams untouched.
    const TableRecord* glyf = find_table(records, tag_of("glyf"));
    const TableRecord* loca = find_table(records, tag_of("loca"));
    const TableRecord* head = find_table(records, tag_of("head"));
    const TableRecord* maxp = find_table(records, tag_of("maxp"));
    Streams font_streams;
    Bytes glyf_meta;
    bool glyf_ok = false;
    if (glyf != nullptr && loca != nullptr && head != nullptr && maxp != nullptr && (*head).length >= 54 &&
        (*maxp).length >= 6) {
        const std::uint32_t loca_long = be16(data + (*head).offset + 50);
        const std::uint32_t glyph_count = be16(data + (*maxp).offset + 4);
        if (loca_long <= 1) {
            glyf_ok = glyf_transform(data + (*glyf).offset, (*glyf).length, data + (*loca).offset, (*loca).length,
                                     glyph_count, loca_long, font_streams, glyf_meta);
        }
    }

    // hmtx is split only beside a transformed glyf, whose boxes its bearings are stored against.
    const TableRecord* hmtx = find_table(records, tag_of("hmtx"));
    const TableRecord* hhea = find_table(records, tag_of("hhea"));
    std::uint32_t metric_count = 0;
    bool hmtx_ok = false;
    if (glyf_ok && hmtx != nullptr && hhea != nullptr && (*hhea).length >= 36) {
        const std::uint32_t glyph_count = be16(data + (*maxp).offset + 4);
        metric_count = be16(data + (*hhea).offset + 34);
        hmtx_ok = metric_count >= 1 && metric_count <= glyph_count &&
                  (*hmtx).length == 4 * metric_count + 2 * (glyph_count - metric_count);
    }

    Bytes& meta = streams.bytes[kStreamMeta];
    meta.push_back(1);
    put_varint(meta, table_count);
    streams.bytes[kStreamDirectory].insert(streams.bytes[kStreamDirectory].end(), data, data + directory_size);
    previous_end = directory_size;
    for (std::uint32_t t = 0; t < table_count; t += 1) {
        const TableRecord& record = physical[t];
        const std::uint8_t* table = data + record.offset;
        put_varint(meta, record.index);
        gap_put(data + previous_end, static_cast<std::uint32_t>(record.offset - previous_end), streams, meta);
        previous_end = static_cast<std::size_t>(record.offset) + record.length;
        if (glyf_ok && record.tag == tag_of("glyf") && &records[record.index] == glyf) {
            meta.push_back(kTableGlyf);
            put_varint(meta, (*loca).index);
            meta.insert(meta.end(), glyf_meta.begin(), glyf_meta.end());
            // glyf_transform writes no CFF groups, so only the coded streams move.
            for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
                streams.bytes[s].insert(streams.bytes[s].end(), font_streams.bytes[s].begin(), font_streams.bytes[s].end());
            }
            continue;
        }
        if (glyf_ok && &records[record.index] == loca) {
            meta.push_back(kTableLoca);
            continue;
        }
        if (hmtx_ok && &records[record.index] == hmtx) {
            meta.push_back(kTableHmtx);
            put_varint(meta, metric_count);
            const std::uint32_t glyph_count = be16(data + (*maxp).offset + 4);
            const bool loca_long = be16(data + (*head).offset + 50) != 0;
            const std::uint8_t* loca_data = data + (*loca).offset;
            const std::uint8_t* glyf_data = data + (*glyf).offset;
            for (std::uint32_t g = 0; g < glyph_count; g += 1) {
                const std::uint32_t start = loca_long ? be32(loca_data + 4 * g) : 2 * be16(loca_data + 2 * g);
                const std::uint32_t end = loca_long ? be32(loca_data + 4 * g + 4) : 2 * be16(loca_data + 2 * g + 2);
                const std::uint8_t* bearing = table + 4 * metric_count + 2 * (g - metric_count);
                if (g < metric_count) {
                    streams.bytes[kStreamHmtxAdvance].insert(streams.bytes[kStreamHmtxAdvance].end(), table + 4 * g,
                                                             table + 4 * g + 2);
                    bearing = table + 4 * g + 2;
                }
                const std::uint32_t relative =
                    static_cast<std::uint32_t>(be16s(bearing) - glyph_bearing_reference(glyf_data, start, end)) & 0xFFFFu;
                streams.bytes[kStreamHmtxLsb].push_back(static_cast<std::uint8_t>(relative >> 8));
                streams.bytes[kStreamHmtxLsb].push_back(static_cast<std::uint8_t>(relative));
            }
            continue;
        }
        if (record.tag == tag_of("CFF ")) {
            // cff_transform records absolute stream positions, so it works on the real
            // streams; on refusal they are cut back to the saved sizes.
            std::size_t saved[kStreamCount];
            for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
                saved[s] = streams.bytes[s].size();
            }
            const std::size_t saved_groups = streams.group_ops.size();
            const std::size_t saved_numbers = streams.numbers.size();
            meta.push_back(kTableCff);
            if (cff_transform(table, record.length, streams, meta)) {
                continue;
            }
            for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
                streams.bytes[s].resize(saved[s]);
            }
            streams.group_ops.resize(saved_groups);
            streams.group_counts.resize(saved_groups);
            streams.numbers.resize(saved_numbers);
        }
        if (record.tag == tag_of("fpgm") || record.tag == tag_of("prep")) {
            meta.push_back(kTableInstr);
            split_instructions(table, record.length, streams);
            continue;
        }
        meta.push_back(kTableRaw);
        const StreamId stream = raw_stream_for(record.tag);
        meta.push_back(static_cast<std::uint8_t>(stream));
        const std::uint32_t stride = choose_stride(table, record.length);
        meta.push_back(static_cast<std::uint8_t>(stride));
        Bytes filtered(table, table + record.length);
        stride_filter(filtered.data(), filtered.size(), stride);
        append_raw_table(streams.bytes[stream], filtered.data(), filtered.size());
    }
    gap_put(data + previous_end, static_cast<std::uint32_t>(size - previous_end), streams, meta);
}

// ---- stream coding ---------------------------------------------------------

struct StreamPlan {
    std::vector<std::uint8_t> classes;
    std::uint32_t class_count;
};

// Classes for kStreamGlyphCoords: which payload byte of which triplet form.
void coords_classes(const Streams& streams, StreamPlan& plan) {
    const Bytes& flags = streams.bytes[kStreamGlyphFlags];
    plan.classes.clear();
    plan.classes.reserve(streams.bytes[kStreamGlyphCoords].size());
    for (std::size_t k = 0; k < flags.size(); k += 1) {
        const std::uint32_t first = triplet_class(flags[k]);
        const std::uint32_t count = triplet_bytes(flags[k]);
        for (std::uint32_t b = 0; b < count; b += 1) {
            plan.classes.push_back(static_cast<std::uint8_t>(first + b));
        }
    }
    plan.class_count = kTripletClassCount;
}

// Classes for kStreamCffNumbers and the fine -> class map stored in kStreamCffNumMap.
void cff_number_classes(Streams& streams, StreamPlan& plan) {
    const Bytes& ops = streams.group_ops;
    const Bytes& counts = streams.group_counts;
    // The classes predict a number's first byte.
    Bytes num1(streams.numbers.size());
    for (std::size_t k = 0; k < num1.size(); k += 1) {
        num1[k] = static_cast<std::uint8_t>(streams.numbers[k]);
    }
    std::vector<std::uint16_t> fine;
    fine.reserve(num1.size());
    for (std::size_t g = 0; g < ops.size(); g += 1) {
        const std::uint32_t base = cff_number_context_base(ops[g], counts[g]);
        for (std::uint32_t j = 0; j < counts[g]; j += 1) {
            fine.push_back(static_cast<std::uint16_t>(base + cff_number_context_index(j)));
        }
    }
    std::vector<std::uint8_t> map;
    plan.class_count = cluster_contexts(num1.data(), fine.data(), num1.size(), kCffNumFineCount, map);
    plan.classes.resize(num1.size());
    for (std::size_t k = 0; k < num1.size(); k += 1) {
        plan.classes[k] = map[fine[k]];
    }
    streams.bytes[kStreamCffNumMap] = map;
}

struct PairCount {
    std::uint32_t pair;    // operator << 8 | count
    std::uint32_t count;
};

struct ByCountDescending {
    bool operator()(const PairCount& a, const PairCount& b) const {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        return a.pair < b.pair;
    }
};

// Builds kStreamCffPairTable and kStreamCffOps from the groups, and turns the
// group indices in kStreamCffIndex into kStreamCffOps positions.
void cff_pair_stream(Streams& streams) {
    const Bytes& ops = streams.group_ops;
    const Bytes& counts = streams.group_counts;
    std::vector<PairCount> tally(65536);
    for (std::uint32_t k = 0; k < 65536; k += 1) {
        tally[k].pair = k;
        tally[k].count = 0;
    }
    for (std::size_t g = 0; g < ops.size(); g += 1) {
        tally[(static_cast<std::uint32_t>(ops[g]) << 8) | counts[g]].count += 1;
    }
    std::sort(tally.begin(), tally.end(), ByCountDescending());
    std::vector<std::uint16_t> symbol_of(65536, kCffPairEscape);
    Bytes& table = streams.bytes[kStreamCffPairTable];
    table.clear();
    for (std::uint32_t k = 0; k < kCffPairCount; k += 1) {
        table.push_back(static_cast<std::uint8_t>(tally[k].pair >> 8));
        table.push_back(static_cast<std::uint8_t>(tally[k].pair));
        if (tally[k].count != 0) {
            symbol_of[tally[k].pair] = static_cast<std::uint16_t>(k);
        }
    }
    Bytes& pairs = streams.bytes[kStreamCffOps];
    pairs.clear();
    std::vector<std::uint32_t> position_of(ops.size() + 1);
    for (std::size_t g = 0; g < ops.size(); g += 1) {
        position_of[g] = static_cast<std::uint32_t>(pairs.size());
        const std::uint32_t symbol = symbol_of[(static_cast<std::uint32_t>(ops[g]) << 8) | counts[g]];
        pairs.push_back(static_cast<std::uint8_t>(symbol));
        if (symbol == kCffPairEscape) {
            pairs.push_back(ops[g]);
            pairs.push_back(counts[g]);
        }
    }
    position_of[ops.size()] = static_cast<std::uint32_t>(pairs.size());

    Bytes& index = streams.bytes[kStreamCffIndex];
    for (std::size_t entry = 0; entry + kCffIndexEntryBytes <= index.size(); entry += kCffIndexEntryBytes) {
        const std::uint32_t position = position_of[le32(index.data() + entry)];
        index[entry] = static_cast<std::uint8_t>(position);
        index[entry + 1] = static_cast<std::uint8_t>(position >> 8);
        index[entry + 2] = static_cast<std::uint8_t>(position >> 16);
        index[entry + 3] = static_cast<std::uint8_t>(position >> 24);
    }
}

void period_classes(std::size_t size, std::uint32_t period, StreamPlan& plan) {
    plan.classes.resize(size);
    for (std::size_t k = 0; k < size; k += 1) {
        plan.classes[k] = static_cast<std::uint8_t>(k % period);
    }
    plan.class_count = period;
}

// Streams the loader gives position-period classes: class = position % class count.
bool is_raw_stream(std::uint32_t stream) {
    return stream == kStreamDirectory || stream == kStreamRawOther || stream == kStreamRawLayout ||
           stream == kStreamRawCmap || stream == kStreamRawPost || stream == kStreamRawName ||
           stream == kStreamRawMetrics || stream == kStreamGlyphBbox || stream == kStreamGlyphComposite ||
           stream == kStreamCffNumWide || stream == kStreamHmtxAdvance || stream == kStreamHmtxLsb;
}

const char* stream_name(std::uint32_t stream) {
    static const char* const names[kStreamCount] = {
        "meta", "directory", "gap", "raw-other", "raw-layout", "raw-cmap", "raw-post", "raw-name", "raw-metrics",
        "glyph-kind", "glyph-contours", "glyph-points", "glyph-wide", "glyph-bbox", "glyph-flags", "glyph-coords",
        "glyph-composite", "glyph-raw", "instr-length", "instr-code", "instr-push", "cff-residual", "cff-ops",
        "cff-pairtable", "cff-numbers", "cff-numwide", "cff-mask", "cff-masklength", "cff-misc", "cff-index",
        "cff-nummap", "hmtx-advance", "hmtx-lsb"};
    return names[stream];
}

// Codes one group's fonts into a body (see detail/format.hpp) and fills in the
// group's container fields. Every stream is solid across the group's fonts
// and nothing outside them: this is the whole of the group split.
void pack_group_body(const std::vector<PackInput>& fonts, const PackGroupSpec& spec, const Executor& executor,
                     std::FILE* log, ContainerGroup& group) {
    // Every stream of the group: large, owned on the heap for the whole body.
    std::unique_ptr<Streams> streams_owner(new Streams);
    Streams* streams = streams_owner.get();
    const Allocator probe_allocator = standard_allocator();
    group.name = spec.name;
    group.eager = spec.eager;
    group.fonts.clear();
    group.font_bytes = 0;
    group.body.clear();
    Bytes& out = group.body;
    // Where each font starts in every stream, so the loader can rebuild fonts independently.
    std::vector<std::size_t> starts;
    for (std::size_t k = 0; k < spec.fonts.size(); k += 1) {
        const PackInput& font = fonts[spec.fonts[k]];
        for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
            starts.push_back(s == kStreamCffNumbers ? (*streams).numbers.size() : (*streams).bytes[s].size());
        }
        font_transform(font.data, *streams);
        ContainerFont entry;
        entry.name = font.name;
        entry.size = font.data.size();
        const std::span<const std::byte> file(reinterpret_cast<const std::byte*>(font.data.data()), font.data.size());
        entry.crc = stx_zlib::crc32(file);
        group.fonts.push_back(entry);
        group.font_bytes += font.data.size();
    }
    for (std::size_t k = 0; k < spec.fonts.size(); k += 1) {
        for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
            put_varint(out, starts[k * kStreamCount + s]);
        }
    }

    StreamPlan number_plan;
    number_plan.class_count = 1;
    if (!(*streams).group_ops.empty()) {
        cff_number_classes(*streams, number_plan);
        cff_pair_stream(*streams);
    }
    // The decoded sizes the loader will find at the head of each coded
    // stream; their sum is the group's stream claim. A number is held as a
    // 16-bit value, so it counts 2 bytes.
    group.stream_bytes = 2 * static_cast<std::uint64_t>((*streams).numbers.size());
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        if (s != kStreamCffNumbers) {
            group.stream_bytes += (*streams).bytes[s].size();
        }
    }

    std::vector<Bytes> coded(kStreamCount);
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        const Bytes& bytes = (*streams).bytes[s];
        Bytes best;
        // Candidate settings; the smallest coding wins. The loader learns the
        // choice from the stream header (class count and quantiser).
        static const std::uint32_t quantisers[3] = {0, 2, 4};
        StreamPlan plan;
        if (s == kStreamCffNumbers) {
            NumberOptions options = number_options_default();
            options.executor = &executor;
            options.classes = number_plan.class_count > 1 ? number_plan.classes.data() : nullptr;
            options.class_count = number_plan.class_count;
            number_encode((*streams).numbers.data(), (*streams).numbers.size(), options, best);
        } else if (s == kStreamGlyphCoords) {
            coords_classes(*streams, plan);
            for (std::uint32_t q = 0; q < 2; q += 1) {
                EncodeOptions options = encode_options_default();
                options.executor = &executor;
                options.classes = plan.classes.data();
                options.class_count = plan.class_count;
                options.quantiser = quantisers[q];
                Bytes trial;
                stream_encode(bytes.data(), bytes.size(), options, trial);
                if (q == 0 || trial.size() < best.size()) {
                    best.swap(trial);
                }
            }
        } else {
            const std::uint32_t period_count = is_raw_stream(s) ? 4 : 1;
            bool first = true;
            for (std::uint32_t p = 0; p < period_count; p += 1) {
                const std::uint32_t period = 1u << p;
                if (period > 1) {
                    period_classes(bytes.size(), period, plan);
                }
                for (std::uint32_t q = 0; q < 3; q += 1) {
                    EncodeOptions options = encode_options_default();
                    options.executor = &executor;
                    if (period > 1) {
                        options.classes = plan.classes.data();
                        options.class_count = period;
                    }
                    options.quantiser = quantisers[q];
                    Bytes trial;
                    stream_encode(bytes.data(), bytes.size(), options, trial);
                    if (first || trial.size() < best.size()) {
                        best.swap(trial);
                        first = false;
                    }
                }
            }
        }
        coded[s].swap(best);
        if (log != nullptr && s == kStreamCffNumbers) {
            NumberDecoder probe;
            std::size_t consumed = 0;
            // Sequence codes, sequence extra bits, literals, low bytes, corrections (codes and extra bits).
            std::size_t payload[5] = {0, 0, 0, 0, 0};
            std::uint32_t sequences = 0;
            std::uint32_t tables = 0;
            if (number_open(probe, probe_allocator, coded[s].data(), coded[s].size(), consumed)) {
                sequences = probe.sequence_total;
                tables = probe.table_count;
                for (std::uint32_t c = 0; c < probe.chunk_count; c += 1) {
                    for (std::uint32_t k = 0; k < kNumberPayloads; k += 1) {
                        payload[k < 4 ? k : 4] += probe.chunks[c].payload_bytes[k];
                    }
                }
                number_close(probe);
            }
            std::fprintf(log, "  %-16s %10zu -> %10zu  sequences %8u tables %3u  codes %8zu extra %8zu literals %8zu low %8zu "
                        "corrections %8zu\n",
                        stream_name(s), (*streams).numbers.size() * 2, coded[s].size(), sequences, tables, payload[0],
                        payload[1], payload[2], payload[3], payload[4]);
        } else if (log != nullptr) {
            StreamDecoder probe;
            std::size_t consumed = 0;
            std::uint32_t sequences = 0;
            std::uint32_t tables = 0;
            std::size_t sequence_bytes = 0;
            std::size_t extra_bytes = 0;
            std::size_t literal_bytes = 0;
            if (stream_open(probe, probe_allocator, coded[s].data(), coded[s].size(), consumed)) {
                sequences = probe.sequence_total;
                tables = probe.table_count;
                for (std::uint32_t c = 0; c < probe.chunk_count; c += 1) {
                    sequence_bytes += probe.chunks[c].sequence_bytes;
                    extra_bytes += probe.chunks[c].extra_bytes;
                    literal_bytes += probe.chunks[c].literal_bytes;
                }
                stream_close(probe);
            }
            std::fprintf(log, "  %-16s %10zu -> %10zu  sequences %8u tables %3u  codes %8zu extra %8zu literals %8zu\n",
                        stream_name(s), bytes.size(), coded[s].size(), sequences, tables, sequence_bytes, extra_bytes,
                        literal_bytes);
        }
    }
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        put_varint(out, coded[s].size());
    }
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        out.insert(out.end(), coded[s].begin(), coded[s].end());
    }
}

// Leading and trailing spaces and tabs removed.
std::string trimmed(const std::string& line) {
    std::size_t first = 0;
    std::size_t last = line.size();
    while (first < last && (line[first] == ' ' || line[first] == '\t' || line[first] == '\r')) {
        first += 1;
    }
    while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t' || line[last - 1] == '\r')) {
        last -= 1;
    }
    return line.substr(first, last - first);
}

// The packer's own copy of the loader's limits, as messages.
bool group_within_limits(const ContainerGroup& group, std::string& error) {
    if (group.name.empty() || group.name.size() > kMaxNameBytes) {
        error = "group name length outside 1.." + std::to_string(static_cast<unsigned>(kMaxNameBytes)) + ": " + group.name;
        return false;
    }
    for (std::size_t f = 0; f < group.fonts.size(); f += 1) {
        const ContainerFont& font = group.fonts[f];
        if (font.name.empty() || font.name.size() > kMaxNameBytes || font.size == 0 || font.size > kMaxFontBytes) {
            error = "font name or size outside the format limits: " + font.name;
            return false;
        }
    }
    if (group.font_bytes > kMaxGroupFontBytes) {
        error = "group " + group.name + " holds more than kMaxGroupFontBytes";
        return false;
    }
    if (group.stream_bytes > kMaxStreamExpansion * group.font_bytes) {
        error = "group " + group.name + " decodes to more than kMaxStreamExpansion times its fonts";
        return false;
    }
    if (group.font_bytes > kMaxDecodeRatio * static_cast<std::uint64_t>(group.body.size())) {
        error = "group " + group.name + " compresses better than kMaxDecodeRatio; the loader would refuse it";
        return false;
    }
    return true;
}

}  // namespace

bool pattern_matches(const std::string& pattern, const std::string& name) noexcept {
    // Greedy match with one remembered star: on a mismatch, the last star
    // takes one more character. Linear in pattern plus name per star retry.
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::string::npos;
    std::size_t star_name = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            p += 1;
            n += 1;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p;
            star_name = n;
            p += 1;
        } else if (star != std::string::npos) {
            p = star + 1;
            star_name += 1;
            n = star_name;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        p += 1;
    }
    return p == pattern.size();
}

bool manifest_groups(const std::string& text, const std::vector<PackInput>& fonts, std::vector<PackGroupSpec>& groups,
                     std::string& error) {
    groups.clear();
    // Filled here and handed over only when the whole manifest is good.
    std::vector<PackGroupSpec> found;
    // Patterns per group, in manifest order.
    std::vector<std::vector<std::string> > patterns;
    std::size_t at = 0;
    std::uint32_t line_number = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = trimmed(text.substr(at, end - at));
        at = end + 1;
        line_number += 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.compare(0, 6, "group ") == 0) {
            const std::string rest = trimmed(line.substr(6));
            const std::size_t space = rest.find_first_of(" \t");
            const std::string mode = space == std::string::npos ? std::string() : trimmed(rest.substr(space));
            if (space == std::string::npos || (mode != "eager" && mode != "lazy")) {
                error = "manifest line " + std::to_string(line_number) + ": expected 'group <name> eager|lazy'";
                return false;
            }
            PackGroupSpec spec;
            spec.name = rest.substr(0, space);
            spec.eager = mode == "eager";
            for (std::size_t g = 0; g < found.size(); g += 1) {
                if (found[g].name == spec.name) {
                    error = "manifest line " + std::to_string(line_number) + ": group " + spec.name + " named twice";
                    return false;
                }
            }
            found.push_back(spec);
            patterns.push_back(std::vector<std::string>());
            continue;
        }
        if (found.empty()) {
            error = "manifest line " + std::to_string(line_number) + ": a pattern before the first group";
            return false;
        }
        patterns.back().push_back(line);
    }
    if (found.empty() || found.size() > kMaxGroups) {
        error = "the manifest must name 1.." + std::to_string(static_cast<unsigned>(kMaxGroups)) + " groups";
        return false;
    }
    // Each font joins the first group, in manifest order, with a pattern
    // matching its name. Fonts keep their input order in a group.
    for (std::size_t f = 0; f < fonts.size(); f += 1) {
        bool placed = false;
        for (std::size_t g = 0; g < found.size() && !placed; g += 1) {
            for (std::size_t k = 0; k < patterns[g].size() && !placed; k += 1) {
                if (pattern_matches(patterns[g][k], fonts[f].name)) {
                    found[g].fonts.push_back(static_cast<std::uint32_t>(f));
                    placed = true;
                }
            }
        }
        if (!placed) {
            error = "no manifest group matches " + fonts[f].name;
            return false;
        }
    }
    for (std::size_t g = 0; g < found.size(); g += 1) {
        if (found[g].fonts.empty()) {
            error = "manifest group " + found[g].name + " matches no font";
            return false;
        }
    }
    groups.swap(found);
    return true;
}

void container_write(const std::vector<ContainerGroup>& groups, Bytes& out) {
    out.clear();
    out.push_back('F');
    out.push_back('P');
    out.push_back('K');
    out.push_back('2');
    put_varint(out, groups.size());
    for (std::size_t g = 0; g < groups.size(); g += 1) {
        const ContainerGroup& group = groups[g];
        put_varint(out, group.name.size());
        out.insert(out.end(), group.name.begin(), group.name.end());
        out.push_back(static_cast<std::uint8_t>(group.eager ? kGroupEager : 0));
        put_varint(out, group.fonts.size());
        put_varint(out, group.font_bytes);
        put_varint(out, group.stream_bytes);
        put_varint(out, group.body.size());
        for (std::size_t f = 0; f < group.fonts.size(); f += 1) {
            const ContainerFont& font = group.fonts[f];
            put_varint(out, font.name.size());
            out.insert(out.end(), font.name.begin(), font.name.end());
            put_varint(out, font.size);
            put_le32(out, font.crc);
        }
    }
    for (std::size_t g = 0; g < groups.size(); g += 1) {
        out.insert(out.end(), groups[g].body.begin(), groups[g].body.end());
        out.insert(out.end(), kStreamPadding, 0);
    }
}

bool pack_fonts(const std::vector<PackInput>& fonts, const std::vector<PackGroupSpec>& groups, const Executor& executor,
                std::FILE* log, Bytes& out, std::string& error) {
    out.clear();
    error.clear();
    std::size_t font_total = 0;
    for (std::size_t g = 0; g < groups.size(); g += 1) {
        font_total += groups[g].fonts.size();
        for (std::size_t k = 0; k < groups[g].fonts.size(); k += 1) {
            if (groups[g].fonts[k] >= fonts.size()) {
                error = "group " + groups[g].name + " names a font that is not an input";
                return false;
            }
        }
    }
    if (groups.empty() || groups.size() > kMaxGroups || font_total > kMaxFonts) {
        error = "a pack holds 1.." + std::to_string(static_cast<unsigned>(kMaxGroups)) + " groups and at most " +
                std::to_string(static_cast<unsigned>(kMaxFonts)) + " fonts";
        return false;
    }
    std::vector<ContainerGroup> coded(groups.size());
    for (std::size_t g = 0; g < groups.size(); g += 1) {
        if (log != nullptr) {
            std::fprintf(log, "group %s (%s, %zu fonts)\n", groups[g].name.c_str(), groups[g].eager ? "eager" : "lazy",
                         groups[g].fonts.size());
        }
        pack_group_body(fonts, groups[g], executor, log, coded[g]);
        if (!group_within_limits(coded[g], error)) {
            return false;
        }
    }
    container_write(coded, out);
    return true;
}

}  // namespace stx_fontpack
