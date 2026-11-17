// stx_fontpack: the byte-stream codec.
//
// One coded stream holds one byte array. It is an LZ77 parse (literal run,
// match length, offset; three repeat offsets) whose literals are coded with
// static rANS tables chosen per literal by a context:
//
//   fine context = class(position) * (Q + 1) + previous
//
// where class(position) comes from an optional per-position class array the
// caller derives from already decoded structure (for example "this byte is
// the first byte of the third argument of an rrcurveto"), Q is the class
// count of the stream's previous-byte quantiser, and previous is 0 when the
// byte before was produced by a match (or starts a chunk) and 1 + quantised
// previous literal otherwise. A stored map takes fine contexts to tables.
//
// The stream is cut into chunks. A chunk's sequences and literals decode
// without any other chunk, so chunks decode in parallel; literals are written
// straight to their final positions. Matches may reach anywhere earlier in
// the stream, so one sequential pass afterwards copies them.
#pragma once

#include "detail/base.hpp"
#include "stx_fontpack/limits.hpp"
#include "stx_fontpack/runtime.hpp"

namespace stx_fontpack {

struct EncodeOptions {
    const std::uint8_t* classes;   // one class per byte, or null for a single class
    std::uint32_t class_count;     // 1 when classes is null; at most 255
    std::uint32_t quantiser;       // previous-byte quantiser selector, 0 for none
    std::uint32_t chunk_bytes;     // target decoded bytes per chunk
    std::uint32_t chain_depth;     // match finder effort
    std::uint32_t nice_length;     // a match this long is taken without further search
    std::uint32_t iterations;      // optimal-parse passes after the first
    const Executor* executor;      // parses chunks on its workers; null: serial_executor
};

EncodeOptions encode_options_default();

// Appends the coded stream for data[0, size) to out.
void stream_encode(const std::uint8_t* data, std::size_t size, const EncodeOptions& options, Bytes& out);

// Reduces per-position fine context ids (each below fine_count) to at most
// kMaxTables classes by clustering their byte histograms. map receives
// fine_count entries; the return value is the class count.
std::uint32_t cluster_contexts(const std::uint8_t* data, const std::uint16_t* fine, std::size_t size,
                               std::uint32_t fine_count, std::vector<std::uint8_t>& map);

// A literal run then a match. Lengths are at most 65535: the encoder splits
// longer literal runs into sequences with no match and never emits a longer match.
struct Sequence {
    std::uint32_t offset;
    std::uint16_t literal_length;
    std::uint16_t match_length;   // 0 for no match
};

// rANS decode table for one frequency table: the symbol of each of the
// kProbScale slots, and each symbol's frequency (low 16 bits) and first slot
// (high 16 bits).
struct DecodeTable {
    std::uint32_t symbol_info[256];
    std::uint8_t slot_symbol[kProbScale];
};

struct ChunkInfo {
    std::uint32_t out_start;
    std::uint32_t out_length;
    std::uint32_t sequence_start;
    std::uint32_t sequence_count;
    const std::uint8_t* sequence_payload;
    std::uint32_t sequence_bytes;
    const std::uint8_t* extra_payload;
    std::uint32_t extra_bytes;
    const std::uint8_t* literal_payload;
    std::uint32_t literal_bytes;
};

// Decoder state for one stream. stream_open fills it and allocates the
// tables and the sequence array (owned here, released by stream_close).
// The blob must outlive the decoder: payload pointers borrow from it.
struct StreamDecoder {
    std::uint32_t size;            // decoded byte count
    bool stored;                   // payload is the bytes themselves
    const std::uint8_t* stored_bytes;
    std::uint32_t class_count;
    std::uint32_t quantiser;
    std::uint32_t table_count;
    std::uint8_t* context_map;     // class_count * (Q + 1) entries
    DecodeTable* literal_tables;   // table_count tables
    DecodeTable* sequence_tables;  // three tables: literal length, match length, offset
    Sequence* sequences;
    std::uint32_t sequence_total;
    ChunkInfo* chunks;
    std::uint32_t chunk_count;
    std::size_t scratch_bytes;     // bytes allocated by stream_open
    const Allocator* allocator;    // borrowed from stream_open's caller; what stream_close releases to
};

// Parses the header at blob[0, blob_size) and reports the bytes consumed.
// blob must be followed by kStreamPadding readable bytes. Tables and arrays
// come from allocator, which must outlive the decoder (until stream_close).
// On failure nothing is left allocated.
bool stream_open(StreamDecoder& decoder, const Allocator& allocator, const std::uint8_t* blob, std::size_t blob_size,
                 std::size_t& consumed);

// Decodes one chunk's sequences and writes its literals into out (the whole
// stream's output array). classes holds the chunk's classes, classes[0]
// being the class of the chunk's first position, out_length of them; it may
// be null only if class_count is 1.
// scratch is caller storage of at least the chunk's out_length + 8 bytes.
// Safe to call for different chunks from different threads (with different scratch).
bool stream_decode_chunk(StreamDecoder& decoder, std::uint32_t chunk_index, std::uint8_t* out,
                         const std::uint8_t* classes, std::uint8_t* scratch);

// Copies every match, in order. Call once after all chunks are decoded.
bool stream_copy_matches(const StreamDecoder& decoder, std::uint8_t* out);

void stream_close(StreamDecoder& decoder);

// ---- number streams ----------------------------------------------------------
//
// A number stream holds CFF number tokens (see token_value in detail/base.hpp).
// It is parsed like a byte stream, with two kinds of match: an exact match
// copies earlier tokens; a fuzzy match copies earlier tokens and adds a small
// coded correction to each number's value, which catches the same shape
// reused with slightly different coordinates. A literal token's first byte is
// coded with a table chosen by the caller's class for that number; its second
// byte, when it has one, with a table chosen by the first byte.
//
// Decoding has the same two phases as a byte stream: chunks decode in
// parallel (literals to their final places, corrections parked in the places
// they will correct), then one sequential pass resolves the matches.

struct NumberOptions {
    const std::uint8_t* classes;   // one class per number, or null for a single class
    std::uint32_t class_count;
    std::uint32_t chunk_numbers;   // numbers per chunk
    std::uint32_t chain_depth;     // candidates tried for exact matches
    std::uint32_t fuzzy_depth;     // candidates tried for fuzzy matches
    std::uint32_t nice_length;
    std::uint32_t max_bad_run;     // consecutive corrected numbers a fuzzy match may contain
    std::uint32_t iterations;
    const Executor* executor;      // parses chunks on its workers; null: serial_executor
};

NumberOptions number_options_default();

void number_encode(const std::uint16_t* tokens, std::size_t count, const NumberOptions& options, Bytes& out);

struct NumberChunk {
    std::uint32_t out_start;
    std::uint32_t out_length;
    std::uint32_t sequence_start;
    std::uint32_t sequence_count;
    const std::uint8_t* payloads[kNumberPayloads];   // see kNumberPayloads
    std::uint32_t payload_bytes[kNumberPayloads];
};

// Sequence::offset carries this bit for a fuzzy match.
enum : std::uint32_t { kFuzzyBit = 0x80000000u };

struct NumberDecoder {
    std::uint32_t count;           // numbers
    std::uint32_t class_count;
    std::uint32_t table_count;
    std::uint8_t* context_map;     // class -> literal table
    std::uint32_t residual_table_count;
    std::uint8_t* residual_map;    // class * kResidualContexts + context -> correction table
    DecodeTable* tables;           // literal tables, then kLowTables, the correction tables, and five sequence tables
    std::uint32_t all_table_count; // length of tables
    Sequence* sequences;
    std::uint32_t sequence_total;
    NumberChunk* chunks;
    std::uint32_t chunk_count;
    std::size_t scratch_bytes;
    const Allocator* allocator;    // borrowed from number_open's caller; what number_close releases to
};

// blob must be followed by kStreamPadding readable bytes. allocator as for stream_open.
bool number_open(NumberDecoder& decoder, const Allocator& allocator, const std::uint8_t* blob, std::size_t blob_size,
                 std::size_t& consumed);

// The decoded form of a number stream is one 16-bit value per number: the
// number's value, or kValueThreeBytes / kValueFiveBytes for the first byte of
// a longer number. A correction applied to anything but a one- or two-byte
// number, or leaving their value range, shows as a value outside
// -kValueLimit..kValueLimit that is neither of those two; the caller must
// reject such values.
//
// Decodes one chunk: literal values to their places in out, each fuzzy
// match's corrections to the places they will correct. classes holds the
// chunk's classes from its first number (as for stream_decode_chunk); scratch
// holds at least the chunk's out_length + 8 bytes.
bool number_decode_chunk(NumberDecoder& decoder, std::uint32_t chunk_index, std::int16_t* out,
                         const std::uint8_t* classes, std::uint8_t* scratch);

// Resolves every match, in order. Call once after all chunks are decoded.
bool number_copy_matches(const NumberDecoder& decoder, std::int16_t* out);

void number_close(NumberDecoder& decoder);

// Convenience: single-threaded whole-stream decode; scratch from allocator.
bool stream_decode_all(const Allocator& allocator, const std::uint8_t* blob, std::size_t blob_size, std::uint8_t* out,
                       const std::uint8_t* classes, std::size_t& consumed);

}  // namespace stx_fontpack

