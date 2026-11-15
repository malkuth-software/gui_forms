// stx_fontpack: stream decoder. See detail/codec.hpp for the model.
#include "detail/codec.hpp"

namespace stx_fontpack {

namespace {

// Decoder tables and arrays come from the caller's allocator (runtime.hpp);
// a size of 0 never reaches it.
void* block_take(const Allocator& allocator, std::size_t size) {
    return allocator.allocate(allocator.context, size == 0 ? 1 : size);
}

void block_give(const Allocator* allocator, void* memory, std::size_t size) {
    if (memory != nullptr) {
        (*allocator).release((*allocator).context, memory, size == 0 ? 1 : size);
    }
}

// LSB-first bit reader used for the header's frequency tables and for the
// sequences' extra bits. The reader looks ahead, so running past the end only
// supplies zero bits and counts them; bit_overrun says whether any were used.
struct BitReader {
    const std::uint8_t* cursor;
    const std::uint8_t* end;
    std::uint64_t bits;
    std::uint32_t count;
    std::uint32_t padding;   // zero bits supplied past the end
};

BitReader bit_reader_make(const std::uint8_t* data, std::size_t size) {
    BitReader reader;
    reader.cursor = data;
    reader.end = data + size;
    reader.bits = 0;
    reader.count = 0;
    reader.padding = 0;
    return reader;
}

inline void bit_refill(BitReader& reader) {
    if (static_cast<std::size_t>(reader.end - reader.cursor) >= 8) {
        // Whole bytes that fit above the bits already held.
        std::uint64_t word;
        std::memcpy(&word, reader.cursor, 8);
        reader.bits |= word << reader.count;
        const std::uint32_t taken = (63u - reader.count) >> 3;
        reader.cursor += taken;
        reader.count += taken * 8;
        return;
    }
    while (reader.count <= 56) {
        if (reader.cursor < reader.end) {
            reader.bits |= static_cast<std::uint64_t>(*reader.cursor) << reader.count;
            reader.cursor += 1;
        } else if (reader.padding < 0x40000000u) {
            reader.padding += 8;
        }
        reader.count += 8;
    }
}

inline bool bit_overrun(const BitReader& reader) {
    return reader.padding > reader.count;
}

// width is at most 32.
inline std::uint32_t bit_take(BitReader& reader, std::uint32_t width) {
    if (width == 0) {
        return 0;
    }
    if (reader.count < width) {
        bit_refill(reader);
    }
    const std::uint32_t value = static_cast<std::uint32_t>(reader.bits & ((1ull << width) - 1ull));
    reader.bits >>= width;
    reader.count -= width;
    return value;
}

std::uint32_t gamma_take(BitReader& reader) {
    std::uint32_t zeros = 0;
    while (bit_take(reader, 1) == 0) {
        zeros += 1;
        if (zeros > 20) {
            // Longer than any valid code; the caller's range checks reject the value.
            return 0xFFFFFFFFu;
        }
    }
    return (1u << zeros) | bit_take(reader, zeros);
}

// Reads one frequency table of alphabet symbols into a decode table.
// An all-zero table is valid (it is never used) and decodes as symbol 0.
bool table_read(BitReader& reader, std::uint32_t alphabet, DecodeTable& table) {
    std::memset(&table, 0, sizeof(table));
    std::uint32_t symbol = 0;
    std::uint32_t slot = 0;
    while (symbol < alphabet) {
        if (bit_take(reader, 1) == 0) {
            const std::uint32_t run = gamma_take(reader);
            if (run > alphabet - symbol || bit_overrun(reader)) {
                return false;
            }
            symbol += run;
            continue;
        }
        const std::uint32_t frequency = gamma_take(reader);
        if (frequency >= kProbScale || slot + frequency > kProbScale || bit_overrun(reader)) {
            return false;
        }
        table.symbol_info[symbol] = frequency | (slot << 16);
        std::memset(table.slot_symbol + slot, static_cast<int>(symbol), frequency);
        slot += frequency;
        symbol += 1;
    }
    if (symbol != alphabet || bit_overrun(reader)) {
        return false;
    }
    if (slot == 0) {
        // Keeps a damaged stream that reaches this table inside the rANS state range.
        table.symbol_info[0] = kProbScale;
        return true;
    }
    return slot == kProbScale;
}

// True when no symbol at or above limit has a frequency, so every code a
// decode over the table yields is below limit. Applied to tables read with a
// wider alphabet than their decoder can index.
bool table_codes_below(const DecodeTable& table, std::uint32_t limit) {
    for (std::uint32_t symbol = limit; symbol < 256; symbol += 1) {
        if ((table.symbol_info[symbol] & 0xFFFFu) != 0) {
            return false;
        }
    }
    return true;
}

// rANS reader over 16-bit little-endian units. Units are read without a
// bounds test (see kStreamPadding); callers compare cursor with end at least
// every eight units and stop a damaged stream there.
struct RansReader {
    const std::uint8_t* cursor;
    const std::uint8_t* end;
};

inline std::uint32_t rans_state(RansReader& reader) {
    std::uint32_t state;
    std::memcpy(&state, reader.cursor, 4);
    reader.cursor += 4;
    return state;
}

inline std::uint32_t rans_step(std::uint32_t& state, const DecodeTable& table, RansReader& reader) {
    const std::uint32_t slot = state & (kProbScale - 1);
    const std::uint32_t symbol = table.slot_symbol[slot];
    const std::uint32_t info = table.symbol_info[symbol];
    const std::uint32_t next = (info & 0xFFFFu) * (state >> kProbBits) + slot - (info >> 16);
    // Renormalise without a branch: the unit is read either way and used only when needed.
    std::uint16_t unit;
    std::memcpy(&unit, reader.cursor, 2);
    // An all-ones mask when the state needs a unit. Written with masks because a
    // conditional here compiles to a branch that mispredicts on about half the symbols.
    const std::uint32_t mask = static_cast<std::uint32_t>(0) - static_cast<std::uint32_t>(next < kRansLow);
    state = (next & ~mask) | (((next << 16) | unit) & mask);
    reader.cursor += mask & 2u;
    return symbol;
}

void decoder_reset(StreamDecoder& decoder) {
    decoder.size = 0;
    decoder.stored = false;
    decoder.stored_bytes = nullptr;
    decoder.class_count = 1;
    decoder.quantiser = 0;
    decoder.table_count = 0;
    decoder.context_map = nullptr;
    decoder.literal_tables = nullptr;
    decoder.sequence_tables = nullptr;
    decoder.sequences = nullptr;
    decoder.sequence_total = 0;
    decoder.chunks = nullptr;
    decoder.chunk_count = 0;
    decoder.scratch_bytes = 0;
    decoder.allocator = nullptr;
}

}  // namespace

void stream_close(StreamDecoder& decoder) {
    if (decoder.allocator != nullptr) {
        // Each size is recomputed from the counts stream_open set before allocating.
        const std::size_t fine_count =
            static_cast<std::size_t>(decoder.class_count) * (quantiser_classes(decoder.quantiser) + 1);
        block_give(decoder.allocator, decoder.context_map, fine_count);
        block_give(decoder.allocator, decoder.literal_tables, static_cast<std::size_t>(decoder.table_count) * sizeof(DecodeTable));
        block_give(decoder.allocator, decoder.sequence_tables, 3 * sizeof(DecodeTable));
        block_give(decoder.allocator, decoder.sequences, static_cast<std::size_t>(decoder.sequence_total) * sizeof(Sequence));
        block_give(decoder.allocator, decoder.chunks, static_cast<std::size_t>(decoder.chunk_count) * sizeof(ChunkInfo));
    }
    decoder_reset(decoder);
}

bool stream_open(StreamDecoder& decoder, const Allocator& allocator, const std::uint8_t* blob, std::size_t blob_size,
                 std::size_t& consumed) {
    decoder_reset(decoder);
    decoder.allocator = &allocator;
    consumed = 0;
    Reader reader = reader_make(blob, blob_size);
    const std::uint64_t size = get_varint(reader);
    if (reader.failed || size > 0x7FFFFFFFu) {
        return false;
    }
    decoder.size = static_cast<std::uint32_t>(size);
    if (size == 0) {
        decoder.stored = true;
        decoder.stored_bytes = reader.cursor;
        consumed = static_cast<std::size_t>(reader.cursor - blob);
        return true;
    }
    const std::uint32_t kind = get_u8(reader);
    if (kind == 0) {
        decoder.stored = true;
        decoder.stored_bytes = get_span(reader, decoder.size);
        consumed = static_cast<std::size_t>(reader.cursor - blob);
        return !reader.failed;
    }
    const std::uint64_t class_count = get_varint(reader);
    const std::uint32_t quantiser = get_u8(reader);
    const std::uint32_t table_count = get_u8(reader);
    if (reader.failed || kind != 1 || class_count == 0 || class_count > 255 || quantiser > 4 || table_count == 0) {
        return false;
    }
    decoder.class_count = static_cast<std::uint32_t>(class_count);
    decoder.quantiser = quantiser;
    decoder.table_count = table_count;
    const std::uint32_t fine_count = decoder.class_count * (quantiser_classes(quantiser) + 1);
    // Every table is named by some fine context (the encoder drops unused
    // clusters), so more tables than contexts is a damaged header. With one
    // class and no quantiser this also keeps the decoder off the class array,
    // which is null then (A-gf-typography-01-F3).
    if (table_count > fine_count) {
        return false;
    }

    decoder.context_map = static_cast<std::uint8_t*>(block_take(allocator, fine_count));
    decoder.literal_tables =
        static_cast<DecodeTable*>(block_take(allocator, static_cast<std::size_t>(table_count) * sizeof(DecodeTable)));
    decoder.sequence_tables = static_cast<DecodeTable*>(block_take(allocator, 3 * sizeof(DecodeTable)));
    decoder.scratch_bytes = fine_count + (static_cast<std::size_t>(table_count) + 3) * sizeof(DecodeTable);
    if (decoder.context_map == nullptr || decoder.literal_tables == nullptr || decoder.sequence_tables == nullptr) {
        stream_close(decoder);
        return false;
    }

    // Context map: runs of (table, run length).
    if (table_count == 1) {
        std::memset(decoder.context_map, 0, fine_count);
    } else {
        std::uint32_t filled = 0;
        while (filled < fine_count) {
            const std::uint32_t table = get_u8(reader);
            const std::uint64_t run = get_varint(reader);
            if (reader.failed || table >= table_count || run == 0 || run > fine_count - filled) {
                stream_close(decoder);
                return false;
            }
            std::memset(decoder.context_map + filled, static_cast<int>(table), static_cast<std::size_t>(run));
            filled += static_cast<std::uint32_t>(run);
        }
    }

    const std::uint64_t table_bytes = get_varint(reader);
    const std::uint8_t* table_data = get_span(reader, static_cast<std::size_t>(table_bytes));
    if (reader.failed) {
        stream_close(decoder);
        return false;
    }
    BitReader bits = bit_reader_make(table_data, static_cast<std::size_t>(table_bytes));
    for (std::uint32_t t = 0; t < table_count; t += 1) {
        if (!table_read(bits, 256, decoder.literal_tables[t])) {
            stream_close(decoder);
            return false;
        }
    }
    for (std::uint32_t t = 0; t < 3; t += 1) {
        if (!table_read(bits, kSeqAlphabet, decoder.sequence_tables[t])) {
            stream_close(decoder);
            return false;
        }
    }
    // Table 2 codes offsets (A-gf-typography-01-F6).
    if (!table_codes_below(decoder.sequence_tables[2], kOffsetCodes)) {
        stream_close(decoder);
        return false;
    }

    const std::uint64_t chunk_count = get_varint(reader);
    // Each chunk record that follows is five varints of at least one byte, so
    // a count the remaining header cannot hold is refused before the chunk
    // array is allocated.
    if (reader.failed || chunk_count == 0 || chunk_count > decoder.size ||
        chunk_count > static_cast<std::uint64_t>(reader.end - reader.cursor) / 5) {
        stream_close(decoder);
        return false;
    }
    decoder.chunk_count = static_cast<std::uint32_t>(chunk_count);
    decoder.chunks = static_cast<ChunkInfo*>(block_take(allocator, static_cast<std::size_t>(chunk_count) * sizeof(ChunkInfo)));
    if (decoder.chunks == nullptr) {
        stream_close(decoder);
        return false;
    }
    decoder.scratch_bytes += static_cast<std::size_t>(chunk_count) * sizeof(ChunkInfo);
    std::uint64_t out_total = 0;
    std::uint64_t sequence_total = 0;
    for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
        ChunkInfo& chunk = decoder.chunks[c];
        const std::uint64_t out_length = get_varint(reader);
        const std::uint64_t sequence_count = get_varint(reader);
        const std::uint64_t sequence_bytes = get_varint(reader);
        const std::uint64_t extra_bytes = get_varint(reader);
        const std::uint64_t literal_bytes = get_varint(reader);
        if (reader.failed || out_length > decoder.size - out_total || sequence_count == 0 ||
            sequence_count > out_length + 1 || sequence_bytes > blob_size || extra_bytes > blob_size ||
            literal_bytes > blob_size) {
            stream_close(decoder);
            return false;
        }
        chunk.out_start = static_cast<std::uint32_t>(out_total);
        chunk.out_length = static_cast<std::uint32_t>(out_length);
        chunk.sequence_start = static_cast<std::uint32_t>(sequence_total);
        chunk.sequence_count = static_cast<std::uint32_t>(sequence_count);
        chunk.sequence_bytes = static_cast<std::uint32_t>(sequence_bytes);
        chunk.extra_bytes = static_cast<std::uint32_t>(extra_bytes);
        chunk.literal_bytes = static_cast<std::uint32_t>(literal_bytes);
        out_total += out_length;
        sequence_total += sequence_count;
    }
    if (out_total != decoder.size) {
        stream_close(decoder);
        return false;
    }
    for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
        ChunkInfo& chunk = decoder.chunks[c];
        chunk.sequence_payload = get_span(reader, chunk.sequence_bytes);
        chunk.extra_payload = get_span(reader, chunk.extra_bytes);
        chunk.literal_payload = get_span(reader, chunk.literal_bytes);
    }
    if (reader.failed) {
        stream_close(decoder);
        return false;
    }
    decoder.sequence_total = static_cast<std::uint32_t>(sequence_total);
    decoder.sequences = static_cast<Sequence*>(block_take(allocator, static_cast<std::size_t>(sequence_total) * sizeof(Sequence)));
    if (decoder.sequences == nullptr) {
        stream_close(decoder);
        return false;
    }
    decoder.scratch_bytes += static_cast<std::size_t>(sequence_total) * sizeof(Sequence);
    consumed = static_cast<std::size_t>(reader.cursor - blob);
    return true;
}

bool stream_decode_chunk(StreamDecoder& decoder, std::uint32_t chunk_index, std::uint8_t* out,
                         const std::uint8_t* classes, std::uint8_t* scratch) {
    if (decoder.stored) {
        if (chunk_index == 0 && decoder.size != 0) {
            std::memcpy(out, decoder.stored_bytes, decoder.size);
        }
        return true;
    }
    if (classes == nullptr && decoder.class_count != 1) {
        return false;
    }
    const ChunkInfo& chunk = decoder.chunks[chunk_index];
    Sequence* sequences = decoder.sequences + chunk.sequence_start;

    // Pass 1: sequences. Repeat offsets restart at every chunk.
    {
        const DecodeTable& length_table = decoder.sequence_tables[0];
        const DecodeTable& match_table = decoder.sequence_tables[1];
        const DecodeTable& offset_table = decoder.sequence_tables[2];
        RansReader rans;
        rans.cursor = chunk.sequence_payload;
        rans.end = chunk.sequence_payload + chunk.sequence_bytes;
        // One state per code kind, so the three decodes of a sequence do not wait on each other.
        std::uint32_t length_state = rans_state(rans);
        std::uint32_t match_state = rans_state(rans);
        std::uint32_t offset_state = rans_state(rans);
        BitReader bits = bit_reader_make(chunk.extra_payload, chunk.extra_bytes);
        std::uint32_t repeat0 = 1;
        std::uint32_t repeat1 = 2;
        std::uint32_t repeat2 = 3;
        std::uint64_t produced = 0;
        for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
            if (rans.cursor > rans.end) {
                return false;
            }
            const std::uint32_t length_code = rans_step(length_state, length_table, rans);
            const std::uint32_t literal_length =
                slot_decode(length_code, kLitLenDirect, bit_take(bits, slot_extra_bits(length_code, kLitLenDirect)));
            const std::uint32_t match_code = rans_step(match_state, match_table, rans);
            std::uint32_t match_length = 0;
            std::uint32_t offset = 0;
            if (match_code != 0) {
                match_length = slot_decode(match_code - 1, kMatchLenDirect,
                                           bit_take(bits, slot_extra_bits(match_code - 1, kMatchLenDirect))) +
                               kMinMatch;
                const std::uint32_t offset_code = rans_step(offset_state, offset_table, rans);
                if (offset_code == 0) {
                    offset = repeat0;
                } else if (offset_code == 1) {
                    offset = repeat1;
                    repeat1 = repeat0;
                    repeat0 = offset;
                } else if (offset_code == 2) {
                    offset = repeat2;
                    repeat2 = repeat1;
                    repeat1 = repeat0;
                    repeat0 = offset;
                } else {
                    offset = slot_decode(offset_code - 3, kOffsetDirect,
                                         bit_take(bits, slot_extra_bits(offset_code - 3, kOffsetDirect))) +
                             1;
                    repeat2 = repeat1;
                    repeat1 = repeat0;
                    repeat0 = offset;
                }
            }
            produced += static_cast<std::uint64_t>(literal_length) + match_length;
            // A match may not reach before the start of the stream.
            if (literal_length > 0xFFFFu || match_length > 0xFFFFu || produced > chunk.out_length ||
                (match_length != 0 && offset > chunk.out_start + produced - match_length)) {
                return false;
            }
            sequences[s].literal_length = static_cast<std::uint16_t>(literal_length);
            sequences[s].match_length = static_cast<std::uint16_t>(match_length);
            sequences[s].offset = offset;
        }
        if (produced != chunk.out_length || bit_overrun(bits)) {
            return false;
        }
    }

    // Pass 2: literals, kLiteralStates interleaved rANS states by literal index.
    {
        RansReader rans;
        rans.cursor = chunk.literal_payload;
        rans.end = chunk.literal_payload + chunk.literal_bytes;
        std::uint32_t states[kLiteralStates];
        for (std::uint32_t k = 0; k < kLiteralStates; k += 1) {
            states[k] = rans_state(rans);
        }
        const DecodeTable* tables = decoder.literal_tables;
        const std::uint8_t* map = decoder.context_map;
        const std::uint32_t quantiser = decoder.quantiser;
        std::uint8_t* cursor = out + chunk.out_start;
        if (quantiser == 0) {
            // No literal depends on another, so they decode as one flat run: gather each
            // literal's table into scratch, decode over it in place, scatter to the output.
            std::uint32_t literal_total = 0;
            if (decoder.table_count != 1) {
                const std::uint8_t* class_cursor = classes + chunk.out_start;
                for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
                    const std::uint32_t literal_length = sequences[s].literal_length;
                    for (std::uint32_t k = 0; k < literal_length; k += 1) {
                        scratch[literal_total + k] = map[class_cursor[k]];
                    }
                    literal_total += literal_length;
                    class_cursor += literal_length + sequences[s].match_length;
                }
            } else {
                for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
                    literal_total += sequences[s].literal_length;
                }
                std::memset(scratch, 0, literal_total);
            }
            std::uint32_t state0 = states[0];
            std::uint32_t state1 = states[1];
            std::uint32_t state2 = states[2];
            std::uint32_t state3 = states[3];
            std::uint32_t state4 = states[4];
            std::uint32_t state5 = states[5];
            std::uint32_t state6 = states[6];
            std::uint32_t state7 = states[7];
            std::uint32_t k = 0;
            while (k + 8 <= literal_total) {
                if (rans.cursor > rans.end) {
                    return false;
                }
                const std::uint32_t symbol0 = rans_step(state0, tables[scratch[k]], rans);
                const std::uint32_t symbol1 = rans_step(state1, tables[scratch[k + 1]], rans);
                const std::uint32_t symbol2 = rans_step(state2, tables[scratch[k + 2]], rans);
                const std::uint32_t symbol3 = rans_step(state3, tables[scratch[k + 3]], rans);
                const std::uint32_t symbol4 = rans_step(state4, tables[scratch[k + 4]], rans);
                const std::uint32_t symbol5 = rans_step(state5, tables[scratch[k + 5]], rans);
                const std::uint32_t symbol6 = rans_step(state6, tables[scratch[k + 6]], rans);
                const std::uint32_t symbol7 = rans_step(state7, tables[scratch[k + 7]], rans);
                scratch[k] = static_cast<std::uint8_t>(symbol0);
                scratch[k + 1] = static_cast<std::uint8_t>(symbol1);
                scratch[k + 2] = static_cast<std::uint8_t>(symbol2);
                scratch[k + 3] = static_cast<std::uint8_t>(symbol3);
                scratch[k + 4] = static_cast<std::uint8_t>(symbol4);
                scratch[k + 5] = static_cast<std::uint8_t>(symbol5);
                scratch[k + 6] = static_cast<std::uint8_t>(symbol6);
                scratch[k + 7] = static_cast<std::uint8_t>(symbol7);
                k += 8;
            }
            states[0] = state0;
            states[1] = state1;
            states[2] = state2;
            states[3] = state3;
            states[4] = state4;
            states[5] = state5;
            states[6] = state6;
            states[7] = state7;
            if (rans.cursor > rans.end) {
                return false;
            }
            // Fewer than eight remain; k is a multiple of eight here.
            for (std::uint32_t tail = 0; k < literal_total; tail += 1) {
                scratch[k] = static_cast<std::uint8_t>(rans_step(states[tail], tables[scratch[k]], rans));
                k += 1;
            }
            const std::uint8_t* literal_cursor = scratch;
            for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
                const std::uint32_t literal_length = sequences[s].literal_length;
                std::memcpy(cursor, literal_cursor, literal_length);
                literal_cursor += literal_length;
                cursor += literal_length + sequences[s].match_length;
            }
        } else {
            // A literal's table depends on the literal before it.
            const std::uint32_t stride = quantiser_classes(quantiser) + 1;
            std::uint32_t phase = 0;
            for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
                const std::uint32_t literal_length = sequences[s].literal_length;
                std::uint32_t previous = 0;
                for (std::uint32_t k = 0; k < literal_length; k += 1) {
                    if (rans.cursor > rans.end) {
                        return false;
                    }
                    std::uint32_t fine = previous;
                    if (classes != nullptr) {
                        fine += static_cast<std::uint32_t>(classes[(cursor - out) + k]) * stride;
                    }
                    const std::uint32_t symbol = rans_step(states[phase], tables[map[fine]], rans);
                    phase = (phase + 1) & (kLiteralStates - 1);
                    cursor[k] = static_cast<std::uint8_t>(symbol);
                    previous = 1 + quantise(quantiser, symbol);
                }
                cursor += literal_length + sequences[s].match_length;
            }
        }
        if (rans.cursor > rans.end) {
            return false;
        }
    }
    return true;
}

bool stream_copy_matches(const StreamDecoder& decoder, std::uint8_t* out) {
    if (decoder.stored) {
        return true;
    }
    std::uint8_t* cursor = out;
    const Sequence* sequences = decoder.sequences;
    for (std::uint32_t s = 0; s < decoder.sequence_total; s += 1) {
        cursor += sequences[s].literal_length;
        const std::uint32_t length = sequences[s].match_length;
        const std::uint32_t offset = sequences[s].offset;
        if (length == 0) {
            continue;
        }
        const std::uint8_t* source = cursor - offset;
        // The bytes after the match are literals already in place, so every
        // copy is exact: no writing past the match's end.
        if (offset < length || length < 8) {
            // Short, or overlapping (which repeats the last offset bytes and must run forwards).
            for (std::uint32_t k = 0; k < length; k += 1) {
                cursor[k] = source[k];
            }
        } else {
            // Eight bytes at a time; the last eight end exactly at the match's end.
            std::uint32_t k = 0;
            while (k + 8 < length) {
                std::uint64_t word;
                std::memcpy(&word, source + k, 8);
                std::memcpy(cursor + k, &word, 8);
                k += 8;
            }
            std::uint64_t word;
            std::memcpy(&word, source + length - 8, 8);
            std::memcpy(cursor + length - 8, &word, 8);
        }
        cursor += length;
    }
    return true;
}

bool stream_decode_all(const Allocator& allocator, const std::uint8_t* blob, std::size_t blob_size, std::uint8_t* out,
                       const std::uint8_t* classes, std::size_t& consumed) {
    StreamDecoder decoder;
    if (!stream_open(decoder, allocator, blob, blob_size, consumed)) {
        return false;
    }
    bool ok = true;
    if (decoder.stored) {
        ok = stream_decode_chunk(decoder, 0, out, classes, nullptr);
    } else {
        std::uint32_t longest = 0;
        for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
            if (decoder.chunks[c].out_length > longest) {
                longest = decoder.chunks[c].out_length;
            }
        }
        const std::size_t scratch_bytes = static_cast<std::size_t>(longest) + 8;
        std::uint8_t* scratch = static_cast<std::uint8_t*>(block_take(allocator, scratch_bytes));
        ok = scratch != nullptr;
        for (std::uint32_t c = 0; c < decoder.chunk_count && ok; c += 1) {
            ok = stream_decode_chunk(decoder, c, out, classes, scratch);
        }
        block_give(&allocator, scratch, scratch_bytes);
        if (ok) {
            ok = stream_copy_matches(decoder, out);
        }
    }
    stream_close(decoder);
    return ok;
}

// ---- number streams ----------------------------------------------------------

namespace {

void number_reset(NumberDecoder& decoder) {
    decoder.count = 0;
    decoder.class_count = 1;
    decoder.table_count = 0;
    decoder.context_map = nullptr;
    decoder.residual_table_count = 0;
    decoder.residual_map = nullptr;
    decoder.tables = nullptr;
    decoder.sequences = nullptr;
    decoder.sequence_total = 0;
    decoder.chunks = nullptr;
    decoder.chunk_count = 0;
    decoder.scratch_bytes = 0;
    decoder.all_table_count = 0;
    decoder.allocator = nullptr;
}

// Reads a context map of count entries, each below table_count, stored as runs.
bool map_read(Reader& reader, std::uint8_t* map, std::uint32_t count, std::uint32_t table_count) {
    if (table_count == 1) {
        std::memset(map, 0, count);
        return true;
    }
    std::uint32_t filled = 0;
    while (filled < count) {
        const std::uint32_t table = get_u8(reader);
        const std::uint64_t run = get_varint(reader);
        if (reader.failed || table >= table_count || run == 0 || run > count - filled) {
            return false;
        }
        std::memset(map + filled, static_cast<int>(table), static_cast<std::size_t>(run));
        filled += static_cast<std::uint32_t>(run);
    }
    return true;
}

}  // namespace

void number_close(NumberDecoder& decoder) {
    if (decoder.allocator != nullptr) {
        // Each size is recomputed from the counts number_open set before allocating.
        block_give(decoder.allocator, decoder.context_map, decoder.class_count);
        block_give(decoder.allocator, decoder.residual_map, static_cast<std::size_t>(decoder.class_count) * kResidualContexts);
        block_give(decoder.allocator, decoder.tables, static_cast<std::size_t>(decoder.all_table_count) * sizeof(DecodeTable));
        block_give(decoder.allocator, decoder.sequences, static_cast<std::size_t>(decoder.sequence_total) * sizeof(Sequence));
        block_give(decoder.allocator, decoder.chunks, static_cast<std::size_t>(decoder.chunk_count) * sizeof(NumberChunk));
    }
    number_reset(decoder);
}

bool number_open(NumberDecoder& decoder, const Allocator& allocator, const std::uint8_t* blob, std::size_t blob_size,
                 std::size_t& consumed) {
    number_reset(decoder);
    decoder.allocator = &allocator;
    consumed = 0;
    Reader reader = reader_make(blob, blob_size);
    const std::uint64_t count = get_varint(reader);
    if (reader.failed || count > 0x7FFFFFFFu) {
        return false;
    }
    decoder.count = static_cast<std::uint32_t>(count);
    if (count == 0) {
        consumed = static_cast<std::size_t>(reader.cursor - blob);
        return true;
    }
    const std::uint32_t class_count = get_u8(reader);
    const std::uint32_t table_count = get_u8(reader);
    // As in stream_open: no more literal tables than classes, so a one-class
    // stream never reads its (null) class array (A-gf-typography-01-F3).
    if (reader.failed || class_count == 0 || table_count == 0 || table_count > class_count) {
        return false;
    }
    decoder.class_count = class_count;
    decoder.table_count = table_count;
    decoder.context_map = static_cast<std::uint8_t*>(block_take(allocator, class_count));
    decoder.residual_map =
        static_cast<std::uint8_t*>(block_take(allocator, static_cast<std::size_t>(class_count) * kResidualContexts));
    if (decoder.context_map == nullptr || decoder.residual_map == nullptr ||
        !map_read(reader, decoder.context_map, class_count, table_count)) {
        number_close(decoder);
        return false;
    }
    const std::uint32_t residual_table_count = get_u8(reader);
    if (reader.failed || residual_table_count == 0 ||
        !map_read(reader, decoder.residual_map, class_count * kResidualContexts, residual_table_count)) {
        number_close(decoder);
        return false;
    }
    decoder.residual_table_count = residual_table_count;
    const std::uint32_t all_tables = table_count + kLowTables + residual_table_count + 5;
    decoder.all_table_count = all_tables;
    decoder.tables = static_cast<DecodeTable*>(block_take(allocator, static_cast<std::size_t>(all_tables) * sizeof(DecodeTable)));
    decoder.scratch_bytes = static_cast<std::size_t>(class_count) * (1 + kResidualContexts) +
                            static_cast<std::size_t>(all_tables) * sizeof(DecodeTable);
    if (decoder.tables == nullptr) {
        number_close(decoder);
        return false;
    }
    const std::uint64_t table_bytes = get_varint(reader);
    const std::uint8_t* table_data = get_span(reader, static_cast<std::size_t>(table_bytes));
    if (reader.failed) {
        number_close(decoder);
        return false;
    }
    BitReader bits = bit_reader_make(table_data, static_cast<std::size_t>(table_bytes));
    // Correction tables are written with a 256-symbol alphabet, but a
    // correction code indexes kResidualAlphabet-entry tables (ValueTables::bucket)
    // and sets a shift of slot_extra_bits(code), so a table that can yield a
    // code at or above kResidualAlphabet is refused here (A-gf-typography-01-F1).
    const std::uint32_t residual_first = table_count + kLowTables;
    const std::uint32_t sequence_first = residual_first + residual_table_count;
    for (std::uint32_t t = 0; t < all_tables; t += 1) {
        const std::uint32_t alphabet = t >= sequence_first ? kSeqAlphabet : 256;
        if (!table_read(bits, alphabet, decoder.tables[t])) {
            number_close(decoder);
            return false;
        }
        if (t >= residual_first && t < sequence_first && !table_codes_below(decoder.tables[t], kResidualAlphabet)) {
            number_close(decoder);
            return false;
        }
    }
    // The fifth sequence table codes offsets (A-gf-typography-01-F6).
    if (!table_codes_below(decoder.tables[sequence_first + 4], kOffsetCodes)) {
        number_close(decoder);
        return false;
    }
    const std::uint64_t chunk_count = get_varint(reader);
    // Each chunk record that follows is 2 + kNumberPayloads varints of at
    // least one byte; a count the remaining header cannot hold is refused
    // before the chunk array is allocated.
    if (reader.failed || chunk_count == 0 || chunk_count > decoder.count ||
        chunk_count > static_cast<std::uint64_t>(reader.end - reader.cursor) / (2 + kNumberPayloads)) {
        number_close(decoder);
        return false;
    }
    decoder.chunk_count = static_cast<std::uint32_t>(chunk_count);
    decoder.chunks = static_cast<NumberChunk*>(block_take(allocator, static_cast<std::size_t>(chunk_count) * sizeof(NumberChunk)));
    if (decoder.chunks == nullptr) {
        number_close(decoder);
        return false;
    }
    decoder.scratch_bytes += static_cast<std::size_t>(chunk_count) * sizeof(NumberChunk);
    std::uint64_t out_total = 0;
    std::uint64_t sequence_total = 0;
    for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
        NumberChunk& chunk = decoder.chunks[c];
        const std::uint64_t out_length = get_varint(reader);
        const std::uint64_t sequence_count = get_varint(reader);
        if (reader.failed || out_length > decoder.count - out_total || sequence_count == 0 ||
            sequence_count > out_length + 1) {
            number_close(decoder);
            return false;
        }
        for (std::uint32_t k = 0; k < kNumberPayloads; k += 1) {
            const std::uint64_t bytes = get_varint(reader);
            if (reader.failed || bytes > blob_size) {
                number_close(decoder);
                return false;
            }
            chunk.payload_bytes[k] = static_cast<std::uint32_t>(bytes);
        }
        chunk.out_start = static_cast<std::uint32_t>(out_total);
        chunk.out_length = static_cast<std::uint32_t>(out_length);
        chunk.sequence_start = static_cast<std::uint32_t>(sequence_total);
        chunk.sequence_count = static_cast<std::uint32_t>(sequence_count);
        out_total += out_length;
        sequence_total += sequence_count;
    }
    if (out_total != decoder.count) {
        number_close(decoder);
        return false;
    }
    for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
        NumberChunk& chunk = decoder.chunks[c];
        for (std::uint32_t k = 0; k < kNumberPayloads; k += 1) {
            chunk.payloads[k] = get_span(reader, chunk.payload_bytes[k]);
        }
    }
    if (reader.failed) {
        number_close(decoder);
        return false;
    }
    decoder.sequence_total = static_cast<std::uint32_t>(sequence_total);
    decoder.sequences = static_cast<Sequence*>(block_take(allocator, static_cast<std::size_t>(sequence_total) * sizeof(Sequence)));
    if (decoder.sequences == nullptr) {
        number_close(decoder);
        return false;
    }
    decoder.scratch_bytes += static_cast<std::size_t>(sequence_total) * sizeof(Sequence);
    consumed = static_cast<std::size_t>(reader.cursor - blob);
    return true;
}

namespace {

// Decoder lookup tables, built once per number stream.
struct ValueTables {
    std::int16_t base[256];        // value of a first byte whose second byte is 0
    std::int8_t slope[256];        // what one unit of second byte adds: 0, +1 or -1
    std::uint8_t bucket[kResidualAlphabet];   // residual_bucket of each correction code, times two
};

void value_tables_fill(ValueTables& tables) {
    for (std::uint32_t first = 0; first < 256; first += 1) {
        tables.slope[first] = 0;
        if (first == 255) {
            tables.base[first] = static_cast<std::int16_t>(kValueFiveBytes);
        } else if (token_has_value(first)) {
            tables.base[first] = static_cast<std::int16_t>(token_value(first));
            if (first >= 251) {
                tables.slope[first] = -1;
            } else if (first >= 247) {
                tables.slope[first] = 1;
            }
        } else {
            // 28, and first bytes no encoder writes: the rebuild rejects those it cannot place.
            tables.base[first] = static_cast<std::int16_t>(kValueThreeBytes);
        }
    }
    for (std::uint32_t code = 0; code < kResidualAlphabet; code += 1) {
        tables.bucket[code] = static_cast<std::uint8_t>(residual_bucket(code) * 2);
    }
}

}  // namespace

namespace {

// One lane of a chunk's corrections while decoding.
struct Lane {
    RansReader rans;
    std::uint32_t state;
    BitReader bits;
    std::uint32_t sequence;       // next sequence to examine
    std::uint32_t position;       // where that sequence starts in the output
    std::uint32_t wanted;         // fuzzy matches to pass over before this lane's next one
    std::uint32_t remaining;      // corrections left in the current match
    std::int16_t* cursor;
    const std::uint8_t* class_cursor;
    std::uint32_t context;
    std::uint32_t before_nonzero;
    bool done;
};

}  // namespace

bool number_decode_chunk(NumberDecoder& decoder, std::uint32_t chunk_index, std::int16_t* out,
                         const std::uint8_t* classes, std::uint8_t* scratch) {
    if (classes == nullptr && decoder.class_count != 1) {
        return false;
    }
    const NumberChunk& chunk = decoder.chunks[chunk_index];
    Sequence* sequences = decoder.sequences + chunk.sequence_start;
    const DecodeTable* literal_tables = decoder.tables;
    const DecodeTable* low_tables = decoder.tables + decoder.table_count;
    const DecodeTable* residual_tables = low_tables + kLowTables;
    const DecodeTable* sequence_tables = residual_tables + decoder.residual_table_count;
    const std::uint8_t* residual_map = decoder.residual_map;
    ValueTables values;
    value_tables_fill(values);

    BitReader bits = bit_reader_make(chunk.payloads[1], chunk.payload_bytes[1]);

    // Sequences. Tables: literal length, kind, exact length, fuzzy length, offset.
    std::uint32_t literal_total = 0;
    {
        RansReader rans;
        rans.cursor = chunk.payloads[0];
        rans.end = chunk.payloads[0] + chunk.payload_bytes[0];
        std::uint32_t length_state = rans_state(rans);
        std::uint32_t kind_state = rans_state(rans);
        std::uint32_t match_state = rans_state(rans);
        std::uint32_t offset_state = rans_state(rans);
        std::uint32_t repeat0 = 1;
        std::uint32_t repeat1 = 2;
        std::uint32_t repeat2 = 3;
        std::uint64_t produced = 0;
        for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
            if (rans.cursor > rans.end) {
                return false;
            }
            const std::uint32_t length_code = rans_step(length_state, sequence_tables[0], rans);
            const std::uint32_t literal_length =
                slot_decode(length_code, kLitLenDirect, bit_take(bits, slot_extra_bits(length_code, kLitLenDirect)));
            const std::uint32_t kind = rans_step(kind_state, sequence_tables[1], rans);
            std::uint32_t match_length = 0;
            std::uint32_t offset = 0;
            if (kind != 0) {
                if (kind > 2) {
                    return false;
                }
                const std::uint32_t match_code = rans_step(match_state, sequence_tables[1 + kind], rans);
                match_length =
                    slot_decode(match_code, kMatchLenDirect, bit_take(bits, slot_extra_bits(match_code, kMatchLenDirect))) +
                    kMinMatch;
                const std::uint32_t offset_code = rans_step(offset_state, sequence_tables[4], rans);
                if (offset_code == 0) {
                    offset = repeat0;
                } else if (offset_code == 1) {
                    offset = repeat1;
                    repeat1 = repeat0;
                    repeat0 = offset;
                } else if (offset_code == 2) {
                    offset = repeat2;
                    repeat2 = repeat1;
                    repeat1 = repeat0;
                    repeat0 = offset;
                } else {
                    offset = slot_decode(offset_code - 3, kOffsetDirect,
                                         bit_take(bits, slot_extra_bits(offset_code - 3, kOffsetDirect))) +
                             1;
                    repeat2 = repeat1;
                    repeat1 = repeat0;
                    repeat0 = offset;
                }
            }
            produced += static_cast<std::uint64_t>(literal_length) + match_length;
            if (literal_length > 0xFFFFu || match_length > 0xFFFFu || produced > chunk.out_length ||
                (match_length != 0 && offset > chunk.out_start + produced - match_length)) {
                return false;
            }
            sequences[s].literal_length = static_cast<std::uint16_t>(literal_length);
            sequences[s].match_length = static_cast<std::uint16_t>(match_length);
            sequences[s].offset = offset | (kind == 2 ? kFuzzyBit : 0u);
            literal_total += literal_length;
        }
        if (produced != chunk.out_length || bit_overrun(bits)) {
            return false;
        }
    }

    // Literal first bytes: gather each literal's table, decode over it in place.
    {
        const std::uint8_t* map = decoder.context_map;
        if (decoder.table_count != 1) {
            const std::uint8_t* class_cursor = classes + chunk.out_start;
            std::uint32_t filled = 0;
            for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
                const std::uint32_t literal_length = sequences[s].literal_length;
                for (std::uint32_t k = 0; k < literal_length; k += 1) {
                    scratch[filled + k] = map[class_cursor[k]];
                }
                filled += literal_length;
                class_cursor += literal_length + sequences[s].match_length;
            }
        } else {
            std::memset(scratch, 0, literal_total);
        }
        RansReader rans;
        rans.cursor = chunk.payloads[2];
        rans.end = chunk.payloads[2] + chunk.payload_bytes[2];
        std::uint32_t state0 = rans_state(rans);
        std::uint32_t state1 = rans_state(rans);
        std::uint32_t state2 = rans_state(rans);
        std::uint32_t state3 = rans_state(rans);
        std::uint32_t state4 = rans_state(rans);
        std::uint32_t state5 = rans_state(rans);
        std::uint32_t state6 = rans_state(rans);
        std::uint32_t state7 = rans_state(rans);
        std::uint32_t k = 0;
        while (k + 8 <= literal_total) {
            if (rans.cursor > rans.end) {
                return false;
            }
            const std::uint32_t symbol0 = rans_step(state0, literal_tables[scratch[k]], rans);
            const std::uint32_t symbol1 = rans_step(state1, literal_tables[scratch[k + 1]], rans);
            const std::uint32_t symbol2 = rans_step(state2, literal_tables[scratch[k + 2]], rans);
            const std::uint32_t symbol3 = rans_step(state3, literal_tables[scratch[k + 3]], rans);
            const std::uint32_t symbol4 = rans_step(state4, literal_tables[scratch[k + 4]], rans);
            const std::uint32_t symbol5 = rans_step(state5, literal_tables[scratch[k + 5]], rans);
            const std::uint32_t symbol6 = rans_step(state6, literal_tables[scratch[k + 6]], rans);
            const std::uint32_t symbol7 = rans_step(state7, literal_tables[scratch[k + 7]], rans);
            scratch[k] = static_cast<std::uint8_t>(symbol0);
            scratch[k + 1] = static_cast<std::uint8_t>(symbol1);
            scratch[k + 2] = static_cast<std::uint8_t>(symbol2);
            scratch[k + 3] = static_cast<std::uint8_t>(symbol3);
            scratch[k + 4] = static_cast<std::uint8_t>(symbol4);
            scratch[k + 5] = static_cast<std::uint8_t>(symbol5);
            scratch[k + 6] = static_cast<std::uint8_t>(symbol6);
            scratch[k + 7] = static_cast<std::uint8_t>(symbol7);
            k += 8;
        }
        if (rans.cursor > rans.end) {
            return false;
        }
        std::uint32_t tail_states[8];
        tail_states[0] = state0;
        tail_states[1] = state1;
        tail_states[2] = state2;
        tail_states[3] = state3;
        tail_states[4] = state4;
        tail_states[5] = state5;
        tail_states[6] = state6;
        tail_states[7] = state7;
        for (std::uint32_t tail = 0; k < literal_total; tail += 1) {
            scratch[k] = static_cast<std::uint8_t>(rans_step(tail_states[tail], literal_tables[scratch[k]], rans));
            k += 1;
        }
        if (rans.cursor > rans.end) {
            return false;
        }
    }

    // Scatter the literals, decoding second bytes as they come up.
    {
        RansReader low_rans;
        low_rans.cursor = chunk.payloads[3];
        low_rans.end = chunk.payloads[3] + chunk.payload_bytes[3];
        std::uint32_t low_states[kNumberStates];
        for (std::uint32_t k = 0; k < kNumberStates; k += 1) {
            low_states[k] = rans_state(low_rans);
        }
        std::uint32_t low_phase = 0;
        const std::uint8_t* literal_cursor = scratch;
        std::int16_t* cursor = out + chunk.out_start;
        for (std::uint32_t s = 0; s < chunk.sequence_count; s += 1) {
            const std::uint32_t literal_length = sequences[s].literal_length;
            if (low_rans.cursor > low_rans.end) {
                return false;
            }
            for (std::uint32_t k = 0; k < literal_length; k += 1) {
                const std::uint32_t first = literal_cursor[k];
                std::int32_t value = values.base[first];
                if (first >= 247 && first != 255) {
                    if (low_rans.cursor > low_rans.end) {
                        return false;
                    }
                    const std::uint32_t low = rans_step(low_states[low_phase], low_tables[first - 247], low_rans);
                    low_phase = (low_phase + 1) & (kNumberStates - 1);
                    value += values.slope[first] * static_cast<std::int32_t>(low);
                }
                cursor[k] = static_cast<std::int16_t>(value);
            }
            literal_cursor += literal_length;
            cursor += literal_length + sequences[s].match_length;
        }
        if (low_rans.cursor > low_rans.end) {
            return false;
        }
    }

    // Park each fuzzy match's corrections where the match will go. The lanes
    // advance together, one correction each per round.
    {
        Lane lanes[kResidualLanes];
        for (std::uint32_t l = 0; l < kResidualLanes; l += 1) {
            Lane& lane = lanes[l];
            lane.rans.cursor = chunk.payloads[4 + l];
            lane.rans.end = chunk.payloads[4 + l] + chunk.payload_bytes[4 + l];
            lane.state = rans_state(lane.rans);
            lane.bits = bit_reader_make(chunk.payloads[4 + kResidualLanes + l], chunk.payload_bytes[4 + kResidualLanes + l]);
            lane.sequence = 0;
            lane.position = chunk.out_start;
            lane.wanted = l;
            lane.remaining = 0;
            lane.cursor = nullptr;
            lane.class_cursor = nullptr;
            lane.context = 0;
            lane.before_nonzero = 0;
            lane.done = false;
        }
        std::uint32_t live = kResidualLanes;
        while (live != 0) {
            live = 0;
            for (std::uint32_t l = 0; l < kResidualLanes; l += 1) {
                Lane& lane = lanes[l];
                if (lane.remaining == 0) {
                    if (lane.done) {
                        continue;
                    }
                    // Find this lane's next fuzzy match: it takes every kResidualLanes-th one.
                    bool found = false;
                    while (lane.sequence < chunk.sequence_count) {
                        const Sequence& sequence = sequences[lane.sequence];
                        const std::uint32_t match_start = lane.position + sequence.literal_length;
                        lane.position = match_start + sequence.match_length;
                        lane.sequence += 1;
                        if ((sequence.offset & kFuzzyBit) == 0) {
                            continue;
                        }
                        if (lane.wanted != 0) {
                            lane.wanted -= 1;
                            continue;
                        }
                        lane.wanted = kResidualLanes - 1;
                        lane.cursor = out + match_start;
                        lane.class_cursor = classes != nullptr ? classes + match_start : nullptr;
                        lane.remaining = sequence.match_length;
                        lane.context = 0;
                        lane.before_nonzero = 0;
                        found = true;
                        break;
                    }
                    if (!found) {
                        lane.done = true;
                        continue;
                    }
                }
                if (lane.rans.cursor > lane.rans.end) {
                    return false;
                }
                std::uint32_t klass = 0;
                if (lane.class_cursor != nullptr) {
                    klass = lane.class_cursor[0];
                    lane.class_cursor += 1;
                }
                const DecodeTable& table = residual_tables[residual_map[klass * kResidualContexts + lane.context]];
                const std::uint32_t code = rans_step(lane.state, table, lane.rans);
                // The next table is known from the code alone, before any extra bits are read.
                lane.context = 1 + values.bucket[code] + lane.before_nonzero;
                lane.before_nonzero = code != 0 ? 1u : 0u;
                const std::uint32_t symbol =
                    slot_decode(code, kResidualDirect, bit_take(lane.bits, slot_extra_bits(code, kResidualDirect)));
                lane.cursor[0] = static_cast<std::int16_t>(residual_value(symbol));
                lane.cursor += 1;
                lane.remaining -= 1;
                live += 1;
            }
        }
        for (std::uint32_t l = 0; l < kResidualLanes; l += 1) {
            if (lanes[l].rans.cursor > lanes[l].rans.end || bit_overrun(lanes[l].bits)) {
                return false;
            }
        }
    }
    return true;
}

bool number_copy_matches(const NumberDecoder& decoder, std::int16_t* out) {
    std::int16_t* cursor = out;
    const Sequence* sequences = decoder.sequences;
    for (std::uint32_t s = 0; s < decoder.sequence_total; s += 1) {
        cursor += sequences[s].literal_length;
        const std::uint32_t length = sequences[s].match_length;
        if (length == 0) {
            continue;
        }
        const std::uint32_t offset = sequences[s].offset & ~kFuzzyBit;
        const std::int16_t* source = cursor - offset;
        if ((sequences[s].offset & kFuzzyBit) == 0) {
            if (offset >= length) {
                std::memcpy(cursor, source, static_cast<std::size_t>(length) * 2);
            } else {
                for (std::uint32_t k = 0; k < length; k += 1) {
                    cursor[k] = source[k];
                }
            }
        } else if (offset >= length) {
            // cursor[k] holds the correction for the number that belongs there. Source and
            // target do not overlap, so this is a plain element-wise add.
            const std::int16_t* __restrict__ from = source;
            std::int16_t* __restrict__ to = cursor;
            for (std::uint32_t k = 0; k < length; k += 1) {
                to[k] = static_cast<std::int16_t>(to[k] + from[k]);
            }
        } else {
            // Overlapping: each number may be the source of a later one in the same match.
            for (std::uint32_t k = 0; k < length; k += 1) {
                cursor[k] = static_cast<std::int16_t>(cursor[k] + source[k]);
            }
        }
        cursor += length;
    }
    return true;
}

}  // namespace stx_fontpack
