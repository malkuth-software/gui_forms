// stx_fontpack: the loader.
//
// Order of work, each step a set of independent jobs the caller's executor
// runs on its workers (one fork-join call per step):
//   1. decode the leading streams: the ones other streams' contexts and the
//      rebuild plan are derived from (triplet flags, CFF operator/count
//      pairs and their tables, the recipes), and plan the rebuild; the
//      font storage is allocated only after the plan has confirmed every
//      font's size; then, in one forward pass per stream, record where each
//      chunk's class walk starts (job_class_starts);
//   2. decode every other stream;
//   3. rebuild the fonts: one job per font, plus one job per chunk of CFF
//      charstrings;
//   4. check every rebuilt font against its stored CRC-32, in pieces of
//      kCheckPieceBytes so a large font is checked on several workers.
// All of this is per group: a group's body is a complete set of streams,
// and pack_load_group decodes exactly one group.
// A stream's chunks decode in parallel; the worker that finishes a stream's
// last chunk then copies that stream's matches.
// Stream contexts are produced per chunk into a worker buffer, never for a
// whole stream, to keep scratch memory low.
//
// Every read from a decoded stream and every write into a font is bounds
// checked; a damaged pack makes pack_load_group refuse the group. Before any
// of that allocates, every decoded size the group claims is checked against
// the format limits and against the container's totals (see "bounded
// claims" in pack_load_group).
//
// Memory: every allocation goes to the caller's Allocator. Scratch goes
// through Loader::scratch, which forwards to it and counts what is held, so
// LoadedGroup::scratch_peak is exact; the font storage and the font table
// handed out go to it directly. All scratch is released before
// pack_load_group returns, whatever happened.
#include "stx_fontpack/loader.hpp"

#include "detail/codec.hpp"
#include "detail/crc_combine.hpp"
#include "detail/glyph.hpp"
#include "detail/owned_array.hpp"

#include <stx_zlib/checksum.hpp>

#include <atomic>
#include <new>

namespace stx_fontpack {

namespace {

// Forward reader over part of a decoded stream.
struct Cursor {
    const std::uint8_t* at;
    const std::uint8_t* end;
    bool failed;
};

inline std::uint32_t take(Cursor& cursor) {
    if (cursor.at >= cursor.end) {
        cursor.failed = true;
        return 0;
    }
    const std::uint32_t value = *cursor.at;
    cursor.at += 1;
    return value;
}

// Returns null (and sets failed) when fewer than size bytes remain.
inline const std::uint8_t* take_span(Cursor& cursor, std::size_t size) {
    if (static_cast<std::size_t>(cursor.end - cursor.at) < size) {
        cursor.failed = true;
        return nullptr;
    }
    const std::uint8_t* start = cursor.at;
    cursor.at += size;
    return start;
}

inline std::uint32_t take_varint(Cursor& cursor) {
    std::uint32_t value = 0;
    std::uint32_t shift = 0;
    for (;;) {
        if (cursor.at >= cursor.end || shift > 28) {
            cursor.failed = true;
            return 0;
        }
        const std::uint32_t byte = *cursor.at;
        cursor.at += 1;
        value |= (byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            return value;
        }
        shift += 7;
    }
}

inline std::uint32_t take_small(Cursor& stream, Cursor& wide) {
    const std::uint32_t value = take(stream);
    if (value < 255) {
        return value;
    }
    return take_varint(wide);
}

struct FontEntry {
    const char* name;
    std::uint32_t name_length;
    std::uint32_t size;
    std::uint32_t crc;           // from the container
    std::uint8_t* out;
    std::uint32_t starts[kStreamCount];
};

struct RegionPlan {
    std::uint32_t start;         // INDEX offset in the CFF table
    std::uint32_t count;
    std::uint32_t off_size;
    std::uint32_t data_length;
    std::uint32_t header_bytes;  // count, offSize and the offset array
};

struct TablePlan {
    std::uint32_t offset;        // in the font file
    std::uint32_t length;
    std::uint32_t gap;
    std::uint32_t gap_kind;
    std::uint32_t kind;
    std::uint32_t raw_stream;
    std::uint32_t stride;        // raw: stride filter, 0 for none
    std::uint32_t metric_count;  // hmtx: numberOfHMetrics
    std::uint32_t loca_offset;   // glyf: where loca goes in the font file
    std::uint32_t loca_length;
    std::uint32_t glyph_count;
    std::uint32_t loca_long;
    std::uint32_t pad;
    std::uint32_t repeat;
    std::uint32_t tail;
    std::uint32_t region_first;  // cff: into Loader::regions
    std::uint32_t region_count;
};

enum : std::uint32_t { kNoTable = 0xFFFFFFFFu };

struct FontPlan {
    std::uint32_t mode;
    std::uint32_t table_count;
    std::uint32_t table_first;   // into Loader::tables
    std::uint32_t glyf_table;    // into Loader::tables, or kNoTable
    std::uint32_t tail_gap;
    std::uint32_t tail_kind;
};

// One chunk of CFF items to rebuild. The plan is made before the font
// storage exists (so that font sizes are confirmed by decoded data before
// they are allocated); offsets and data are set from font, offsets_at and
// data_at once it does.
struct CffChunk {
    std::uint32_t font;
    std::uint32_t offsets_at;    // where the INDEX's offset array starts in the font file
    std::uint32_t data_at;       // where its item bytes start
    std::uint8_t* offsets;       // the INDEX's offset array
    std::uint32_t off_size;
    std::uint8_t* data;          // the INDEX's item bytes
    std::uint32_t data_length;
    std::uint32_t item_first;
    std::uint32_t item_count;
    std::uint32_t positions[kCffIndexFieldCount];
    std::uint32_t data_end;      // where the chunk's last item must end
};

// Where one chunk's class walk starts: the walk's state at the last step
// boundary at or before the chunk's first position. source_at indexes the
// stream the walk reads (triplet flags, or CFF operator pairs); position is
// the derived stream's position at that boundary.
struct ClassStart {
    std::uint32_t source_at;
    std::uint32_t position;
};

struct Worker {
    std::uint8_t* class_buffer;
    std::size_t class_capacity;
    std::uint8_t* literal_buffer;   // the stream decoder's per-chunk scratch
    std::size_t literal_capacity;
    std::uint8_t* point_bytes;   // 5 bytes per point
    std::int16_t* point_dx;
    std::int16_t* point_dy;
    std::uint8_t* point_flags;
    std::size_t point_capacity;
};

struct Loader;

struct Job {
    void (*run)(Loader& loader, const Job& job, Worker& worker);
    std::uint32_t a;
    std::uint32_t b;
};

struct Loader {
    FontEntry* fonts;                  // font_count + 1 entries from scratch, or null
    std::uint32_t font_count;
    const std::uint8_t* blobs[kStreamCount];
    std::size_t blob_sizes[kStreamCount];
    StreamDecoder decoders[kStreamCount];
    bool opened[kStreamCount];
    std::uint8_t* decoded[kStreamCount];
    std::size_t decoded_bytes[kStreamCount];   // what decoded[s] was allocated with
    std::atomic<std::uint32_t> chunks_left[kStreamCount];   // chunk jobs not yet finished
    NumberDecoder number_decoder;      // kStreamCffNumbers is a number stream
    bool number_opened;
    std::int16_t* numbers;             // its decoded values
    std::size_t numbers_bytes;         // what numbers was allocated with
    std::atomic<std::uint32_t> number_chunks_left;
    OwnedArray<FontPlan> font_plans;
    OwnedArray<TablePlan> tables;
    OwnedArray<RegionPlan> regions;
    OwnedArray<CffChunk> cff_chunks;
    // One per chunk of kStreamGlyphCoords and of the number stream, filled by
    // job_class_starts before step 2 when the stream derives classes.
    OwnedArray<ClassStart> coords_starts;
    OwnedArray<ClassStart> number_starts;
    std::uint32_t pair_base[kCffPairCount];   // number context base of each pair symbol
    std::uint8_t pair_op[kCffPairCount];
    std::uint8_t pair_count[kCffPairCount];
    std::atomic<bool> failed;
    std::atomic<bool> no_memory;       // an allocation failed (set with failed)
    // Step 4: piece p of font f has its CRC-32 at piece_crc[piece_first[f] + p].
    OwnedArray<std::uint32_t> piece_first;   // font_count + 1 entries
    OwnedArray<std::uint32_t> piece_crc;
    std::atomic<std::size_t> scratch_now;
    std::atomic<std::size_t> scratch_peak;
    Allocator caller;                  // the caller's allocator
    Allocator scratch;                 // caller, counted (counted_allocate); context is this Loader
    const Executor* executor;          // borrowed for the call
    std::uint32_t worker_count;        // 1..kMaxWorkers
    Worker* workers;                   // worker_count entries in pack_load_group's frame
    // The current step's jobs; the workers take them in order through job_next.
    const Job* jobs;
    std::uint32_t job_count;
    std::atomic<std::uint32_t> job_next;
    std::uint16_t value_tokens[2 * kValueLimit + 1];   // token of value v at [v + kValueLimit]
};

// At most this many jobs in one step, so job_next (one increment per job and
// one per worker that finds the step empty) cannot wrap.
enum : std::uint32_t { kMaxStepJobs = 0x7FFFFFFFu };

// Loader::scratch's functions: the caller's allocator, with what is held
// counted. A failure marks the load as out of memory.
void* counted_allocate(void* context, std::size_t size) {
    Loader& loader = *static_cast<Loader*>(context);
    void* memory = loader.caller.allocate(loader.caller.context, size);
    if (memory == nullptr) {
        loader.no_memory.store(true);
        loader.failed.store(true);
        return nullptr;
    }
    const std::size_t now = loader.scratch_now.fetch_add(size) + size;
    std::size_t peak = loader.scratch_peak.load();
    while (now > peak && !loader.scratch_peak.compare_exchange_weak(peak, now)) {
        // peak now holds the current value; try again while ours is larger.
    }
    return memory;
}

void counted_release(void* context, void* memory, std::size_t size) {
    Loader& loader = *static_cast<Loader*>(context);
    loader.caller.release(loader.caller.context, memory, size);
    loader.scratch_now.fetch_sub(size);
}

// A size of 0 never reaches the allocator; take and give must see the same size.
void* scratch_take(Loader& loader, std::size_t size) {
    return loader.scratch.allocate(loader.scratch.context, size == 0 ? 1 : size);
}

void scratch_give(Loader& loader, void* memory, std::size_t size) {
    if (memory != nullptr) {
        loader.scratch.release(loader.scratch.context, memory, size == 0 ? 1 : size);
    }
}

inline Cursor stream_cursor(const Loader& loader, std::uint32_t stream, std::uint32_t start) {
    Cursor cursor;
    if (loader.decoded[stream] == nullptr) {
        // The number stream is not read through a byte cursor.
        cursor.at = nullptr;
        cursor.end = nullptr;
        cursor.failed = false;
        return cursor;
    }
    const std::uint32_t size = loader.decoders[stream].size;
    cursor.failed = start > size;
    cursor.at = loader.decoded[stream] + (start > size ? size : start);
    cursor.end = loader.decoded[stream] + size;
    return cursor;
}

// ---- job runner ------------------------------------------------------------

void job_loop(Loader& loader, Worker& worker) {
    for (;;) {
        const std::uint32_t index = loader.job_next.fetch_add(1);
        if (index >= loader.job_count) {
            return;
        }
        if (!loader.failed.load(std::memory_order_relaxed)) {
            const Job& job = loader.jobs[index];
            job.run(loader, job, worker);
        }
    }
}

// WorkerTask::run for every step: worker takes jobs until none are left.
void step_worker(void* context, std::uint32_t worker) {
    Loader& loader = *static_cast<Loader*>(context);
    if (worker >= loader.worker_count) {
        return;
    }
    job_loop(loader, loader.workers[worker]);
}

// Runs the jobs on the executor; returns when all are done (the executor's
// completion makes every job's writes visible here).
void run_jobs(Loader& loader, const OwnedArray<Job>& jobs) {
    if (jobs.length == 0) {
        return;
    }
    loader.jobs = jobs.data;
    loader.job_count = static_cast<std::uint32_t>(jobs.length);
    loader.job_next.store(0);
    WorkerTask task;
    task.run = step_worker;
    task.context = &loader;
    task.worker_count = loader.worker_count;
    (*loader.executor).run_workers((*loader.executor).context, task);
    loader.jobs = nullptr;
    loader.job_count = 0;
}

// Appends a job to the step being built. False (and the load failed) when the
// step would hold more than kMaxStepJobs or the array cannot grow.
bool job_add(Loader& loader, OwnedArray<Job>& jobs, void (*run)(Loader&, const Job&, Worker&), std::uint32_t a,
             std::uint32_t b) {
    Job job;
    job.run = run;
    job.a = a;
    job.b = b;
    if (jobs.length >= kMaxStepJobs || !owned_array_append(jobs, loader.scratch, job)) {
        loader.failed.store(true);
        return false;
    }
    return true;
}

// ---- stream decoding -------------------------------------------------------

bool is_period_stream(std::uint32_t stream) {
    return stream == kStreamDirectory || stream == kStreamRawOther || stream == kStreamRawLayout ||
           stream == kStreamRawCmap || stream == kStreamRawPost || stream == kStreamRawName ||
           stream == kStreamRawMetrics || stream == kStreamGlyphBbox || stream == kStreamGlyphComposite ||
           stream == kStreamCffNumWide || stream == kStreamHmtxAdvance || stream == kStreamHmtxLsb;
}

// Streams other streams' contexts or the rebuild plan are derived from; they decode first.
bool is_leading_stream(std::uint32_t stream) {
    return stream == kStreamMeta || stream == kStreamDirectory || stream == kStreamGlyphFlags ||
           stream == kStreamCffOps || stream == kStreamCffPairTable || stream == kStreamCffIndex ||
           stream == kStreamCffNumMap;
}

// Classes of kStreamGlyphCoords positions [start, start + length): which payload
// byte of which triplet form. Walks the flags from the chunk's recorded start
// (job_class_starts), the boundary at or before start reached by walking
// from the nearest font start.
bool coords_classes(const Loader& loader, const ClassStart& from, std::uint32_t start, std::uint32_t length,
                    std::uint8_t* classes) {
    const std::uint8_t* flags = loader.decoded[kStreamGlyphFlags];
    const std::uint32_t flag_count = loader.decoders[kStreamGlyphFlags].size;
    std::uint32_t flag_at = from.source_at;
    std::uint32_t position = from.position;
    if (position > start) {
        // Every class of the range must be written; pack_load_group already
        // refuses a first font that does not start at 0, which is the only way here.
        return false;
    }
    const std::uint32_t end = start + length;
    while (position < end) {
        if (flag_at >= flag_count) {
            return false;
        }
        const std::uint32_t flag = flags[flag_at];
        flag_at += 1;
        const std::uint32_t first = triplet_class(flag);
        const std::uint32_t count = triplet_bytes(flag);
        for (std::uint32_t b = 0; b < count; b += 1) {
            if (position >= start && position < end) {
                classes[position - start] = static_cast<std::uint8_t>(first + b);
            }
            position += 1;
        }
    }
    return true;
}

// Classes of the numbers [start, start + length) of kStreamCffNumbers: the stored class of
// each number's (operator, count, index) context. Walks the pair stream from
// the chunk's recorded start (job_class_starts), the boundary at or before
// start reached from the nearest chunk entry of kStreamCffIndex.
bool number_classes(const Loader& loader, const ClassStart& from, std::uint32_t start, std::uint32_t length,
                    std::uint8_t* classes) {
    const std::uint8_t* pairs = loader.decoded[kStreamCffOps];
    const std::uint32_t pair_size = loader.decoders[kStreamCffOps].size;
    const std::uint8_t* map = loader.decoded[kStreamCffNumMap];
    std::uint32_t pair_at = from.source_at;
    std::uint32_t position = from.position;
    if (position > start) {
        return false;
    }
    const std::uint32_t end = start + length;
    while (position < end) {
        if (pair_at >= pair_size) {
            return false;
        }
        const std::uint32_t symbol = pairs[pair_at];
        pair_at += 1;
        std::uint32_t base;
        std::uint32_t count;
        if (symbol != kCffPairEscape) {
            base = loader.pair_base[symbol];
            count = loader.pair_count[symbol];
        } else {
            if (pair_size - pair_at < 2 || pairs[pair_at] >= kCffOpCount) {
                return false;
            }
            count = pairs[pair_at + 1];
            base = cff_number_context_base(pairs[pair_at], count);
            pair_at += 2;
        }
        if (position + count <= start) {
            position += count;
            continue;
        }
        if (position >= start) {
            // A context row's first twelve entries are the classes of arguments 0..11.
            // Copying all twelve may run past the group (and by up to eleven bytes past
            // the range, into the buffer's slack); the next group overwrites the excess.
            std::memcpy(classes + (position - start), map + base, 12);
            for (std::uint32_t j = 12; j < count; j += 1) {
                if (position + j < end) {
                    classes[position + j - start] = map[base + cff_number_context_index(j)];
                }
            }
            position += count;
            continue;
        }
        for (std::uint32_t j = 0; j < count; j += 1) {
            if (position >= start && position < end) {
                classes[position - start] = map[base + cff_number_context_index(j)];
            }
            position += 1;
        }
    }
    return true;
}

// One step of a pair walk: the numbers pair_at's symbol stands for, and the
// pair bytes it takes. False for a damaged escape.
bool pair_step(const Loader& loader, std::uint32_t pair_at, std::uint32_t& count, std::uint32_t& taken) {
    const std::uint8_t* pairs = loader.decoded[kStreamCffOps];
    const std::uint32_t pair_size = loader.decoders[kStreamCffOps].size;
    const std::uint32_t symbol = pairs[pair_at];
    if (symbol != kCffPairEscape) {
        count = loader.pair_count[symbol];
        taken = 1;
        return true;
    }
    if (pair_size - pair_at - 1 < 2 || pairs[pair_at + 1] >= kCffOpCount) {
        return false;
    }
    count = pairs[pair_at + 2];
    taken = 3;
    return true;
}

// a: 0 for kStreamGlyphCoords, 1 for the number stream. Records where each
// chunk's class walk starts, in one forward pass over the stream the classes
// come from (A-gf-typography-01-F5). Restarting every chunk's walk from the
// nearest font start (or index entry) cost chunks times stream length on a
// pack cut into many small chunks; the pass costs one walk of the stream.
// Each chunk's walk still begins exactly where the restart would have put
// it, so the classes are unchanged.
void job_class_starts(Loader& loader, const Job& job, Worker& worker) {
    static_cast<void>(worker);
    if (job.a == 0) {
        const StreamDecoder& decoder = loader.decoders[kStreamGlyphCoords];
        if (decoder.stored || decoder.class_count <= 1) {
            return;
        }
        if (!owned_array_resize_zeroed(loader.coords_starts, loader.scratch, decoder.chunk_count)) {
            loader.failed.store(true);
            return;
        }
        const std::uint8_t* flags = loader.decoded[kStreamGlyphFlags];
        const std::uint32_t flag_count = loader.decoders[kStreamGlyphFlags].size;
        // Font 0 starts every stream at 0 (pack_load_group checks it).
        std::uint32_t font = 0;
        std::uint32_t flag_at = loader.fonts[0].starts[kStreamGlyphFlags];
        std::uint32_t position = loader.fonts[0].starts[kStreamGlyphCoords];
        for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
            const std::uint32_t start = decoder.chunks[c].out_start;
            // The last font whose start is at or before start; starts never decrease.
            std::uint32_t low = font;
            while (low + 1 < loader.font_count && loader.fonts[low + 1].starts[kStreamGlyphCoords] <= start) {
                low += 1;
            }
            if (low != font) {
                font = low;
                flag_at = loader.fonts[low].starts[kStreamGlyphFlags];
                position = loader.fonts[low].starts[kStreamGlyphCoords];
            }
            // Whole triplets that end at or before start; a walk that runs out
            // of flags stops here and the chunk's own walk refuses.
            while (position <= start && flag_at < flag_count) {
                const std::uint32_t bytes = triplet_bytes(flags[flag_at]);
                if (position + bytes > start) {
                    break;
                }
                position += bytes;
                flag_at += 1;
            }
            loader.coords_starts.data[c].source_at = flag_at;
            loader.coords_starts.data[c].position = position;
        }
        return;
    }
    const NumberDecoder& decoder = loader.number_decoder;
    if (decoder.count == 0 || decoder.class_count <= 1) {
        return;
    }
    if (!owned_array_resize_zeroed(loader.number_starts, loader.scratch, decoder.chunk_count)) {
        loader.failed.store(true);
        return;
    }
    const std::uint8_t* index = loader.decoded[kStreamCffIndex];
    const std::uint32_t entries = loader.decoders[kStreamCffIndex].size / kCffIndexEntryBytes;
    const std::uint32_t pair_size = loader.decoders[kStreamCffOps].size;
    // The index entries' positions never decrease (checked after step 1), so
    // the entry each chunk starts from never moves back, and a walk that
    // changes entry starts past everything walked for earlier chunks.
    std::uint32_t entry = 0;
    std::uint32_t pair_at = 0;
    std::uint32_t position = 0;
    bool walking = false;
    for (std::uint32_t c = 0; c < decoder.chunk_count; c += 1) {
        const std::uint32_t start = decoder.chunks[c].out_start;
        if (entries == 0 || le32(index + kCffIndexNumbers * 4) > start) {
            // number_classes refuses a chunk before the first entry.
            loader.failed.store(true);
            return;
        }
        std::uint32_t low = walking ? entry : 0;
        while (low + 1 < entries && le32(index + (low + 1) * kCffIndexEntryBytes + kCffIndexNumbers * 4) <= start) {
            low += 1;
        }
        if (!walking || low != entry) {
            walking = true;
            entry = low;
            pair_at = le32(index + low * kCffIndexEntryBytes + kCffIndexOps * 4);
            position = le32(index + low * kCffIndexEntryBytes + kCffIndexNumbers * 4);
        }
        while (position <= start && pair_at < pair_size) {
            std::uint32_t count = 0;
            std::uint32_t taken = 0;
            if (!pair_step(loader, pair_at, count, taken)) {
                break;
            }
            if (position + count > start) {
                break;
            }
            position += count;
            pair_at += taken;
        }
        loader.number_starts.data[c].source_at = pair_at;
        loader.number_starts.data[c].position = position;
    }
}

// a: stream. Parses the stream's header, builds its tables and takes its output buffer.
void job_open_stream(Loader& loader, const Job& job, Worker& worker) {
    static_cast<void>(worker);
    const std::uint32_t s = job.a;
    std::size_t consumed = 0;
    bool ok = true;
    if (s == kStreamCffNumbers) {
        // The number stream has its own decoder; its byte-stream slot only records the count
        // so that the checks on stream positions cover it too.
        std::memset(&loader.decoders[s], 0, sizeof(StreamDecoder));
        // An open decoder holds its tables whatever the header's length:
        // it is marked open before the length is checked, so that
        // loader_release closes it on refusal too.
        const bool opened = number_open(loader.number_decoder, loader.scratch, loader.blobs[s], loader.blob_sizes[s],
                                        consumed);
        loader.number_opened = opened;
        ok = opened && consumed == loader.blob_sizes[s];
        if (ok) {
            loader.decoders[s].size = loader.number_decoder.count;
            loader.decoders[s].stored = true;
            const std::size_t bytes = (static_cast<std::size_t>(loader.number_decoder.count) + 8) * sizeof(std::int16_t);
            loader.numbers = static_cast<std::int16_t*>(scratch_take(loader, bytes));
            ok = loader.numbers != nullptr;
            loader.numbers_bytes = ok ? bytes : 0;
        }
    } else {
        // Marked open before the length check, as the number stream is.
        const bool opened = stream_open(loader.decoders[s], loader.scratch, loader.blobs[s], loader.blob_sizes[s],
                                        consumed);
        loader.opened[s] = opened;
        ok = opened && consumed == loader.blob_sizes[s];
        if (ok) {
            const std::size_t bytes = static_cast<std::size_t>(loader.decoders[s].size) + 8;
            loader.decoded[s] = static_cast<std::uint8_t*>(scratch_take(loader, bytes));
            ok = loader.decoded[s] != nullptr;
            loader.decoded_bytes[s] = ok ? bytes : 0;
        }
    }
    if (!ok) {
        // Leave a size the later checks can read.
        loader.decoders[s].size = 0;
        loader.failed.store(true);
    }
}

// a: stream, b: chunk.
void job_decode_chunk(Loader& loader, const Job& job, Worker& worker) {
    const std::uint32_t stream = job.a;
    StreamDecoder& decoder = loader.decoders[stream];
    const std::uint8_t* classes = nullptr;
    if (!decoder.stored && decoder.class_count > 1) {
        const ChunkInfo& chunk = decoder.chunks[job.b];
        if (worker.class_capacity < chunk.out_length) {
            scratch_give(loader, worker.class_buffer, worker.class_capacity);
            worker.class_capacity = static_cast<std::size_t>(chunk.out_length) + 4096;
            worker.class_buffer = static_cast<std::uint8_t*>(scratch_take(loader, worker.class_capacity));
            if (worker.class_buffer == nullptr) {
                worker.class_capacity = 0;
                return;
            }
        }
        bool ok = true;
        if (stream == kStreamGlyphCoords) {
            ok = decoder.class_count == kTripletClassCount && job.b < loader.coords_starts.length &&
                 coords_classes(loader, loader.coords_starts.data[job.b], chunk.out_start, chunk.out_length,
                                worker.class_buffer);
        } else if (is_period_stream(stream) && (decoder.class_count & (decoder.class_count - 1)) == 0) {
            // Periods are powers of two.
            const std::uint32_t mask = decoder.class_count - 1;
            for (std::uint32_t k = 0; k < chunk.out_length; k += 1) {
                worker.class_buffer[k] = static_cast<std::uint8_t>((chunk.out_start + k) & mask);
            }
        } else {
            ok = false;
        }
        if (!ok) {
            loader.failed.store(true);
            return;
        }
        // The codec indexes classes from the chunk's first position.
        classes = worker.class_buffer;
    }
    if (!decoder.stored && worker.literal_capacity < static_cast<std::size_t>(decoder.chunks[job.b].out_length) + 8) {
        scratch_give(loader, worker.literal_buffer, worker.literal_capacity);
        worker.literal_capacity = static_cast<std::size_t>(decoder.chunks[job.b].out_length) + 4096;
        worker.literal_buffer = static_cast<std::uint8_t*>(scratch_take(loader, worker.literal_capacity));
        if (worker.literal_buffer == nullptr) {
            worker.literal_capacity = 0;
            return;
        }
    }
    {
        if (!stream_decode_chunk(decoder, job.b, loader.decoded[stream], classes, worker.literal_buffer)) {
            loader.failed.store(true);
        }
    }
    // Whoever finishes a stream's last chunk copies its matches, while the
    // other workers carry on with other streams.
    if (loader.chunks_left[stream].fetch_sub(1) == 1 && !loader.failed.load()) {
        if (!stream_copy_matches(decoder, loader.decoded[stream])) {
            loader.failed.store(true);
        }
        const std::uint32_t size = decoder.size;
        stream_close(decoder);
        decoder.size = size;   // the decoded length is still needed for bounds
        loader.opened[stream] = false;
    }
}

bool worker_buffers(Loader& loader, Worker& worker, std::size_t length) {
    if (worker.class_capacity < length) {
        scratch_give(loader, worker.class_buffer, worker.class_capacity);
        worker.class_capacity = length + 4096;
        worker.class_buffer = static_cast<std::uint8_t*>(scratch_take(loader, worker.class_capacity));
        if (worker.class_buffer == nullptr) {
            worker.class_capacity = 0;
            return false;
        }
    }
    if (worker.literal_capacity < length + 8) {
        scratch_give(loader, worker.literal_buffer, worker.literal_capacity);
        worker.literal_capacity = length + 4096;
        worker.literal_buffer = static_cast<std::uint8_t*>(scratch_take(loader, worker.literal_capacity));
        if (worker.literal_buffer == nullptr) {
            worker.literal_capacity = 0;
            return false;
        }
    }
    return true;
}

// a: chunk of the number stream.
void job_number_chunk(Loader& loader, const Job& job, Worker& worker) {
    NumberDecoder& decoder = loader.number_decoder;
    const NumberChunk& chunk = decoder.chunks[job.a];
    if (!worker_buffers(loader, worker, chunk.out_length)) {
        return;
    }
    const std::uint8_t* classes = nullptr;
    if (decoder.class_count > 1) {
        if (job.a >= loader.number_starts.length ||
            !number_classes(loader, loader.number_starts.data[job.a], chunk.out_start, chunk.out_length,
                            worker.class_buffer)) {
            loader.failed.store(true);
            return;
        }
        classes = worker.class_buffer;
    }
    {
        if (!number_decode_chunk(decoder, job.a, loader.numbers, classes, worker.literal_buffer)) {
            loader.failed.store(true);
        }
    }
    if (loader.number_chunks_left.fetch_sub(1) == 1 && !loader.failed.load()) {
        if (!number_copy_matches(decoder, loader.numbers)) {
            loader.failed.store(true);
        }
        const std::uint32_t count = decoder.count;
        number_close(decoder);
        decoder.count = count;
        loader.number_opened = false;
    }
}

// ---- plan ------------------------------------------------------------------

// Reads every font's recipe from kStreamMeta and checks it against the font
// sizes, so the rebuild jobs can write without rechecking table placement.
bool plan_fonts(Loader& loader) {
    const std::uint32_t index_entries = loader.decoders[kStreamCffIndex].size / kCffIndexEntryBytes;
    std::uint32_t index_entry = 0;
    if (!owned_array_resize_zeroed(loader.font_plans, loader.scratch, loader.font_count)) {
        return false;
    }
    for (std::uint32_t f = 0; f < loader.font_count; f += 1) {
        const FontEntry& font = loader.fonts[f];
        FontPlan& plan = loader.font_plans.data[f];
        Cursor meta = stream_cursor(loader, kStreamMeta, font.starts[kStreamMeta]);
        plan.mode = take(meta);
        plan.table_count = 0;
        plan.table_first = static_cast<std::uint32_t>(loader.tables.length);
        plan.glyf_table = kNoTable;
        plan.tail_gap = 0;
        plan.tail_kind = 0;
        if (meta.failed || plan.mode > 1) {
            return false;
        }
        if (plan.mode == 0) {
            // A verbatim font is its own bytes in kStreamRawOther, up to the
            // next font's start there: its size must be exactly that extent.
            const std::uint32_t raw_end =
                f + 1 < loader.font_count ? loader.fonts[f + 1].starts[kStreamRawOther] : loader.decoders[kStreamRawOther].size;
            if (raw_end < font.starts[kStreamRawOther] || raw_end - font.starts[kStreamRawOther] != font.size) {
                return false;
            }
            continue;
        }
        plan.table_count = take_varint(meta);
        const std::uint64_t directory_size = 12 + static_cast<std::uint64_t>(plan.table_count) * 16;
        Cursor directory = stream_cursor(loader, kStreamDirectory, font.starts[kStreamDirectory]);
        const std::uint8_t* records = take_span(directory, static_cast<std::size_t>(directory_size));
        if (meta.failed || records == nullptr || directory_size > font.size) {
            return false;
        }
        std::uint64_t previous_end = directory_size;
        for (std::uint32_t t = 0; t < plan.table_count; t += 1) {
            TablePlan table;
            std::memset(&table, 0, sizeof(table));
            const std::uint32_t record = take_varint(meta);
            if (meta.failed || record >= plan.table_count) {
                return false;
            }
            table.offset = be32(records + 12 + record * 16 + 8);
            table.length = be32(records + 12 + record * 16 + 12);
            table.gap = take_varint(meta);
            table.gap_kind = take(meta);
            table.kind = take(meta);
            if (meta.failed || table.gap_kind > 1 || previous_end + table.gap != table.offset ||
                table.length > font.size - table.offset || table.offset > font.size) {
                return false;
            }
            previous_end = static_cast<std::uint64_t>(table.offset) + table.length;
            if (table.kind == kTableRaw) {
                table.raw_stream = take(meta);
                table.stride = take(meta);
                if (table.raw_stream >= kStreamCount) {
                    return false;
                }
            } else if (table.kind == kTableHmtx) {
                table.metric_count = take_varint(meta);
            } else if (table.kind == kTableGlyf) {
                const std::uint32_t loca_record = take_varint(meta);
                table.glyph_count = take_varint(meta);
                table.loca_long = take(meta);
                table.pad = take(meta);
                table.repeat = take(meta);
                table.tail = take_varint(meta);
                if (meta.failed || loca_record >= plan.table_count || table.loca_long > 1 ||
                    (table.pad != 1 && table.pad != 2 && table.pad != 4) || table.tail > table.length ||
                    table.glyph_count > 65535) {
                    return false;
                }
                table.loca_offset = be32(records + 12 + loca_record * 16 + 8);
                table.loca_length = be32(records + 12 + loca_record * 16 + 12);
                if (table.loca_length != (table.glyph_count + 1) * (table.loca_long != 0 ? 4u : 2u) ||
                    table.loca_offset > font.size || table.loca_length > font.size - table.loca_offset) {
                    return false;
                }
            } else if (table.kind == kTableCff) {
                table.region_first = static_cast<std::uint32_t>(loader.regions.length);
                table.region_count = take_varint(meta);
                std::uint64_t region_end = 0;
                for (std::uint32_t r = 0; r < table.region_count; r += 1) {
                    RegionPlan region;
                    region.start = take_varint(meta);
                    region.count = take_varint(meta);
                    region.off_size = take(meta);
                    region.data_length = take_varint(meta);
                    if (meta.failed || region.start < region_end || region.count > 65535) {
                        return false;
                    }
                    if (region.count == 0) {
                        region.header_bytes = 2;
                        if (region.data_length != 0) {
                            return false;
                        }
                    } else {
                        if (region.off_size < 1 || region.off_size > 4) {
                            return false;
                        }
                        region.header_bytes = 3 + (region.count + 1) * region.off_size;
                        // Every offset must fit the INDEX's offset size.
                        if (region.off_size < 4 &&
                            static_cast<std::uint64_t>(region.data_length) + 1 >= (1ull << (8 * region.off_size))) {
                            return false;
                        }
                    }
                    region_end = static_cast<std::uint64_t>(region.start) + region.header_bytes + region.data_length;
                    if (region_end > table.length) {
                        return false;
                    }
                    const std::uint32_t chunks = (region.count + kCffChunkItems - 1) / kCffChunkItems;
                    if (chunks > index_entries - index_entry) {
                        return false;
                    }
                    for (std::uint32_t c = 0; c < chunks; c += 1) {
                        const std::uint8_t* entry =
                            loader.decoded[kStreamCffIndex] + static_cast<std::size_t>(index_entry + c) * kCffIndexEntryBytes;
                        CffChunk chunk;
                        chunk.off_size = region.off_size;
                        // Both lie inside the table: region_end <= table.length was checked above.
                        chunk.font = f;
                        chunk.offsets_at = table.offset + region.start + 3;
                        chunk.data_at = table.offset + region.start + region.header_bytes;
                        chunk.offsets = nullptr;
                        chunk.data = nullptr;
                        chunk.data_length = region.data_length;
                        chunk.item_first = c * kCffChunkItems;
                        chunk.item_count = region.count - chunk.item_first;
                        if (chunk.item_count > kCffChunkItems) {
                            chunk.item_count = kCffChunkItems;
                        }
                        for (std::uint32_t k = 0; k < kCffIndexFieldCount; k += 1) {
                            chunk.positions[k] = le32(entry + k * 4);
                        }
                        chunk.data_end = region.data_length;
                        if (c + 1 < chunks) {
                            chunk.data_end = le32(entry + kCffIndexEntryBytes + kCffIndexData * 4);
                        }
                        if (chunk.positions[kCffIndexData] > chunk.data_end || chunk.data_end > region.data_length) {
                            return false;
                        }
                        if (!owned_array_append(loader.cff_chunks, loader.scratch, chunk)) {
                            return false;
                        }
                    }
                    index_entry += chunks;
                    if (!owned_array_append(loader.regions, loader.scratch, region)) {
                        return false;
                    }
                }
            } else if (table.kind != kTableLoca && table.kind != kTableInstr) {
                return false;
            }
            if (table.kind == kTableGlyf) {
                // One glyf table per font: its rebuild owns one loca.
                if (plan.glyf_table != kNoTable) {
                    return false;
                }
                plan.glyf_table = static_cast<std::uint32_t>(loader.tables.length);
            }
            if (!owned_array_append(loader.tables, loader.scratch, table)) {
                return false;
            }
        }
        // The glyf table's loca is a table this font plans as loca, so the
        // font job's loca writes land in bytes no other job writes (the plan's
        // tables are disjoint), never over a CFF table that chunk jobs
        // rebuild at the same time (A-gf-typography-01-F8).
        if (plan.glyf_table != kNoTable) {
            const TablePlan& glyf = loader.tables.data[plan.glyf_table];
            bool loca_planned = false;
            for (std::uint32_t t = 0; t < plan.table_count && !loca_planned; t += 1) {
                const TablePlan& table = loader.tables.data[plan.table_first + t];
                loca_planned = table.kind == kTableLoca && table.offset == glyf.loca_offset &&
                               table.length == glyf.loca_length;
            }
            if (!loca_planned) {
                return false;
            }
        }
        // A split hmtx needs the font's glyf and must be exactly the size its counts give.
        for (std::uint32_t t = 0; t < plan.table_count; t += 1) {
            const TablePlan& table = loader.tables.data[plan.table_first + t];
            if (table.kind != kTableHmtx) {
                continue;
            }
            if (plan.glyf_table == kNoTable) {
                return false;
            }
            const std::uint32_t glyph_count = loader.tables.data[plan.glyf_table].glyph_count;
            if (table.metric_count < 1 || table.metric_count > glyph_count ||
                table.length != 4 * table.metric_count + 2 * (glyph_count - table.metric_count)) {
                return false;
            }
        }
        plan.tail_gap = take_varint(meta);
        plan.tail_kind = take(meta);
        if (meta.failed || plan.tail_kind > 1 || previous_end + plan.tail_gap != font.size) {
            return false;
        }
    }
    return true;
}

// ---- rebuild ---------------------------------------------------------------

// Interleaves opcodes and pushed data back into length instruction bytes.
bool merge_instructions(std::uint8_t* out, std::uint32_t length, Cursor& code, Cursor& push) {
    std::uint32_t at = 0;
    while (at < length) {
        std::uint8_t pair[2];
        pair[0] = static_cast<std::uint8_t>(take(code));
        pair[1] = 0;
        const std::uint32_t remaining_after = length - at - 1;
        if ((pair[0] == 0x40 || pair[0] == 0x41) && remaining_after != 0) {
            pair[1] = static_cast<std::uint8_t>(take(code));
        }
        std::uint32_t count_byte = 0;
        const std::uint32_t pushed = instr_push_bytes(pair, remaining_after, count_byte);
        out[at] = pair[0];
        at += 1;
        if (count_byte != 0) {
            out[at] = pair[1];
            at += 1;
        }
        const std::uint8_t* data = take_span(push, pushed);
        if (data == nullptr) {
            return false;
        }
        std::memcpy(out + at, data, pushed);
        at += pushed;
    }
    return !code.failed;
}

struct FontCursors {
    Cursor streams[kStreamCount];
};

bool worker_points(Loader& loader, Worker& worker, std::uint32_t points) {
    if (worker.point_capacity >= points) {
        return true;
    }
    scratch_give(loader, worker.point_bytes, worker.point_capacity * 5);
    scratch_give(loader, worker.point_dx, worker.point_capacity * 2);
    scratch_give(loader, worker.point_dy, worker.point_capacity * 2);
    scratch_give(loader, worker.point_flags, worker.point_capacity);
    worker.point_capacity = static_cast<std::size_t>(points) + 1024;
    worker.point_bytes = static_cast<std::uint8_t*>(scratch_take(loader, worker.point_capacity * 5));
    worker.point_dx = static_cast<std::int16_t*>(scratch_take(loader, worker.point_capacity * 2));
    worker.point_dy = static_cast<std::int16_t*>(scratch_take(loader, worker.point_capacity * 2));
    worker.point_flags = static_cast<std::uint8_t*>(scratch_take(loader, worker.point_capacity));
    if (worker.point_bytes == nullptr || worker.point_dx == nullptr || worker.point_dy == nullptr ||
        worker.point_flags == nullptr) {
        worker.point_capacity = 0;
        return false;
    }
    return true;
}

bool rebuild_glyf(Loader& loader, Worker& worker, const FontEntry& font, const TablePlan& table, FontCursors& cursors) {
    std::uint8_t* glyf = font.out + table.offset;
    std::uint8_t* loca = font.out + table.loca_offset;
    const std::uint32_t body_limit = table.length - table.tail;
    Cursor& kinds = cursors.streams[kStreamGlyphKind];
    Cursor& wide = cursors.streams[kStreamGlyphWide];
    Cursor& bbox = cursors.streams[kStreamGlyphBbox];
    Cursor& flags = cursors.streams[kStreamGlyphFlags];
    Cursor& coords = cursors.streams[kStreamGlyphCoords];
    Cursor& instr_length = cursors.streams[kStreamInstrLength];
    std::uint32_t offset = 0;
    for (std::uint32_t g = 0; g <= table.glyph_count; g += 1) {
        if (table.loca_long != 0) {
            put_be32(loca + 4 * g, offset);
        } else {
            if ((offset & 1u) != 0 || offset > 0x1FFFEu) {
                return false;
            }
            put_be16(loca + 2 * g, offset >> 1);
        }
        if (g == table.glyph_count) {
            break;
        }
        const std::uint32_t kind = take(kinds);
        const std::uint32_t type = kind & kGlyphKindMask;
        const std::uint32_t room = body_limit - offset;
        std::uint8_t* out = glyf + offset;
        std::uint32_t length = 0;
        if (type == kGlyphEmpty) {
            continue;
        }
        if (type == kGlyphVerbatim) {
            const std::uint32_t slot = take_varint(wide);
            const std::uint8_t* bytes = take_span(cursors.streams[kStreamGlyphRaw], slot);
            if (bytes == nullptr || slot > room) {
                return false;
            }
            std::memcpy(out, bytes, slot);
            offset += slot;
            continue;
        }
        if (type == kGlyphComposite) {
            const std::uint8_t* box = take_span(bbox, 8);
            Cursor& records = cursors.streams[kStreamGlyphComposite];
            bool has_instructions = false;
            const std::size_t record_bytes =
                tt_composite_length(records.at, static_cast<std::size_t>(records.end - records.at), has_instructions);
            if (box == nullptr || record_bytes == 0 || room < 10 || record_bytes > room - 10) {
                return false;
            }
            put_be16(out, 0xFFFFu);
            std::memcpy(out + 2, box, 8);
            std::memcpy(out + 10, records.at, record_bytes);
            records.at += record_bytes;
            length = 10 + static_cast<std::uint32_t>(record_bytes);
            if (has_instructions) {
                const std::uint32_t instructions = take_varint(instr_length);
                if (instructions > 0xFFFFu || room - length < 2 || instructions > room - length - 2) {
                    return false;
                }
                put_be16(out + length, instructions);
                length += 2;
                if (!merge_instructions(out + length, instructions, cursors.streams[kStreamInstrCode],
                                        cursors.streams[kStreamInstrPush])) {
                    return false;
                }
                length += instructions;
            }
        } else {
            const std::uint8_t* box = nullptr;
            if ((kind & kGlyphExplicitBbox) != 0) {
                box = take_span(bbox, 8);
                if (box == nullptr) {
                    return false;
                }
            }
            const std::uint32_t contours = take_small(cursors.streams[kStreamGlyphContours], wide);
            if (contours > 0x7FFFu || room < 12 || contours * 2 > room - 12) {
                return false;
            }
            put_be16(out, contours);
            std::uint32_t points = 0;
            for (std::uint32_t c = 0; c < contours; c += 1) {
                const std::uint32_t contour_points = take_small(cursors.streams[kStreamGlyphPoints], wide);
                points += contour_points;
                if (contour_points == 0 || points > 65536) {
                    return false;
                }
                put_be16(out + 10 + 2 * c, points - 1);
            }
            length = 10 + 2 * contours;
            const std::uint32_t instructions = take_varint(instr_length);
            if (instructions > 0xFFFFu || instructions > room - length - 2) {
                return false;
            }
            put_be16(out + length, instructions);
            length += 2;
            if (!merge_instructions(out + length, instructions, cursors.streams[kStreamInstrCode],
                                    cursors.streams[kStreamInstrPush])) {
                return false;
            }
            length += instructions;
            if (!worker_points(loader, worker, points)) {
                return false;
            }
            const std::uint8_t* point_flags = take_span(flags, points);
            if (point_flags == nullptr) {
                return false;
            }
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::int32_t x_min = 0;
            std::int32_t y_min = 0;
            std::int32_t x_max = 0;
            std::int32_t y_max = 0;
            for (std::uint32_t k = 0; k < points; k += 1) {
                const std::uint32_t flag = point_flags[k];
                const std::uint8_t* payload = take_span(coords, triplet_bytes(flag));
                if (payload == nullptr) {
                    return false;
                }
                std::int32_t dx;
                std::int32_t dy;
                triplet_decode(flag, payload, dx, dy);
                if (dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767) {
                    return false;
                }
                worker.point_dx[k] = static_cast<std::int16_t>(dx);
                worker.point_dy[k] = static_cast<std::int16_t>(dy);
                x += dx;
                y += dy;
                if (k == 0) {
                    x_min = x;
                    x_max = x;
                    y_min = y;
                    y_max = y;
                } else {
                    x_min = x < x_min ? x : x_min;
                    x_max = x > x_max ? x : x_max;
                    y_min = y < y_min ? y : y_min;
                    y_max = y > y_max ? y : y_max;
                }
            }
            if (box != nullptr) {
                std::memcpy(out + 2, box, 8);
            } else {
                put_be16(out + 2, static_cast<std::uint32_t>(x_min) & 0xFFFFu);
                put_be16(out + 4, static_cast<std::uint32_t>(y_min) & 0xFFFFu);
                put_be16(out + 6, static_cast<std::uint32_t>(x_max) & 0xFFFFu);
                put_be16(out + 8, static_cast<std::uint32_t>(y_max) & 0xFFFFu);
            }
            // A glyph with no points writes no point bytes; the point buffers
            // may not exist yet then (worker_points allocates nothing for 0),
            // so nothing reaches them (A-gf-typography-01-F9).
            if (points != 0) {
                tt_flags_fill(worker.point_flags, point_flags, worker.point_dx, worker.point_dy, points,
                              (kind & kGlyphOverlapBit) != 0);
                const std::size_t point_bytes = tt_points_write(worker.point_bytes, worker.point_flags, worker.point_dx,
                                                                worker.point_dy, points, table.repeat);
                if (point_bytes > room - length) {
                    return false;
                }
                std::memcpy(out + length, worker.point_bytes, point_bytes);
                length += static_cast<std::uint32_t>(point_bytes);
            }
        }
        const std::uint32_t padded = (length + table.pad - 1) & ~(table.pad - 1);
        if (padded > room) {
            return false;
        }
        std::memset(out + length, 0, padded - length);
        offset += padded;
    }
    const std::uint8_t* tail = take_span(cursors.streams[kStreamGlyphRaw], table.tail);
    if (tail == nullptr || offset != body_limit) {
        return false;
    }
    std::memcpy(glyf + offset, tail, table.tail);
    return true;
}

// Writes a CFF table's bytes outside its charstring items: the residual
// pieces, each INDEX's count, offset size and last offset.
bool rebuild_cff_frame(const Loader& loader, const FontEntry& font, const TablePlan& table, FontCursors& cursors) {
    std::uint8_t* out = font.out + table.offset;
    Cursor& residual = cursors.streams[kStreamCffResidual];
    std::uint32_t at = 0;
    for (std::uint32_t r = 0; r < table.region_count; r += 1) {
        const RegionPlan& region = loader.regions.data[table.region_first + r];
        const std::uint8_t* piece = take_span(residual, region.start - at);
        if (piece == nullptr) {
            return false;
        }
        std::memcpy(out + at, piece, region.start - at);
        put_be16(out + region.start, region.count);
        if (region.count != 0) {
            out[region.start + 2] = static_cast<std::uint8_t>(region.off_size);
            std::uint8_t* last = out + region.start + 3 + region.count * region.off_size;
            const std::uint32_t value = region.data_length + 1;
            for (std::uint32_t b = 0; b < region.off_size; b += 1) {
                last[b] = static_cast<std::uint8_t>(value >> (8 * (region.off_size - 1 - b)));
            }
        }
        at = region.start + region.header_bytes + region.data_length;
    }
    const std::uint8_t* piece = take_span(residual, table.length - at);
    if (piece == nullptr) {
        return false;
    }
    std::memcpy(out + at, piece, table.length - at);
    return true;
}

bool rebuild_hmtx(const FontEntry& font, const TablePlan& table, const TablePlan& glyf, FontCursors& cursors) {
    std::uint8_t* out = font.out + table.offset;
    const std::uint8_t* glyf_bytes = font.out + glyf.offset;
    const std::uint8_t* loca = font.out + glyf.loca_offset;
    Cursor& advances = cursors.streams[kStreamHmtxAdvance];
    Cursor& bearings = cursors.streams[kStreamHmtxLsb];
    for (std::uint32_t g = 0; g < glyf.glyph_count; g += 1) {
        const std::uint32_t start = glyf.loca_long != 0 ? be32(loca + 4 * g) : 2 * be16(loca + 2 * g);
        const std::uint32_t end = glyf.loca_long != 0 ? be32(loca + 4 * g + 4) : 2 * be16(loca + 2 * g + 2);
        const std::uint8_t* relative = take_span(bearings, 2);
        if (relative == nullptr || end < start || end > glyf.length) {
            return false;
        }
        const std::uint32_t bearing =
            static_cast<std::uint32_t>(be16s(relative) + glyph_bearing_reference(glyf_bytes, start, end)) & 0xFFFFu;
        std::uint8_t* bearing_out = out + 4 * table.metric_count + 2 * (g - table.metric_count);
        if (g < table.metric_count) {
            const std::uint8_t* advance = take_span(advances, 2);
            if (advance == nullptr) {
                return false;
            }
            std::memcpy(out + 4 * g, advance, 2);
            bearing_out = out + 4 * g + 2;
        }
        put_be16(bearing_out, bearing);
    }
    return true;
}

// a: font.
void job_font(Loader& loader, const Job& job, Worker& worker) {
    const FontEntry& font = loader.fonts[job.a];
    const FontPlan& plan = loader.font_plans.data[job.a];
    FontCursors cursors;
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        cursors.streams[s] = stream_cursor(loader, s, font.starts[s]);
    }
    bool ok = true;
    if (plan.mode == 0) {
        const std::uint8_t* bytes = take_span(cursors.streams[kStreamRawOther], font.size);
        ok = bytes != nullptr;
        if (ok) {
            std::memcpy(font.out, bytes, font.size);
        }
    } else {
        const std::uint32_t directory_size = 12 + plan.table_count * 16;
        const std::uint8_t* directory = take_span(cursors.streams[kStreamDirectory], directory_size);
        ok = directory != nullptr;
        if (ok) {
            std::memcpy(font.out, directory, directory_size);
        }
        std::uint32_t tail_from = directory_size;
        for (std::uint32_t t = 0; t < plan.table_count && ok; t += 1) {
            const TablePlan& table = loader.tables.data[plan.table_first + t];
            std::uint8_t* out = font.out + table.offset;
            if (table.gap_kind == 0) {
                std::memset(out - table.gap, 0, table.gap);
            } else {
                const std::uint8_t* bytes = take_span(cursors.streams[kStreamGap], table.gap);
                ok = bytes != nullptr;
                if (ok) {
                    std::memcpy(out - table.gap, bytes, table.gap);
                }
            }
            tail_from = table.offset + table.length;
            if (!ok) {
                break;
            }
            if (table.kind == kTableRaw) {
                Cursor& raw = cursors.streams[table.raw_stream];
                const std::uint8_t* bytes = take_span(raw, table.length);
                ok = bytes != nullptr;
                if (ok) {
                    std::memcpy(out, bytes, table.length);
                    stride_unfilter(out, table.length, table.stride);
                    // Raw streams hold each table padded to four bytes.
                    const std::uint32_t padding = (4 - (table.length & 3u)) & 3u;
                    ok = take_span(raw, padding) != nullptr;
                }
            } else if (table.kind == kTableInstr) {
                ok = merge_instructions(out, table.length, cursors.streams[kStreamInstrCode],
                                        cursors.streams[kStreamInstrPush]);
            } else if (table.kind == kTableGlyf) {
                ok = rebuild_glyf(loader, worker, font, table, cursors);
            } else if (table.kind == kTableCff) {
                ok = rebuild_cff_frame(loader, font, table, cursors);
            }
        }
        // hmtx last: its bearings are relative to the glyph boxes just rebuilt.
        for (std::uint32_t t = 0; t < plan.table_count && ok; t += 1) {
            const TablePlan& table = loader.tables.data[plan.table_first + t];
            if (table.kind == kTableHmtx) {
                ok = rebuild_hmtx(font, table, loader.tables.data[plan.glyf_table], cursors);
            }
        }
        if (ok) {
            if (plan.tail_kind == 0) {
                std::memset(font.out + tail_from, 0, plan.tail_gap);
            } else {
                const std::uint8_t* bytes = take_span(cursors.streams[kStreamGap], plan.tail_gap);
                ok = bytes != nullptr;
                if (ok) {
                    std::memcpy(font.out + tail_from, bytes, plan.tail_gap);
                }
            }
        }
    }
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        if (cursors.streams[s].failed) {
            ok = false;
        }
    }
    if (!ok) {
        loader.failed.store(true);
    }
}

// a: index into Loader::cff_chunks.
void job_cff_chunk(Loader& loader, const Job& job, Worker& worker) {
    static_cast<void>(worker);
    const CffChunk& chunk = loader.cff_chunks.data[job.a];
    Cursor pairs = stream_cursor(loader, kStreamCffOps, chunk.positions[kCffIndexOps]);
    const std::uint32_t number_count = loader.number_decoder.count;
    const std::int16_t* number_at =
        loader.numbers + (chunk.positions[kCffIndexNumbers] < number_count ? chunk.positions[kCffIndexNumbers] : number_count);
    const std::int16_t* const number_end = loader.numbers + number_count;
    bool numbers_failed = chunk.positions[kCffIndexNumbers] > number_count;
    Cursor wide = stream_cursor(loader, kStreamCffNumWide, chunk.positions[kCffIndexNumWide]);
    Cursor mask = stream_cursor(loader, kStreamCffMask, chunk.positions[kCffIndexMask]);
    Cursor mask_lengths = stream_cursor(loader, kStreamCffMaskLength, chunk.positions[kCffIndexMaskLength]);
    Cursor misc = stream_cursor(loader, kStreamCffMisc, chunk.positions[kCffIndexMisc]);
    std::uint8_t* out = chunk.data + chunk.positions[kCffIndexData];
    std::uint8_t* const limit = chunk.data + chunk.data_end;
    bool ok = true;
    for (std::uint32_t item = 0; item < chunk.item_count && ok; item += 1) {
        const std::uint32_t start = static_cast<std::uint32_t>(out - chunk.data) + 1;
        std::uint8_t* entry = chunk.offsets + static_cast<std::size_t>(chunk.item_first + item) * chunk.off_size;
        for (std::uint32_t b = 0; b < chunk.off_size; b += 1) {
            entry[b] = static_cast<std::uint8_t>(start >> (8 * (chunk.off_size - 1 - b)));
        }
        std::int32_t mask_length = -1;
        for (;;) {
            const std::uint32_t symbol = take(pairs);
            std::uint32_t op;
            std::uint32_t count;
            if (symbol != kCffPairEscape) {
                op = loader.pair_op[symbol];
                count = loader.pair_count[symbol];
            } else {
                op = take(pairs);
                count = take(pairs);
            }
            if (pairs.failed || op >= kCffOpCount) {
                ok = false;
                break;
            }
            // Numbers: at most five bytes each.
            if (static_cast<std::size_t>(number_end - number_at) < count) {
                numbers_failed = true;
                ok = false;
                break;
            }
            const std::int16_t* values = number_at;
            number_at += count;
            bool fast = static_cast<std::size_t>(limit - out) >= static_cast<std::size_t>(count) * 5;
            if (fast) {
                // One- and two-byte numbers without a branch: two bytes are always written and the
                // cursor moves by the number's real width. Anything else ends the fast run.
                std::uint32_t j = 0;
                for (; j < count; j += 1) {
                    const std::uint32_t index = static_cast<std::uint32_t>(values[j] + kValueLimit);
                    if (index > 2 * kValueLimit) {
                        break;
                    }
                    const std::uint32_t token = loader.value_tokens[index];
                    out[0] = static_cast<std::uint8_t>(token);
                    out[1] = static_cast<std::uint8_t>(token >> 8);
                    out += 1 + ((token & 0xFFu) >= 247 ? 1 : 0);
                }
                if (j != count) {
                    // A longer number (or a damaged value): finish the group the careful way.
                    values += j;
                    count -= j;
                    fast = false;
                }
            }
            if (!fast) {
                if (static_cast<std::size_t>(limit - out) < static_cast<std::size_t>(count) * 5) {
                    // Near the end of the chunk's room: measure the group first.
                    std::size_t needed = 0;
                    for (std::uint32_t j = 0; j < count; j += 1) {
                        const std::int32_t value = values[j];
                        if (value >= -107 && value <= 107) {
                            needed += 1;
                        } else if (value >= -kValueLimit && value <= kValueLimit) {
                            needed += 2;
                        } else if (value == kValueThreeBytes) {
                            needed += 3;
                        } else {
                            needed += 5;
                        }
                    }
                    if (static_cast<std::size_t>(limit - out) < needed) {
                        ok = false;
                        break;
                    }
                }
                for (std::uint32_t j = 0; j < count; j += 1) {
                    const std::int32_t value = values[j];
                    if (value >= -107 && value <= 107) {
                        out[0] = static_cast<std::uint8_t>(value + 139);
                        out += 1;
                    } else if (value >= 108 && value <= kValueLimit) {
                        const std::uint32_t wide_value = static_cast<std::uint32_t>(value - 108);
                        out[0] = static_cast<std::uint8_t>(247 + (wide_value >> 8));
                        out[1] = static_cast<std::uint8_t>(wide_value);
                        out += 2;
                    } else if (value <= -108 && value >= -kValueLimit) {
                        const std::uint32_t wide_value = static_cast<std::uint32_t>(-value - 108);
                        out[0] = static_cast<std::uint8_t>(251 + (wide_value >> 8));
                        out[1] = static_cast<std::uint8_t>(wide_value);
                        out += 2;
                    } else if (value == kValueThreeBytes) {
                        const std::uint8_t* bytes = take_span(wide, 2);
                        if (bytes == nullptr) {
                            ok = false;
                            break;
                        }
                        out[0] = 28;
                        std::memcpy(out + 1, bytes, 2);
                        out += 3;
                    } else if (value == kValueFiveBytes) {
                        const std::uint8_t* bytes = take_span(wide, 4);
                        if (bytes == nullptr) {
                            ok = false;
                            break;
                        }
                        out[0] = 255;
                        std::memcpy(out + 1, bytes, 4);
                        out += 5;
                    } else {
                        // A correction left the range of one- and two-byte numbers: a damaged pack.
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok) {
                break;
            }
            if (op < 32) {
                std::uint32_t extra = 0;
                if (op == 12) {
                    extra = 1;
                } else if (op == 19 || op == 20) {
                    if (mask_length < 0) {
                        mask_length = static_cast<std::int32_t>(take(mask_lengths));
                    }
                    extra = static_cast<std::uint32_t>(mask_length);
                }
                if (static_cast<std::size_t>(limit - out) < 1 + extra) {
                    ok = false;
                    break;
                }
                out[0] = static_cast<std::uint8_t>(op);
                out += 1;
                if (op == 12) {
                    out[0] = static_cast<std::uint8_t>(take(misc));
                    out += 1;
                } else if (extra != 0) {
                    const std::uint8_t* bytes = take_span(mask, extra);
                    if (bytes == nullptr) {
                        ok = false;
                        break;
                    }
                    std::memcpy(out, bytes, extra);
                    out += extra;
                }
                continue;
            }
            if (op == kCffOpEnd) {
                break;
            }
            if (op == kCffOpRawTail) {
                const std::uint32_t length = take_varint(misc);
                const std::uint8_t* bytes = take_span(misc, length);
                if (bytes == nullptr || static_cast<std::size_t>(limit - out) < length) {
                    ok = false;
                    break;
                }
                std::memcpy(out, bytes, length);
                out += length;
                break;
            }
            // kCffOpMoreArgs: the group continues.
        }
    }
    if (!ok || out != limit || pairs.failed || numbers_failed || wide.failed || mask.failed ||
        mask_lengths.failed || misc.failed) {
        loader.failed.store(true);
    }
}

// ---- check -----------------------------------------------------------------

enum : std::uint32_t { kCheckPieceBytes = 1u << 20 };

// a: font, b: piece. CRC-32 of one piece of a rebuilt font.
void job_check_piece(Loader& loader, const Job& job, Worker& worker) {
    static_cast<void>(worker);
    const FontEntry& font = loader.fonts[job.a];
    const std::size_t start = static_cast<std::size_t>(job.b) * kCheckPieceBytes;
    std::size_t length = font.size - start;
    if (length > kCheckPieceBytes) {
        length = kCheckPieceBytes;
    }
    const std::span<const std::byte> piece(reinterpret_cast<const std::byte*>(font.out + start), length);
    loader.piece_crc.data[loader.piece_first.data[job.a] + job.b] = stx_zlib::crc32(piece);
}

void loader_release(Loader& loader) {
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        if (loader.opened[s]) {
            stream_close(loader.decoders[s]);
            loader.opened[s] = false;
        }
        scratch_give(loader, loader.decoded[s], loader.decoded_bytes[s]);
        loader.decoded[s] = nullptr;
    }
    if (loader.number_opened) {
        number_close(loader.number_decoder);
        loader.number_opened = false;
    }
    scratch_give(loader, loader.numbers, loader.numbers_bytes);
    loader.numbers = nullptr;
    for (std::uint32_t w = 0; w < loader.worker_count; w += 1) {
        Worker& worker = loader.workers[w];
        scratch_give(loader, worker.class_buffer, worker.class_capacity);
        scratch_give(loader, worker.literal_buffer, worker.literal_capacity);
        scratch_give(loader, worker.point_bytes, worker.point_capacity * 5);
        scratch_give(loader, worker.point_dx, worker.point_capacity * 2);
        scratch_give(loader, worker.point_dy, worker.point_capacity * 2);
        scratch_give(loader, worker.point_flags, worker.point_capacity);
    }
    owned_array_release(loader.font_plans, loader.scratch);
    owned_array_release(loader.tables, loader.scratch);
    owned_array_release(loader.regions, loader.scratch);
    owned_array_release(loader.cff_chunks, loader.scratch);
    owned_array_release(loader.coords_starts, loader.scratch);
    owned_array_release(loader.number_starts, loader.scratch);
    owned_array_release(loader.piece_first, loader.scratch);
    owned_array_release(loader.piece_crc, loader.scratch);
    if (loader.fonts != nullptr) {
        scratch_give(loader, loader.fonts, (static_cast<std::size_t>(loader.font_count) + 1) * sizeof(FontEntry));
        loader.fonts = nullptr;
    }
}

void group_reset(LoadedGroup& out, std::uint32_t group, const Allocator& allocator) {
    out.group = group;
    out.storage = nullptr;
    out.storage_bytes = 0;
    out.fonts = nullptr;
    out.font_count = 0;
    out.refusal = kGroupLoaded;
    out.scratch_peak = 0;
    out.storage_requested = 0;
    out.allocator = allocator;
}

// Records the first reason a load stopped.
void refuse(GroupRefusal& refusal, bool ok, GroupRefusal reason) {
    if (!ok && refusal == kGroupLoaded) {
        refusal = reason;
    }
}

// The fields of a fresh Loader; pack_load_group constructs it in memory from
// the caller's allocator.
void loader_reset(Loader& loader, const Allocator& caller, const Executor& executor, Worker* workers,
                  std::uint32_t worker_count) {
    loader.fonts = nullptr;
    loader.font_count = 0;
    for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
        loader.blobs[s] = nullptr;
        loader.blob_sizes[s] = 0;
        std::memset(&loader.decoders[s], 0, sizeof(StreamDecoder));
        loader.opened[s] = false;
        loader.decoded[s] = nullptr;
        loader.decoded_bytes[s] = 0;
        loader.chunks_left[s].store(0);
    }
    std::memset(&loader.number_decoder, 0, sizeof(NumberDecoder));
    loader.number_decoder.class_count = 1;
    loader.number_opened = false;
    loader.numbers = nullptr;
    loader.numbers_bytes = 0;
    loader.number_chunks_left.store(0);
    owned_array_reset(loader.font_plans);
    owned_array_reset(loader.tables);
    owned_array_reset(loader.regions);
    owned_array_reset(loader.cff_chunks);
    owned_array_reset(loader.coords_starts);
    owned_array_reset(loader.number_starts);
    owned_array_reset(loader.piece_first);
    owned_array_reset(loader.piece_crc);
    loader.failed.store(false);
    loader.no_memory.store(false);
    loader.scratch_now.store(0);
    loader.scratch_peak.store(0);
    loader.caller = caller;
    loader.scratch.allocate = counted_allocate;
    loader.scratch.release = counted_release;
    loader.scratch.context = &loader;
    loader.executor = &executor;
    loader.worker_count = worker_count;
    loader.workers = workers;
    loader.jobs = nullptr;
    loader.job_count = 0;
    loader.job_next.store(0);
    for (std::int32_t value = -kValueLimit; value <= kValueLimit; value += 1) {
        loader.value_tokens[value + kValueLimit] = static_cast<std::uint16_t>(token_from_value(value));
    }
}

// Rounds a font size up to the 8-byte slot it gets in the group's storage.
std::uint64_t storage_slot(std::uint32_t size) {
    return (static_cast<std::uint64_t>(size) + 7) & ~static_cast<std::uint64_t>(7);
}

}  // namespace

const char* refusal_name(GroupRefusal refusal) noexcept {
    switch (refusal) {
        case kGroupLoaded: return "loaded";
        case kGroupBadIndex: return "bad index";
        case kGroupBadClaims: return "bad claims";
        case kGroupBadStreams: return "bad streams";
        case kGroupBadCheck: return "bad check value";
        case kGroupNoMemory: return "no memory";
        default: return "unknown refusal";
    }
}

// ---- container ---------------------------------------------------------------

bool pack_open(std::span<const std::byte> pack_bytes, PackIndex& index) noexcept {
    index.pack = nullptr;
    index.pack_size = 0;
    index.group_count = 0;
    index.font_count = 0;
    const std::uint8_t* pack = reinterpret_cast<const std::uint8_t*>(pack_bytes.data());
    const std::size_t pack_size = pack_bytes.size();
    Reader reader = reader_make(pack, pack_size);
    const std::uint8_t* magic = get_span(reader, 4);
    if (reader.failed || std::memcmp(magic, "FPK2", 4) != 0) {
        return false;
    }
    const std::uint64_t group_count = get_varint(reader);
    if (reader.failed || group_count == 0 || group_count > kMaxGroups) {
        return false;
    }
    std::uint32_t font_total = 0;
    for (std::uint32_t g = 0; g < group_count; g += 1) {
        PackGroup& group = index.groups[g];
        const std::uint64_t name_length = get_varint(reader);
        if (reader.failed || name_length == 0 || name_length > kMaxNameBytes) {
            return false;
        }
        group.name_length = static_cast<std::uint32_t>(name_length);
        group.name = reinterpret_cast<const char*>(get_span(reader, group.name_length));
        const std::uint32_t flags = get_u8(reader);
        const std::uint64_t font_count = get_varint(reader);
        group.font_bytes = get_varint(reader);
        group.stream_bytes = get_varint(reader);
        const std::uint64_t body_size = get_varint(reader);
        // Order matters: once font_bytes is known to be at most
        // kMaxGroupFontBytes (2^28), the expansion product cannot overflow.
        // The ratio test is ceil(font_bytes / ratio) > body_size, which is
        // font_bytes > ratio * body_size without the multiplication.
        if (reader.failed || (flags & ~static_cast<std::uint32_t>(kGroupEager)) != 0 || font_count == 0 ||
            font_count > kMaxFonts - font_total || group.font_bytes > kMaxGroupFontBytes ||
            group.stream_bytes > kMaxStreamExpansion * group.font_bytes || body_size > pack_size ||
            (group.font_bytes + kMaxDecodeRatio - 1) / kMaxDecodeRatio > body_size) {
            return false;
        }
        group.eager = flags == kGroupEager;
        group.font_first = font_total;
        group.font_count = static_cast<std::uint32_t>(font_count);
        group.body = nullptr;
        group.body_size = static_cast<std::size_t>(body_size);
        // At most kMaxFonts sizes of at most kMaxFontBytes (2^26) each: the sum
        // stays below 2^34 and cannot overflow.
        std::uint64_t size_sum = 0;
        for (std::uint32_t f = 0; f < group.font_count; f += 1) {
            PackFont& font = index.fonts[font_total + f];
            const std::uint64_t font_name_length = get_varint(reader);
            if (reader.failed || font_name_length == 0 || font_name_length > kMaxNameBytes) {
                return false;
            }
            font.name_length = static_cast<std::uint32_t>(font_name_length);
            font.name = reinterpret_cast<const char*>(get_span(reader, font.name_length));
            const std::uint64_t size = get_varint(reader);
            const std::uint8_t* crc = get_span(reader, 4);
            // Test failed before reading the CRC: a failed span may hold fewer than 4 bytes.
            if (reader.failed || size == 0 || size > kMaxFontBytes) {
                return false;
            }
            font.size = static_cast<std::uint32_t>(size);
            font.crc = le32(crc);
            font.group = g;
            size_sum += size;
        }
        if (size_sum != group.font_bytes) {
            return false;
        }
        font_total += group.font_count;
    }
    for (std::uint32_t g = 0; g < group_count; g += 1) {
        index.groups[g].body = get_span(reader, index.groups[g].body_size);
        // The stream decoders read up to kStreamPadding bytes past a body.
        get_span(reader, kStreamPadding);
        if (reader.failed) {
            return false;
        }
    }
    if (reader.cursor != reader.end) {
        return false;
    }
    index.pack = pack;
    index.pack_size = pack_size;
    index.group_count = static_cast<std::uint32_t>(group_count);
    index.font_count = font_total;
    return true;
}

// ---- group -------------------------------------------------------------------

void group_release(LoadedGroup& group) noexcept {
    if (group.storage != nullptr) {
        group.allocator.release(group.allocator.context, group.storage, group.storage_requested);
    }
    if (group.fonts != nullptr) {
        group.allocator.release(group.allocator.context, group.fonts,
                                (static_cast<std::size_t>(group.font_count) + 1) * sizeof(LoadedFont));
    }
    group.storage = nullptr;
    group.storage_bytes = 0;
    group.storage_requested = 0;
    group.fonts = nullptr;
    group.font_count = 0;
}

bool pack_load_group(const PackIndex& index, std::uint32_t group_index, const Allocator& allocator,
                     const Executor& executor, LoadedGroup& out) noexcept {
    group_reset(out, group_index, allocator);

    // The index is trusted only as pack_open leaves it; what this function
    // relies on is rechecked, because it is cheap.
    if (group_index >= index.group_count || group_index >= kMaxGroups || index.font_count > kMaxFonts) {
        out.refusal = kGroupBadIndex;
        return false;
    }
    const PackGroup& group = index.groups[group_index];
    bool index_ok = group.body != nullptr && group.font_count != 0 && group.font_first <= index.font_count &&
                    group.font_count <= index.font_count - group.font_first && group.font_bytes <= kMaxGroupFontBytes &&
                    group.stream_bytes <= kMaxStreamExpansion * group.font_bytes;
    std::uint64_t size_sum = 0;
    for (std::uint32_t f = 0; f < group.font_count && index_ok; f += 1) {
        const std::uint32_t size = index.fonts[group.font_first + f].size;
        index_ok = size != 0 && size <= kMaxFontBytes;
        size_sum += size;
    }
    if (!index_ok || size_sum != group.font_bytes) {
        out.refusal = kGroupBadIndex;
        return false;
    }

    std::uint32_t worker_count = executor.worker_count;
    if (worker_count == 0) {
        worker_count = 1;
    }
    if (worker_count > kMaxWorkers) {
        worker_count = kMaxWorkers;
    }
    // Each worker's private scratch; the buffers in it come from Loader::scratch.
    Worker workers[kMaxWorkers];
    std::memset(workers, 0, sizeof(workers));

    void* loader_memory = allocator.allocate(allocator.context, sizeof(Loader));
    if (loader_memory == nullptr) {
        out.refusal = kGroupNoMemory;
        return false;
    }
    Loader* loader_owner = new (loader_memory) Loader;
    Loader& loader = *loader_owner;
    loader_reset(loader, allocator, executor, workers, worker_count);
    GroupRefusal refusal = kGroupLoaded;

    // Fonts: names, sizes and check values from the container; stream starts
    // from the body. font_count is at most kMaxFonts, so this allocation is
    // bounded by the format.
    loader.fonts = static_cast<FontEntry*>(
        scratch_take(loader, (static_cast<std::size_t>(group.font_count) + 1) * sizeof(FontEntry)));
    bool ok = loader.fonts != nullptr;
    refuse(refusal, ok, kGroupNoMemory);
    if (ok) {
        loader.font_count = group.font_count;
    }
    Reader reader = reader_make(group.body, group.body_size);
    for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
        const PackFont& source = index.fonts[group.font_first + f];
        FontEntry& font = loader.fonts[f];
        font.name = source.name;
        font.name_length = source.name_length;
        font.size = source.size;
        font.crc = source.crc;
        font.out = nullptr;
        for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
            const std::uint64_t start = get_varint(reader);
            // Font 0 starts every stream at 0 (the packer writes 0): class
            // derivation walks from the nearest font start at or before a
            // chunk, so a first font starting later would leave the classes
            // before it unwritten (A-gf-typography-01-F2).
            const bool misplaced = f == 0 ? start != 0 : start < loader.fonts[f - 1].starts[s];
            if (reader.failed || start > 0x7FFFFFFFu || misplaced) {
                ok = false;
            }
            font.starts[s] = static_cast<std::uint32_t>(start);
        }
    }
    for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
        const std::uint64_t size = get_varint(reader);
        ok = !reader.failed && size <= group.body_size;
        loader.blob_sizes[s] = static_cast<std::size_t>(size);
    }
    for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
        loader.blobs[s] = get_span(reader, loader.blob_sizes[s]);
        ok = !reader.failed;
    }
    // The body is exactly its parts; the container's padding follows it.
    ok = ok && reader.cursor == reader.end;

    // Bounded claims. Every stream begins with its decoded size (for the
    // number stream, its count of numbers, each held as 2 bytes). All of them
    // are read and their sum compared with the container's stream total
    // before any stream allocates tables or output. That total was itself
    // checked by pack_open against kMaxStreamExpansion times the group's
    // font bytes, which were checked against kMaxGroupFontBytes and
    // kMaxDecodeRatio times the body size. So no decoded size below can
    // exceed what the format allows for a body of this size.
    std::uint64_t declared[kStreamCount];
    std::memset(declared, 0, sizeof(declared));
    std::uint64_t declared_total = 0;
    for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
        Reader peek = reader_make(loader.blobs[s], loader.blob_sizes[s]);
        declared[s] = get_varint(peek);
        ok = !peek.failed && declared[s] <= 0x7FFFFFFFu;
        // Each term is below 2^32 and there are kStreamCount (33) of them: no overflow.
        declared_total += s == kStreamCffNumbers ? 2 * declared[s] : declared[s];
    }
    ok = ok && declared_total == group.stream_bytes;
    // Every font's stream starts must lie inside the streams.
    for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
        for (std::uint32_t s = 0; s < kStreamCount; s += 1) {
            if (loader.fonts[f].starts[s] > declared[s]) {
                ok = false;
            }
        }
    }
    refuse(refusal, ok, kGroupBadClaims);

    // One job list, refilled for every step.
    OwnedArray<Job> jobs;
    owned_array_reset(jobs);
    if (ok) {
        // Step 0: open every stream (header, tables, buffers).
        for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
            ok = job_add(loader, jobs, job_open_stream, s, 0);
        }
        if (ok) {
            run_jobs(loader, jobs);
        }
        jobs.length = 0;
        ok = !loader.failed.load();
    }
    // A stream must decode to the size it declared (the same varint; kept as a guard).
    for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
        if (loader.decoders[s].size != declared[s]) {
            ok = false;
        }
    }

    for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
        loader.chunks_left[s].store(loader.decoders[s].stored ? 1 : loader.decoders[s].chunk_count);
    }
    loader.number_chunks_left.store(loader.number_decoder.count != 0 ? loader.number_decoder.chunk_count : 0);

    // Step 1: leading streams.
    if (ok) {
        for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
            if (!is_leading_stream(s)) {
                continue;
            }
            const std::uint32_t chunks = loader.chunks_left[s].load();
            for (std::uint32_t c = 0; c < chunks && ok; c += 1) {
                ok = job_add(loader, jobs, job_decode_chunk, s, c);
            }
        }
        if (ok) {
            run_jobs(loader, jobs);
        }
        jobs.length = 0;
        ok = !loader.failed.load();
    }
    // What the other streams' contexts and the rebuild need from them.
    if (ok) {
        const StreamDecoder& table = loader.decoders[kStreamCffPairTable];
        const StreamDecoder& map = loader.decoders[kStreamCffNumMap];
        const bool has_cff = loader.decoders[kStreamCffOps].size != 0;
        if (has_cff) {
            ok = table.size == kCffPairCount * 2 && map.size == kCffNumFineCount;
            for (std::uint32_t k = 0; k < kCffPairCount && ok; k += 1) {
                loader.pair_op[k] = loader.decoded[kStreamCffPairTable][2 * k];
                loader.pair_count[k] = loader.decoded[kStreamCffPairTable][2 * k + 1];
                if (loader.pair_op[k] >= kCffOpCount) {
                    ok = false;
                    break;
                }
                loader.pair_base[k] = cff_number_context_base(loader.pair_op[k], loader.pair_count[k]);
            }
            // Number classes index the number stream's context map.
            const std::uint32_t classes = loader.number_decoder.class_count;
            for (std::uint32_t k = 0; k < kCffNumFineCount && ok; k += 1) {
                if (loader.decoded[kStreamCffNumMap][k] >= classes) {
                    ok = false;
                }
            }
        }
        ok = ok && loader.decoders[kStreamCffIndex].size % kCffIndexEntryBytes == 0;
        // The positions the class walks start from never decrease from one
        // entry to the next (the packer appends entries in stream order);
        // job_class_starts relies on it to stay one forward pass.
        const std::uint8_t* cff_entries = loader.decoded[kStreamCffIndex];
        const std::uint32_t entries = ok ? loader.decoders[kStreamCffIndex].size / kCffIndexEntryBytes : 0;
        for (std::uint32_t k = 1; k < entries && ok; k += 1) {
            const std::uint8_t* previous = cff_entries + (k - 1) * kCffIndexEntryBytes;
            const std::uint8_t* current = cff_entries + k * kCffIndexEntryBytes;
            ok = le32(current + kCffIndexOps * 4) >= le32(previous + kCffIndexOps * 4) &&
                 le32(current + kCffIndexNumbers * 4) >= le32(previous + kCffIndexNumbers * 4);
        }
    }
    if (ok) {
        ok = plan_fonts(loader);
    }
    refuse(refusal, ok || !loader.no_memory.load(), kGroupNoMemory);
    refuse(refusal, ok, kGroupBadStreams);

    // Where each chunk's class walk starts, for the two streams whose classes
    // are walked: one forward pass each, as two jobs.
    if (ok) {
        ok = job_add(loader, jobs, job_class_starts, 0, 0) && job_add(loader, jobs, job_class_starts, 1, 0);
        if (ok) {
            run_jobs(loader, jobs);
        }
        jobs.length = 0;
        ok = !loader.failed.load();
        refuse(refusal, ok || !loader.no_memory.load(), kGroupNoMemory);
        refuse(refusal, ok, kGroupBadStreams);
    }

    // Output storage, only now: plan_fonts has tied every font's size to its
    // decoded recipe (table placement and tail, or a verbatim font's extent),
    // so a size claim the streams do not bear out is refused before this
    // allocation. Each font is rounded up to 8 bytes: at most kMaxFonts times
    // 7 bytes above font_bytes, far inside 64 bits and below 2^31.
    std::uint64_t storage_bytes = 0;
    for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
        storage_bytes += storage_slot(loader.fonts[f].size);
    }
    std::uint8_t* storage = nullptr;
    std::size_t storage_requested = 0;
    if (ok) {
        // Zeroed so that no byte of a font is ever uninitialised, whatever the pack says.
        storage_requested = static_cast<std::size_t>(storage_bytes) + 8;
        out.storage_requested = storage_requested;
        storage = static_cast<std::uint8_t*>(allocator.allocate(allocator.context, storage_requested));
        ok = storage != nullptr;
        refuse(refusal, ok, kGroupNoMemory);
        if (ok) {
            std::memset(storage, 0, storage_requested);
        }
    }
    if (ok) {
        std::uint64_t at = 0;
        for (std::uint32_t f = 0; f < loader.font_count; f += 1) {
            loader.fonts[f].out = storage + at;
            at += storage_slot(loader.fonts[f].size);
        }
        for (std::size_t c = 0; c < loader.cff_chunks.length; c += 1) {
            CffChunk& chunk = loader.cff_chunks.data[c];
            chunk.offsets = loader.fonts[chunk.font].out + chunk.offsets_at;
            chunk.data = loader.fonts[chunk.font].out + chunk.data_at;
        }
    }

    // Step 2: every other stream. The longest stream's chunks go first so its
    // single-worker match copy overlaps the rest.
    if (ok) {
        const std::uint32_t number_chunks = loader.number_chunks_left.load();
        for (std::uint32_t c = 0; c < number_chunks && ok; c += 1) {
            ok = job_add(loader, jobs, job_number_chunk, c, 0);
        }
        for (std::uint32_t pass = 0; pass < 2 && ok; pass += 1) {
            for (std::uint32_t s = 0; s < kStreamCount && ok; s += 1) {
                if (is_leading_stream(s) || s == kStreamCffNumbers) {
                    continue;
                }
                // The glyph coordinates are the next longest stream.
                if ((s == kStreamGlyphCoords) != (pass == 0)) {
                    continue;
                }
                const std::uint32_t chunks = loader.chunks_left[s].load();
                for (std::uint32_t c = 0; c < chunks && ok; c += 1) {
                    ok = job_add(loader, jobs, job_decode_chunk, s, c);
                }
            }
        }
        if (ok) {
            run_jobs(loader, jobs);
        }
        jobs.length = 0;
        ok = !loader.failed.load();
    }

    // Step 3: rebuild.
    if (ok) {
        // CFF chunks first: they are the largest jobs.
        for (std::size_t c = 0; c < loader.cff_chunks.length && ok; c += 1) {
            ok = job_add(loader, jobs, job_cff_chunk, static_cast<std::uint32_t>(c), 0);
        }
        for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
            ok = job_add(loader, jobs, job_font, f, 0);
        }
        if (ok) {
            run_jobs(loader, jobs);
        }
        jobs.length = 0;
        ok = !loader.failed.load();
    }
    refuse(refusal, ok || !loader.no_memory.load(), kGroupNoMemory);
    refuse(refusal, ok, kGroupBadStreams);

    // Step 4: check. A font of size n has ceil(n / kCheckPieceBytes) pieces
    // (at least one, since n > 0); the pieces' values are joined in order.
    // At most kMaxFonts fonts of at most 64 pieces each: the counts fit 32 bits.
    if (ok) {
        ok = owned_array_resize_zeroed(loader.piece_first, loader.scratch, static_cast<std::size_t>(loader.font_count) + 1);
        std::uint32_t pieces = 0;
        for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
            loader.piece_first.data[f] = pieces;
            pieces += (loader.fonts[f].size + kCheckPieceBytes - 1) / kCheckPieceBytes;
        }
        if (ok) {
            loader.piece_first.data[loader.font_count] = pieces;
            ok = owned_array_resize_zeroed(loader.piece_crc, loader.scratch, pieces);
        }
        for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
            const std::uint32_t count = loader.piece_first.data[f + 1] - loader.piece_first.data[f];
            for (std::uint32_t p = 0; p < count && ok; p += 1) {
                ok = job_add(loader, jobs, job_check_piece, f, p);
            }
        }
        if (ok) {
            run_jobs(loader, jobs);
        }
        jobs.length = 0;
        refuse(refusal, ok || !loader.no_memory.load(), kGroupNoMemory);
        refuse(refusal, ok, kGroupBadStreams);
        for (std::uint32_t f = 0; f < loader.font_count && ok; f += 1) {
            const FontEntry& font = loader.fonts[f];
            const std::uint32_t first = loader.piece_first.data[f];
            const std::uint32_t count = loader.piece_first.data[f + 1] - first;
            std::uint32_t crc = loader.piece_crc.data[first];
            for (std::uint32_t p = 1; p < count; p += 1) {
                // Every piece but the last is full; the last holds the remainder.
                const std::uint32_t length =
                    p + 1 < count ? static_cast<std::uint32_t>(kCheckPieceBytes) : font.size - p * kCheckPieceBytes;
                crc = crc32_combine(crc, loader.piece_crc.data[first + p], length);
            }
            ok = crc == font.crc;
            refuse(refusal, ok, kGroupBadCheck);
        }
    }
    owned_array_release(jobs, loader.scratch);

    const std::size_t font_table_bytes = (static_cast<std::size_t>(loader.font_count) + 1) * sizeof(LoadedFont);
    if (ok) {
        out.fonts = static_cast<LoadedFont*>(allocator.allocate(allocator.context, font_table_bytes));
        ok = out.fonts != nullptr;
        refuse(refusal, ok, kGroupNoMemory);
    }
    if (ok) {
        for (std::uint32_t f = 0; f < loader.font_count; f += 1) {
            out.fonts[f].name = loader.fonts[f].name;
            out.fonts[f].name_length = loader.fonts[f].name_length;
            out.fonts[f].data = loader.fonts[f].out;
            out.fonts[f].size = loader.fonts[f].size;
        }
        out.font_count = loader.font_count;
        out.storage = storage;
        out.storage_bytes = static_cast<std::size_t>(storage_bytes);
    } else if (storage != nullptr) {
        allocator.release(allocator.context, storage, storage_requested);
    }
    out.refusal = refusal;
    loader_release(loader);
    out.scratch_peak = loader.scratch_peak.load();
    loader.~Loader();
    allocator.release(allocator.context, loader_memory, sizeof(Loader));
    return ok;
}

// ---- lazy set ----------------------------------------------------------------

bool set_open(std::span<const std::byte> pack, const Allocator& allocator, const Executor& executor,
              LoadedSet& set) noexcept {
    set.allocator = allocator;
    set.executor = executor;
    set.index_refused = false;
    set.open_refused_group = 0;
    set.open_refusal = kGroupLoaded;
    for (std::uint32_t g = 0; g < kMaxGroups; g += 1) {
        group_reset(set.groups[g], g, allocator);
        set.loaded[g] = false;
        set.refused[g] = false;
    }
    if (!pack_open(pack, set.index)) {
        set.index_refused = true;
        return false;
    }
    for (std::uint32_t g = 0; g < set.index.group_count; g += 1) {
        if (set.index.groups[g].eager && !set_need_group(set, g)) {
            set.open_refused_group = g;
            set.open_refusal = set.groups[g].refusal;
            set_release(set);
            return false;
        }
    }
    return true;
}

bool set_need_group(LoadedSet& set, std::uint32_t group) noexcept {
    if (group >= set.index.group_count) {
        return false;
    }
    if (set.loaded[group]) {
        return true;
    }
    if (set.refused[group]) {
        return false;
    }
    if (!pack_load_group(set.index, group, set.allocator, set.executor, set.groups[group])) {
        set.refused[group] = true;
        return false;
    }
    set.loaded[group] = true;
    return true;
}

const LoadedFont* set_find_font(LoadedSet& set, std::string_view name) noexcept {
    for (std::uint32_t f = 0; f < set.index.font_count; f += 1) {
        const PackFont& font = set.index.fonts[f];
        if (font.name_length != name.size() || std::memcmp(font.name, name.data(), name.size()) != 0) {
            continue;
        }
        if (!set_need_group(set, font.group)) {
            return nullptr;
        }
        return &set.groups[font.group].fonts[f - set.index.groups[font.group].font_first];
    }
    return nullptr;
}

void set_release(LoadedSet& set) noexcept {
    for (std::uint32_t g = 0; g < kMaxGroups; g += 1) {
        if (set.loaded[g]) {
            group_release(set.groups[g]);
        }
        set.loaded[g] = false;
        set.refused[g] = false;
    }
    set.index.group_count = 0;
    set.index.font_count = 0;
}

}  // namespace stx_fontpack
