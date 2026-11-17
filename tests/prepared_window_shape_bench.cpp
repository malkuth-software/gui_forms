#include "harfbuzz_font_engine.hpp"
#include "bounded_shape_workspace.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <streambuf>
#include <vector>
#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// This executable counts ordinary C++ new/new[] requested bytes. Native C
// library allocation and allocator bookkeeping are excluded. No measurement
// runs concurrently; result vectors/CSV formatting are outside the probe.
namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0U;
std::size_t bytes = 0U;
void* allocate(const std::size_t requested) {
    if (enabled) {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        if (calls == maximum || requested > maximum - bytes) throw std::bad_alloc();
        ++calls;
        bytes += requested;
    }
    const std::size_t allocation = requested == 0U ? 1U : requested;
    void* result = std::malloc(allocation);
    if (result == nullptr) throw std::bad_alloc();
    return result;
}
}
void* operator new(const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void* operator new[](const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void operator delete(void* address) noexcept { std::free(address); }
void operator delete[](void* address) noexcept { std::free(address); }
void operator delete(void* address, std::size_t) noexcept { std::free(address); }
void operator delete[](void* address, std::size_t) noexcept { std::free(address); }

namespace {
using namespace gui_forms;
using namespace gui_forms::render::text;
void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Exclusive creation is one filesystem operation. Never unlink on failure:
// a failed run's partial bytes remain evidence. FILE owns the acquired descriptor.
std::FILE* create_csv(const std::filesystem::path& path) {
#if defined(_WIN32)
    const int descriptor = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
        _S_IREAD | _S_IWRITE);
#else
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    require(descriptor >= 0, "exclusive CSV creation failed; existing paths are not replaced");
#if defined(_WIN32)
    std::FILE* stream = _fdopen(descriptor, "wb");
#else
    std::FILE* stream = fdopen(descriptor, "wb");
#endif
    if (stream == nullptr) {
#if defined(_WIN32)
        static_cast<void>(_close(descriptor));
#else
        static_cast<void>(close(descriptor));
#endif
        throw std::runtime_error("CSV stream acquisition failed; partial file retained");
    }
    return stream;
}

class CsvBuffer final : public std::streambuf {
public:
    explicit CsvBuffer(const std::filesystem::path& path) : file_(create_csv(path)) {}
    CsvBuffer(const CsvBuffer&) = delete;
    CsvBuffer& operator=(const CsvBuffer&) = delete;
    ~CsvBuffer() override {
        if (file_ != nullptr) static_cast<void>(std::fclose(file_));
    }
    bool close_file() noexcept {
        if (file_ == nullptr) return false;
        const int status = std::fclose(file_);
        file_ = nullptr;
        const bool success = status == 0;
        return success;
    }
protected:
    std::streamsize xsputn(const char* source, const std::streamsize count) override {
        if (file_ == nullptr || count <= 0) return 0;
        const std::size_t requested = static_cast<std::size_t>(count);
        const std::size_t written = std::fwrite(source, 1U, requested, file_);
        const std::streamsize result = static_cast<std::streamsize>(written);
        return result;
    }
    int_type overflow(const int_type value) override {
        const int_type end = traits_type::eof();
        if (file_ == nullptr) return end;
        if (traits_type::eq_int_type(value, end)) {
            const int_type accepted = traits_type::not_eof(value);
            return accepted;
        }
        const char character = traits_type::to_char_type(value);
        const unsigned char byte = static_cast<unsigned char>(character);
        const int status = std::fputc(byte, file_);
        if (status == EOF) return end;
        return value;
    }
    int sync() override {
        if (file_ == nullptr) return -1;
        const int result = std::fflush(file_);
        return result;
    }
private:
    std::FILE* file_{};
};

class CsvOutput final {
public:
    explicit CsvOutput(const std::filesystem::path& path) : buffer_(path), stream_(&buffer_) {}
    CsvOutput(const CsvOutput&) = delete;
    CsvOutput& operator=(const CsvOutput&) = delete;
    std::ostream& stream() noexcept { return stream_; }
    void finish() {
        stream_.flush();
        const bool flushed = static_cast<bool>(stream_);
        const bool closed = buffer_.close_file();
        require(flushed && closed, "CSV flush/close failed; partial raw file retained");
    }
private:
    CsvBuffer buffer_;
    std::ostream stream_;
};

class CsvFixture final {
public:
    CsvFixture() {
#if defined(_WIN32)
        const int process = _getpid();
#else
        const pid_t process = getpid();
#endif
        const std::string identity = std::to_string(process);
        const std::filesystem::path temporary = std::filesystem::temp_directory_path();
        for (std::size_t attempt = 0U; attempt < 1000U; ++attempt) {
            const std::string suffix = std::to_string(attempt);
            const std::string name = "gui-forms-shape-csv-" + identity + "-" + suffix;
            directory = temporary / name;
            const bool created = std::filesystem::create_directory(directory);
            if (created) {
                owned_ = true;
                output = directory / "output.csv";
                return;
            }
        }
        throw std::runtime_error("could not acquire disposable CSV fixture directory");
    }
    CsvFixture(const CsvFixture&) = delete;
    CsvFixture& operator=(const CsvFixture&) = delete;
    ~CsvFixture() {
        if (owned_) {
            std::error_code ignored{};
            std::filesystem::remove(output, ignored);
            std::filesystem::remove(directory, ignored);
        }
    }
    std::filesystem::path directory{};
    std::filesystem::path output{};
private:
    bool owned_{false};
};

void verify_csv_output() {
    CsvFixture fixture{};
    {
        CsvOutput first(fixture.output);
        std::ostream& output = first.stream();
        output << "raw,preserved\n";
        bool refused = false;
        try { CsvOutput duplicate(fixture.output); }
        catch (const std::runtime_error&) { refused = true; }
        require(refused, "existing CSV must be refused before replacement");
        first.finish();
    }
    {
        std::ifstream input(fixture.output, std::ios::binary);
        std::string line{};
        std::getline(input, line);
        require(line == "raw,preserved", "exclusive refusal changed original bytes");
    }
    const bool removed = std::filesystem::remove(fixture.output);
    require(removed, "disposable fixture reset failed");
    try {
        CsvOutput partial(fixture.output);
        std::ostream& output = partial.stream();
        output << "partial,evidence\n";
        throw std::runtime_error("simulated benchmark failure");
    } catch (const std::runtime_error&) {
        std::ifstream input(fixture.output, std::ios::binary);
        std::string line{};
        std::getline(input, line);
        require(line == "partial,evidence", "failure must retain partial raw output");
    }
}
void compare(const BoundedShapedText& expected, const BoundedShapedText& actual) {
    require(expected.width == actual.width && expected.height == actual.height &&
        expected.ascent == actual.ascent && expected.descent == actual.descent,
        "workspace metrics match bounded control");
    require(expected.missing_clusters == actual.missing_clusters &&
        expected.missing_primary_face == actual.missing_primary_face, "coverage matches bounded control");
    require(expected.run_count == actual.run_count && expected.glyph_count == actual.glyph_count,
        "active counts match bounded control");
    require(actual.run_capacity == actual.run_count && actual.glyph_capacity == actual.glyph_count,
        "output capacity equals actual counts");
    for (std::size_t index = 0U; index < actual.run_count; ++index) {
        const BoundedFontRun& left = expected.runs[index];
        const BoundedFontRun& right = actual.runs[index];
        require(left.face == right.face && left.source_range == right.source_range &&
            left.glyph_begin == right.glyph_begin && left.glyph_count == right.glyph_count,
            "run identity and source extents match");
    }
    for (std::size_t index = 0U; index < actual.glyph_count; ++index) {
        require(expected.glyphs[index] == actual.glyphs[index], "glyph geometry matches");
    }
    const std::size_t run_bytes = actual.run_count * sizeof(BoundedFontRun);
    const std::size_t glyph_bytes = actual.glyph_count * sizeof(ShapedGlyph);
    const std::size_t bytes = sizeof(BoundedShapedText) + run_bytes + glyph_bytes;
    require(actual.controlled_output_bytes == bytes, "exact retained output accounting");
}

struct Options final {
    std::filesystem::path fonts{};
    std::filesystem::path csv{};
    std::string scene{"preview-seven"};
    std::size_t warmup{10U};
    std::size_t samples{101U};
    double font_size{13.0};
    double scale{1.0};
    double width{190.0};
    bool measure{false};
};

std::size_t parse_count(const std::string_view text) {
    std::size_t count = 0U;
    const char* end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, count);
    require(parsed.ec == std::errc{} && parsed.ptr == end && count <= 10000U, "invalid bounded count");
    return count;
}

double parse_positive(const std::string_view text) {
    double value = 0.0;
    const char* end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    require(parsed.ec == std::errc{} && parsed.ptr == end && value > 0.0 && value <= 4096.0,
        "invalid positive profile value");
    return value;
}

Options parse_options(const int argc, char** argv) {
    Options result{};
    bool selected_mode = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view name(argv[index]);
        if (name == "--verify-only" || name == "--measure") {
            require(!selected_mode, "choose exactly one execution mode");
            selected_mode = true;
            result.measure = name == "--measure";
            continue;
        }
        require(index + 1 < argc, "option requires value");
        ++index;
        const std::string_view value(argv[index]);
        if (name == "--fonts") result.fonts = argv[index];
        else if (name == "--csv") result.csv = argv[index];
        else if (name == "--scene") result.scene = value;
        else if (name == "--samples") result.samples = parse_count(value);
        else if (name == "--warmup") result.warmup = parse_count(value);
        else if (name == "--font-size") result.font_size = parse_positive(value);
        else if (name == "--scale") result.scale = parse_positive(value);
        else if (name == "--width") result.width = parse_positive(value);
        else throw std::invalid_argument("unknown benchmark option");
    }
    require(selected_mode && !result.fonts.empty(), "--fonts and --verify-only or --measure required");
    require(result.scene == "preview-seven" || result.scene == "window-512", "unknown scene");
    require(!result.measure || (!result.csv.empty() && result.samples >= 100U), "measurement requires CSV and at least 100 samples");
    return result;
}

class PlatformClock final {
public:
    PlatformClock() {
#if defined(_WIN32)
        LARGE_INTEGER frequency{};
        const BOOL valid = QueryPerformanceFrequency(&frequency);
        require(valid != 0 && frequency.QuadPart > 0, "QPC frequency unavailable");
        frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
#endif
    }
    std::uint64_t read() const {
#if defined(_WIN32)
        LARGE_INTEGER counter{};
        const BOOL valid = QueryPerformanceCounter(&counter);
        require(valid != 0 && counter.QuadPart >= 0, "QPC read failed");
        const std::uint64_t result = static_cast<std::uint64_t>(counter.QuadPart);
#else
        timespec counter{};
        const int status = clock_gettime(CLOCK_MONOTONIC, &counter);
        require(status == 0 && counter.tv_sec >= 0 && counter.tv_nsec >= 0, "monotonic clock unavailable");
        const std::uint64_t seconds = static_cast<std::uint64_t>(counter.tv_sec);
        const std::uint64_t fraction = static_cast<std::uint64_t>(counter.tv_nsec);
        const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        require(seconds <= (maximum - fraction) / 1000000000U, "clock overflow");
        const std::uint64_t result = seconds * 1000000000U + fraction;
#endif
        return result;
    }
    double microseconds(const std::uint64_t begin, const std::uint64_t end) const {
        require(end >= begin, "clock moved backwards");
        const double ticks = static_cast<double>(end - begin);
        const double frequency = static_cast<double>(frequency_);
        const double result = ticks * 1000000.0 / frequency;
        return result;
    }
    void calibrate(std::ostream& output) const {
        std::uint64_t previous = read();
        std::uint64_t minimum = std::numeric_limits<std::uint64_t>::max();
        std::size_t positive = 0U;
        for (std::size_t index = 0U; index < 10000U; ++index) {
            const std::uint64_t now = read();
            require(now >= previous, "nonmonotonic calibration");
            const std::uint64_t difference = now - previous;
            if (difference != 0U) {
                minimum = std::min(minimum, difference);
                ++positive;
            }
            previous = now;
        }
        require(positive != 0U, "clock calibration observed no positive intervals");
        const double quantum = microseconds(0U, minimum);
        output << "clock_frequency=" << frequency_ << ",positive_intervals=" << positive
               << ",minimum_observed_us=" << quantum << '\n';
    }
private:
    std::uint64_t frequency_{1000000000U};
};

using FontBank = std::array<std::shared_ptr<const std::vector<std::byte>>, 4U>;
FontBank load_fonts(const std::filesystem::path& directory) {
    const std::array<const char*, 4U> names{"Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    FontBank bank{};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        const std::filesystem::path path = directory / names[index];
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        require(static_cast<bool>(input), "font fixture open failed");
        const std::streampos end = input.tellg();
        const std::streamoff offset = static_cast<std::streamoff>(end);
        require(offset > 0 && offset <= 4 * 1024 * 1024, "font size outside fixture bound");
        const std::size_t size = static_cast<std::size_t>(offset);
        std::shared_ptr<std::vector<std::byte>> bytes = std::make_shared<std::vector<std::byte>>(size);
        char* destination = reinterpret_cast<char*>((*bytes).data());
        const std::streamsize read_size = static_cast<std::streamsize>(size);
        input.seekg(0);
        input.read(destination, read_size);
        require(static_cast<bool>(input), "font fixture read failed");
        bank[index] = std::move(bytes);
    }
    return bank;
}

void register_fonts(HarfBuzzFontEngine& engine, const FontBank& bank) {
    const std::size_t charged = engine.configure_bounded_registration(bank.size(), 16U * 1024U * 1024U);
    require(charged != 0U, "font registration uncharged");
    for (std::size_t index = 0U; index < bank.size(); ++index) {
        std::optional<FontRole> role{};
        if (index == 0U) role = FontRole::content;
        const std::optional<FontFaceId> face = engine.register_shared_typeface(role, 400U, false, bank[index]);
        require(face.has_value(), "fixture registration failed");
    }
}

struct Scene final {
    std::array<std::string, 512U> rows{};
    std::size_t count{};
    std::size_t display_bytes{};
};
Scene make_scene(const std::string& name) {
    const std::array<std::string_view, 7U> lines{"office AV 123", "á é", "العربية 123",
        "שלום ABC 123", "", "Hello 😀", "[U+0000]"};
    Scene scene{};
    scene.count = name == "preview-seven" ? 7U : 512U;
    for (std::size_t index = 0U; index < scene.count; ++index) {
        scene.rows[index] = lines[index % lines.size()];
        if (scene.count == 512U && !scene.rows[index].empty()) {
            const std::string number = std::to_string(index);
            scene.rows[index].append(" ");
            scene.rows[index].append(number);
        }
        scene.display_bytes += scene.rows[index].size();
    }
    scene.display_bytes += scene.count - 1U;
    require(scene.display_bytes <= 16384U, "scene exceeds window display-byte bound");
    return scene;
}

using RetainedRows = std::array<std::unique_ptr<BoundedShapedText>, 512U>;
void verify_scene(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace,
    const Scene& scene, const FontSpec font, const double width) {
    RetainedRows retained{};
    std::size_t bytes = sizeof(retained);
    for (std::size_t index = 0U; index < scene.count; ++index) {
        ShapeStorageLimits limits{};
        require(bytes <= limits.output_bytes, "retained owner array exceeded allowance");
        limits.output_bytes -= bytes;
        retained[index] = engine.shape_with_workspace(scene.rows[index], font, limits, workspace);
        bytes += (*retained[index]).controlled_output_bytes;
        require((*retained[index]).missing_clusters == 0U && !(*retained[index]).missing_primary_face,
            "fixture has missing font coverage");
        require((*retained[index]).width <= width, "fixture needs wrapping at chosen profile");
    }
    for (std::size_t index = 0U; index < scene.count; ++index) {
        const std::unique_ptr<BoundedShapedText> legacy = engine.shape_bounded(scene.rows[index], font, {});
        compare(*legacy, *retained[index]);
    }
}

struct Sample final {
    std::array<double, 512U> row_us{};
    std::array<std::size_t, 512U> row_allocations{};
    std::array<std::size_t, 512U> row_requested_bytes{};
    double total_us{};
    double retirement_us{};
    double full_cycle_us{};
    std::size_t allocations{};
    std::size_t requested_bytes{};
    std::size_t peak_output_bytes{};
    std::size_t workspace_bytes{};
    std::size_t runs{};
};

template <bool Reusable>
Sample sample(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace,
    const Scene& scene, const FontSpec font, const PlatformClock& clock) {
    Sample result{};
    RetainedRows retained{};
    std::size_t live = sizeof(retained);
    allocation_probe::calls = 0U;
    allocation_probe::bytes = 0U;
    allocation_probe::enabled = true;
    const std::uint64_t begin = clock.read();
    for (std::size_t index = 0U; index < scene.count; ++index) {
        const std::size_t old_calls = allocation_probe::calls;
        const std::size_t old_bytes = allocation_probe::bytes;
        const std::uint64_t row_begin = clock.read();
        std::unique_ptr<BoundedShapedText> output{};
        if constexpr (Reusable) {
            ShapeStorageLimits limits{};
            require(live <= limits.output_bytes, "aggregate exhausted");
            limits.output_bytes -= live;
            output = engine.shape_with_workspace(scene.rows[index], font, limits, workspace);
        } else {
            output = engine.shape_bounded(scene.rows[index], font, {});
        }
        const std::uint64_t row_end = clock.read();
        result.row_allocations[index] = allocation_probe::calls - old_calls;
        result.row_requested_bytes[index] = allocation_probe::bytes - old_bytes;
        result.row_us[index] = clock.microseconds(row_begin, row_end);
        result.runs += (*output).run_count;
        result.workspace_bytes = std::max(result.workspace_bytes, (*output).controlled_workspace_peak);
        if constexpr (Reusable) {
            live += (*output).controlled_output_bytes;
            result.peak_output_bytes = live;
            retained[index] = std::move(output);
        } else {
            result.peak_output_bytes = std::max(result.peak_output_bytes, (*output).controlled_output_bytes);
        }
    }
    const std::uint64_t ready = clock.read();
    for (std::size_t index = 0U; index < scene.count; ++index) retained[index].reset();
    const std::uint64_t retired = clock.read();
    allocation_probe::enabled = false;
    result.total_us = clock.microseconds(begin, ready);
    result.retirement_us = clock.microseconds(ready, retired);
    result.full_cycle_us = clock.microseconds(begin, retired);
    result.allocations = allocation_probe::calls;
    result.requested_bytes = allocation_probe::bytes;
    return result;
}

struct Distribution final {
    std::vector<double> windows{};
    std::vector<double> rows{};
    std::vector<double> retirement{};
    std::vector<double> full_cycle{};
};

void summarize(std::vector<double>& values, const char* group, const char* arm, const char* phase) {
    require(!values.empty(), "empty distribution");
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    const std::size_t p50 = (count * 50U + 99U) / 100U - 1U;
    const std::size_t p95 = (count * 95U + 99U) / 100U - 1U;
    const std::size_t p99 = (count * 99U + 99U) / 100U - 1U;
    std::cout << group << ',' << arm << ',' << phase << ",n=" << count
        << ",p50_us=" << values[p50] << ",p95_us=" << values[p95]
        << ",p99_us=" << values[p99] << ",max_us=" << values.back() << '\n';
}

void write_sample(std::ostream& csv, const char* group, const std::size_t pair,
    const std::size_t order, const std::size_t arm, const bool reusable,
    const Scene& scene, const Sample& result, Distribution& distribution) {
    const char* variant = reusable ? "workspace" : "legacy";
    csv << group << ',' << pair << ',' << order << ',' << arm << ',' << variant
        << ",window_ready,-1," << result.total_us << ',' << result.allocations << ','
        << result.requested_bytes << ',' << result.peak_output_bytes << ','
        << result.workspace_bytes << ',' << result.runs << '\n';
    csv << group << ',' << pair << ',' << order << ',' << arm << ',' << variant
        << ",retirement,-1," << result.retirement_us << ",0,0,0,0,0\n";
    csv << group << ',' << pair << ',' << order << ',' << arm << ',' << variant
        << ",full_cycle,-1," << result.full_cycle_us << ',' << result.allocations << ','
        << result.requested_bytes << ',' << result.peak_output_bytes << ','
        << result.workspace_bytes << ',' << result.runs << '\n';
    distribution.windows.push_back(result.total_us);
    distribution.retirement.push_back(result.retirement_us);
    distribution.full_cycle.push_back(result.full_cycle_us);
    for (std::size_t row = 0U; row < scene.count; ++row) {
        csv << group << ',' << pair << ',' << order << ',' << arm << ',' << variant
            << ",row," << row << ',' << result.row_us[row] << ',' << result.row_allocations[row]
            << ',' << result.row_requested_bytes[row] << ",,,\n";
        distribution.rows.push_back(result.row_us[row]);
    }
}

void measure_pairs(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace,
    const Scene& scene, const FontSpec font, const Options& options,
    const PlatformClock& clock, std::ostream& csv, const bool same_binary) {
    const char* group = same_binary ? "AA" : "AB";
    std::array<Distribution, 2U> distributions{};
    for (std::size_t arm = 0U; arm < distributions.size(); ++arm) {
        distributions[arm].windows.reserve(options.samples);
        distributions[arm].retirement.reserve(options.samples);
        distributions[arm].full_cycle.reserve(options.samples);
        const std::size_t row_samples = options.samples * scene.count;
        distributions[arm].rows.reserve(row_samples);
    }
    for (std::size_t pair = 0U; pair < options.warmup + options.samples; ++pair) {
        for (std::size_t order = 0U; order < 2U; ++order) {
            const std::size_t arm = (pair + order) % 2U;
            const bool reusable = !same_binary && arm == 1U;
            Sample result{};
            if (reusable) result = sample<true>(engine, workspace, scene, font, clock);
            else result = sample<false>(engine, workspace, scene, font, clock);
            if (pair >= options.warmup) {
                const std::size_t recorded_pair = pair - options.warmup;
                write_sample(csv, group, recorded_pair, order, arm, reusable, scene, result, distributions[arm]);
            }
        }
    }
    for (std::size_t arm = 0U; arm < distributions.size(); ++arm) {
        const char* label = arm == 0U ? "arm0" : "arm1";
        summarize(distributions[arm].windows, group, label, "window_ready");
        summarize(distributions[arm].rows, group, label, "row");
        summarize(distributions[arm].retirement, group, label, "retirement");
        summarize(distributions[arm].full_cycle, group, label, "full_cycle");
    }
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        const FontBank fonts = load_fonts(options.fonts);
        const Scene scene = make_scene(options.scene);
        const double device_size = options.font_size * options.scale;
        const double device_width = options.width * options.scale;
        require(device_size <= 4096.0, "scaled font exceeds profile");
        const FontSpec font{FontRole::content, device_size, 400U, false};
        HarfBuzzFontEngine engine{};
        BoundedShapeWorkspace workspace{};
        if (!options.measure) {
            verify_csv_output();
            register_fonts(engine, fonts);
            engine.prepare_workspace(workspace, {});
            verify_scene(engine, workspace, scene, font, device_width);
            Scene empty{};
            empty.count = 512U;
            empty.display_bytes = 511U;
            verify_scene(engine, workspace, empty, font, device_width);
            std::cout << "Exact geometry, retained rows, coverage, width and empty-EOF fixtures passed; no timings run\n";
            return EXIT_SUCCESS;
        }
        CsvOutput csv_output(options.csv);
        std::ostream& csv = csv_output.stream();
        PlatformClock clock{};
        clock.calibrate(std::cout);
        const std::uint64_t registration_begin = clock.read();
        register_fonts(engine, fonts);
        const std::uint64_t registration_end = clock.read();
        const double registration_us = clock.microseconds(registration_begin, registration_end);
        const std::uint64_t preparation_begin = clock.read();
        engine.prepare_workspace(workspace, {});
        const std::uint64_t preparation_end = clock.read();
        const double preparation_us = clock.microseconds(preparation_begin, preparation_end);
        verify_scene(engine, workspace, scene, font, device_width);
        csv << std::setprecision(17);
        std::cout << std::setprecision(17) << "nominal_profile,font_size=" << options.font_size
            << ",scale=" << options.scale << ",width=" << options.width
            << ",scene=" << options.scene << ",rows=" << scene.count
            << ",display_bytes_with_LF=" << scene.display_bytes
            << ",registration_us=" << registration_us << ",preparation_us=" << preparation_us << '\n';
        std::cout << "Excluded: font-file IO, native C allocations, allocator overhead, certified input/batch metadata, UI paint. "
            "Legacy window_ready includes row destruction; workspace window_ready precedes retirement. "
            "full_cycle includes retirement for both. Control label is literal source.\n";
        csv << "group,pair,order,arm,variant,phase,row,microseconds,cpp_allocations,cpp_requested_bytes,"
            "peak_controlled_output_bytes,controlled_workspace_bytes,completed_runs\n";
        measure_pairs(engine, workspace, scene, font, options, clock, csv, true);
        measure_pairs(engine, workspace, scene, font, options, clock, csv, false);
        csv_output.finish();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        allocation_probe::enabled = false;
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
