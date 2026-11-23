#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The sources behind each Gallery page, for the page header's Source button
// (the survey's "source button on every Gallery page", worth it with no
// library API: notes planning/audits/DEVEXPRESS_OVERKILL_SURVEY_2026-11-22.md).
// cmake/GalleryPageSources.cmake embeds the page builders, the shared page
// parts and the page form documents at build time into
// gallery_page_sources.inc; this header names the files that make each page.
namespace gui_forms::gallery::page_sources {

// One embedded file: its path in the source tree and its bytes (UTF-8).
struct EmbeddedSource final {
    std::string_view path{};
    const unsigned char* bytes{};
    std::size_t size{};
};

} // namespace gui_forms::gallery::page_sources

#include "gallery_page_sources.inc"

namespace gui_forms::gallery::page_sources {

// The window form's own pages, all declared in demo/gallery_form_pages.form.
inline constexpr std::array<std::string_view, 4> window_form_pages{"basics", "values", "retained-rows", "instrument"};
// The composition pages that share demo_parts.hpp.
inline constexpr std::array<std::string_view, 6> composition_pages{"lookups-and-pickers", "media-and-motion",
                                                                   "navigation-patterns", "dashboard-pieces",
                                                                   "workspaces-and-themes", "ui-templates"};

// The file name of `path` without its directory and extension.
[[nodiscard]] inline std::string_view path_stem(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1U);
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0U, dot);
}

[[nodiscard]] inline bool listed(std::string_view page_id, std::span<const std::string_view> pages) noexcept {
    for (const std::string_view page : pages) {
        if (page == page_id) return true;
    }
    return false;
}

// The files that make page `page_id`, page code first: its builder, its
// form document, and the shared files it is composed with. Empty for an
// unknown page.
[[nodiscard]] inline std::vector<const EmbeddedSource*> page_files(std::string_view page_id) {
    std::vector<const EmbeddedSource*> builders{};
    std::vector<const EmbeddedSource*> documents{};
    std::vector<const EmbeddedSource*> shared{};
    const bool window_page = listed(page_id, window_form_pages);
    const bool composition = listed(page_id, composition_pages);
    for (const EmbeddedSource& file : embedded) {
        const std::string_view stem = path_stem(file.path);
        const bool form = file.path.ends_with(".form");
        if (stem == page_id && !form) builders.push_back(&file);
        if (stem == page_id && form) documents.push_back(&file);
        if (window_page && file.path == "demo/gallery_form_pages.form") documents.push_back(&file);
        if (page_id == "ui-templates" && file.path.starts_with("demo/pages/templates/")) documents.push_back(&file);
        if (composition && stem == "demo_parts") shared.push_back(&file);
    }
    std::vector<const EmbeddedSource*> files = builders;
    files.insert(files.end(), documents.begin(), documents.end());
    files.insert(files.end(), shared.begin(), shared.end());
    return files;
}

// The embedded file's text.
[[nodiscard]] inline std::string file_text(const EmbeddedSource& file) {
    std::string text(file.size, '\0');
    for (std::size_t at = 0; at < file.size; ++at) text[at] = static_cast<char>(file.bytes[at]);
    return text;
}

} // namespace gui_forms::gallery::page_sources
