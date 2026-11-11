// RTF import from arbitrary bytes (audit N115), through stx_rtf2md into the
// markdown dialect. Invariants: a failed import leaves the markdown empty
// and stops within the input; an import that succeeded exports back to RTF,
// and that RTF imports to the same markdown (rtf_conversion.hpp: canonical
// RTF reads back to the same document).

#include "gui_forms/text/markdown/rtf_conversion.hpp"
#include "fuzz_standalone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("rtf import fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> rtf(reinterpret_cast<const std::byte*>(data), size);
    std::string markdown{};
    const gui_forms::RtfConversionResult imported = gui_forms::import_rtf(rtf, markdown);
    if (!imported.succeeded()) {
        if (!markdown.empty()) fail("a failed import leaves the markdown empty");
        if (imported.offset() > size) fail("a failed import stops within the input");
        return 0;
    }
    std::vector<std::byte> exported{};
    const gui_forms::RtfConversionResult written = gui_forms::export_rtf(markdown, exported);
    if (!written.succeeded()) fail("imported markdown exports");
    std::string again{};
    const gui_forms::RtfConversionResult reread = gui_forms::import_rtf(exported, again);
    if (!reread.succeeded() || again != markdown) fail("exported RTF imports to the same markdown");
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
// The corpus directories on the command line (stx_rtf2md's dialect corpus,
// installed with the stx package, in the suite), then the seed below; the
// mutations explore around them all.
int main(int argument_count, char** argument_values) {
    const std::array<std::string_view, 1> seeds{
        "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0 Calibri;}}\n"
        "{\\pard\\b Heading\\b0\\par}\n"
        "{\\pard Plain, \\i italic\\i0 , and a {\\field{\\*\\fldinst HYPERLINK \"https://example.org\"}"
        "{\\fldrslt link}}.\\par}\n"
        "{\\pard\\li720 \\'95\\tab item\\par}\n"
        "}"};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "rtf import";
    run.seeds = seeds;
    run.mutated_runs = 20'000U;
    run.random_state = 0xcbbb9d5dc1059ed8ULL;
    run.first_fill = '{';
    run.second_fill = '\\';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
