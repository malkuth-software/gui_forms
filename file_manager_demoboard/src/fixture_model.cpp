#include "file_manager_demoboard/fixture_model.hpp"

#include <algorithm>
#include <array>

namespace file_manager_demoboard {

FixtureCatalogue::FixtureCatalogue()
    : id_("file-manager-demoboard-001"), generation_(86),
      current_path_("/Users/quentin/Work/Projects"),
      project_objects_({
          {"obj-file-manager", "File Manager", "Folder", "1.3G", "folder-blue", false},
          {"obj-orchard-study", "Orchard Study", "Folder", "250M", "folder-green", false},
          {"obj-field-recordings", "Field Recordings", "Folder", "3.6G", "folder", false},
          {"obj-north-shore", "North Shore", "Folder", "725M", "folder", false},
          {"obj-print-masters", "Print Masters", "Folder", "90M", "folder", false},
          {"obj-facade-study", "Facade Study.png", "PNG image", "2.8 MB", "image", true},
          {"obj-material-notes", "Material notes.txt", "Plain text", "18 KB", "document", false},
          {"obj-palette-study", "palette-study.css", "Stylesheet", "31 KB", "code", false},
          {"obj-survey-plates", "Survey plates.pdf", "PDF document", "6.4 MB", "pdf", false},
          {"obj-room-tone", "Room tone 03.aiff", "Audio", "48 MB", "music", false},
          {"obj-delivery-archive", "Delivery 2026-07.zip", "ZIP archive", "12.7 MB", "archive", false},
          {"obj-invoice-quartz", "invoice quartz.txt", "Plain text", "42 KB", "document", false},
          {"obj-map-detail", "Map detail 17.tif", "TIFF image", "14.2 MB", "image", false},
          {"obj-index-fixture", "index fixture.json", "JSON document", "9 KB", "code", false},
      }),
      current_path_stack_({"projects", "/Users/quentin/Work/Projects",
          {"Macintosh HD", "Users", "quentin", "Work", "Projects"}, true}),
      recent_paths_({
          {"orchard-study", "/Users/quentin/Work/Projects/Orchard Study",
           {"Macintosh HD", "Users", "quentin", "Work", "Projects",
            "Orchard Study"}, true},
          {"legal-2026", "/Users/quentin/Documents/Legal/2026",
           {"Macintosh HD", "Users", "quentin", "Documents", "Legal",
            "2026"}, true},
          {"archive-material-library",
           "/Volumes/Archive 04/Reference/Material Library",
           {"Archive 04", "Reference", "Material Library"}, false},
          {"pictures-scans", "/Users/quentin/Pictures/Scans",
           {"Macintosh HD", "Users", "quentin", "Pictures", "Scans"}, true},
          {"reference-materials",
           "/Users/quentin/Work/Reference/Materials",
           {"Macintosh HD", "Users", "quentin", "Work", "Reference",
            "Materials"}, true},
      }),
      path_completions_({
          {"orchard-study", "/Users/quentin/Work/Projects/Orchard Study/"},
          {"north-shore", "/Users/quentin/Work/Projects/North Shore/"},
          {"print-masters", "/Users/quentin/Work/Projects/Print Masters/"},
      }),
      search_query_("invoice quartz"),
      search_results_({
          {"result-invoice-text", "invoice quartz.txt", "Projects",
           "Projects / invoice quartz.txt · exact filename “invoice quartz” · current catalogue record",
           "Received the revised invoice for the North Shore quartz samples. Reference QZ-0428…",
           "98%", {"Local index", "Text extractor"},
           "Generation 86 · direct filesystem identity", "document",
           "projects", "obj-invoice-quartz", true, false, true},
          {"result-invoice-pdf", "Invoice 0428.pdf",
           "Projects / Orchard Study / Billing",
           "Projects / Orchard Study / Billing · filename “invoice” · extracted text “quartz”",
           "Material schedule: honed white quartz. See approved invoice 0428 for quantities…",
           "91%", {"Local index", "PDF text"},
           "Evidence is inspectable; rank is advisory.", "pdf",
           "orchard-study", "search-invoice-pdf", true, false, false},
          {"result-quartz-image", "quartz-countertop-final.png",
           "Projects / North Shore / Final",
           "Projects / North Shore / Final · filename substring “quartz” · no content claim",
           "Raster preview: pale stone counter surface, final presentation crop, 3840 × 2160.",
           "82%", {"Local index", "Image metadata"},
           "No semantic provider contributed.", "image",
           "north-shore", "search-quartz-image", true, false, false},
          {"result-correspondence", "Client correspondence.rtf",
           "Projects / North Shore",
           "Projects / North Shore · extracted text contains “invoice” and “quartz”",
           "Could you confirm the quartz sample before we approve the final invoice?",
           "76%", {"Local index", "RTF extractor"},
           "Generation 86 · extracted fixture text", "document",
           "north-shore", "search-correspondence", true, false, false},
          {"result-pages-offline", "Invoice quartz sample.pages",
           "Archive 04 / 2025 / Templates",
           "Archive 04 / 2025 / Templates · stored filename evidence only · source volume offline",
           "Excerpt unavailable. Match derives from the stored filename only.",
           "61%", {"Local index"},
           "Stale evidence retained and labelled.", "document",
           "archive-04", {}, false, false, false},
          {"result-stone-schedule", "Stone schedule.csv",
           "Projects / North Shore / Specifications",
           "Projects / North Shore / Specifications · row text mentions quartz · related invoice identifier",
           "Quartz sample QZ-0428 appears in the material schedule beside its invoice reference.",
           "58%", {"Local index", "Delimited text"},
           "Generation 86 · current extracted row evidence", "code",
           "north-shore", "search-stone-schedule", true, false, false},
          {"result-quartz-order", "Quartz order.msg",
           "Projects / North Shore / Correspondence",
           "Projects / North Shore / Correspondence · stored subject contains quartz · stale invoice relation",
           "Subject: Quartz order confirmation. Stored extractor relation references the project invoice.",
           "54%", {"Local index", "Message metadata"},
           "Extractor record is stale and explicitly labelled.", "document",
           "north-shore", "search-quartz-order", true, true, false},
      }),
      criteria_modules_({
          {"criteria-kind-images", "Kind criterion",
           {{"property", "Property", "Kind", {"Kind", "Name", "Source volume"}},
            {"operator", "Operator", "is", {"is", "is not", "contains"}, false, false, .8},
            {"value", "Value", "Images", {"Images", "Documents", "Folders"}, false, false, 1.1}},
           "live · inexpensive", true, false, false, 30},
          {"criteria-modified-2026", "Modified criterion",
           {{"property", "Property", "Modified", {"Modified", "Created"}},
            {"operator", "Operator", "during", {"during", "before", "after"}, false, false, .8},
            {"value", "Value", "2026", {"2026", "2025", "2024"}, false, false, 1.1}},
           "live · inexpensive", true, false, false, 20},
          {"criteria-content-facade", "Content criterion",
           {{"property", "Property", "Content", {"Content", "Name", "Plugin indication"}},
            {"operator", "Operator", "resembles", {"resembles", "contains", "is"}, false, false, .8},
            {"value", "Value", "Facade", {}, true, true, 1.1}},
           "staged · expensive · not applied", true, true, true, 10},
      }),
      criterion_templates_({
          {"name-study", "Name criterion",
           {{"property", "Property", "Name", {"Name"}},
            {"operator", "Operator", "contains", {"contains"}, false, false, .8},
            {"value", "Value", "study", {}, true, true, 1.1}},
           "live · inexpensive", true, false, false, 15},
          {"size-10mb", "Size criterion",
           {{"property", "Property", "Size", {"Size"}},
            {"operator", "Operator", "greater than", {"greater than", "less than"}, false, false, .8},
            {"value", "Value", "10 MB", {}, true, true, 1.1}},
           "live · inexpensive", true, false, false, 14},
          {"source-archive", "Source volume criterion",
           {{"property", "Property", "Source volume", {"Source volume"}},
            {"operator", "Operator", "is", {"is", "is not"}, false, false, .8},
            {"value", "Value", "Archive 04", {"Archive 04", "Macintosh HD"}, false, false, 1.1}},
           "live · inexpensive · source offline", true, false, false, 13},
          {"content-quartz", "Content criterion",
           {{"property", "Property", "Content", {"Content"}},
            {"operator", "Operator", "resembles", {"resembles"}, false, false, .8},
            {"value", "Value", "Quartz", {}, true, true, 1.1}},
           "staged · expensive · not applied", true, true, true, 12},
          {"plugin-approved", "Plugin indication criterion",
           {{"property", "Property", "Plugin indication", {"Plugin indication"}},
            {"operator", "Operator", "is", {"is", "is not"}, false, false, .8},
            {"value", "Value", "Approved", {"Approved", "Unreviewed"}, false, false, 1.1}},
           "staged · producer-dependent · not applied", true, true, true, 11},
      }) {
    criteria_objects_ = {
        {"obj-facade-study", "Facade Study.png", "PNG image", "2.8 MB", "image", true},
        {"obj-map-detail", "Map detail 17.tif", "TIFF image", "14.2 MB", "image", false},
        {"criteria-obj-quartz-elevation", "Quartz elevation.png", "PNG image", "5.6 MB", "image", false},
        {"criteria-obj-orchard-plate", "Orchard plate 03.tif", "TIFF image", "22.4 MB", "image", false},
        {"criteria-obj-north-shore-18", "North shore 18.png", "PNG image", "7.1 MB", "image", false},
    };
    static constexpr std::array<std::string_view, 5> stems{
        "Facade section", "Material study", "Project elevation",
        "Window detail", "Site plate"};
    static constexpr std::array<std::string_view, 3> extensions{
        ".png", ".tif", ".jpg"};
    for (std::size_t index = criteria_objects_.size(); index < 31U; ++index) {
        const std::string name = std::string(stems[index % stems.size()]) + " " +
            std::to_string(index + 1U) +
            std::string(extensions[index % extensions.size()]);
        criteria_objects_.push_back({
            "criteria-generated-" + std::to_string(index + 1U), name,
            extensions[index % extensions.size()] == ".tif" ? "TIFF image"
                                                               : "Image",
            std::to_string(2U + (index * 7U) % 29U) + "." +
                std::to_string((index * 3U) % 10U) + " MB",
            "image", false});
    }
}

const FixtureCatalogue& FixtureCatalogue::instance() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static const FixtureCatalogue* const catalogue = new const FixtureCatalogue();
    return *catalogue;
}

static bool replace_path_prefix(std::string& path,
                         std::string_view prefix,
                         std::string_view replacement) {
    if (!path.starts_with(prefix)) return false;
    path.replace(0, prefix.size(), replacement);
    return true;
}

static void expand_path_prefix(std::string& path,
                        std::string_view prefix,
                        std::string_view replacement) {
    if (path.starts_with(prefix)) path.replace(0, prefix.size(), replacement);
}

FixturePathResolution FixtureCatalogue::resolve_path(
    std::string_view input) const {
    std::string expanded(input);
    if (expanded.starts_with("$UNKNOWN") || expanded.starts_with("${UNKNOWN}")) {
        return {false, expanded, {},
                "Unknown variable $UNKNOWN · use ~, $HOME, or $PROJECTS"};
    }
    static_cast<void>(replace_path_prefix(
        expanded, "${PROJECTS}", "/Users/quentin/Work/Projects"));
    static_cast<void>(replace_path_prefix(
        expanded, "$PROJECTS", "/Users/quentin/Work/Projects"));
    static_cast<void>(replace_path_prefix(expanded, "${HOME}", "/Users/quentin"));
    static_cast<void>(replace_path_prefix(expanded, "$HOME", "/Users/quentin"));
    static_cast<void>(replace_path_prefix(expanded, "~", "/Users/quentin"));
    while (expanded.size() > 1U && expanded.back() == '/') expanded.pop_back();

    if (current_path_stack_.path == expanded) {
        return {true, expanded, current_path_stack_.id,
                "Resolved current fixture location"};
    }
    for (const FixturePath& path : recent_paths_) {
        if (path.path != expanded) continue;
        return {true, expanded, path.id,
                path.available ? "Resolved fixture location"
                                 : "Resolved stored path · source volume offline"};
    }
    for (const FixturePathCompletion& completion : path_completions_) {
        std::string path = completion.path;
        while (path.size() > 1U && path.back() == '/') path.pop_back();
        if (path == expanded) {
            return {true, expanded, completion.id,
                    "Resolved fixture location"};
        }
    }
    return {false, expanded, {},
            "No fixture destination matches “" + expanded + "”"};
}

std::vector<FixturePathCompletion> FixtureCatalogue::complete_path(
    std::string_view input) const {
    const FixturePathResolution expanded = resolve_path(input);
    std::string prefix = expanded.expanded_path;
    if (!expanded.valid) {
        prefix.assign(input);
        expand_path_prefix(prefix, "${PROJECTS}", "/Users/quentin/Work/Projects");
        expand_path_prefix(prefix, "$PROJECTS", "/Users/quentin/Work/Projects");
        expand_path_prefix(prefix, "${HOME}", "/Users/quentin");
        expand_path_prefix(prefix, "$HOME", "/Users/quentin");
        expand_path_prefix(prefix, "~", "/Users/quentin");
    }
    std::vector<FixturePathCompletion> matches;
    for (const FixturePathCompletion& completion : path_completions_) {
        if (completion.path.starts_with(prefix)) matches.push_back(completion);
    }
    return matches;
}

std::vector<CapabilityEntry> initial_capability_report() {
    return {
        {"FM-X01", "supported", "product shell uses only public C++ controls"},
        {"FM-X03", "supported", "retained controls and fixture objects have stable IDs"},
        {"FM-LY01", "supported", "table, flow, dock, anchor and split composition"},
        {"FM-LY05", "measured-partial", "split panes provide live bounded resize, useful maximums, remembered extents, automatic/user origin, and focus-safe seam tabs"},
        {"FM-T01", "measured-partial", "bundled Portsmouth, Carlito and Cousine specimen pack"},
        {"FM-O01", "measured-partial", "public TreeView has stable hierarchy, expansion, keyboard, type-select and bounded semantics"},
        {"FM-O02", "measured-partial", "public ObjectView provides stable virtual icon/details projections and bounded realization"},
        {"FM-O03", "measured-partial", "public ObjectView preserves stable multi-selection, primary, anchor and independent focus across view/sort changes"},
        {"FM-O11", "measured-partial", "public PropertyList owns grouped read-only/text/choice rows, inline validation, disclosures and one preview/property scroll plane"},
        {"FM-O12", "measured-partial", "Selection preview disclosure and session Name/handler commits are retained and accessible"},
        {"FM-C03", "measured-partial", "public Command and tokenized accelerators bind menu, ribbon, status and navigation presentations; complete shelf remains open"},
        {"FM-C05", "measured-partial", "public MenuStrip and ContextMenu provide retained switching, nested menus, keyboard focus scopes, edge avoidance and native menu semantics"},
        {"FM-C09", "measured-partial", "correspondence hover intent is pointer-neutral and one retained row may be pinned without changing activation authority"},
        {"FM-C10", "measured-partial", "public correspondence rows keep focus, delayed hover, one pin, selection, activation and expanded semantics independent"},
        {"FM-LY08", "measured-partial", "sparse variable-height virtualization preserves viewport anchors with bounded realization and no per-item controls"},
        {"FM-S01", "measured-partial", "public CorrespondenceView renders compact and expanded evidence, metric, marked excerpt, plugin lane, offline and stale states"},
        {"FM-S02", "measured-partial", "public InstrumentRack composes stable real enable, property, operator, value, status and remove controls with responsive wrap and bounded scroll"},
        {"FM-S03", "measured-partial", "consumer-owned criterion meaning separates cheap live projection from explicit expensive staging and deterministic Apply generations"},
        {"FM-W03", "unavailable", "custom host chrome/title participation is not yet public"},
        {"FM-A07", "unavailable", "native accessibility publisher closure remains open"},
    };
}

} // namespace file_manager_demoboard
