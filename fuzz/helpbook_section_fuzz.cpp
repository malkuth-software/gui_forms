#include "gui_forms/help/helpbook_section/helpbook_section.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#if defined(GUI_FORMS_FUZZ_STANDALONE)
#include "gui_forms/help/help_records/help_records.hpp"
#endif

// Reads the input as a helpbook section; whatever is accepted must write,
// and the written form must read back and write the same bytes.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    gui_forms::HelpBookReadLimits limits{};
    limits.maximum_bytes = 1U * 1024U * 1024U;
    limits.maximum_records = 4096U;
    limits.maximum_entries = 65536U;
    const std::span<const std::byte> input(reinterpret_cast<const std::byte*>(data), size);
    const gui_forms::HelpBookReadResult result = gui_forms::read_helpbook_section(input, limits);
    if (!result) return 0;
    std::vector<std::byte> written;
    gui_forms::write_helpbook_section(result.section, written);
    const gui_forms::HelpBookReadResult reread = gui_forms::read_helpbook_section(written, limits);
    if (!reread) std::abort();
    std::vector<std::byte> rewritten;
    gui_forms::write_helpbook_section(reread.section, rewritten);
    if (rewritten != written) std::abort();
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
namespace {

// One xorshift64 step: advances `state` and returns it.
std::uint64_t next_random(std::uint64_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

} // namespace

int main() {
    // The seed: a small compiled section.
    gui_forms::HelpCatalog catalog;
    gui_forms::HelpRecordEntry entry{};
    entry.stable_id = "main";
    entry.title = std::string("Main");
    entry.help_line = std::string("The window.");
    entry.help = std::string("Some **help** with [a link](main.copy) and words.");
    entry.keywords = std::string("window; main, start");
    catalog.built_in().put(entry);
    entry.stable_id = "main.copy";
    entry.title = std::string("Copy");
    entry.help = std::string("Copies the selection to the clipboard.");
    entry.keywords = std::string("clipboard");
    catalog.built_in().put(entry);
    const gui_forms::HelpBookCompileResult compiled = gui_forms::compile_helpbook_section(catalog, {}, {});
    std::vector<std::byte> seed;
    gui_forms::write_helpbook_section(compiled.section, seed);
    if (LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(seed.data()), seed.size()) != 0) return 1;
    std::uint64_t state = 0x6a09e667f3bcc909ULL;
    std::vector<std::byte> input;
    for (std::size_t run = 0; run < 50'000; ++run) {
        input = seed;
        const std::uint64_t run_draw = next_random(state);
        const std::size_t mutations = 1U + static_cast<std::size_t>(run_draw % 8U);
        for (std::size_t mutation = 0; mutation < mutations; ++mutation) {
            const std::uint64_t draw = next_random(state);
            const std::size_t position = static_cast<std::size_t>(draw % input.size());
            const unsigned bit = static_cast<unsigned>((draw >> 32U) % 8U);
            input[position] = static_cast<std::byte>(std::to_integer<unsigned>(input[position]) ^ (1U << bit));
        }
        if (run % 16U == 0U) input.resize(static_cast<std::size_t>(run_draw % (input.size() + 1U)));
        static_cast<void>(LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(input.data()), input.size()));
    }
    return 0;
}
#endif
