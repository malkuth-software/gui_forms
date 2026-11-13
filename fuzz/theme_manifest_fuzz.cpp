// A theme package's theme.json manifest from arbitrary bytes (audit N115),
// through its JSON reader and the manifest schema. Invariants: a refusal
// names a manifest code (E6xx) and, for a JSON error, a position within the
// input; and a second read gives the same outcome and manifest, or the same
// problem.

#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "fuzz_standalone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

using namespace gui_forms;
using namespace gui_forms::theme_language;

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("theme manifest fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);
    ThemeManifest manifest{};
    ThemeManifestProblem problem{};
    const ThemeManifestOutcome outcome = read_theme_manifest(bytes, current_engine_version, manifest, problem);
    ThemeManifest again{};
    ThemeManifestProblem again_problem{};
    const ThemeManifestOutcome reread = read_theme_manifest(bytes, current_engine_version, again, again_problem);
    if (reread != outcome) fail("reading is repeatable");
    if (outcome == ThemeManifestOutcome::read) {
        if (!(again == manifest)) fail("a read manifest reads the same again");
        return 0;
    }
    const std::uint32_t code = static_cast<std::uint32_t>(problem.code);
    if (code < 600U || code > 699U) fail("a refusal names a manifest code");
    if (problem.position.byte_offset > size) fail("a refusal's position lies within the input");
    if (again_problem.code != problem.code || again_problem.detail != problem.detail) {
        fail("a refusal is repeatable");
    }
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
// The seeds are a minimal manifest and one using every key; the mutations
// explore around them.
int main(int argument_count, char** argument_values) {
    const std::array<std::string_view, 2> seeds{
        "{\"format\": \"gui-forms-theme/1\", \"name\": \"harbour-dark\", \"version\": \"1.4.0\", "
        "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": "
        "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}}",
        "{\"format\": \"gui-forms-theme/1\", \"name\": \"harbour-dark\", \"version\": \"1.4.0\", "
        "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": "
        "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}, "
        "\"author\": \"rainstar\", \"provenance\": \"first-party\", "
        "\"imports\": {\"harbour-base\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"}, "
        "\"resources\": [{\"name\": \"icon.check\", \"path\": \"images/check.png\", "
        "\"sha256\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"}], "
        "\"published_values\": [\"progress\", \"level-2\"], \"coverage\": [\"button\", \"check-box\"]}"};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "theme manifest";
    run.seeds = seeds;
    run.mutated_runs = 50'000U;
    run.random_state = 0x5be0cd19137e2179ULL;
    run.first_fill = '[';
    run.second_fill = '"';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
