#include "model_inspect_sections.hpp"

#include "../../../core/model_inspection/model_inspection.hpp"

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"
#include "gui_forms/window/window.hpp"

#include <string>

namespace gui_forms::automation {

namespace {

void summary_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.window == nullptr) {
        text += "no window\n";
        return;
    }
    const SemanticSnapshot snapshot = (*request.window).semantic_snapshot();
    text += model_inspection::window_state_summary(snapshot);
    text += '\n';
}

void layout_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.control == nullptr) {
        text += "name a target: layout reasons are a control's\n";
        return;
    }
    text += model_inspection::layout_reasons(*request.control);
}

void resources_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.window == nullptr) {
        text += "no window\n";
        return;
    }
    text += model_inspection::resource_summary((*request.window).image_resource_snapshot(), allocation_statistics());
}

} // namespace

std::size_t register_model_inspect_sections(ToolRegistry& tools) {
    std::size_t added = 0U;
    if (tools.add_inspect_section("summary",
                                  "the window in one paragraph: focus, selection, checked, expanded, busy, invalid, "
                                  "disabled and what waits on the user",
                                  &summary_section, nullptr) == ToolRegistration::added) {
        added = added + 1U;
    }
    if (tools.add_inspect_section("layout",
                                  "why the target has its place and size: requested and arranged bounds, its layout "
                                  "properties, what its parent gives it, tight constraints and overflow records",
                                  &layout_section, nullptr) == ToolRegistration::added) {
        added = added + 1U;
    }
    if (tools.add_inspect_section("resources", "the window's images and the counted allocations by subsystem",
                                  &resources_section, nullptr) == ToolRegistration::added) {
        added = added + 1U;
    }
    return added;
}

} // namespace gui_forms::automation
