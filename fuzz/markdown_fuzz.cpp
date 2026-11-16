// Fuzzes the markdown framework's parser (gui_forms/text/markdown): any
// input either fails with a named error and an empty document, or parses
// into blocks whose spans follow the source in order, lie inside it, keep
// their link indices inside the link table, and stay inside the limits.
// Run as a libFuzzer target, or standalone (GUI_FORMS_FUZZ_STANDALONE) over
// seeded mutations of dialect documents.
#include "gui_forms/text/markdown/markdown.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("markdown fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

// Small bounds, so the limit paths are reached as well as the parse.
gui_forms::MarkdownLimits fuzz_limits() noexcept {
    gui_forms::MarkdownLimits limits{};
    limits.source_bytes = 64U * 1024U;
    limits.blocks = 512U;
    limits.spans = 2048U;
    limits.text_bytes = 64U * 1024U;
    return limits;
}

void check(std::string_view source, const gui_forms::MarkdownDocument& document,
           const gui_forms::MarkdownLimits& limits) noexcept {
    if (document.blocks.size() > limits.blocks) fail("the blocks stay inside the limit");
    if (document.spans.size() > limits.spans) fail("the spans stay inside the limit");
    std::size_t previous = 0U;
    std::size_t next_span = 0U;
    for (const gui_forms::MarkdownBlock& block : document.blocks) {
        if (block.first_span != next_span) fail("the blocks' spans tile the span table");
        next_span += block.span_count;
        if (next_span > document.spans.size()) fail("a block's spans lie in the table");
        if (block.source.start.value() > block.source.end.value()) fail("a block's source is ordered");
        if (block.source.end.value() > source.size()) fail("a block's source lies in the input");
        for (std::size_t index = 0U; index < block.span_count; ++index) {
            const gui_forms::MarkdownSpan& span = document.spans[block.first_span + index];
            if (span.source.start.value() > span.source.end.value()) fail("a span's source is ordered");
            if (span.source.end.value() > source.size()) fail("a span's source lies in the input");
            if (span.source.start.value() < previous) fail("spans follow the source in order");
            previous = span.source.end.value();
            if (span.link.present() && (*span.link.value()) >= document.links.size()) fail("a link index is in the table");
            if (span.style.size_step < gui_forms::minimum_markdown_size_step ||
                span.style.size_step > gui_forms::maximum_markdown_size_step) {
                fail("a size step is in range");
            }
        }
    }
    if (next_span != document.spans.size()) fail("every span belongs to a block");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view source(reinterpret_cast<const char*>(data), size);
    const gui_forms::MarkdownLimits limits = fuzz_limits();
    gui_forms::MarkdownDocument document{};
    const gui_forms::MarkdownParseResult result = gui_forms::parse_markdown(source, document, limits);
    if (!result.succeeded()) {
        if (result.error().absent()) fail("a failure names its error");
        if (!document.blocks.empty() || !document.spans.empty() || !document.links.empty()) {
            fail("a failed parse leaves the document empty");
        }
        return 0;
    }
    check(source, document, limits);
    std::string plain{};
    gui_forms::markdown_plain_text(document, plain);
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
    // Each seed parses completely; the mutations explore around them.
    const std::array<std::string_view, 5> seeds{
        "# [Title]{color=#c45911}\n\nThis has **three** goals; see [[the tracker]{u color=#0000ff}]"
        "(https://example.com/plan).\n\n1. Ship\n\n2. Fix the *picker*\n\n- Risks\n\n  - Slips by `2 weeks`\n",
        "> {align=center}\n> [*Ship small.*]{color=#000000}\n\nWater is H[2]{sub}O; E = mc[2]{sup}.\n",
        "```c\nint x;\n\n```\n\n***\n\nSetext\n===\n\nline\\\nbreak &copy; &#x1F600; \\[x]{u}\r\n",
        "| a | b |\n|---|---|\n- [ ] task\n![alt](i.png) <b>html</b> <https://a.b> ~~s~~ ~t~\n",
        "{align=right}\n\n[a [b [c]{size=+4}]{bg=#ABCDEF sub}]{i b s u}\n    indented\n> > > - 9. deep\n"};
    std::uint64_t state = 0x3c6ef372fe94f82bULL;
    for (std::size_t run = 0; run < 50'000; ++run) {
        const std::uint64_t run_draw = next_random(state);
        std::string input(seeds[run % seeds.size()]);
        const std::size_t mutations = 1U + static_cast<std::size_t>(run_draw % 8U);
        // Each mutation flips one bit of one byte, or replaces it with a
        // markup character.
        constexpr std::string_view markup = "[]{}*_~`#>-\\&\n ";
        for (std::size_t mutation = 0; mutation < mutations; ++mutation) {
            const std::uint64_t draw = next_random(state);
            if (input.empty()) continue;
            const std::size_t offset = static_cast<std::size_t>(draw % input.size());
            if ((draw >> 32U) % 2U == 0U) {
                const char flipped_bit = static_cast<char>(1U << (draw % 8U));
                input[offset] = static_cast<char>(input[offset] ^ flipped_bit);
            } else {
                input[offset] = markup[static_cast<std::size_t>((draw >> 40U) % markup.size())];
            }
        }
        if (run % 5U == 0 && !input.empty()) {
            input.resize(static_cast<std::size_t>(state % input.size()));
        } else if (run % 7U == 0) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 512U), '[');
        } else if (run % 11U == 0) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 512U), '>');
        } else if (run % 13U == 0) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 4096U), '\n');
        }
        LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
    }
    return 0;
}
#endif
