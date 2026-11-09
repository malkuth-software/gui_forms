#include "support.hpp"

#include "gui_forms/controls/instruments/sparkline/sparkline.hpp"
#include "gui_forms/controls/plots/plot_view/plot_view.hpp"
#include "gui_forms/controls/range_control/value_bar/value_bar.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace gallery_pages_tests {
namespace {

// Registry ids whose controls do not yet declare their own anatomy (stage
// T1). The coverage test fails when this list and the Gallery disagree in
// either direction, so a control that gains an anatomy must leave the list
// in the same change. The T1 gate is this list empty.
constexpr std::array<std::string_view, 0> anatomy_pending{
};

// Registry entries that are not controls: nonvisual components and the
// LiveSurface service type. They have no parts; a component that shows
// something (a context menu, a tool tip) does so through an internal control.
constexpr std::array<std::string_view, 27> nonvisual_entries{
    "command",
    "gallery_command",
    "font_command",
    "color_command",
    "context_menu",
    "tool_tip",
    "ribbon_tool_tip",
    "timer",
    "background_worker",
    "notify_icon",
    "open_file_dialog",
    "save_file_dialog",
    "color_dialog",
    "folder_browser_dialog",
    "thread_exception_dialog",
    "image_list",
    "error_provider",
    "help_provider",
    "semantic_feedback",
    "binding",
    "binding_source",
    "binding_context",
    "live_surface",
    "dock_workspace",
    // A PlotView's item children declare its axes, series and annotations;
    // the PlotView paints them as its own parts.
    "plot_axis_item",
    "plot_series_item",
    "plot_annotation_item",
};

// True when every state a part publishes is one its declaration lists.
bool part_states_declared(const PartDeclaration& part, const PartGeometry& published) {
    if (published.states.custom_bits() != 0U) return false;
    for (std::size_t index = 0; index < anatomy_state_count; ++index) {
        const AnatomyState state = static_cast<AnatomyState>(index);
        if (published.states.contains(state) && !declares_part_state(part, state)) return false;
    }
    return true;
}

// A part's name and role, followed by the states it takes on its own.
std::string describe_part(const PartDeclaration& part) {
    std::string text = std::string(part.name) + " (" + std::string(part_role_name(part.role));
    for (std::size_t index = 0; index < part.states.size(); ++index) {
        text += index == 0U ? "; " : ", ";
        text += std::string(anatomy_state_name(part.states[index]));
    }
    text += ")";
    return text;
}

// Prints one control type's declared anatomy as a table row: id, parts
// with roles, states, and the item anatomy when there is one.
void print_anatomy_row(const ControlAnatomy& anatomy) {
    std::string parts;
    for (std::size_t index = 0; index < anatomy.parts.size(); ++index) {
        if (index > 0U) parts += ", ";
        parts += describe_part(anatomy.parts[index]);
    }
    std::string states;
    for (std::size_t index = 0; index < anatomy.states.size(); ++index) {
        if (index > 0U) states += ", ";
        states += std::string(anatomy_state_name(anatomy.states[index]));
    }
    for (std::size_t index = 0; index < anatomy.custom_states.size(); ++index) {
        states += ", " + std::string(anatomy.custom_states[index].name) + " (custom)";
    }
    // Each item system after the first is introduced by its name.
    std::string items;
    std::string item_states;
    for (std::size_t system_index = 0; system_index < anatomy.item_systems.size(); ++system_index) {
        const ItemSystemDeclaration& system = anatomy.item_systems[system_index];
        if (system_index > 0U) {
            items += "; " + std::string(system.name) + ": ";
            item_states += "; " + std::string(system.name) + ": ";
        }
        for (std::size_t index = 0; index < system.parts.size(); ++index) {
            if (index > 0U) items += ", ";
            items += describe_part(system.parts[index]);
        }
        for (std::size_t index = 0; index < system.states.size(); ++index) {
            if (index > 0U) item_states += ", ";
            item_states += std::string(anatomy_state_name(system.states[index]));
        }
        for (std::size_t index = 0; index < system.custom_states.size(); ++index) {
            item_states += ", " + std::string(system.custom_states[index].name) + " (custom)";
        }
    }
    std::cout << "| " << anatomy.control_id << " | " << parts << " | " << states << " | "
              << items << " | " << item_states << " |\n";
}

bool finite_rect(Rect rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height);
}

bool contains_id(std::span<const std::string> ids, std::string_view id) {
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index] == id) return true;
    }
    return false;
}

bool pending(std::string_view id) {
    for (std::size_t index = 0; index < anatomy_pending.size(); ++index) {
        if (anatomy_pending[index] == id) return true;
    }
    return false;
}

bool nonvisual(std::string_view id) {
    for (std::size_t index = 0; index < nonvisual_entries.size(); ++index) {
        if (nonvisual_entries[index] == id) return true;
    }
    return false;
}

// Checks one control's published anatomy and records its declaring id.
void inspect_control(const Control& control, std::vector<std::string>& seen_ids) {
    const ControlAnatomy& anatomy = control.anatomy();
    const AnatomyValidation validation = validate_control_anatomy(anatomy);
    if (validation.defect != AnatomyDefect::none) {
        throw std::runtime_error(std::string(anatomy.control_id) +
                                 " anatomy is malformed: " +
                                 std::string(anatomy_defect_name(validation.defect)));
    }
    std::vector<PartGeometry> geometry(anatomy.parts.size());
    control.fill_part_geometry(geometry);
    for (std::size_t index = 0; index < geometry.size(); ++index) {
        const PartGeometry& part = geometry[index];
        if (!finite_rect(part.bounds) || (!part.present && !part.bounds.empty())) {
            throw std::runtime_error(std::string(anatomy.control_id) + " part " +
                                     std::string(anatomy.parts[index].name) +
                                     " publishes invalid geometry");
        }
        if (!part_states_declared(anatomy.parts[index], part)) {
            throw std::runtime_error(std::string(anatomy.control_id) + " part " +
                                     std::string(anatomy.parts[index].name) +
                                     " publishes an undeclared part state");
        }
    }
    // Every control raises Control's events and validates its own list.
    const std::vector<InteractionDeclaration> interactions = control.interactions();
    const InteractionValidation interaction_validation = validate_interactions(interactions);
    if (interaction_validation.defect != InteractionDefect::none) {
        throw std::runtime_error(std::string(anatomy.control_id) + " interactions are malformed: " +
                                 std::string(interaction_defect_name(interaction_validation.defect)));
    }
    if (find_interaction(interactions, "visible_changed") == interactions.size()) {
        throw std::runtime_error(std::string(anatomy.control_id) +
                                 " does not inherit Control's interactions");
    }
    const AnatomyStateSet active = control.anatomy_states();
    for (std::size_t index = 0; index < anatomy_state_count; ++index) {
        const AnatomyState state = static_cast<AnatomyState>(index);
        if (active.contains(state) && !declares_state(anatomy, state)) {
            throw std::runtime_error(std::string(anatomy.control_id) +
                                     " publishes undeclared state " +
                                     std::string(anatomy_state_name(state)));
        }
    }
    // Every realized item: its system, identity, geometry and states.
    const std::size_t items = control.realized_item_count();
    std::vector<PartGeometry> item_geometry;
    for (std::size_t item = 0; item < items; ++item) {
        if (control.realized_item_stable_id(item).empty()) {
            throw std::runtime_error(std::string(anatomy.control_id) +
                                     " publishes an item without a stable ID");
        }
        const ItemSystemIndex system_index = control.realized_item_system(item);
        if (system_index.value >= anatomy.item_systems.size()) {
            throw std::runtime_error(std::string(anatomy.control_id) +
                                     " publishes an item of an undeclared item system");
        }
        const ItemSystemDeclaration& system = anatomy.item_systems[system_index.value];
        item_geometry.assign(system.parts.size(), PartGeometry{});
        control.fill_realized_item_geometry(item, item_geometry);
        for (std::size_t index = 0; index < item_geometry.size(); ++index) {
            const PartGeometry& part = item_geometry[index];
            if (!finite_rect(part.bounds) || (!part.present && !part.bounds.empty())) {
                throw std::runtime_error(std::string(anatomy.control_id) + " item part " +
                                         std::string(system.parts[index].name) +
                                         " publishes invalid geometry");
            }
            if (!part_states_declared(system.parts[index], part)) {
                throw std::runtime_error(std::string(anatomy.control_id) + " item part " +
                                         std::string(system.parts[index].name) +
                                         " publishes an undeclared part state");
            }
        }
        const AnatomyStateSet item_states = control.realized_item_anatomy_states(item);
        const unsigned int declared_custom =
            (1U << static_cast<unsigned int>(system.custom_states.size())) - 1U;
        if ((static_cast<unsigned int>(item_states.custom_bits()) & ~declared_custom) != 0U) {
            throw std::runtime_error(std::string(anatomy.control_id) +
                                     " publishes an undeclared item custom state");
        }
        for (std::size_t index = 0; index < anatomy_state_count; ++index) {
            const AnatomyState state = static_cast<AnatomyState>(index);
            if (item_states.contains(state) && !declares_item_state(system, state)) {
                throw std::runtime_error(std::string(anatomy.control_id) +
                                         " publishes undeclared item state " +
                                         std::string(anatomy_state_name(state)));
            }
        }
    }
    const std::string id(anatomy.control_id);
    if (!contains_id(seen_ids, id)) {
        seen_ids.push_back(id);
        // GUI_FORMS_ANATOMY_TABLE=1 prints the coverage table the T1 gate
        // record keeps under notes:results/.
        const char* table = std::getenv("GUI_FORMS_ANATOMY_TABLE");
        if (table != nullptr && std::string_view(table) == "1") {
            print_anatomy_row(anatomy);
        }
    }
}

void inspect_tree(const Control::Ptr& root, std::vector<std::string>& seen_ids,
                  std::size_t& inspected) {
    std::vector<Control::Ptr> pending_controls{root};
    while (!pending_controls.empty()) {
        const Control::Ptr control = pending_controls.back();
        pending_controls.pop_back();
        inspect_control(*control, seen_ids);
        ++inspected;
        const std::span<const Control::Ptr> children = (*control).children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            pending_controls.push_back(children[index]);
        }
    }
}

} // namespace

// T1 gate test: lay out every Gallery page, read every control's part
// geometry and states, and compare the registry against the controls that
// publish their own anatomy.
void test_anatomy_coverage() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    std::vector<std::string> seen_ids;
    std::size_t inspected = 0;
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        require(gallery::select_gallery_page(window, page.id),
                "every Gallery page must be selectable");
        window.perform_layout();
        inspect_tree(window.root(), seen_ids, inspected);
    }
    // Registry types the Gallery does not attach to its root tree: base
    // types reached only through subclasses, and overlays.
    const std::shared_ptr<ButtonBase> button_base =
        make_control<ButtonBase>(StableId("anatomy.coverage.button-base"), "Base");
    const std::shared_ptr<ScrollBar> scroll_bar =
        make_control<ScrollBar>(StableId("anatomy.coverage.scroll-bar"));
    // Overlays and dialogs open only on demand and live outside the root tree.
    const std::shared_ptr<AnchoredOverlayLayer> overlay_layer = make_control<AnchoredOverlayLayer>(
        StableId("anatomy.coverage.anchored-overlay-layer"), button_base,
        AnchoredOverlayPlacement{});
    const std::shared_ptr<RangeControl> range_control =
        make_control<RangeControl>(StableId("anatomy.coverage.range-control"));
    const std::shared_ptr<TaskDialog> task_dialog =
        make_control<TaskDialog>(StableId("anatomy.coverage.task-dialog"));
    inspect_control(*button_base, seen_ids);
    inspect_control(*scroll_bar, seen_ids);
    inspect_control(*overlay_layer, seen_ids);
    inspect_control(*range_control, seen_ids);
    // A dialog opens on demand, outside the root tree.
    const std::shared_ptr<FontDialog> font_dialog =
        make_control<FontDialog>(StableId("anatomy.coverage.font-dialog"));
    inspect_control(*font_dialog, seen_ids);
    inspect_control(*task_dialog, seen_ids);
    // A toast shows only on demand, in its window's toast overlay.
    ToastContent toast_content{};
    toast_content.message = "Anatomy coverage";
    toast_content.action_text = "Undo";
    const std::shared_ptr<Toast> toast =
        make_control<Toast>(StableId("anatomy.coverage.toast"), std::move(toast_content));
    inspect_control(*toast, seen_ids);
    inspected += 6U;
    // Controls that have no Gallery page yet (the plots and instruments
    // packages' Sparkline and PlotView, and the ValueBar): their anatomies are checked on instances made here until a
    // page shows them.
    const std::shared_ptr<Sparkline> sparkline = make_control<Sparkline>(StableId("anatomy.coverage.sparkline"));
    const std::shared_ptr<PlotView> plot_view = make_control<PlotView>(StableId("anatomy.coverage.plot-view"));
    const std::shared_ptr<ValueBar> value_bar = make_control<ValueBar>(StableId("anatomy.coverage.value-bar"));
    inspect_control(*sparkline, seen_ids);
    inspect_control(*plot_view, seen_ids);
    inspect_control(*value_bar, seen_ids);
    inspected += 3U;

    std::string unexpected_gaps;
    std::string stale_pending;
    std::size_t covered = 0;
    std::size_t controls = 0;
    for (const gallery::CatalogEntry& entry : gallery::control_catalog) {
        if (nonvisual(entry.id)) continue;
        ++controls;
        const bool declared = contains_id(seen_ids, entry.id);
        if (declared) ++covered;
        if (!declared && !pending(entry.id)) {
            unexpected_gaps += " " + std::string(entry.id);
        }
        if (declared && pending(entry.id)) {
            stale_pending += " " + std::string(entry.id);
        }
    }
    std::cout << "anatomy coverage: " << covered << " of " << controls
              << " registry controls (" << nonvisual_entries.size()
              << " nonvisual entries excluded); "
              << inspected << " control instances inspected\n";
    if (!unexpected_gaps.empty()) {
        throw std::runtime_error("registry controls without their own anatomy on a "
                                 "laid-out Gallery page:" + unexpected_gaps);
    }
    if (!stale_pending.empty()) {
        throw std::runtime_error("anatomy_pending lists controls that now declare "
                                 "an anatomy; remove them:" + stale_pending);
    }
}

} // namespace gallery_pages_tests
