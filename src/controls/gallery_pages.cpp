#include "gallery_pages.hpp"

#include "gallery.hpp"
#include "gallery/pages/pages.hpp"
#include "gallery_page_specs.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui_forms::gallery {

using namespace pages;

namespace {

using namespace std::chrono_literals;

struct NavigationEntry final {
    std::string_view group;
    const GalleryPageSpec* page{};
};

// Pages in navigation order: catalog categories in registry order, then the
// Labs group; within a group, gallery_pages order.
std::vector<NavigationEntry> navigation_order() {
    std::vector<NavigationEntry> order;
    order.reserve(gallery_pages.size());
    for (const std::string_view category : catalog_categories) {
        for (const GalleryPageSpec& page : gallery_pages) {
            if (gallery_page_group(page) == category) {
                order.push_back({category, &page});
            }
        }
    }
    for (const GalleryPageSpec& page : gallery_pages) {
        if (gallery_page_group(page) == labs_group) {
            order.push_back({labs_group, &page});
        }
    }
    return order;
}

std::string page_control_line(const CatalogEntry& entry) {
    return std::string(entry.type) + " · " + std::string(entry.header) +
           " · WinForms: " + std::string(entry.winforms);
}

// Fills one page body: the form document's pages mount their group; the
// pages migrated from the Complete Showcase keep their builders unchanged.
void populate_page_body(std::string_view page_id,
                        const std::shared_ptr<Surface>& body,
                        const std::shared_ptr<PageContext>& context,
                        const GalleryTree& compiled) {
    for (const GalleryMount& mount : compiled.mounts) {
        if (!mount.page.empty() && mount.page == page_id) {
            (*body).add_at(mount.control, {0.0, 0.0, mount.size.width, mount.size.height});
            return;
        }
    }
    if (page_id == "control-spectrum") add_control_spectrum(body, context);
    else if (page_id == "ranges-and-progress") add_ranges(body, context);
    else if (page_id == "containers-and-focus") add_containers(body, context);
    else if (page_id == "animation-laboratory") add_animation(body, context);
    else if (page_id == "states-and-diagnostics") add_states(body, context);
    else if (page_id == "text-and-input") add_text_input(body, context);
    else if (page_id == "collections-and-overlays") add_collections(body, context);
    else if (page_id == "values-and-spinners") add_values(body, context);
    else if (page_id == "images-and-drawing") add_images_and_drawing(body, context);
    else if (page_id == "tabs-and-pages") add_tabs_and_pages(body, context);
    else if (page_id == "checked-collections") add_checked_collections(body, context);
    else if (page_id == "timing-and-tooltips") add_timing_and_tooltips(body, context);
    else if (page_id == "dates-and-calendar") add_dates_and_calendar(body, context);
    else if (page_id == "dialogs-and-host-services") add_dialogs_and_host_services(body, context);
    else if (page_id == "retained-layout-panels") add_layout_panels(body, context);
    else if (page_id == "css-layout-panels") add_css_layout_panels(body, context);
    else if (page_id == "dock-and-anchor") add_dock_and_anchor(body, context);
    else if (page_id == "docking-workspace") add_docking_workspace(body, context);
    else if (page_id == "commands-and-menus") add_commands_and_menus(body, context);
    else if (page_id == "tree-and-object-views") add_tree_and_object_views(body, context);
    else if (page_id == "choices-and-sections") add_choices_and_sections(body, context);
    else if (page_id == "value-editors") add_value_editors(body, context);
    else if (page_id == "responsive-layouts") add_responsive_layouts(body, context);
    else if (page_id == "anchored-overlays") add_anchored_overlays(body, context);
    else if (page_id == "live-surfaces") add_live_surfaces(body, context);
    else if (page_id == "typography-and-inspection") add_typography_and_inspection(body, context);
    else if (page_id == "semantic-feedback") add_semantic_feedback(body, context);
    else if (page_id == "notices-and-badges") add_notices_and_badges(body, context);
    else throw std::logic_error("Gallery page has neither a form-document mount nor a builder");
}

// The one place the retained page context is recovered from the root Tag.
std::shared_ptr<PageContext> retained_page_context(const Window& window) {
    const std::shared_ptr<gui_forms::ScaledPanel> root =
        std::dynamic_pointer_cast<ScaledPanel>(window.root());
    if (!root) return nullptr;
    const std::shared_ptr<PageContext>* const retained =
        std::any_cast<std::shared_ptr<PageContext>>(&(*root).tag());
    if (retained == nullptr) return nullptr;
    return *retained;
}

// The shell's own handlers: navigation rows and the live-motion toggle. It
// borrows the page context weakly; the context's ComponentContainer owns it.
class ShellActions final : public Component {
public:
    explicit ShellActions(std::weak_ptr<PageContext> context) noexcept
        : context_(std::move(context)) {}

    void navigate(const int index) {
        if (const std::shared_ptr<PageContext> context = context_.lock()) {
            (*context).select_page(static_cast<std::size_t>(index));
        }
    }

    void live_motion(const bool enabled) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context) return;
        (*context).live_motion = enabled;
        (*context).apply_motion_policy();
    }

private:
    std::weak_ptr<PageContext> context_;
};

} // namespace

GalleryShell build_gallery_shell(const GalleryTree& compiled) {
    std::shared_ptr<PageContext> context = std::make_shared<PageContext>();
    (*context).compiled = compiled.context;
    // Pages capture the status line while they are built, so it exists first;
    // the status strip adopts it below.
    (*context).status = label("gallery.status.text", "", 11.0, 600, shell_blue_dark);
    std::shared_ptr<gui_forms::ScaledPanel> root = make_control<ScaledPanel>(
        StableId("gallery.shell"), gallery_default_size);
    (*root).set_background(canvas);
    (*root).set_tag(context);

    // The form document's command strip: Reset, Diagnostics toggle, status badge.
    if (const Control::Ptr strip = compiled.mount("gallery.command-strip")) {
        (*root).add_at(strip, {0.0, 0.0, 1280.0, 34.0});
    }

    const std::vector<NavigationEntry> order = navigation_order();

    // The navigation lists every registry page, more than fit in one column,
    // so the sidebar scrolls; select_page keeps the chosen row in view. It
    // runs down to the window's bottom edge, beside the status strip (sweep
    // defect D20: it stopped at the strip's top, leaving a blank block).
    constexpr double sidebar_height = 786.0;
    std::shared_ptr<gui_forms::ScrollableControl> sidebar = make_control<ScrollableControl>(
        StableId("gallery.sidebar"));
    (*sidebar).auto_scroll = true;
    (*root).add_at(sidebar, {0.0, 34.0, 238.0, sidebar_height});
    // First child, so it paints beneath the rows; sized to the content below.
    std::shared_ptr<gui_forms::Panel> sidebar_paper =
        make_control<Panel>(StableId("gallery.sidebar.paper"));
    (*sidebar_paper).set_background(Color::rgba(220, 229, 237));
    (*sidebar).add_child(sidebar_paper);
    double navigation_y = 8.0;
    std::string_view current_group;
    std::size_t group_index = 0;
    for (const NavigationEntry& entry : order) {
        if (entry.group != current_group) {
            current_group = entry.group;
            std::shared_ptr<Label> heading = label(
                "gallery.sidebar.group." + std::to_string(group_index),
                std::string(entry.group), 10.0, 700, shell_blue_dark);
            (*heading).font = FontSpec{FontRole::control, 10.0, 700, false, 0.24};
            (*heading).set_bounds({18.0, navigation_y, 180.0, 16.0});
            (*sidebar).add_child(heading);
            navigation_y += 18.0;
            ++group_index;
        }
        std::shared_ptr<gui_forms::Button> navigation = make_control<Button>(
            StableId(std::string(navigation_id_prefix) + std::string((*entry.page).id)),
            std::string((*entry.page).title));
        (*navigation).font = {FontRole::control, 12.0, 400, false, 0.24};
        (*navigation).set_visual_style(ButtonVisualStyle::command);
        (*navigation).set_style(page_style());
        (*navigation).set_bounds({16.0, navigation_y, 190.0, 24.0});
        (*sidebar).add_child(navigation);
        navigation_y += 24.0;
        (*context).navigation.push_back(navigation);
        (*context).page_ids.push_back((*entry.page).id);
        (*context).page_titles.push_back((*entry.page).title);
        if ((*entry.page).id == "timing-and-tooltips") {
            (*context).timing_page = (*context).page_ids.size() - 1U;
        }
    }
    // Narrower than the viewport beside the vertical bar, so no horizontal
    // scrollbar appears; the column's own background shows at the edge.
    (*sidebar_paper).set_bounds({0.0, 0.0, 206.0, std::max(sidebar_height, navigation_y + 8.0)});
    (*sidebar).auto_scroll_min_size = {206.0, navigation_y + 8.0};

    std::shared_ptr<gui_forms::ScaledPanel> content = make_control<Surface>(
        StableId("gallery.content"), Size{1042.0, 742.0});
    (*content).set_background(canvas);
    (*root).add_at(content, {238.0, 34.0, 1042.0, 742.0});
    for (std::size_t index = 0; index < order.size(); ++index) {
        const GalleryPageSpec& spec = *order[index].page;
        const std::string surface_id =
            std::string(surface_id_prefix) + std::string(spec.id);
        std::vector<const CatalogEntry*> demonstrated;
        for (const CatalogEntry& entry : control_catalog) {
            if (entry.gallery_page == spec.id) demonstrated.push_back(&entry);
        }
        // Header strip: title, then one line per registry record this page
        // demonstrates (or the page's own summary when none names it), 16
        // units apart so a line's descenders and underscores stay inside its
        // own label (sweep defect D1).
        constexpr double header_line_pitch = 16.0;
        const std::size_t line_count = std::max<std::size_t>(1U, demonstrated.size());
        const double strip_height = 28.0 + header_line_pitch * static_cast<double>(line_count) + 8.0;
        // The page is drawn at its design size, unscaled, in a scrolling
        // viewport: a page taller than the viewport scrolls instead of being
        // squeezed vertically, which overprinted the header lines and the
        // title (sweep defect D2). Its height is known once the body holds
        // its content.
        std::shared_ptr<gui_forms::ScrollableControl> viewport = make_control<ScrollableControl>(
            StableId(surface_id + ".viewport"));
        (*viewport).auto_scroll = true;
        (*viewport).visible = index == 0U;
        (*content).add_at(viewport, {20.0, 0.0, 1022.0, 742.0});
        std::shared_ptr<gui_forms::ScaledPanel> page = make_control<Surface>(
            StableId(surface_id), Size{1000.0, strip_height + 660.0});
        (*page).set_background(canvas);
        (*viewport).add_child(page);
        std::shared_ptr<Label> title = label(surface_id + ".title", std::string(spec.title),
                                             15.0, 700, shell_blue_dark);
        (*title).font = FontSpec{FontRole::control, 15.0, 700, false};
        (*page).add_at(title, {0.0, 4.0, 1000.0, 22.0});
        if (demonstrated.empty()) {
            std::shared_ptr<Label> summary = label(surface_id + ".summary",
                                                   std::string(spec.summary), 10.0, 400, muted);
            (*page).add_at(summary, {0.0, 28.0, 1000.0, header_line_pitch});
        }
        for (std::size_t line = 0; line < demonstrated.size(); ++line) {
            std::shared_ptr<Label> control_line = label(
                surface_id + ".control." + std::to_string(line),
                page_control_line(*demonstrated[line]), 10.0, 400, muted);
            (*page).add_at(control_line,
                           {0.0, 28.0 + header_line_pitch * static_cast<double>(line), 1000.0,
                            header_line_pitch});
        }
        std::shared_ptr<gui_forms::ScaledPanel> body = make_control<Surface>(
            StableId(std::string(body_id_prefix) + std::string(spec.id)),
            Size{1000.0, 660.0});
        (*body).set_background(canvas);
        (*page).add_at(body, {0.0, strip_height, 1000.0, 660.0});
        populate_page_body(spec.id, body, context, compiled);
        // The body is as tall as its content (at least 660): content placed
        // below 660 was cut off with no way to reach it (sweep defect D3).
        double body_height = 660.0;
        for (const Control::Ptr& child : (*body).children()) {
            const std::optional<Rect> slot = (*body).design_bounds(*child);
            const Rect placed = slot ? *slot : (*child).requested_bounds();
            body_height = std::max(body_height, placed.y + placed.height + 16.0);
        }
        const double page_height = strip_height + body_height;
        (*body).design_size = Size{1000.0, body_height};
        (*page).set_design_bounds(*body, {0.0, strip_height, 1000.0, body_height});
        (*page).design_size = Size{1000.0, page_height};
        (*page).set_bounds({0.0, 0.0, 1000.0, page_height});
        (*viewport).auto_scroll_min_size = {1000.0, page_height};
        (*context).pages.push_back(viewport);
    }

    std::shared_ptr<gui_forms::ScaledPanel> status_surface = make_control<Surface>(
        StableId("gallery.status"), Size{1042.0, 44.0});
    (*status_surface).set_background(Color::rgba(225, 232, 238));
    (*root).add_at(status_surface, {238.0, 776.0, 1042.0, 44.0});
    std::shared_ptr<gui_forms::Panel> status_rule =
        make_control<Panel>(StableId("gallery.status.rule"));
    (*status_rule).set_background(rule);
    (*status_surface).add_at(status_rule, {0.0, 0.0, 1042.0, 1.0});
    (*status_surface).add_at((*context).status, {18.0, 10.0, 800.0, 24.0});
    std::shared_ptr<gui_forms::CheckBox> live = make_control<CheckBox>(
        StableId("gallery.status.live"), "Live animation");
    (*live).set_checked(true);
    (*live).set_indicator_style(ChoiceIndicatorStyle::toggle);
    (*status_surface).add_at(live, {846.0, 8.0, 180.0, 28.0});

    // The form document's diagnostics overlay; hidden until the command toggles it.
    if (const Control::Ptr diagnostics = compiled.mount("gallery.diagnostics")) {
        (*root).add_at(diagnostics, {1000.0, 34.0, 280.0, 742.0});
    }

    const std::shared_ptr<ShellActions> actions = std::make_shared<ShellActions>(context);
    for (std::size_t index = 0; index < (*context).navigation.size(); ++index) {
        on((*(*context).navigation[index]).clicked(), *actions, &ShellActions::navigate,
           static_cast<int>(index));
    }
    on((*live).checked_changed(), *actions, &ShellActions::live_motion);
    (*context).components.add(actions);
    (*context).apply_motion_policy();
    (*context).select_page(0U);
    return {root};
}

bool select_gallery_page(Window& window, std::string_view page_id) {
    const std::shared_ptr<PageContext> context = retained_page_context(window);
    if (!context) return false;
    for (std::size_t index = 0; index < (*context).page_ids.size(); ++index) {
        if ((*context).page_ids[index] == page_id) {
            (*context).select_page(index);
            return true;
        }
    }
    return false;
}

std::string_view selected_gallery_page(const Window& window) {
    const std::shared_ptr<PageContext> context = retained_page_context(window);
    if (!context || (*context).selected_page >= (*context).page_ids.size()) return {};
    return (*context).page_ids[(*context).selected_page];
}

namespace {

// Window-bound runtime handlers (binding source, providers, tooltips, the
// shell Timer tick). Created once the Window exists; the page context's
// ComponentContainer owns it, so disposing the context revokes every handler.
class RuntimeActions final : public Component {
public:
    explicit RuntimeActions(std::weak_ptr<PageContext> context) noexcept
        : context_(std::move(context)) {}

    void set_persistent_tooltip_target(std::weak_ptr<Button> target) noexcept {
        persistent_ = std::move(target);
    }

    void validate_profile_name(BindingConvertEvent& event) {
        const std::string* text = std::get_if<std::string>(&event.value);
        if (text == nullptr || (*text).empty()) {
            throw std::invalid_argument("Profile name may not be empty");
        }
    }

    void update_binding_status() {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context || !(*context).binding_source || !(*context).binding_status) return;
        const BindingSourceSnapshot snapshot = (*(*context).binding_source).snapshot();
        (*(*context).binding_status).text = "Current " + std::to_string(snapshot.position + 1) + " / " +
            std::to_string(snapshot.count) + " · " + snapshot.current_stable_id +
            " · text commits on validation · revision " + std::to_string(snapshot.revision);
    }

    // Bound value: -1 moves to the previous record, +1 to the next.
    void navigate_binding(const int direction) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context || !(*context).binding_source) return;
        if (direction < 0) static_cast<void>((*(*context).binding_source).move_previous());
        else static_cast<void>((*(*context).binding_source).move_next());
    }

    void help_requested(HelpRequestEvent& request) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context) return;
        (*(*context).status).text = "Help · " + request.help_string;
        request.handled = true;
    }

    void ribbon_help_requested(const RibbonToolTipHelpRequest& request) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context) return;
        (*(*context).status).text = "Help topic · " + request.help_topic;
    }

    void toggle_provider_error() {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context) return;
        if ((*(*context).error_provider).error(*(*context).provider_error_target).empty()) {
            (*(*context).error_provider).set_error(
                (*context).provider_error_target,
                "Endpoint is intentionally marked invalid for provider dogfood.");
            (*(*context).provider_error_toggle).text = "Clear";
        } else {
            (*(*context).error_provider).set_error((*context).provider_error_target, {});
            (*(*context).provider_error_toggle).text = "Restore";
        }
    }

    void timer_tick() {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context) return;
        ++(*context).timer_ticks;
        const double cycle = static_cast<double>((*context).timer_ticks % 20U) / 19.0;
        (*(*context).timer_progress).set_value(cycle * 100.0);
        constexpr double travel = 98.0;
        const double eased = 0.5 - std::cos(cycle * 6.283185307179586) * 0.5;
        (*(*context).timer_motion).set_design_bounds(
            *(*context).timer_motion_target, {14.0 + travel * eased, 39.0, 168.0, 34.0});
        (*(*context).timer_status).text = "Running · tick " + std::to_string((*context).timer_ticks) + " · " +
            std::to_string(static_cast<int>((*(*context).ui_timer).interval().count())) +
            " ms · UI thread";
    }

    void show_persistent_tooltip() {
        const std::shared_ptr<PageContext> context = context_.lock();
        const std::shared_ptr<Button> target = persistent_.lock();
        if (!context || !target) return;
        (*(*context).tooltips).show(target, 0ms);
    }

    void show_disabled_tooltip() {
        if (const std::shared_ptr<PageContext> context = context_.lock()) {
            (*(*context).tooltips).show((*context).tooltip_disabled_target, 0ms);
        }
    }

private:
    std::weak_ptr<PageContext> context_;
    std::weak_ptr<Button> persistent_;
};

} // namespace

void initialize_gallery_runtime(Window& window) {
    const std::shared_ptr<PageContext> context = retained_page_context(window);
    if (!context) {
        throw std::logic_error("gallery runtime requires a retained context tag");
    }
    if ((*context).ui_timer) return;
    const std::shared_ptr<RuntimeActions> actions = std::make_shared<RuntimeActions>(context);
    (*context).components.add(actions);

    (*context).ui_timer = std::make_shared<Timer>(window, 250ms);
    (*context).tooltips = std::make_shared<ToolTip>(window);
    (*context).ribbon_tooltips = std::make_shared<RibbonToolTip>(window);
    (*context).error_provider = std::make_shared<ErrorProvider>(window);
    (*context).help_provider = std::make_shared<HelpProvider>(window);
    (*context).binding_source = std::make_shared<BindingSource>(window);
    (*context).components.add((*context).ui_timer);
    (*context).components.add((*context).tooltips);
    (*context).components.add((*context).ribbon_tooltips);
    (*context).components.add((*context).error_provider);
    (*context).components.add((*context).help_provider);
    (*context).components.add((*context).binding_source);
    (*(*context).binding_source).set_records({
        {"profile.local", {{"name", std::string("Local index")},
                            {"enabled", true}, {"gain", 28.0}}},
        {"profile.archive", {{"name", std::string("Archive review")},
                              {"enabled", false}, {"gain", 61.0}}},
        {"profile.plugins", {{"name", std::string("Plugin surfaces")},
                              {"enabled", true}, {"gain", 84.0}}},
    });
    if ((*context).binding_navigator) {
        (*(*context).binding_navigator).set_binding_source((*context).binding_source);
    }
    BindingOptions validated_binding;
    validated_binding.formatting_enabled = true;
    validated_binding.data_source_update_mode =
        DataSourceUpdateMode::on_validation;
    const std::shared_ptr<Binding> name_binding = (*(*context).binding_editor).data_bindings().add(
        "text", (*context).binding_source, "name", validated_binding);
    on((*name_binding).parse(), *actions, &RuntimeActions::validate_profile_name);
    BindingOptions immediate_binding;
    immediate_binding.data_source_update_mode =
        DataSourceUpdateMode::on_property_changed;
    static_cast<void>((*(*context).binding_enabled).data_bindings().add(
        "checked", (*context).binding_source, "enabled", immediate_binding));
    static_cast<void>((*(*context).binding_gain).data_bindings().add(
        "value", (*context).binding_source, "gain", immediate_binding));
    (*(*context).tooltips).initial_delay = 420ms;
    (*(*context).tooltips).reshow_delay = 90ms;
    (*(*context).tooltips).auto_pop_delay = 4000ms;
    (*(*context).tooltips).show_always = true;
    (*(*context).error_provider).set_blink_style(ErrorBlinkStyle::never_blink);
    (*(*context).error_provider).bind_to_data_and_errors((*context).binding_source);
    (*(*context).error_provider).set_error(
        (*context).provider_error_target,
        "Endpoint is intentionally marked invalid for provider dogfood.");
    (*(*context).help_provider).set_help_string(
        (*context).provider_error_target,
        "Enter a host and port, then clear validation after the endpoint is accepted.");
    (*(*context).help_provider).set_help_keyword(
        (*context).provider_error_target, "network.endpoint");

    const Control::Ptr hover = window.find("gallery.timing.tooltip.hover");
    const Control::Ptr keyboard = window.find("gallery.timing.tooltip.focus");
    const std::shared_ptr<gui_forms::Button> persistent = std::dynamic_pointer_cast<Button>(
        window.find("gallery.timing.tooltip.persistent"));
    (*(*context).tooltips).set_tool_tip(
        hover, "Retained hover help is delayed, cancellable, clipped to the client, and input-transparent.");
    // Tooltip text belongs to the control; the ToolTip provider is only the
    // Windows Forms way to set it.
    (*keyboard).tool_tip_text = "Keyboard focus shows the same text, below the control, without changing its accessible name.";
    (*(*context).tooltips).set_tool_tip(
        persistent, "Click to hold this tooltip open until another interaction dismisses it.");
    (*(*context).tooltips).set_tool_tip(
        (*context).tooltip_disabled_target,
        "This action is disabled because no compatible device is selected.");
    (*(*context).tooltips).set_tool_tip(
        (*context).timer_motion_target,
        "The overlay re-anchors when this retained target moves on UI Timer ticks.");
    for (const std::shared_ptr<gui_forms::Button>& navigation : (*context).navigation) {
        (*(*context).tooltips).set_tool_tip(
            navigation, "Open the " + (*navigation).text.read() + " Gallery page.");
    }

    // Enhanced tooltips on the commands page's ToolStrip items: title,
    // description and shortcut come from each item's Command; Save shows
    // its availability reason while the document is clean.
    const std::shared_ptr<ToolStrip> document_tools =
        std::dynamic_pointer_cast<ToolStrip>(window.find("gallery.commands.tool-strip"));
    if (document_tools) {
        (*(*context).ribbon_tooltips).set_item_tool_tip(document_tools, "strip.new",
                                                        {.help_topic = "gallery.commands.new"});
        (*(*context).ribbon_tooltips).set_item_tool_tip(
            document_tools, "strip.save",
            {.disabled_reason = "Nothing to save: the document has no changes.",
             .help_topic = "gallery.commands.save"});
        (*(*context).ribbon_tooltips).set_item_tool_tip(document_tools, "strip.wrap", {});
    }
    (*actions).set_persistent_tooltip_target(persistent);
    on((*(*context).binding_source).current_changed(), *actions, &RuntimeActions::update_binding_status);
    on((*(*context).binding_source).current_item_changed(), *actions,
       &RuntimeActions::update_binding_status);
    on((*(*context).binding_previous).clicked(), *actions, &RuntimeActions::navigate_binding, -1);
    on((*(*context).binding_next).clicked(), *actions, &RuntimeActions::navigate_binding, 1);
    (*actions).update_binding_status();
    on((*(*context).help_provider).help_requested(), *actions, &RuntimeActions::help_requested);
    on((*(*context).ribbon_tooltips).help_requested(), *actions, &RuntimeActions::ribbon_help_requested);
    on((*(*context).provider_error_toggle).clicked(), *actions, &RuntimeActions::toggle_provider_error);
    // The timing page's own Commands and Value drive start, stop and interval;
    // the shell keeps only the tick, since the Timer needs the Window.
    on((*(*context).ui_timer).tick(), *actions, &RuntimeActions::timer_tick);
    on((*persistent).clicked(), *actions, &RuntimeActions::show_persistent_tooltip);
    on((*(*context).tooltip_show_disabled).clicked(), *actions, &RuntimeActions::show_disabled_tooltip);
    (*context).select_page((*context).selected_page);
}

} // namespace gui_forms::gallery
