// stx_fontpack: stream encoder. See detail/codec.hpp for the model.
//
// The encoder runs a cost-based forward parse per chunk, several times: each
// pass prices literals and sequence codes with the tables the previous pass
// produced. Literal contexts are clustered into at most kMaxTables tables.
#include "detail/codec.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace stx_fontpack {

namespace {

// Prices are in 1/64 bit.
enum : std::int32_t {
    kPriceOne = 64,
    kPriceAbsent = 18 * 64,
    kPriceInfinite = 0x7FFFFFFF
};

enum : std::uint32_t {
    kHashBits = 20,
    kLengthPriceCache = 1024
};

struct BitWriter {
    Bytes bytes;
    std::uint64_t bits;
    std::uint32_t count;
};

void bit_writer_reset(BitWriter& writer) {
    writer.bytes.clear();
    writer.bits = 0;
    writer.count = 0;
}

// width is at most 32.
void bit_put(BitWriter& writer, std::uint32_t value, std::uint32_t width) {
    if (width == 0) {
        return;
    }
    writer.bits |= static_cast<std::uint64_t>(value) << writer.count;
    writer.count += width;
    while (writer.count >= 8) {
        writer.bytes.push_back(static_cast<std::uint8_t>(writer.bits));
        writer.bits >>= 8;
        writer.count -= 8;
    }
}

void bit_flush(BitWriter& writer) {
    if (writer.count != 0) {
        writer.bytes.push_back(static_cast<std::uint8_t>(writer.bits));
        writer.bits = 0;
        writer.count = 0;
    }
}

void gamma_put(BitWriter& writer, std::uint32_t value) {
    const std::uint32_t top = 31u - static_cast<std::uint32_t>(std::countl_zero(value));
    bit_put(writer, 0, top);
    bit_put(writer, 1, 1);
    bit_put(writer, value - (1u << top), top);
}

void table_write(BitWriter& writer, const std::uint16_t* frequencies, std::uint32_t alphabet) {
    std::uint32_t symbol = 0;
    while (symbol < alphabet) {
        if (frequencies[symbol] == 0) {
            std::uint32_t run = 1;
            while (symbol + run < alphabet && frequencies[symbol + run] == 0) {
                run += 1;
            }
            bit_put(writer, 0, 1);
            gamma_put(writer, run);
            symbol += run;
        } else {
            bit_put(writer, 1, 1);
            gamma_put(writer, frequencies[symbol]);
            symbol += 1;
        }
    }
}

// Scales counts to frequencies summing to kProbScale, every used symbol in
// [1, kProbScale - 1]. All-zero counts give all-zero frequencies.
void normalise(const std::uint32_t* counts, std::uint32_t alphabet, std::uint16_t* frequencies) {
    std::uint64_t total = 0;
    std::uint32_t used = 0;
    std::uint32_t only = 0;
    for (std::uint32_t s = 0; s < alphabet; s += 1) {
        frequencies[s] = 0;
        total += counts[s];
        if (counts[s] != 0) {
            used += 1;
            only = s;
        }
    }
    if (used == 0) {
        return;
    }
    if (used == 1) {
        // The decode entry holds 12 bits of frequency, so a lone symbol shares with a dummy.
        frequencies[only] = static_cast<std::uint16_t>(kProbScale - 1);
        frequencies[only == 0 ? 1 : 0] = 1;
        return;
    }
    std::int32_t sum = 0;
    for (std::uint32_t s = 0; s < alphabet; s += 1) {
        if (counts[s] != 0) {
            std::uint64_t scaled = (static_cast<std::uint64_t>(counts[s]) * kProbScale + total / 2) / total;
            if (scaled == 0) {
                scaled = 1;
            }
            frequencies[s] = static_cast<std::uint16_t>(scaled);
            sum += static_cast<std::int32_t>(scaled);
        }
    }
    // Move single units where they cost least (or gain most) until the sum is exact.
    while (sum != static_cast<std::int32_t>(kProbScale)) {
        const bool grow = sum < static_cast<std::int32_t>(kProbScale);
        double best_score = 0.0;
        std::uint32_t best_symbol = alphabet;
        for (std::uint32_t s = 0; s < alphabet; s += 1) {
            const std::uint32_t f = frequencies[s];
            if (f == 0 || (!grow && f == 1)) {
                continue;
            }
            double score;
            if (grow) {
                score = static_cast<double>(counts[s]) * std::log2(static_cast<double>(f + 1) / f);
            } else {
                score = -static_cast<double>(counts[s]) * std::log2(static_cast<double>(f) / (f - 1));
            }
            if (best_symbol == alphabet || score > best_score) {
                best_score = score;
                best_symbol = s;
            }
        }
        if (grow) {
            frequencies[best_symbol] += 1;
            sum += 1;
        } else {
            frequencies[best_symbol] -= 1;
            sum -= 1;
        }
    }
}

std::int32_t price_of(std::uint32_t frequency) {
    if (frequency == 0) {
        return kPriceAbsent;
    }
    const double bits = std::log2(static_cast<double>(kProbScale) / frequency);
    return static_cast<std::int32_t>(bits * 64.0 + 0.5);
}

struct Stats {
    std::vector<std::uint32_t> literals;   // fine_count * 256
    std::uint32_t length_codes[kSeqAlphabet];
    std::uint32_t match_codes[kSeqAlphabet];
    std::uint32_t offset_codes[kSeqAlphabet];
};

void stats_reset(Stats& stats, std::uint32_t fine_count) {
    stats.literals.assign(static_cast<std::size_t>(fine_count) * 256, 0);
    std::memset(stats.length_codes, 0, sizeof(stats.length_codes));
    std::memset(stats.match_codes, 0, sizeof(stats.match_codes));
    std::memset(stats.offset_codes, 0, sizeof(stats.offset_codes));
}

void stats_add(Stats& into, const Stats& from) {
    for (std::size_t k = 0; k < into.literals.size(); k += 1) {
        into.literals[k] += from.literals[k];
    }
    for (std::uint32_t k = 0; k < kSeqAlphabet; k += 1) {
        into.length_codes[k] += from.length_codes[k];
        into.match_codes[k] += from.match_codes[k];
        into.offset_codes[k] += from.offset_codes[k];
    }
}

struct Model {
    std::uint32_t fine_count;
    std::uint32_t table_count;
    std::vector<std::uint8_t> map;               // fine context -> table
    std::vector<std::uint16_t> literal_frequencies;  // table_count * 256
    std::vector<std::int32_t> literal_prices;        // table_count * 256
    std::uint16_t sequence_frequencies[3][kSeqAlphabet];
    std::int32_t length_prices[kSeqAlphabet];    // code price plus extra bits
    std::int32_t match_prices[kSeqAlphabet];
    std::int32_t offset_prices[kSeqAlphabet];
};

double entropy_term(double count) {
    if (count <= 0.0) {
        return 0.0;
    }
    return count * std::log2(count);
}

// Bits to code a histogram with its own ideal table: N log N - sum h log h.
double histogram_bits(const std::uint32_t* histogram, std::uint64_t total) {
    double sum = entropy_term(static_cast<double>(total));
    for (std::uint32_t s = 0; s < 256; s += 1) {
        sum -= entropy_term(static_cast<double>(histogram[s]));
    }
    return sum;
}

double merged_bits(const std::uint32_t* a, const std::uint32_t* b, std::uint64_t total) {
    double sum = entropy_term(static_cast<double>(total));
    for (std::uint32_t s = 0; s < 256; s += 1) {
        sum -= entropy_term(static_cast<double>(a[s] + b[s]));
    }
    return sum;
}

// Estimated header bits for a table with this histogram.
double table_bits(const std::uint32_t* histogram) {
    std::uint32_t used = 0;
    for (std::uint32_t s = 0; s < 256; s += 1) {
        if (histogram[s] != 0) {
            used += 1;
        }
    }
    return 24.0 + 9.0 * used;
}

struct ByTotalDescending {
    const std::uint64_t* totals;
    bool operator()(std::uint32_t a, std::uint32_t b) const {
        if (totals[a] != totals[b]) {
            return totals[a] > totals[b];
        }
        return a < b;
    }
};

// Smoothed code lengths (bits) of every symbol under a cluster histogram.
void cluster_lengths(const std::uint32_t* histogram, std::uint64_t total, float* lengths) {
    const double denominator = static_cast<double>(total) + 16.0;
    for (std::uint32_t s = 0; s < 256; s += 1) {
        const double numerator = static_cast<double>(histogram[s]) + 16.0 / 256.0;
        lengths[s] = static_cast<float>(std::log2(denominator / numerator));
    }
}

// Clusters the fine contexts of stats into tables and fills the model's
// literal side. Deterministic.
void cluster_literals(const Stats& stats, std::uint32_t fine_count, std::uint32_t table_limit, Model& model) {
    std::vector<std::uint64_t> totals(fine_count, 0);
    std::vector<std::uint32_t> active;
    for (std::uint32_t f = 0; f < fine_count; f += 1) {
        const std::uint32_t* histogram = stats.literals.data() + static_cast<std::size_t>(f) * 256;
        std::uint64_t total = 0;
        for (std::uint32_t s = 0; s < 256; s += 1) {
            total += histogram[s];
        }
        totals[f] = total;
        if (total != 0) {
            active.push_back(f);
        }
    }
    model.fine_count = fine_count;
    model.map.assign(fine_count, 0);
    if (active.empty()) {
        model.table_count = 1;
        model.literal_frequencies.assign(256, 0);
        model.literal_prices.assign(256, kPriceAbsent);
        return;
    }
    ByTotalDescending order;
    order.totals = totals.data();
    std::sort(active.begin(), active.end(), order);

    // Seeds: the largest contexts. The rest join the seed that codes them cheapest.
    std::uint32_t cluster_count = static_cast<std::uint32_t>(std::min<std::size_t>(active.size(), table_limit));
    std::vector<std::uint32_t> cluster_histograms(static_cast<std::size_t>(cluster_count) * 256, 0);
    std::vector<std::uint64_t> cluster_totals(cluster_count, 0);
    std::vector<std::uint32_t> assignment(fine_count, 0);
    std::vector<float> lengths(static_cast<std::size_t>(cluster_count) * 256, 0.0f);
    for (std::uint32_t c = 0; c < cluster_count; c += 1) {
        const std::uint32_t f = active[c];
        std::memcpy(cluster_histograms.data() + static_cast<std::size_t>(c) * 256,
                    stats.literals.data() + static_cast<std::size_t>(f) * 256, 256 * sizeof(std::uint32_t));
        cluster_totals[c] = totals[f];
        assignment[f] = c;
        cluster_lengths(cluster_histograms.data() + static_cast<std::size_t>(c) * 256, cluster_totals[c],
                        lengths.data() + static_cast<std::size_t>(c) * 256);
    }
    std::vector<std::uint32_t> used_symbols;
    for (std::size_t k = cluster_count; k < active.size(); k += 1) {
        const std::uint32_t f = active[k];
        const std::uint32_t* histogram = stats.literals.data() + static_cast<std::size_t>(f) * 256;
        used_symbols.clear();
        for (std::uint32_t s = 0; s < 256; s += 1) {
            if (histogram[s] != 0) {
                used_symbols.push_back(s);
            }
        }
        float best_cost = 0.0f;
        std::uint32_t best_cluster = 0;
        for (std::uint32_t c = 0; c < cluster_count; c += 1) {
            const float* cluster_length = lengths.data() + static_cast<std::size_t>(c) * 256;
            float cost = 0.0f;
            for (std::size_t u = 0; u < used_symbols.size(); u += 1) {
                const std::uint32_t s = used_symbols[u];
                cost += static_cast<float>(histogram[s]) * cluster_length[s];
            }
            if (c == 0 || cost < best_cost) {
                best_cost = cost;
                best_cluster = c;
            }
        }
        assignment[f] = best_cluster;
    }
    for (std::size_t k = cluster_count; k < active.size(); k += 1) {
        const std::uint32_t f = active[k];
        const std::uint32_t c = assignment[f];
        const std::uint32_t* histogram = stats.literals.data() + static_cast<std::size_t>(f) * 256;
        std::uint32_t* cluster = cluster_histograms.data() + static_cast<std::size_t>(c) * 256;
        for (std::uint32_t s = 0; s < 256; s += 1) {
            cluster[s] += histogram[s];
        }
        cluster_totals[c] += totals[f];
    }

    // Agglomerative merging while a merge saves more header than it costs in data.
    std::vector<bool> alive(cluster_count, true);
    std::vector<double> own_bits(cluster_count, 0.0);
    std::vector<double> own_table(cluster_count, 0.0);
    for (std::uint32_t c = 0; c < cluster_count; c += 1) {
        own_bits[c] = histogram_bits(cluster_histograms.data() + static_cast<std::size_t>(c) * 256, cluster_totals[c]);
        own_table[c] = table_bits(cluster_histograms.data() + static_cast<std::size_t>(c) * 256);
    }
    // gain[a][b] for a < b: bits saved by merging (positive is good).
    std::vector<double> gain(static_cast<std::size_t>(cluster_count) * cluster_count, 0.0);
    std::vector<std::uint32_t> scratch(256, 0);
    for (std::uint32_t a = 0; a < cluster_count; a += 1) {
        for (std::uint32_t b = a + 1; b < cluster_count; b += 1) {
            const std::uint32_t* ha = cluster_histograms.data() + static_cast<std::size_t>(a) * 256;
            const std::uint32_t* hb = cluster_histograms.data() + static_cast<std::size_t>(b) * 256;
            const double together = merged_bits(ha, hb, cluster_totals[a] + cluster_totals[b]);
            for (std::uint32_t s = 0; s < 256; s += 1) {
                scratch[s] = ha[s] + hb[s];
            }
            const double table_saving = own_table[a] + own_table[b] - table_bits(scratch.data());
            gain[static_cast<std::size_t>(a) * cluster_count + b] = table_saving - (together - own_bits[a] - own_bits[b]);
        }
    }
    for (;;) {
        double best_gain = 0.0;
        std::uint32_t best_a = cluster_count;
        std::uint32_t best_b = cluster_count;
        for (std::uint32_t a = 0; a < cluster_count; a += 1) {
            if (!alive[a]) {
                continue;
            }
            for (std::uint32_t b = a + 1; b < cluster_count; b += 1) {
                if (!alive[b]) {
                    continue;
                }
                const double g = gain[static_cast<std::size_t>(a) * cluster_count + b];
                if (g > best_gain) {
                    best_gain = g;
                    best_a = a;
                    best_b = b;
                }
            }
        }
        if (best_a == cluster_count) {
            break;
        }
        std::uint32_t* ha = cluster_histograms.data() + static_cast<std::size_t>(best_a) * 256;
        const std::uint32_t* hb = cluster_histograms.data() + static_cast<std::size_t>(best_b) * 256;
        for (std::uint32_t s = 0; s < 256; s += 1) {
            ha[s] += hb[s];
        }
        cluster_totals[best_a] += cluster_totals[best_b];
        alive[best_b] = false;
        own_bits[best_a] = histogram_bits(ha, cluster_totals[best_a]);
        own_table[best_a] = table_bits(ha);
        for (std::uint32_t f = 0; f < fine_count; f += 1) {
            if (assignment[f] == best_b) {
                assignment[f] = best_a;
            }
        }
        for (std::uint32_t other = 0; other < cluster_count; other += 1) {
            if (!alive[other] || other == best_a) {
                continue;
            }
            const std::uint32_t* ho = cluster_histograms.data() + static_cast<std::size_t>(other) * 256;
            const double together = merged_bits(ha, ho, cluster_totals[best_a] + cluster_totals[other]);
            for (std::uint32_t s = 0; s < 256; s += 1) {
                scratch[s] = ha[s] + ho[s];
            }
            const double table_saving = own_table[best_a] + own_table[other] - table_bits(scratch.data());
            const double g = table_saving - (together - own_bits[best_a] - own_bits[other]);
            const std::uint32_t low = std::min(best_a, other);
            const std::uint32_t high = std::max(best_a, other);
            gain[static_cast<std::size_t>(low) * cluster_count + high] = g;
        }
    }

    // Two reassignment rounds against the merged clusters.
    for (std::uint32_t round = 0; round < 2; round += 1) {
        for (std::uint32_t c = 0; c < cluster_count; c += 1) {
            if (alive[c]) {
                cluster_lengths(cluster_histograms.data() + static_cast<std::size_t>(c) * 256, cluster_totals[c],
                                lengths.data() + static_cast<std::size_t>(c) * 256);
            }
        }
        for (std::size_t k = 0; k < active.size(); k += 1) {
            const std::uint32_t f = active[k];
            const std::uint32_t* histogram = stats.literals.data() + static_cast<std::size_t>(f) * 256;
            used_symbols.clear();
            for (std::uint32_t s = 0; s < 256; s += 1) {
                if (histogram[s] != 0) {
                    used_symbols.push_back(s);
                }
            }
            float best_cost = 0.0f;
            std::uint32_t best_cluster = cluster_count;
            for (std::uint32_t c = 0; c < cluster_count; c += 1) {
                if (!alive[c]) {
                    continue;
                }
                const float* cluster_length = lengths.data() + static_cast<std::size_t>(c) * 256;
                float cost = 0.0f;
                for (std::size_t u = 0; u < used_symbols.size(); u += 1) {
                    const std::uint32_t s = used_symbols[u];
                    cost += static_cast<float>(histogram[s]) * cluster_length[s];
                }
                if (best_cluster == cluster_count || cost < best_cost) {
                    best_cost = cost;
                    best_cluster = c;
                }
            }
            assignment[f] = best_cluster;
        }
        std::fill(cluster_histograms.begin(), cluster_histograms.end(), 0u);
        std::fill(cluster_totals.begin(), cluster_totals.end(), 0ull);
        for (std::size_t k = 0; k < active.size(); k += 1) {
            const std::uint32_t f = active[k];
            const std::uint32_t c = assignment[f];
            const std::uint32_t* histogram = stats.literals.data() + static_cast<std::size_t>(f) * 256;
            std::uint32_t* cluster = cluster_histograms.data() + static_cast<std::size_t>(c) * 256;
            for (std::uint32_t s = 0; s < 256; s += 1) {
                cluster[s] += histogram[s];
            }
            cluster_totals[c] += totals[f];
        }
        for (std::uint32_t c = 0; c < cluster_count; c += 1) {
            if (cluster_totals[c] == 0) {
                alive[c] = false;
            }
        }
    }

    // Renumber the surviving clusters and derive tables and prices.
    std::vector<std::uint32_t> renumber(cluster_count, 0);
    std::uint32_t table_count = 0;
    for (std::uint32_t c = 0; c < cluster_count; c += 1) {
        if (alive[c]) {
            renumber[c] = table_count;
            table_count += 1;
        }
    }
    model.table_count = table_count;
    model.literal_frequencies.assign(static_cast<std::size_t>(table_count) * 256, 0);
    model.literal_prices.assign(static_cast<std::size_t>(table_count) * 256, 0);
    for (std::uint32_t c = 0; c < cluster_count; c += 1) {
        if (!alive[c]) {
            continue;
        }
        const std::uint32_t t = renumber[c];
        std::uint16_t* frequencies = model.literal_frequencies.data() + static_cast<std::size_t>(t) * 256;
        normalise(cluster_histograms.data() + static_cast<std::size_t>(c) * 256, 256, frequencies);
        for (std::uint32_t s = 0; s < 256; s += 1) {
            model.literal_prices[static_cast<std::size_t>(t) * 256 + s] = price_of(frequencies[s]);
        }
    }
    // Unused fine contexts take the table of the nearest used context below
    // them so the stored map has long runs.
    std::uint32_t last_table = 0;
    for (std::uint32_t f = 0; f < fine_count; f += 1) {
        if (totals[f] != 0) {
            last_table = renumber[assignment[f]];
        }
        model.map[f] = static_cast<std::uint8_t>(last_table);
    }
}

void sequence_prices(const std::uint32_t* counts, std::uint16_t* frequencies, std::int32_t* prices,
                     std::uint32_t direct, std::uint32_t shift) {
    normalise(counts, kSeqAlphabet, frequencies);
    std::uint32_t total = 0;
    for (std::uint32_t c = 0; c < kSeqAlphabet; c += 1) {
        total += frequencies[c];
    }
    for (std::uint32_t c = 0; c < kSeqAlphabet; c += 1) {
        std::int32_t price = total == 0 ? 5 * kPriceOne : price_of(frequencies[c]);
        if (c >= shift) {
            price += static_cast<std::int32_t>(slot_extra_bits(c - shift, direct)) * kPriceOne;
        }
        prices[c] = price;
    }
}

void model_build(const Stats& stats, std::uint32_t fine_count, Model& model) {
    cluster_literals(stats, fine_count, kMaxTables, model);
    sequence_prices(stats.length_codes, model.sequence_frequencies[0], model.length_prices, kLitLenDirect, 0);
    sequence_prices(stats.match_codes, model.sequence_frequencies[1], model.match_prices, kMatchLenDirect, 1);
    sequence_prices(stats.offset_codes, model.sequence_frequencies[2], model.offset_prices, kOffsetDirect, 3);
}

struct CodedSequence {
    std::uint32_t literal_length;
    std::uint32_t match_length;
    std::uint32_t offset;
    std::uint32_t offset_code;   // 0..2 repeat index, 3 new offset
};

struct Node {
    std::int32_t price;
    std::uint32_t match_length;   // 0: arrived by a literal
    std::uint32_t offset;
    std::uint32_t offset_code;
    std::uint32_t literal_run;    // literals since the last match on the best path here
    std::uint32_t repeats[3];
};

// Everything one stream's parse passes share. The chain array is built once
// and only read by the chunk workers.
struct ParseShared {
    const std::uint8_t* data;
    std::size_t size;
    const std::uint8_t* classes;
    std::uint32_t quantiser;
    std::uint32_t stride;         // quantiser classes + 1
    std::uint32_t chain_depth;
    std::uint32_t nice_length;
    const std::uint32_t* chain;   // previous position + 1 with the same 3-byte hash, 0 for none
    const Model* model;
    std::int32_t match_length_prices[kLengthPriceCache];
};

inline std::uint32_t hash3(const std::uint8_t* p) {
    const std::uint32_t value = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                                (static_cast<std::uint32_t>(p[2]) << 16);
    return (value * 2654435761u) >> (32 - kHashBits);
}

void chain_build(const std::uint8_t* data, std::size_t size, std::vector<std::uint32_t>& chain) {
    chain.assign(size, 0);
    if (size < 3) {
        return;
    }
    std::vector<std::uint32_t> head(static_cast<std::size_t>(1) << kHashBits, 0);
    for (std::size_t position = 0; position + 3 <= size; position += 1) {
        const std::uint32_t h = hash3(data + position);
        chain[position] = head[h];
        head[h] = static_cast<std::uint32_t>(position + 1);
    }
}

inline std::uint32_t match_extent(const std::uint8_t* a, const std::uint8_t* b, std::uint32_t limit) {
    std::uint32_t length = 0;
    while (length + 8 <= limit) {
        std::uint64_t x;
        std::uint64_t y;
        std::memcpy(&x, a + length, 8);
        std::memcpy(&y, b + length, 8);
        if (x != y) {
            return length + (static_cast<std::uint32_t>(std::countr_zero(x ^ y)) >> 3);
        }
        length += 8;
    }
    while (length < limit && a[length] == b[length]) {
        length += 1;
    }
    return length;
}

inline std::int32_t match_length_price(const ParseShared& shared, std::uint32_t length) {
    if (length < kLengthPriceCache) {
        return shared.match_length_prices[length];
    }
    const SlotCode slot = slot_encode(length - kMinMatch, kMatchLenDirect);
    return (*shared.model).match_prices[slot.code + 1];
}

inline std::int32_t literal_length_price(const Model& model, std::uint32_t length) {
    const SlotCode slot = slot_encode(length, kLitLenDirect);
    return model.length_prices[slot.code];
}

inline std::uint32_t fine_context(const ParseShared& shared, std::size_t position, bool after_literal) {
    std::uint32_t fine = 0;
    if (shared.classes != nullptr) {
        fine = static_cast<std::uint32_t>(shared.classes[position]) * shared.stride;
    }
    if (after_literal && shared.quantiser != 0) {
        fine += 1 + quantise(shared.quantiser, shared.data[position - 1]);
    }
    return fine;
}

inline void relax_match(Node* nodes, std::uint32_t from, std::uint32_t length, std::uint32_t offset,
                        std::uint32_t offset_code, std::int32_t price) {
    Node& target = nodes[from + length];
    if (price >= target.price) {
        return;
    }
    const Node& source = nodes[from];
    target.price = price;
    target.match_length = length;
    target.offset = offset;
    target.offset_code = offset_code;
    target.literal_run = 0;
    if (offset_code == 0) {
        target.repeats[0] = source.repeats[0];
        target.repeats[1] = source.repeats[1];
        target.repeats[2] = source.repeats[2];
    } else if (offset_code == 1) {
        target.repeats[0] = source.repeats[1];
        target.repeats[1] = source.repeats[0];
        target.repeats[2] = source.repeats[2];
    } else if (offset_code == 2) {
        target.repeats[0] = source.repeats[2];
        target.repeats[1] = source.repeats[0];
        target.repeats[2] = source.repeats[1];
    } else {
        target.repeats[0] = offset;
        target.repeats[1] = source.repeats[0];
        target.repeats[2] = source.repeats[1];
    }
}

// Appends match-less sequences of 65535 literals until at most 65535 remain
// for the caller's own sequence.
void push_literal_overflow(std::vector<CodedSequence>& sequences, std::uint32_t run) {
    while (run > 0xFFFFu) {
        CodedSequence full;
        full.literal_length = 0xFFFFu;
        full.match_length = 0;
        full.offset = 0;
        full.offset_code = 0;
        sequences.push_back(full);
        run -= 0xFFFFu;
    }
}

// Parses data[start, start + length) and appends its sequences.
// nodes has length + 1 entries of caller scratch.
void parse_chunk(const ParseShared& shared, std::size_t start, std::uint32_t length, Node* nodes,
                 std::vector<CodedSequence>& sequences) {
    const Model& model = *shared.model;
    const std::uint8_t* data = shared.data;
    for (std::uint32_t k = 1; k <= length; k += 1) {
        nodes[k].price = kPriceInfinite;
    }
    nodes[0].price = literal_length_price(model, 0);
    nodes[0].match_length = 0;
    nodes[0].offset = 0;
    nodes[0].offset_code = 0;
    nodes[0].literal_run = 0;
    nodes[0].repeats[0] = 1;
    nodes[0].repeats[1] = 2;
    nodes[0].repeats[2] = 3;
    const std::int32_t zero_run_price = literal_length_price(model, 0);

    std::uint32_t skip_until = 0;
    for (std::uint32_t i = 0; i < length; i += 1) {
        if (i < skip_until || nodes[i].price == kPriceInfinite) {
            continue;
        }
        const Node here = nodes[i];
        const std::size_t position = start + i;

        // Literal step.
        {
            const std::uint32_t fine = fine_context(shared, position, here.literal_run != 0);
            const std::uint32_t table = model.map[fine];
            std::int32_t price = here.price + model.literal_prices[static_cast<std::size_t>(table) * 256 + data[position]];
            price += literal_length_price(model, here.literal_run + 1) - literal_length_price(model, here.literal_run);
            Node& next = nodes[i + 1];
            if (price < next.price) {
                next.price = price;
                next.match_length = 0;
                next.offset = 0;
                next.offset_code = 0;
                next.literal_run = here.literal_run + 1;
                next.repeats[0] = here.repeats[0];
                next.repeats[1] = here.repeats[1];
                next.repeats[2] = here.repeats[2];
            }
        }

        const std::uint32_t limit = std::min<std::uint32_t>(length - i, 0xFFFFu);
        if (limit < kMinMatch) {
            continue;
        }
        // The pending literal-length code is already in here.price; a match
        // closes it and opens the next sequence with an empty run.
        const std::int32_t base = here.price + zero_run_price;
        std::uint32_t longest = 0;

        // Repeat offsets.
        for (std::uint32_t r = 0; r < 3; r += 1) {
            const std::uint32_t offset = here.repeats[r];
            if (offset > position) {
                continue;
            }
            const std::uint32_t extent = match_extent(data + position, data + position - offset, limit);
            if (extent < kMinMatch) {
                continue;
            }
            const std::int32_t offset_price = model.offset_prices[r];
            std::uint32_t from_length = kMinMatch;
            if (extent >= shared.nice_length) {
                from_length = extent;
            }
            for (std::uint32_t l = from_length; l <= extent; l += 1) {
                relax_match(nodes, i, l, offset, r, base + offset_price + match_length_price(shared, l));
            }
            if (extent > longest) {
                longest = extent;
            }
        }

        // Hash chain: nearest first, so each longer candidate covers the lengths the nearer ones could not.
        if (limit >= 3 && longest < shared.nice_length) {
            std::uint32_t covered = 2;
            std::uint32_t link = shared.chain[position];
            for (std::uint32_t depth = 0; depth < shared.chain_depth && link != 0; depth += 1) {
                const std::size_t candidate = link - 1;
                link = shared.chain[candidate];
                if (covered < limit && data[candidate + covered] != data[position + covered]) {
                    continue;
                }
                const std::uint32_t extent = match_extent(data + position, data + candidate, limit);
                if (extent <= covered) {
                    continue;
                }
                const std::uint32_t offset = static_cast<std::uint32_t>(position - candidate);
                const SlotCode slot = slot_encode(offset - 1, kOffsetDirect);
                const std::int32_t offset_price = model.offset_prices[slot.code + 3];
                std::uint32_t from_length = covered + 1;
                if (extent >= shared.nice_length) {
                    from_length = extent;
                }
                for (std::uint32_t l = from_length; l <= extent; l += 1) {
                    relax_match(nodes, i, l, offset, 3, base + offset_price + match_length_price(shared, l));
                }
                covered = extent;
                if (extent > longest) {
                    longest = extent;
                }
                if (extent >= shared.nice_length || extent == limit) {
                    break;
                }
            }
        }
        if (longest >= shared.nice_length) {
            skip_until = i + longest;
        }
    }

    // Walk back from the end, then emit forwards.
    std::vector<CodedSequence> reversed;
    std::uint32_t at = length;
    while (at != 0) {
        const Node& node = nodes[at];
        if (node.match_length == 0) {
            at -= 1;
            continue;
        }
        CodedSequence sequence;
        sequence.literal_length = at - node.match_length;   // match start for now
        sequence.match_length = node.match_length;
        sequence.offset = node.offset;
        sequence.offset_code = node.offset_code;
        reversed.push_back(sequence);
        at -= node.match_length;
    }
    std::uint32_t done = 0;
    for (std::size_t k = reversed.size(); k != 0; k -= 1) {
        CodedSequence sequence = reversed[k - 1];
        const std::uint32_t match_start = sequence.literal_length;
        push_literal_overflow(sequences, match_start - done);
        sequence.literal_length = (match_start - done) % 0xFFFFu;
        if (sequence.literal_length == 0 && match_start != done) {
            // push_literal_overflow left exactly one full run for this sequence.
            sequence.literal_length = 0xFFFFu;
        }
        sequences.push_back(sequence);
        done = match_start + sequence.match_length;
    }
    if (done != length || sequences.empty()) {
        push_literal_overflow(sequences, length - done);
        CodedSequence last;
        last.literal_length = (length - done) % 0xFFFFu;
        if (last.literal_length == 0 && length != done) {
            last.literal_length = 0xFFFFu;
        }
        last.match_length = 0;
        last.offset = 0;
        last.offset_code = 0;
        sequences.push_back(last);
    }
}

void chunk_stats(const ParseShared& shared, std::size_t start, const std::vector<CodedSequence>& sequences, Stats& stats) {
    std::size_t position = start;
    for (std::size_t k = 0; k < sequences.size(); k += 1) {
        const CodedSequence& sequence = sequences[k];
        for (std::uint32_t l = 0; l < sequence.literal_length; l += 1) {
            const std::uint32_t fine = fine_context(shared, position, l != 0);
            stats.literals[static_cast<std::size_t>(fine) * 256 + shared.data[position]] += 1;
            position += 1;
        }
        stats.length_codes[slot_encode(sequence.literal_length, kLitLenDirect).code] += 1;
        if (sequence.match_length == 0) {
            stats.match_codes[0] += 1;
        } else {
            stats.match_codes[slot_encode(sequence.match_length - kMinMatch, kMatchLenDirect).code + 1] += 1;
            if (sequence.offset_code < 3) {
                stats.offset_codes[sequence.offset_code] += 1;
            } else {
                stats.offset_codes[slot_encode(sequence.offset - 1, kOffsetDirect).code + 3] += 1;
            }
            position += sequence.match_length;
        }
    }
}

// rANS encoder writing 16-bit units; symbols are fed last to first.
struct RansWriter {
    std::vector<std::uint16_t> units;   // in emission order, reversed on output
};

inline void rans_put(RansWriter& writer, std::uint32_t& state, std::uint32_t start, std::uint32_t frequency) {
    const std::uint64_t ceiling = static_cast<std::uint64_t>(frequency) << 20;   // (kRansLow >> kProbBits) << 16
    std::uint32_t x = state;
    while (x >= ceiling) {
        writer.units.push_back(static_cast<std::uint16_t>(x));
        x >>= 16;
    }
    state = ((x / frequency) << kProbBits) + (x % frequency) + start;
}

void rans_finish(RansWriter& writer, const std::uint32_t* states, std::uint32_t state_count, Bytes& out) {
    for (std::uint32_t k = state_count; k != 0; k -= 1) {
        writer.units.push_back(static_cast<std::uint16_t>(states[k - 1] >> 16));
        writer.units.push_back(static_cast<std::uint16_t>(states[k - 1]));
    }
    for (std::size_t k = writer.units.size(); k != 0; k -= 1) {
        const std::uint16_t unit = writer.units[k - 1];
        out.push_back(static_cast<std::uint8_t>(unit));
        out.push_back(static_cast<std::uint8_t>(unit >> 8));
    }
}

void cumulative(const std::uint16_t* frequencies, std::uint32_t alphabet, std::uint16_t* starts) {
    std::uint32_t sum = 0;
    for (std::uint32_t s = 0; s < alphabet; s += 1) {
        starts[s] = static_cast<std::uint16_t>(sum);
        sum += frequencies[s];
    }
}

struct ChunkPayload {
    Bytes sequence_bytes;
    Bytes extra_bytes;
    Bytes literal_bytes;
};

void chunk_emit(const ParseShared& shared, std::size_t start, const std::vector<CodedSequence>& sequences,
                const std::vector<std::uint16_t>& literal_starts, const std::uint16_t sequence_starts[3][kSeqAlphabet],
                ChunkPayload& payload) {
    const Model& model = *shared.model;
    // Extra bits, forwards.
    BitWriter bits;
    bit_writer_reset(bits);
    // Sequence codes in decode order, then fed to rANS backwards.
    std::vector<std::uint16_t> codes;   // table index << 8 | code
    // Literals in decode order: table << 8 | symbol.
    std::vector<std::uint16_t> literals;
    std::size_t position = start;
    for (std::size_t k = 0; k < sequences.size(); k += 1) {
        const CodedSequence& sequence = sequences[k];
        for (std::uint32_t l = 0; l < sequence.literal_length; l += 1) {
            const std::uint32_t fine = fine_context(shared, position, l != 0);
            literals.push_back(static_cast<std::uint16_t>((static_cast<std::uint32_t>(model.map[fine]) << 8) | shared.data[position]));
            position += 1;
        }
        const SlotCode length_slot = slot_encode(sequence.literal_length, kLitLenDirect);
        codes.push_back(static_cast<std::uint16_t>(length_slot.code));
        bit_put(bits, length_slot.extra_value, length_slot.extra_bits);
        if (sequence.match_length == 0) {
            codes.push_back(static_cast<std::uint16_t>(1u << 8));
            continue;
        }
        const SlotCode match_slot = slot_encode(sequence.match_length - kMinMatch, kMatchLenDirect);
        codes.push_back(static_cast<std::uint16_t>((1u << 8) | (match_slot.code + 1)));
        bit_put(bits, match_slot.extra_value, match_slot.extra_bits);
        if (sequence.offset_code < 3) {
            codes.push_back(static_cast<std::uint16_t>((2u << 8) | sequence.offset_code));
        } else {
            const SlotCode offset_slot = slot_encode(sequence.offset - 1, kOffsetDirect);
            codes.push_back(static_cast<std::uint16_t>((2u << 8) | (offset_slot.code + 3)));
            bit_put(bits, offset_slot.extra_value, offset_slot.extra_bits);
        }
        position += sequence.match_length;
    }
    bit_flush(bits);
    payload.extra_bytes = bits.bytes;

    {
        // One state per code kind (literal length, match length, offset), matching the decoder.
        RansWriter writer;
        std::uint32_t states[3];
        states[0] = kRansLow;
        states[1] = kRansLow;
        states[2] = kRansLow;
        for (std::size_t k = codes.size(); k != 0; k -= 1) {
            const std::uint32_t table = codes[k - 1] >> 8;
            const std::uint32_t code = codes[k - 1] & 0xFFu;
            rans_put(writer, states[table], sequence_starts[table][code], model.sequence_frequencies[table][code]);
        }
        payload.sequence_bytes.clear();
        rans_finish(writer, states, 3, payload.sequence_bytes);
    }
    {
        RansWriter writer;
        std::uint32_t states[kLiteralStates];
        for (std::uint32_t k = 0; k < kLiteralStates; k += 1) {
            states[k] = kRansLow;
        }
        for (std::size_t k = literals.size(); k != 0; k -= 1) {
            const std::uint32_t table = literals[k - 1] >> 8;
            const std::uint32_t symbol = literals[k - 1] & 0xFFu;
            const std::size_t index = static_cast<std::size_t>(table) * 256 + symbol;
            rans_put(writer, states[(k - 1) & (kLiteralStates - 1)], literal_starts[index], model.literal_frequencies[index]);
        }
        payload.literal_bytes.clear();
        rans_finish(writer, states, kLiteralStates, payload.literal_bytes);
    }
}

// One worker's share of a parse pass: chunks first, first + step, ...
struct ParseJob {
    const ParseShared* shared;
    std::uint32_t chunk_bytes;
    std::uint32_t chunk_count;
    std::uint32_t first;
    std::uint32_t step;
    std::vector<std::vector<CodedSequence> >* chunk_sequences;
    Stats* stats;
};

void parse_worker(ParseJob* job);

// WorkerTask::run over an array of ParseJob, one per worker.
void parse_task(void* context, std::uint32_t worker) {
    ParseJob* jobs = static_cast<ParseJob*>(context);
    parse_worker(&jobs[worker]);
}

void parse_worker(ParseJob* job) {
    const ParseShared& shared = *(*job).shared;
    std::vector<Node> nodes(static_cast<std::size_t>((*job).chunk_bytes) + 1);
    for (std::uint32_t c = (*job).first; c < (*job).chunk_count; c += (*job).step) {
        const std::size_t start = static_cast<std::size_t>(c) * (*job).chunk_bytes;
        const std::uint32_t length =
            static_cast<std::uint32_t>(std::min<std::size_t>((*job).chunk_bytes, shared.size - start));
        std::vector<CodedSequence>& sequences = (*(*job).chunk_sequences)[c];
        sequences.clear();
        parse_chunk(shared, start, length, nodes.data(), sequences);
        chunk_stats(shared, start, sequences, *(*job).stats);
    }
}

}  // namespace

EncodeOptions encode_options_default() {
    EncodeOptions options;
    options.classes = nullptr;
    options.class_count = 1;
    options.quantiser = 0;
    options.chunk_bytes = 1u << 17;
    options.chain_depth = 96;
    options.nice_length = 128;
    options.iterations = 2;
    options.executor = nullptr;
    return options;
}

std::uint32_t cluster_contexts(const std::uint8_t* data, const std::uint16_t* fine, std::size_t size,
                               std::uint32_t fine_count, std::vector<std::uint8_t>& map) {
    Stats stats;
    stats_reset(stats, fine_count);
    for (std::size_t position = 0; position < size; position += 1) {
        stats.literals[static_cast<std::size_t>(fine[position]) * 256 + data[position]] += 1;
    }
    Model model;
    cluster_literals(stats, fine_count, kMaxTables, model);
    map = model.map;
    return model.table_count;
}

void stream_encode(const std::uint8_t* data, std::size_t size, const EncodeOptions& options, Bytes& out) {
    put_varint(out, size);
    if (size == 0) {
        return;
    }
    const std::uint32_t stride = quantiser_classes(options.quantiser) + 1;
    const std::uint32_t fine_count = options.class_count * stride;
    const std::uint32_t chunk_bytes = options.chunk_bytes;
    const std::uint32_t chunk_count = static_cast<std::uint32_t>((size + chunk_bytes - 1) / chunk_bytes);

    std::vector<std::uint32_t> chain;
    chain_build(data, size, chain);

    ParseShared shared;
    shared.data = data;
    shared.size = size;
    shared.classes = options.classes;
    shared.quantiser = options.quantiser;
    shared.stride = stride;
    shared.chain_depth = options.chain_depth;
    shared.nice_length = options.nice_length;
    shared.chain = chain.data();

    // First model: every byte counted as a literal that follows a literal.
    Stats stats;
    stats_reset(stats, fine_count);
    for (std::size_t position = 0; position < size; position += 1) {
        const std::uint32_t fine = fine_context(shared, position, position != 0);
        stats.literals[static_cast<std::size_t>(fine) * 256 + data[position]] += 1;
    }
    Model model;
    model_build(stats, fine_count, model);

    const Executor serial = serial_executor();
    const Executor& executor = options.executor != nullptr ? *options.executor : serial;
    std::uint32_t worker_count = executor.worker_count;
    if (worker_count == 0) {
        worker_count = 1;
    }
    if (worker_count > kMaxWorkers) {
        worker_count = kMaxWorkers;
    }
    if (worker_count > chunk_count) {
        worker_count = chunk_count;
    }
    std::vector<std::vector<CodedSequence> > chunk_sequences(chunk_count);
    std::vector<Stats> worker_stats(worker_count);
    for (std::uint32_t pass = 0; pass <= options.iterations; pass += 1) {
        shared.model = &model;
        for (std::uint32_t l = 0; l < kLengthPriceCache; l += 1) {
            if (l < kMinMatch) {
                shared.match_length_prices[l] = kPriceInfinite / 2;
            } else {
                shared.match_length_prices[l] = model.match_prices[slot_encode(l - kMinMatch, kMatchLenDirect).code + 1];
            }
        }
        std::vector<ParseJob> jobs(worker_count);
        for (std::uint32_t w = 0; w < worker_count; w += 1) {
            stats_reset(worker_stats[w], fine_count);
            jobs[w].shared = &shared;
            jobs[w].chunk_bytes = chunk_bytes;
            jobs[w].chunk_count = chunk_count;
            jobs[w].first = w;
            jobs[w].step = worker_count;
            jobs[w].chunk_sequences = &chunk_sequences;
            jobs[w].stats = &worker_stats[w];
        }
        WorkerTask task;
        task.run = parse_task;
        task.context = jobs.data();
        task.worker_count = worker_count;
        executor.run_workers(executor.context, task);
        stats_reset(stats, fine_count);
        for (std::uint32_t w = 0; w < worker_count; w += 1) {
            stats_add(stats, worker_stats[w]);
        }
        Model next;
        model_build(stats, fine_count, next);
        model = next;
    }
    // model now holds tables built from exactly the sequences in chunk_sequences.
    shared.model = &model;

    Bytes coded;
    coded.push_back(1);
    put_varint(coded, options.class_count);
    coded.push_back(static_cast<std::uint8_t>(options.quantiser));
    coded.push_back(static_cast<std::uint8_t>(model.table_count));
    if (model.table_count != 1) {
        std::uint32_t f = 0;
        while (f < fine_count) {
            std::uint32_t run = 1;
            while (f + run < fine_count && model.map[f + run] == model.map[f]) {
                run += 1;
            }
            coded.push_back(model.map[f]);
            put_varint(coded, run);
            f += run;
        }
    }
    BitWriter table_bits_writer;
    bit_writer_reset(table_bits_writer);
    for (std::uint32_t t = 0; t < model.table_count; t += 1) {
        table_write(table_bits_writer, model.literal_frequencies.data() + static_cast<std::size_t>(t) * 256, 256);
    }
    for (std::uint32_t t = 0; t < 3; t += 1) {
        table_write(table_bits_writer, model.sequence_frequencies[t], kSeqAlphabet);
    }
    bit_flush(table_bits_writer);
    put_varint(coded, table_bits_writer.bytes.size());
    coded.insert(coded.end(), table_bits_writer.bytes.begin(), table_bits_writer.bytes.end());

    std::vector<std::uint16_t> literal_starts(static_cast<std::size_t>(model.table_count) * 256);
    for (std::uint32_t t = 0; t < model.table_count; t += 1) {
        cumulative(model.literal_frequencies.data() + static_cast<std::size_t>(t) * 256, 256,
                   literal_starts.data() + static_cast<std::size_t>(t) * 256);
    }
    std::uint16_t sequence_starts[3][kSeqAlphabet];
    for (std::uint32_t t = 0; t < 3; t += 1) {
        cumulative(model.sequence_frequencies[t], kSeqAlphabet, sequence_starts[t]);
    }
    std::vector<ChunkPayload> payloads(chunk_count);
    put_varint(coded, chunk_count);
    for (std::uint32_t c = 0; c < chunk_count; c += 1) {
        const std::size_t start = static_cast<std::size_t>(c) * chunk_bytes;
        const std::size_t length = std::min<std::size_t>(chunk_bytes, size - start);
        chunk_emit(shared, start, chunk_sequences[c], literal_starts, sequence_starts, payloads[c]);
        put_varint(coded, length);
        put_varint(coded, chunk_sequences[c].size());
        put_varint(coded, payloads[c].sequence_bytes.size());
        put_varint(coded, payloads[c].extra_bytes.size());
        put_varint(coded, payloads[c].literal_bytes.size());
    }
    for (std::uint32_t c = 0; c < chunk_count; c += 1) {
        coded.insert(coded.end(), payloads[c].sequence_bytes.begin(), payloads[c].sequence_bytes.end());
        coded.insert(coded.end(), payloads[c].extra_bytes.begin(), payloads[c].extra_bytes.end());
        coded.insert(coded.end(), payloads[c].literal_bytes.begin(), payloads[c].literal_bytes.end());
    }

    if (coded.size() >= size + 1) {
        out.push_back(0);
        out.insert(out.end(), data, data + size);
    } else {
        out.insert(out.end(), coded.begin(), coded.end());
    }
}

// ---- number streams ----------------------------------------------------------

namespace {

enum : std::uint32_t {
    kSeqLength = 0,
    kSeqKind = 1,
    kSeqExact = 2,
    kSeqFuzzy = 3,
    kSeqOffset = 4,
    kSeqTables = 5,
    kFuzzyLengthLimit = 4095,
    kSeedReach = 16,     // a fuzzy match may start this many numbers before an exact three-number seed
    kSeedDepth = 6      // candidates tried per such seed
};

enum : std::uint32_t { kResidualTableLimit = 24 };

enum : std::int32_t { kFuzzyGiveUp = 96 * 64 };   // 96 bits, in price units

struct NumberStats {
    Stats literals;   // only the literal histograms are used: class * 256 + first byte
    std::uint32_t low[kLowTables][256];
    Stats residuals;  // correction codes: (class * kResidualContexts + context) * 256 + code
    std::uint32_t codes[kSeqTables][kSeqAlphabet];
};

void number_stats_reset(NumberStats& stats, std::uint32_t class_count) {
    stats_reset(stats.literals, class_count);
    std::memset(stats.low, 0, sizeof(stats.low));
    stats_reset(stats.residuals, class_count * kResidualContexts);
    std::memset(stats.codes, 0, sizeof(stats.codes));
}

void number_stats_add(NumberStats& into, const NumberStats& from) {
    stats_add(into.literals, from.literals);
    for (std::uint32_t t = 0; t < kLowTables; t += 1) {
        for (std::uint32_t s = 0; s < 256; s += 1) {
            into.low[t][s] += from.low[t][s];
        }
    }
    stats_add(into.residuals, from.residuals);
    for (std::uint32_t t = 0; t < kSeqTables; t += 1) {
        for (std::uint32_t s = 0; s < kSeqAlphabet; s += 1) {
            into.codes[t][s] += from.codes[t][s];
        }
    }
}

struct NumberModel {
    Model literals;   // context map, literal tables and prices
    std::uint16_t low_frequencies[kLowTables][256];
    std::int32_t low_prices[kLowTables][256];
    Model residuals;  // correction context map, tables and prices (with extra bits)
    std::uint16_t code_frequencies[kSeqTables][kSeqAlphabet];
    std::int32_t code_prices[kSeqTables][kSeqAlphabet];   // with extra bits
};

// Frequencies and prices for one small table; an unused table prices every symbol at fallback.
void table_prices(const std::uint32_t* counts, std::uint32_t alphabet, std::uint16_t* frequencies, std::int32_t* prices,
                  std::int32_t fallback) {
    normalise(counts, alphabet, frequencies);
    std::uint32_t total = 0;
    for (std::uint32_t s = 0; s < alphabet; s += 1) {
        total += frequencies[s];
    }
    for (std::uint32_t s = 0; s < alphabet; s += 1) {
        prices[s] = total == 0 ? fallback : price_of(frequencies[s]);
    }
}

void number_model_build(const NumberStats& stats, std::uint32_t class_count, NumberModel& model) {
    cluster_literals(stats.literals, class_count, kMaxTables, model.literals);
    for (std::uint32_t t = 0; t < kLowTables; t += 1) {
        table_prices(stats.low[t], 256, model.low_frequencies[t], model.low_prices[t], 8 * kPriceOne);
    }
    // Few enough correction tables to stay in the first-level cache: each correction's table
    // depends on the correction before it, so a table miss is paid in series.
    cluster_literals(stats.residuals, class_count * kResidualContexts, kResidualTableLimit, model.residuals);
    bool residual_seen = false;
    for (std::size_t k = 0; k < stats.residuals.literals.size() && !residual_seen; k += 1) {
        residual_seen = stats.residuals.literals[k] != 0;
    }
    for (std::uint32_t t = 0; t < model.residuals.table_count; t += 1) {
        for (std::uint32_t s = 0; s < kResidualAlphabet; s += 1) {
            std::int32_t& price = model.residuals.literal_prices[static_cast<std::size_t>(t) * 256 + s];
            if (!residual_seen) {
                // Before any fuzzy match exists: a guess that lets the first pass try them.
                price = kPriceOne / 2;
                if (s != 0) {
                    const double magnitude = static_cast<double>((slot_decode(s, kResidualDirect, 0) + 1) / 2);
                    price = static_cast<std::int32_t>((3.0 + 1.5 * std::log2(magnitude + 1.0)) * 64.0);
                }
            } else {
                price += static_cast<std::int32_t>(slot_extra_bits(s, kResidualDirect)) * kPriceOne;
            }
        }
    }
    for (std::uint32_t t = 0; t < kSeqTables; t += 1) {
        table_prices(stats.codes[t], kSeqAlphabet, model.code_frequencies[t], model.code_prices[t],
                     t == kSeqKind ? 1 * kPriceOne : 5 * kPriceOne);
    }
    for (std::uint32_t c = 0; c < kSeqAlphabet; c += 1) {
        model.code_prices[kSeqLength][c] += static_cast<std::int32_t>(slot_extra_bits(c, kLitLenDirect)) * kPriceOne;
        model.code_prices[kSeqExact][c] += static_cast<std::int32_t>(slot_extra_bits(c, kMatchLenDirect)) * kPriceOne;
        model.code_prices[kSeqFuzzy][c] += static_cast<std::int32_t>(slot_extra_bits(c, kMatchLenDirect)) * kPriceOne;
        if (c >= 3) {
            model.code_prices[kSeqOffset][c] += static_cast<std::int32_t>(slot_extra_bits(c - 3, kOffsetDirect)) * kPriceOne;
        }
    }
}

struct NumberSequence {
    std::uint32_t literal_length;
    std::uint32_t match_length;
    std::uint32_t offset;
    std::uint32_t offset_code;   // 0..2 repeat index, 3 new offset
    std::uint32_t kind;          // 0 no match, 1 exact, 2 fuzzy
};

struct NumberNode {
    std::int32_t price;
    std::uint32_t match_length;   // 0: arrived by a literal
    std::uint32_t offset;
    std::uint32_t offset_code;
    std::uint32_t kind;
    std::uint32_t literal_run;
    std::uint32_t repeats[3];
};

struct NumberShared {
    const std::uint16_t* tokens;
    std::size_t count;
    const std::uint8_t* classes;
    std::uint32_t chain_depth;
    std::uint32_t fuzzy_depth;
    std::uint32_t nice_length;
    std::uint32_t max_bad_run;
    const std::uint32_t* chain;
    const NumberModel* model;
};

inline std::uint32_t hash_tokens(const std::uint16_t* p) {
    const std::uint32_t value = static_cast<std::uint32_t>(p[0]) * 2654435761u ^ static_cast<std::uint32_t>(p[1]) * 40503u ^
                                static_cast<std::uint32_t>(p[2]) * 2246822519u;
    return value >> (32 - kHashBits);
}

void number_chain_build(const std::uint16_t* tokens, std::size_t count, std::vector<std::uint32_t>& chain) {
    chain.assign(count, 0);
    if (count < 3) {
        return;
    }
    std::vector<std::uint32_t> head(static_cast<std::size_t>(1) << kHashBits, 0);
    for (std::size_t position = 0; position + 3 <= count; position += 1) {
        const std::uint32_t h = hash_tokens(tokens + position);
        chain[position] = head[h];
        head[h] = static_cast<std::uint32_t>(position + 1);
    }
}

inline std::uint32_t token_extent(const std::uint16_t* a, const std::uint16_t* b, std::uint32_t limit) {
    std::uint32_t length = 0;
    while (length < limit && a[length] == b[length]) {
        length += 1;
    }
    return length;
}

enum : std::uint32_t { kNoCorrection = 0xFFFFFFFFu };

// The zigzag correction that turns source into target, or kNoCorrection when
// the tokens differ and one of them has no value to correct.
inline std::uint32_t correction_symbol(std::uint32_t target, std::uint32_t source) {
    if (target == source) {
        return 0;
    }
    if (!token_has_value(target) || !token_has_value(source)) {
        return kNoCorrection;
    }
    return residual_symbol(token_value(target) - token_value(source));
}

inline std::int32_t number_literal_price(const NumberShared& shared, std::size_t position) {
    const NumberModel& model = *shared.model;
    const std::uint32_t token = shared.tokens[position];
    const std::uint32_t klass = shared.classes != nullptr ? shared.classes[position] : 0;
    const std::uint32_t table = model.literals.map[klass];
    std::int32_t price = model.literals.literal_prices[static_cast<std::size_t>(table) * 256 + (token & 0xFFu)];
    if (token_two_bytes(token)) {
        price += model.low_prices[(token & 0xFFu) - 247][token >> 8];
    }
    return price;
}

inline void number_relax(NumberNode* nodes, std::uint32_t from, std::uint32_t length, std::uint32_t offset,
                         std::uint32_t offset_code, std::uint32_t kind, std::int32_t price) {
    NumberNode& target = nodes[from + length];
    if (price >= target.price) {
        return;
    }
    const NumberNode& source = nodes[from];
    target.price = price;
    target.match_length = length;
    target.offset = offset;
    target.offset_code = offset_code;
    target.kind = kind;
    target.literal_run = 0;
    if (offset_code == 0) {
        target.repeats[0] = source.repeats[0];
        target.repeats[1] = source.repeats[1];
        target.repeats[2] = source.repeats[2];
    } else if (offset_code == 1) {
        target.repeats[0] = source.repeats[1];
        target.repeats[1] = source.repeats[0];
        target.repeats[2] = source.repeats[2];
    } else if (offset_code == 2) {
        target.repeats[0] = source.repeats[2];
        target.repeats[1] = source.repeats[0];
        target.repeats[2] = source.repeats[1];
    } else {
        target.repeats[0] = offset;
        target.repeats[1] = source.repeats[0];
        target.repeats[2] = source.repeats[1];
    }
}

inline std::int32_t number_length_price(const NumberModel& model, std::uint32_t table, std::uint32_t length) {
    return model.code_prices[table][slot_encode(length - kMinMatch, kMatchLenDirect).code];
}

// Tries fuzzy matches of every length from the source at offset. Returns the longest length tried.
std::uint32_t number_try_fuzzy(const NumberShared& shared, NumberNode* nodes, std::uint32_t i, std::size_t position,
                               std::uint32_t offset, std::uint32_t offset_code, std::int32_t base, std::uint32_t limit) {
    const NumberModel& model = *shared.model;
    const std::uint16_t* target = shared.tokens + position;
    const std::uint16_t* source = target - offset;
    if (limit > kFuzzyLengthLimit) {
        limit = kFuzzyLengthLimit;
    }
    std::int32_t cost = base + model.code_prices[kSeqKind][2];
    std::int32_t cost_at_longest = cost;
    std::uint32_t context = 0;
    std::uint32_t before_nonzero = 0;
    std::uint32_t bad_run = 0;
    std::uint32_t corrected = 0;
    std::uint32_t longest = 0;
    for (std::uint32_t j = 0; j < limit; j += 1) {
        const std::uint32_t symbol = correction_symbol(target[j], source[j]);
        if (symbol == kNoCorrection) {
            break;
        }
        if (symbol != 0) {
            bad_run += 1;
            corrected += 1;
            if (bad_run > shared.max_bad_run) {
                break;
            }
        } else {
            bad_run = 0;
        }
        const std::uint32_t code = slot_encode(symbol, kResidualDirect).code;
        const std::uint32_t klass = shared.classes != nullptr ? shared.classes[position + j] : 0;
        const std::uint32_t table = model.residuals.map[klass * kResidualContexts + context];
        cost += model.residuals.literal_prices[static_cast<std::size_t>(table) * 256 + code];
        context = residual_next_context(code, before_nonzero);
        before_nonzero = code != 0 ? 1u : 0u;
        // Only matches that correct something, and that end on an exact number, are worth a sequence.
        if (j + 1 >= kMinMatch && corrected != 0 && symbol == 0) {
            number_relax(nodes, i, j + 1, offset, offset_code, 2, cost + number_length_price(model, kSeqFuzzy, j + 1));
            longest = j + 1;
            cost_at_longest = cost;
        } else if (cost - cost_at_longest > kFuzzyGiveUp) {
            // Corrections since the last useful end already cost more than a fresh match would.
            break;
        }
    }
    return longest;
}

void number_parse_chunk(const NumberShared& shared, std::size_t start, std::uint32_t length, NumberNode* nodes,
                        std::vector<NumberSequence>& sequences) {
    const NumberModel& model = *shared.model;
    const std::uint16_t* tokens = shared.tokens;
    const std::int32_t* length_prices = model.code_prices[kSeqLength];
    const std::int32_t* offset_prices = model.code_prices[kSeqOffset];
    for (std::uint32_t k = 1; k <= length; k += 1) {
        nodes[k].price = kPriceInfinite;
    }
    const std::int32_t zero_run_price = length_prices[0];
    nodes[0].price = zero_run_price;
    nodes[0].match_length = 0;
    nodes[0].offset = 0;
    nodes[0].offset_code = 0;
    nodes[0].kind = 0;
    nodes[0].literal_run = 0;
    nodes[0].repeats[0] = 1;
    nodes[0].repeats[1] = 2;
    nodes[0].repeats[2] = 3;
    const std::int32_t exact_kind_price = model.code_prices[kSeqKind][1];

    std::uint32_t skip_until = 0;
    for (std::uint32_t i = 0; i < length; i += 1) {
        if (i < skip_until || nodes[i].price == kPriceInfinite) {
            continue;
        }
        const NumberNode here = nodes[i];
        const std::size_t position = start + i;
        {
            std::int32_t price = here.price + number_literal_price(shared, position);
            price += length_prices[slot_encode(here.literal_run + 1, kLitLenDirect).code] -
                     length_prices[slot_encode(here.literal_run, kLitLenDirect).code];
            NumberNode& next = nodes[i + 1];
            if (price < next.price) {
                next.price = price;
                next.match_length = 0;
                next.offset = 0;
                next.offset_code = 0;
                next.kind = 0;
                next.literal_run = here.literal_run + 1;
                next.repeats[0] = here.repeats[0];
                next.repeats[1] = here.repeats[1];
                next.repeats[2] = here.repeats[2];
            }
        }
        const std::uint32_t limit = std::min<std::uint32_t>(length - i, 0xFFFFu);
        if (limit < kMinMatch) {
            continue;
        }
        const std::int32_t base = here.price + zero_run_price;
        std::uint32_t longest = 0;

        for (std::uint32_t r = 0; r < 3; r += 1) {
            const std::uint32_t offset = here.repeats[r];
            if (offset > position) {
                continue;
            }
            const std::uint32_t extent = token_extent(tokens + position, tokens + position - offset, limit);
            if (extent >= kMinMatch) {
                std::uint32_t from_length = kMinMatch;
                if (extent >= shared.nice_length) {
                    from_length = extent;
                }
                for (std::uint32_t l = from_length; l <= extent; l += 1) {
                    number_relax(nodes, i, l, offset, r, 1,
                                 base + exact_kind_price + offset_prices[r] + number_length_price(model, kSeqExact, l));
                }
                if (extent > longest) {
                    longest = extent;
                }
            }
            if (extent < limit && shared.fuzzy_depth != 0) {
                const std::uint32_t fuzzy = number_try_fuzzy(shared, nodes, i, position, offset, r, base + offset_prices[r], limit);
                if (fuzzy > longest) {
                    longest = fuzzy;
                }
            }
        }

        if (limit >= 3 && longest < shared.nice_length) {
            std::uint32_t covered = 2;
            std::uint32_t link = shared.chain[position];
            for (std::uint32_t depth = 0; depth < shared.chain_depth && link != 0; depth += 1) {
                const std::size_t candidate = link - 1;
                link = shared.chain[candidate];
                const std::uint32_t offset = static_cast<std::uint32_t>(position - candidate);
                const std::int32_t offset_price = offset_prices[slot_encode(offset - 1, kOffsetDirect).code + 3];
                const bool try_exact = covered >= limit || tokens[candidate + covered] == tokens[position + covered];
                std::uint32_t extent = 0;
                if (try_exact) {
                    extent = token_extent(tokens + position, tokens + candidate, limit);
                    if (extent > covered) {
                        std::uint32_t from_length = covered + 1;
                        if (extent >= shared.nice_length) {
                            from_length = extent;
                        }
                        for (std::uint32_t l = from_length; l <= extent; l += 1) {
                            number_relax(nodes, i, l, offset, 3, 1,
                                         base + exact_kind_price + offset_price + number_length_price(model, kSeqExact, l));
                        }
                        covered = extent;
                        if (extent > longest) {
                            longest = extent;
                        }
                    }
                }
                if (depth < shared.fuzzy_depth && extent < limit) {
                    const std::uint32_t fuzzy = number_try_fuzzy(shared, nodes, i, position, offset, 3, base + offset_price, limit);
                    if (fuzzy > longest) {
                        longest = fuzzy;
                    }
                }
                if (extent >= shared.nice_length || extent == limit) {
                    break;
                }
            }
        }
        // Fuzzy matches that begin with corrections: aligned with an exact seed a little further on.
        if (shared.fuzzy_depth != 0 && longest < shared.nice_length) {
            for (std::uint32_t reach = 1; reach <= kSeedReach && i + reach + 3 <= length; reach += 1) {
                std::uint32_t link = shared.chain[position + reach];
                for (std::uint32_t depth = 0; depth < kSeedDepth && link != 0; depth += 1) {
                    const std::size_t candidate = link - 1;
                    link = shared.chain[candidate];
                    if (candidate < reach) {
                        continue;
                    }
                    const std::uint32_t offset = static_cast<std::uint32_t>(position + reach - candidate);
                    const std::int32_t offset_price = offset_prices[slot_encode(offset - 1, kOffsetDirect).code + 3];
                    const std::uint32_t fuzzy = number_try_fuzzy(shared, nodes, i, position, offset, 3, base + offset_price, limit);
                    if (fuzzy > longest) {
                        longest = fuzzy;
                    }
                }
            }
        }
        if (longest >= shared.nice_length) {
            skip_until = i + longest;
        }
    }

    std::vector<NumberSequence> reversed;
    std::uint32_t at = length;
    while (at != 0) {
        const NumberNode& node = nodes[at];
        if (node.match_length == 0) {
            at -= 1;
            continue;
        }
        NumberSequence sequence;
        sequence.literal_length = at - node.match_length;   // match start for now
        sequence.match_length = node.match_length;
        sequence.offset = node.offset;
        sequence.offset_code = node.offset_code;
        sequence.kind = node.kind;
        reversed.push_back(sequence);
        at -= node.match_length;
    }
    NumberSequence filler;
    filler.match_length = 0;
    filler.offset = 0;
    filler.offset_code = 0;
    filler.kind = 0;
    std::uint32_t done = 0;
    for (std::size_t k = reversed.size(); k != 0; k -= 1) {
        NumberSequence sequence = reversed[k - 1];
        std::uint32_t run = sequence.literal_length - done;
        while (run > 0xFFFFu) {
            filler.literal_length = 0xFFFFu;
            sequences.push_back(filler);
            run -= 0xFFFFu;
        }
        done = sequence.literal_length + sequence.match_length;
        sequence.literal_length = run;
        sequences.push_back(sequence);
    }
    std::uint32_t run = length - done;
    while (run > 0xFFFFu) {
        filler.literal_length = 0xFFFFu;
        sequences.push_back(filler);
        run -= 0xFFFFu;
    }
    if (run != 0 || sequences.empty()) {
        filler.literal_length = run;
        sequences.push_back(filler);
    }
}

// Everything a chunk contributes to the coded stream, in decode order.
struct NumberSymbols {
    std::vector<std::uint16_t> codes;       // table << 8 | code
    std::vector<std::uint16_t> literals;    // table << 8 | first byte
    std::vector<std::uint16_t> lows;        // table << 8 | second byte
    std::vector<std::uint16_t> residuals[kResidualLanes];   // per lane: table << 8 | code
    BitWriter residual_extra[kResidualLanes];
    BitWriter extra;
};

// Walks a chunk's sequences once, feeding the statistics and (when symbols is not null) the symbol lists.
void number_walk(const NumberShared& shared, std::size_t start, const std::vector<NumberSequence>& sequences,
                 NumberStats* stats, NumberSymbols* symbols) {
    const NumberModel& model = *shared.model;
    std::size_t position = start;
    std::uint32_t fuzzy_seen = 0;
    for (std::size_t k = 0; k < sequences.size(); k += 1) {
        const NumberSequence& sequence = sequences[k];
        for (std::uint32_t l = 0; l < sequence.literal_length; l += 1) {
            const std::uint32_t token = shared.tokens[position];
            const std::uint32_t klass = shared.classes != nullptr ? shared.classes[position] : 0;
            if (stats != nullptr) {
                (*stats).literals.literals[static_cast<std::size_t>(klass) * 256 + (token & 0xFFu)] += 1;
                if (token_two_bytes(token)) {
                    (*stats).low[(token & 0xFFu) - 247][token >> 8] += 1;
                }
            }
            if (symbols != nullptr) {
                (*symbols).literals.push_back(
                    static_cast<std::uint16_t>((static_cast<std::uint32_t>(model.literals.map[klass]) << 8) | (token & 0xFFu)));
                if (token_two_bytes(token)) {
                    (*symbols).lows.push_back(static_cast<std::uint16_t>((((token & 0xFFu) - 247) << 8) | (token >> 8)));
                }
            }
            position += 1;
        }
        const SlotCode length_slot = slot_encode(sequence.literal_length, kLitLenDirect);
        std::uint32_t offset_code = sequence.offset_code;
        SlotCode match_slot;
        SlotCode offset_slot;
        match_slot.code = 0;
        match_slot.extra_bits = 0;
        match_slot.extra_value = 0;
        offset_slot = match_slot;
        if (sequence.kind != 0) {
            match_slot = slot_encode(sequence.match_length - kMinMatch, kMatchLenDirect);
            if (sequence.offset_code == 3) {
                offset_slot = slot_encode(sequence.offset - 1, kOffsetDirect);
                offset_code = offset_slot.code + 3;
            }
        }
        const std::uint32_t length_table = sequence.kind == 2 ? kSeqFuzzy : kSeqExact;
        if (stats != nullptr) {
            (*stats).codes[kSeqLength][length_slot.code] += 1;
            (*stats).codes[kSeqKind][sequence.kind] += 1;
            if (sequence.kind != 0) {
                (*stats).codes[length_table][match_slot.code] += 1;
                (*stats).codes[kSeqOffset][offset_code] += 1;
            }
        }
        if (symbols != nullptr) {
            (*symbols).codes.push_back(static_cast<std::uint16_t>((kSeqLength << 8) | length_slot.code));
            bit_put((*symbols).extra, length_slot.extra_value, length_slot.extra_bits);
            (*symbols).codes.push_back(static_cast<std::uint16_t>((kSeqKind << 8) | sequence.kind));
            if (sequence.kind != 0) {
                (*symbols).codes.push_back(static_cast<std::uint16_t>((length_table << 8) | match_slot.code));
                bit_put((*symbols).extra, match_slot.extra_value, match_slot.extra_bits);
                (*symbols).codes.push_back(static_cast<std::uint16_t>((kSeqOffset << 8) | offset_code));
                bit_put((*symbols).extra, offset_slot.extra_value, offset_slot.extra_bits);
            }
        }
        if (sequence.kind == 2) {
            const std::uint32_t lane = fuzzy_seen % kResidualLanes;
            fuzzy_seen += 1;
            std::uint32_t context = 0;
            std::uint32_t before_nonzero = 0;
            for (std::uint32_t l = 0; l < sequence.match_length; l += 1) {
                const std::uint32_t symbol =
                    correction_symbol(shared.tokens[position + l], shared.tokens[position + l - sequence.offset]);
                const SlotCode slot = slot_encode(symbol, kResidualDirect);
                const std::uint32_t klass = shared.classes != nullptr ? shared.classes[position + l] : 0;
                const std::uint32_t fine = klass * kResidualContexts + context;
                if (stats != nullptr) {
                    (*stats).residuals.literals[static_cast<std::size_t>(fine) * 256 + slot.code] += 1;
                }
                if (symbols != nullptr) {
                    (*symbols).residuals[lane].push_back(
                        static_cast<std::uint16_t>((static_cast<std::uint32_t>(model.residuals.map[fine]) << 8) | slot.code));
                    bit_put((*symbols).residual_extra[lane], slot.extra_value, slot.extra_bits);
                }
                context = residual_next_context(slot.code, before_nonzero);
                before_nonzero = slot.code != 0 ? 1u : 0u;
            }
        }
        position += sequence.match_length;
    }
}

struct NumberJob {
    const NumberShared* shared;
    std::uint32_t chunk_numbers;
    std::uint32_t chunk_count;
    std::uint32_t first;
    std::uint32_t step;
    std::vector<std::vector<NumberSequence> >* chunk_sequences;
    NumberStats* stats;
};

void number_worker(NumberJob* job);

// WorkerTask::run over an array of NumberJob, one per worker.
void number_task(void* context, std::uint32_t worker) {
    NumberJob* jobs = static_cast<NumberJob*>(context);
    number_worker(&jobs[worker]);
}

void number_worker(NumberJob* job) {
    const NumberShared& shared = *(*job).shared;
    std::vector<NumberNode> nodes(static_cast<std::size_t>((*job).chunk_numbers) + 1);
    for (std::uint32_t c = (*job).first; c < (*job).chunk_count; c += (*job).step) {
        const std::size_t start = static_cast<std::size_t>(c) * (*job).chunk_numbers;
        const std::uint32_t length =
            static_cast<std::uint32_t>(std::min<std::size_t>((*job).chunk_numbers, shared.count - start));
        std::vector<NumberSequence>& sequences = (*(*job).chunk_sequences)[c];
        sequences.clear();
        number_parse_chunk(shared, start, length, nodes.data(), sequences);
        number_walk(shared, start, sequences, (*job).stats, nullptr);
    }
}

// Codes symbols (table << 8 | symbol, in decode order) with state_count interleaved states by symbol index.
void rans_code(const std::vector<std::uint16_t>& symbols, const std::uint16_t* frequencies, const std::uint16_t* starts,
               std::uint32_t alphabet, std::uint32_t state_count, Bytes& out) {
    RansWriter writer;
    std::uint32_t states[8];
    for (std::uint32_t k = 0; k < 8; k += 1) {
        states[k] = kRansLow;
    }
    for (std::size_t k = symbols.size(); k != 0; k -= 1) {
        const std::uint32_t table = symbols[k - 1] >> 8;
        const std::uint32_t symbol = symbols[k - 1] & 0xFFu;
        const std::size_t index = static_cast<std::size_t>(table) * alphabet + symbol;
        rans_put(writer, states[(k - 1) % state_count], starts[index], frequencies[index]);
    }
    out.clear();
    rans_finish(writer, states, state_count, out);
}

}  // namespace

NumberOptions number_options_default() {
    NumberOptions options;
    options.classes = nullptr;
    options.class_count = 1;
    options.chunk_numbers = 1u << 17;
    options.chain_depth = 96;
    options.fuzzy_depth = 24;
    options.nice_length = 128;
    options.max_bad_run = 0xFFFFu;
    options.iterations = 2;
    options.executor = nullptr;
    return options;
}

void number_encode(const std::uint16_t* tokens, std::size_t count, const NumberOptions& options, Bytes& out) {
    put_varint(out, count);
    if (count == 0) {
        return;
    }
    const std::uint32_t chunk_numbers = options.chunk_numbers;
    const std::uint32_t chunk_count = static_cast<std::uint32_t>((count + chunk_numbers - 1) / chunk_numbers);
    std::vector<std::uint32_t> chain;
    number_chain_build(tokens, count, chain);

    NumberShared shared;
    shared.tokens = tokens;
    shared.count = count;
    shared.classes = options.classes;
    shared.chain_depth = options.chain_depth;
    shared.fuzzy_depth = options.fuzzy_depth;
    shared.nice_length = options.nice_length;
    shared.max_bad_run = options.max_bad_run;
    shared.chain = chain.data();

    // First model: every number a literal.
    // Large tables: owned on the heap for the whole encode.
    std::unique_ptr<NumberStats> stats_owner(new NumberStats);
    NumberStats* stats = stats_owner.get();
    number_stats_reset(*stats, options.class_count);
    for (std::size_t position = 0; position < count; position += 1) {
        const std::uint32_t token = tokens[position];
        const std::uint32_t klass = options.classes != nullptr ? options.classes[position] : 0;
        (*stats).literals.literals[static_cast<std::size_t>(klass) * 256 + (token & 0xFFu)] += 1;
        if (token_two_bytes(token)) {
            (*stats).low[(token & 0xFFu) - 247][token >> 8] += 1;
        }
    }
    std::unique_ptr<NumberModel> model_owner(new NumberModel);
    NumberModel* model = model_owner.get();
    number_model_build(*stats, options.class_count, *model);

    const Executor serial = serial_executor();
    const Executor& executor = options.executor != nullptr ? *options.executor : serial;
    std::uint32_t worker_count = executor.worker_count;
    if (worker_count == 0) {
        worker_count = 1;
    }
    if (worker_count > kMaxWorkers) {
        worker_count = kMaxWorkers;
    }
    if (worker_count > chunk_count) {
        worker_count = chunk_count;
    }
    std::vector<std::vector<NumberSequence> > chunk_sequences(chunk_count);
    std::vector<NumberStats> worker_stats(worker_count);
    for (std::uint32_t pass = 0; pass <= options.iterations; pass += 1) {
        shared.model = model;
        std::vector<NumberJob> jobs(worker_count);
        for (std::uint32_t w = 0; w < worker_count; w += 1) {
            number_stats_reset(worker_stats[w], options.class_count);
            jobs[w].shared = &shared;
            jobs[w].chunk_numbers = chunk_numbers;
            jobs[w].chunk_count = chunk_count;
            jobs[w].first = w;
            jobs[w].step = worker_count;
            jobs[w].chunk_sequences = &chunk_sequences;
            jobs[w].stats = &worker_stats[w];
        }
        WorkerTask task;
        task.run = number_task;
        task.context = jobs.data();
        task.worker_count = worker_count;
        executor.run_workers(executor.context, task);
        number_stats_reset(*stats, options.class_count);
        for (std::uint32_t w = 0; w < worker_count; w += 1) {
            number_stats_add(*stats, worker_stats[w]);
        }
        std::unique_ptr<NumberModel> next(new NumberModel);
        number_model_build(*stats, options.class_count, *next);
        model_owner = std::move(next);
        model = model_owner.get();
    }
    shared.model = model;
    const NumberModel& final_model = *model;
    out.push_back(static_cast<std::uint8_t>(options.class_count));
    out.push_back(static_cast<std::uint8_t>(final_model.literals.table_count));
    if (final_model.literals.table_count != 1) {
        std::uint32_t f = 0;
        while (f < options.class_count) {
            std::uint32_t run = 1;
            while (f + run < options.class_count && final_model.literals.map[f + run] == final_model.literals.map[f]) {
                run += 1;
            }
            out.push_back(final_model.literals.map[f]);
            put_varint(out, run);
            f += run;
        }
    }
    // Correction contexts: (class, context) -> table.
    out.push_back(static_cast<std::uint8_t>(final_model.residuals.table_count));
    if (final_model.residuals.table_count != 1) {
        const std::uint32_t fine_count = options.class_count * kResidualContexts;
        std::uint32_t f = 0;
        while (f < fine_count) {
            std::uint32_t run = 1;
            while (f + run < fine_count && final_model.residuals.map[f + run] == final_model.residuals.map[f]) {
                run += 1;
            }
            out.push_back(final_model.residuals.map[f]);
            put_varint(out, run);
            f += run;
        }
    }
    BitWriter table_writer;
    bit_writer_reset(table_writer);
    for (std::uint32_t t = 0; t < final_model.literals.table_count; t += 1) {
        table_write(table_writer, final_model.literals.literal_frequencies.data() + static_cast<std::size_t>(t) * 256, 256);
    }
    for (std::uint32_t t = 0; t < kLowTables; t += 1) {
        table_write(table_writer, final_model.low_frequencies[t], 256);
    }
    for (std::uint32_t t = 0; t < final_model.residuals.table_count; t += 1) {
        table_write(table_writer, final_model.residuals.literal_frequencies.data() + static_cast<std::size_t>(t) * 256, 256);
    }
    for (std::uint32_t t = 0; t < kSeqTables; t += 1) {
        table_write(table_writer, final_model.code_frequencies[t], kSeqAlphabet);
    }
    bit_flush(table_writer);
    put_varint(out, table_writer.bytes.size());
    out.insert(out.end(), table_writer.bytes.begin(), table_writer.bytes.end());

    std::vector<std::uint16_t> literal_starts(static_cast<std::size_t>(final_model.literals.table_count) * 256);
    for (std::uint32_t t = 0; t < final_model.literals.table_count; t += 1) {
        cumulative(final_model.literals.literal_frequencies.data() + static_cast<std::size_t>(t) * 256, 256,
                   literal_starts.data() + static_cast<std::size_t>(t) * 256);
    }
    std::uint16_t low_starts[kLowTables][256];
    for (std::uint32_t t = 0; t < kLowTables; t += 1) {
        cumulative(final_model.low_frequencies[t], 256, low_starts[t]);
    }
    std::vector<std::uint16_t> residual_starts(static_cast<std::size_t>(final_model.residuals.table_count) * 256);
    for (std::uint32_t t = 0; t < final_model.residuals.table_count; t += 1) {
        cumulative(final_model.residuals.literal_frequencies.data() + static_cast<std::size_t>(t) * 256, 256,
                   residual_starts.data() + static_cast<std::size_t>(t) * 256);
    }
    std::uint16_t code_starts[kSeqTables][kSeqAlphabet];
    for (std::uint32_t t = 0; t < kSeqTables; t += 1) {
        cumulative(final_model.code_frequencies[t], kSeqAlphabet, code_starts[t]);
    }

    std::vector<Bytes> payloads(static_cast<std::size_t>(chunk_count) * kNumberPayloads);
    put_varint(out, chunk_count);
    for (std::uint32_t c = 0; c < chunk_count; c += 1) {
        const std::size_t start = static_cast<std::size_t>(c) * chunk_numbers;
        const std::size_t length = std::min<std::size_t>(chunk_numbers, count - start);
        NumberSymbols symbols;
        bit_writer_reset(symbols.extra);
        for (std::uint32_t lane = 0; lane < kResidualLanes; lane += 1) {
            bit_writer_reset(symbols.residual_extra[lane]);
        }
        number_walk(shared, start, chunk_sequences[c], nullptr, &symbols);
        bit_flush(symbols.extra);
        Bytes* chunk_payloads = payloads.data() + static_cast<std::size_t>(c) * kNumberPayloads;
        // Sequence codes: one state per table kind, exact and fuzzy lengths sharing one.
        {
            RansWriter writer;
            std::uint32_t states[4];
            states[0] = kRansLow;
            states[1] = kRansLow;
            states[2] = kRansLow;
            states[3] = kRansLow;
            for (std::size_t k = symbols.codes.size(); k != 0; k -= 1) {
                const std::uint32_t table = symbols.codes[k - 1] >> 8;
                const std::uint32_t code = symbols.codes[k - 1] & 0xFFu;
                std::uint32_t state = table;
                if (table == kSeqFuzzy) {
                    state = 2;
                } else if (table == kSeqOffset) {
                    state = 3;
                }
                rans_put(writer, states[state], code_starts[table][code], final_model.code_frequencies[table][code]);
            }
            rans_finish(writer, states, 4, chunk_payloads[0]);
        }
        chunk_payloads[1] = symbols.extra.bytes;
        rans_code(symbols.literals, final_model.literals.literal_frequencies.data(), literal_starts.data(), 256,
                  kLiteralStates, chunk_payloads[2]);
        rans_code(symbols.lows, final_model.low_frequencies[0], low_starts[0], 256, kNumberStates, chunk_payloads[3]);
        for (std::uint32_t lane = 0; lane < kResidualLanes; lane += 1) {
            rans_code(symbols.residuals[lane], final_model.residuals.literal_frequencies.data(), residual_starts.data(), 256,
                      1, chunk_payloads[4 + lane]);
            bit_flush(symbols.residual_extra[lane]);
            chunk_payloads[4 + kResidualLanes + lane] = symbols.residual_extra[lane].bytes;
        }
        put_varint(out, length);
        put_varint(out, chunk_sequences[c].size());
        for (std::uint32_t k = 0; k < kNumberPayloads; k += 1) {
            put_varint(out, chunk_payloads[k].size());
        }
    }
    for (std::size_t k = 0; k < payloads.size(); k += 1) {
        out.insert(out.end(), payloads[k].begin(), payloads[k].end());
    }
}

}  // namespace stx_fontpack
