// A saved dock layout restored from arbitrary bytes (audit N115), over five
// registered panels. Invariants: a refused record leaves the layout as it
// was (restore_xml validates before it changes anything); a restored
// layout writes, and what it writes restores into a fresh layout that
// writes the same text (the written form is canonical).

#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "fuzz_standalone.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace gui_forms;

// Names the broken invariant on stderr and stops.
[[noreturn]] void fail(const char* invariant) noexcept {
    std::fputs("dock layout fuzz: ", stderr);
    std::fputs(invariant, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

DockPanelDescriptor panel(std::string_view id, DockPanelKind kind, DockSide side) {
    DockPanelDescriptor descriptor{};
    descriptor.id = DockPanelId(std::string(id));
    descriptor.kind = kind;
    descriptor.default_placement.side = side;
    return descriptor;
}

// Registers the panels the seeds name: three tools and two documents.
void register_panels(DockLayout& layout) {
    const std::array<DockPanelDescriptor, 5> panels{
        panel("tools.solution", DockPanelKind::tool, DockSide::left),
        panel("tools.properties", DockPanelKind::tool, DockSide::right),
        panel("tools.output", DockPanelKind::tool, DockSide::bottom),
        panel("doc.a", DockPanelKind::document, DockSide::left),
        panel("doc.b", DockPanelKind::document, DockSide::left)};
    for (const DockPanelDescriptor& descriptor : panels) {
        if (!layout.register_panel(descriptor)) fail("a seed panel registers");
    }
}

// The layout's written text; a layout that cannot write is a broken
// invariant, since every layout restore_xml or the program made is valid.
std::string written(const DockLayout& layout) {
    DockLayoutWriteResult result = layout.write_xml();
    if (!result) fail("a layout writes");
    return std::move(result.text);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view xml(reinterpret_cast<const char*>(data), size);
    DockLayout layout{};
    register_panels(layout);
    const std::string before = written(layout);
    const DockRestoreResult result = layout.restore_xml(xml, DockRestoreOptions{});
    const std::string after = written(layout);
    if (result.status == DockRestoreStatus::kept_current) {
        if (after != before) fail("a refused record leaves the layout as it was");
        return 0;
    }
    DockLayout again{};
    register_panels(again);
    const DockRestoreResult reread = again.restore_xml(after, DockRestoreOptions{});
    if (reread.status != DockRestoreStatus::restored) fail("a written layout restores");
    if (written(again) != after) fail("a restored layout writes the same text again");
    return 0;
}

#if defined(GUI_FORMS_FUZZ_STANDALONE)
// The seeds are a version 2 layout of the five panels and a version 1
// record (upgraded on restore); the mutations explore around them.
int main(int argument_count, char** argument_values) {
    const std::array<std::string_view, 2> seeds{
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<dock_layout format=\"2\" application_version=\"0\" next_id=\"9\">\n"
        "  <main_site>\n"
        "    <split id=\"7\" axis=\"vertical\" ratio=\"0.75\">\n"
        "      <split id=\"5\" axis=\"horizontal\" ratio=\"0.75\">\n"
        "        <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
        "          <tool_stack id=\"2\" selected=\"0\">\n"
        "            <panel id=\"tools.solution\"/>\n"
        "          </tool_stack>\n"
        "          <document_area id=\"1\">\n"
        "            <document_stack id=\"8\" selected=\"1\">\n"
        "              <panel id=\"doc.a\"/>\n"
        "              <panel id=\"doc.b\"/>\n"
        "            </document_stack>\n"
        "          </document_area>\n"
        "        </split>\n"
        "        <tool_stack id=\"4\" selected=\"0\">\n"
        "          <panel id=\"tools.properties\"/>\n"
        "        </tool_stack>\n"
        "      </split>\n"
        "      <tool_stack id=\"6\" selected=\"0\">\n"
        "        <panel id=\"tools.output\"/>\n"
        "      </tool_stack>\n"
        "    </split>\n"
        "  </main_site>\n"
        "  <panel_record id=\"doc.a\" kind=\"document\" last=\"docked\"/>\n"
        "  <panel_record id=\"doc.b\" kind=\"document\" last=\"docked\"/>\n"
        "  <panel_record id=\"tools.output\" kind=\"tool\" last=\"docked\"/>\n"
        "  <panel_record id=\"tools.properties\" kind=\"tool\" last=\"docked\"/>\n"
        "  <panel_record id=\"tools.solution\" kind=\"tool\" last=\"docked\"/>\n"
        "</dock_layout>\n",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<dock_layout format=\"1\" application_version=\"0\" next_id=\"5\">\n"
        "  <main_site>\n"
        "    <split id=\"3\" axis=\"horizontal\" ratio=\"0.3\">\n"
        "      <stack id=\"2\" kind=\"tool\" selected=\"0\">\n"
        "        <panel id=\"tools.solution\"/>\n"
        "      </stack>\n"
        "      <document_area id=\"1\">\n"
        "        <stack id=\"4\" kind=\"document\" selected=\"0\">\n"
        "          <panel id=\"doc.a\"/>\n"
        "        </stack>\n"
        "      </document_area>\n"
        "    </split>\n"
        "  </main_site>\n"
        "</dock_layout>\n"};
    gui_forms::fuzz::StandaloneRun run{};
    run.name = "dock layout";
    run.seeds = seeds;
    run.mutated_runs = 20'000U;
    run.random_state = 0x1f83d9abfb41bd6bULL;
    run.first_fill = '<';
    run.second_fill = '"';
    gui_forms::fuzz::StandaloneCounts counts{};
    const int status = gui_forms::fuzz::run_standalone(argument_count, argument_values, run, counts);
    return status;
}
#endif
