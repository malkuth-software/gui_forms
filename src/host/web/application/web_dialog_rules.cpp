// The browser dialogs' plain rules (web_surface_host.hpp): no Emscripten,
// so they are tested on every platform (tests/web_host_rules_tests.cpp).
#include "web_surface_host.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::host::web_detail {

std::string web_accept_list(const std::vector<HostFileDialogFilter>& filters) {
    std::string accept;
    for (std::size_t filter = 0U; filter < filters.size(); ++filter) {
        const std::vector<std::string>& extensions = filters[filter].extensions;
        for (std::size_t index = 0U; index < extensions.size(); ++index) {
            std::string_view extension = extensions[index];
            if (extension.starts_with("*")) extension.remove_prefix(1U);
            if (extension.starts_with(".")) extension.remove_prefix(1U);
            if (extension.empty() || extension == "*") return std::string{};
            if (extension.find_first_of(",\"'<>") != std::string_view::npos) continue;
            if (!accept.empty()) accept.push_back(',');
            accept.push_back('.');
            accept.append(extension);
        }
    }
    return accept;
}

std::string web_save_name(const HostSaveFileDialogRequest& save) {
    std::string name = std::filesystem::path(save.suggested_name).filename().string();
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char character = name[index];
        if (character == '/' || character == '\\' || character == '\0') name[index] = '_';
    }
    if (name.empty() || name == "." || name == "..") name = "untitled";
    if (std::filesystem::path(name).extension().empty() && !save.default_extension.empty()) {
        if (save.default_extension.front() != '.') name.push_back('.');
        name.append(save.default_extension);
    }
    return name;
}

} // namespace gui_forms::host::web_detail
