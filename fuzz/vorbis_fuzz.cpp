// stx_vorbis over arbitrary bytes (audit I3), through the stb-compatible
// interface the audio service decodes with (src/audio/audio.cpp): open from
// memory, read the stream information, decode every frame, close.
// Invariants: an opened stream reports 1 to 255 channels (the format's
// limit: one byte in the identification header) and a positive sample rate;
// each decoded frame has the stream's channel count; and decoding stops
// within a bound of frames proportional to the input (no frame without
// input consumed forever).

#include "stx_vorbis/stb_compatible.h"
#include "fuzz_standalone.hpp"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("vorbis fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > static_cast<std::size_t>(INT_MAX)) return 0;
    int error = 0;
    stb_vorbis* const decoder = stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
    if (decoder == nullptr) return 0;
    const stb_vorbis_info info = stb_vorbis_get_info(decoder);
    if (info.channels < 1 || info.channels > 255) fail("an opened stream has 1 to 255 channels");
    if (info.sample_rate == 0U) fail("an opened stream has a sample rate");
    // An Ogg page carries at least a 27-byte header, and every audio packet
    // at least one byte, so a stream cannot hold more frames than bytes.
    const std::size_t maximum_frames = size + 1U;
    std::size_t frames = 0U;
    for (;;) {
        int channels = 0;
        float** output = nullptr;
        const int samples = stb_vorbis_get_frame_float(decoder, &channels, &output);
        if (samples == 0) break;
        if (channels != info.channels) fail("each frame has the stream's channel count");
        ++frames;
        if (frames > maximum_frames) fail("decoding stops within a frame per input byte");
    }
    stb_vorbis_close(decoder);
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
// The seed is the file named on the command line (the reference examples'
// tone.ogg in the suite); there is no built-in seed, since a valid stream
// is kilobytes of setup headers.
int main(int argument_count, char** argument_values) {
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "vorbis";
    run.mutated_runs = 3'000U;
    run.random_state = 0x9b05688c2b3e6c1fULL;
    run.first_fill = '\0';
    run.second_fill = '\xff';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
