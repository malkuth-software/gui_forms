#include "gallery_pages.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/binding/binding/binding.hpp"
#include "gui_forms/binding/binding_schema/binding_schema.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/binding/binding_table/binding_table.hpp"
#include "gui_forms/binding/control_bindings_collection/control_bindings_collection.hpp"
#include "gui_forms/binding/types/binding_contract_types.hpp"
#include "gui_forms/components/error_provider/error_provider.hpp"
#include "gui_forms/components/error_provider/error_provider_values/error_provider_values.hpp"
#include "gui_forms/components/help_provider/help_provider.hpp"
#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp"
#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/controls/list_view.hpp"
#include "gui_forms/controls/list_view_values.hpp"
#include "gui_forms/controls/menu_strip.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/controls/ribbon.hpp"
#include "gui_forms/controls/scaled_panel.hpp"
#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/rules/rule_set/rule_set.hpp"
#include "gui_forms/timer/timer/timer.hpp"

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
#include <stxlib/optional.hpp>
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

// One of the window form's own pages: its group, moved into the page body
// at its design size.
struct FormPage final {
    std::string_view page{};
    Control::Ptr control;
    Size size{};
};

// Fills one page body: a form page mounts its group; the other pages run
// their builder from the page list.
void populate_page_body(std::string_view page_id,
                        const std::shared_ptr<Surface>& body,
                        const std::shared_ptr<PageContext>& context,
                        const std::array<FormPage, 4>& form_pages) {
    for (const FormPage& form_page : form_pages) {
        if (form_page.page == page_id) {
            (*body).add_at(form_page.control, {0.0, 0.0, form_page.size.width, form_page.size.height});
            return;
        }
    }
    for (const PageBuilderEntry& builder : page_builders) {
        if (builder.id == page_id) {
            builder.build(body, context);
            return;
        }
    }
    throw std::logic_error("Gallery page has neither a form page nor a builder");
}

// Arms `rule_action` on `trigger` in `rules`; a refusal is a defect of the
// shell's own wiring.
void arm(RuleSet& rules, const RuleTrigger& trigger, PageContext& context,
         void (*rule_action)(PageContext&, const RuleFiring&)) {
    const RuleAddResult added = on(rules, trigger, context, rule_action);
    if (added.refusal != RuleRefusal::none) {
        throw std::logic_error("a Gallery shell rule was refused: " +
                               std::string(rule_refusal_name(added.refusal)));
    }
}

// The one place the retained page context is recovered from the root Tag.
// The shell's handlers (a Component, the documented handler owner): a
// navigation row's activation opens its page; F6 and Shift+F6 on the
// window's key_down (its KeyPreview) cycle the shell's panes (owner ruling
// 3): the command strip, the navigation, the page area, the status strip,
// then the diagnostics while shown, each entered at its first Tab stop.
// Inside a focus region a page registered (the docking page's panes) F6
// stays the window's, cycling those regions; Tab leaves them. The context
// owns this component; the window outlives it.
class ShellActions final : public Component {
public:
    ShellActions(std::weak_ptr<PageContext> context, GalleryWindow& window) noexcept
        : context_(std::move(context)), window_(&window) {
        panes_ = {window.gallery_command_strip, window.gallery_sidebar, window.gallery_content,
                  window.gallery_status, window.gallery_diagnostics};
    }

    void navigation_activated(const ListViewItemActivation& activation) {
        const std::shared_ptr<PageContext> context = context_.lock();
        if (!context) return;
        (*context).select_page(activation.index);
    }

    void shell_key_down(KeyEventArgs& arguments) {
        if (arguments.handled || arguments.physical_key != PhysicalKey::f6) return;
        const bool forward = arguments.modifiers == Modifier::none;
        if (!forward && arguments.modifiers != Modifier::shift) return;
        const Control::Ptr focused = (*window_).focused_control();
        if (focused && in_page_region(*focused)) return;
        const std::size_t count = panes_.size();
        std::size_t current = count;
        for (std::size_t index = 0U; index < count && focused; ++index) {
            const Control::Ptr pane = panes_[index].lock();
            if (pane && holds(*pane, *focused)) current = index;
        }
        for (std::size_t step = 1U; step <= count; ++step) {
            std::size_t index = 0U;
            if (current == count) {
                index = forward ? step - 1U : count - step;
            } else {
                index = forward ? (current + step) % count : (current + count - step) % count;
            }
            const Control::Ptr pane = panes_[index].lock();
            if (!pane || !(*pane).eligible_for_input()) continue;
            const Control::Ptr first = first_tab_stop(pane);
            if (first && (*window_).request_focus(first)) {
                arguments.handled = true;
                return;
            }
        }
    }

private:
    // Whether `control` is inside a focus region a page registered that can
    // take input now (a hidden page's regions do not count).
    [[nodiscard]] bool in_page_region(const Control& control) const {
        const std::vector<Control::Ptr> regions = (*window_).focus_regions();
        for (const Control::Ptr& region : regions) {
            if (region && (*region).eligible_for_input() && holds(*region, control)) return true;
        }
        return false;
    }

    // Whether `control` is `pane` or below it.
    [[nodiscard]] static bool holds(const Control& pane, const Control& control) noexcept {
        const Control* walker = &control;
        while (walker != nullptr) {
            if (walker == &pane) return true;
            const Control::Ptr up = (*walker).parent();
            walker = up.get();
        }
        return false;
    }

    // The first focusable Tab stop under `root` in Tab order (siblings by
    // tab_index, then insertion), depth first; null when there is none.
    // Recurses once per control level: bounded by maximum_control_tree_depth.
    [[nodiscard]] static Control::Ptr first_tab_stop(const Control::Ptr& root) {
        if (!(*root).eligible_for_input()) return nullptr;
        if ((*root).focusable.read() && (*root).tab_stop.read()) return root;
        std::vector<Control::Ptr> children((*root).children().begin(), (*root).children().end());
        std::stable_sort(children.begin(), children.end(), &ShellActions::tab_order_less);
        for (const Control::Ptr& child : children) {
            Control::Ptr found = first_tab_stop(child);
            if (found) return found;
        }
        return nullptr;
    }

    [[nodiscard]] static bool tab_order_less(const Control::Ptr& left, const Control::Ptr& right) noexcept {
        return (*left).tab_index.read() < (*right).tab_index.read();
    }

    std::weak_ptr<PageContext> context_;
    GalleryWindow* window_{};
    std::array<std::weak_ptr<Control>, 5> panes_{};
};

std::shared_ptr<PageContext> retained_page_context(const Window& window) {
    const std::shared_ptr<gui_forms::Panel> root =
        std::dynamic_pointer_cast<Panel>(window.root());
    if (!root) return nullptr;
    const std::any held = (*root).tag.read();
    const std::shared_ptr<PageContext>* const retained =
        std::any_cast<std::shared_ptr<PageContext>>(&held);
    if (retained == nullptr) return nullptr;
    return *retained;
}

} // namespace

void build_gallery_shell(GalleryWindow& window, std::shared_ptr<GalleryContext> compiled) {
    std::shared_ptr<PageContext> context = std::make_shared<PageContext>();
    (*context).compiled = std::move(compiled);
    // Pages capture the status line while they are built; the form made it.
    (*context).status = window.gallery_status_text;
    (*(*context).status).font = stxlib::Optional<FontSpec>::of(FontSpec{FontRole::content, 11.0, 600, false});
    static_cast<void>((*(*context).status).add_class("heading"));
    // The form lays the shell out (demo/gallery.form): nothing scales with
    // the window, so a resize moves or resizes only the docked parts it
    // reaches and repaints only what changed (resize R-8).
    Panel& root = *window.gallery_shell;
    root.tag = context;

    // The Gallery's own pages leave their declared boundary for their
    // bodies; the boundary was never attached, so each group is attached
    // once, in its page body.
    GalleryFormPagesDocument<GalleryModel>& own_pages = (*(*context).compiled).pages;
    const std::array<FormPage, 4> form_pages{{
        {"basics", own_pages.gallery_backplane, {660.0, 200.0}},
        {"values", own_pages.gallery_values_group, {660.0, 116.0}},
        {"retained-rows", own_pages.gallery_collection_group, {660.0, 144.0}},
        {"instrument", own_pages.gallery_instrument_group, {660.0, 150.0}},
    }};
    Panel& boundary = *own_pages.gallery_form_pages;
    for (const FormPage& form_page : form_pages) {
        static_cast<void>(boundary.remove_child((*form_page.control).runtime_id()));
    }

    const std::vector<NavigationEntry> order = navigation_order();

    // The page navigation (owner rulings 2026-11-13): one ListView, one Tab
    // stop, a single details column without a header, one group per
    // category. Up, Down, Home and End move between pages without opening
    // one; a click (activation one_click), Enter or the accessible press
    // opens the page (item_activated, ShellActions::navigation_activated).
    // Rows keep the stable IDs gallery.page.<page>. The list fills the
    // sidebar, which runs down to the window's bottom edge beside the status
    // strip (sweep defect D20), and scrolls itself.
    ScrollableControl& sidebar = *window.gallery_sidebar;
    sidebar.auto_scroll = false;
    sidebar.auto_scroll_min_size = Size{};
    std::shared_ptr<ListView> navigation = make_control<ListView>(StableId("gallery.sidebar.pages"));
    (*navigation).accessible_name = "Pages";
    // Shell chrome: the control typography role (gallery.theme.css).
    static_cast<void>((*navigation).add_class("navigation-list"));
    (*navigation).multi_select = false;
    (*navigation).full_row_select = true;
    (*navigation).activation = ListViewActivation::one_click;
    (*navigation).header_style = ListViewHeaderStyle::none;
    (*navigation).set_columns({ListViewColumn{.stable_id = "page", .text = "Page", .width = 206.0}});
    std::vector<ListViewGroup> groups;
    std::vector<ListViewItem> rows;
    rows.reserve(order.size());
    std::string_view current_group;
    for (const NavigationEntry& entry : order) {
        if (entry.group != current_group) {
            current_group = entry.group;
            ListViewGroup group{};
            group.stable_id = "gallery.sidebar.group." + std::to_string(groups.size());
            group.header = std::string(entry.group);
            groups.push_back(std::move(group));
        }
        ListViewItem row{};
        row.stable_id = std::string(navigation_id_prefix) + std::string((*entry.page).id);
        row.text = std::string((*entry.page).title);
        row.group = groups.back().stable_id;
        rows.push_back(std::move(row));
        (*context).page_ids.push_back((*entry.page).id);
        (*context).page_titles.push_back((*entry.page).title);
        if ((*entry.page).id == "timing-and-tooltips") {
            (*context).timing_page = (*context).page_ids.size() - 1U;
        }
    }
    (*navigation).set_groups(std::move(groups));
    (*navigation).set_items(std::move(rows));
    (*navigation).dock = DockStyle::fill;
    sidebar.add_child(navigation);
    (*context).navigation = navigation;

    // Tab order follows reading order across the shell (owner ruling 2):
    // the command strip, the navigation, the page area, then the status
    // strip and the diagnostics. GUI.Forms orders Tab by tab_index, then
    // child order (as Windows Forms); the form puts the status strip before
    // the page area so that it docks first, so the shell sets the indices.
    (*window.gallery_command_strip).tab_index = 0U;
    (*window.gallery_sidebar).tab_index = 1U;
    (*window.gallery_content).tab_index = 2U;
    (*window.gallery_status).tab_index = 3U;
    (*window.gallery_diagnostics).tab_index = 4U;

    // The page area: each page's viewport is anchored to all four edges,
    // keeping its 20-unit left margin.
    const std::shared_ptr<gui_forms::Panel> content = window.gallery_content;
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
        (*viewport).set_requested_bounds({20.0, 0.0, 1022.0, 742.0});
        (*viewport).anchor = AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left | AnchorStyles::right;
        std::shared_ptr<gui_forms::ScaledPanel> page = make_control<Surface>(
            StableId(surface_id), Size{1000.0, strip_height + 660.0});
        // The shell canvas is a theme class: a familiar theme shows its window face.
        static_cast<void>((*page).add_class("shell-canvas"));
        (*viewport).add_child(page);
        std::shared_ptr<Label> title = label(surface_id + ".title", std::string(spec.title),
                                             15.0, 700, "heading");
        (*title).font = stxlib::Optional<FontSpec>::of(FontSpec{FontRole::control, 15.0, 700, false});
        (*page).add_at(title, {0.0, 4.0, 1000.0, 22.0});
        if (demonstrated.empty()) {
            std::shared_ptr<Label> summary = label(surface_id + ".summary",
                                                   std::string(spec.summary), 10.0, 400, "muted");
            (*page).add_at(summary, {0.0, 28.0, 1000.0, header_line_pitch});
        }
        for (std::size_t line = 0; line < demonstrated.size(); ++line) {
            std::shared_ptr<Label> control_line = label(
                surface_id + ".control." + std::to_string(line),
                page_control_line(*demonstrated[line]), 10.0, 400, "muted");
            (*page).add_at(control_line,
                           {0.0, 28.0 + header_line_pitch * static_cast<double>(line), 1000.0,
                            header_line_pitch});
        }
        std::shared_ptr<gui_forms::ScaledPanel> body = make_control<Surface>(
            StableId(std::string(body_id_prefix) + std::string(spec.id)),
            Size{1000.0, 660.0});
        static_cast<void>((*body).add_class("shell-canvas"));
        (*page).add_at(body, {0.0, strip_height, 1000.0, 660.0});
        populate_page_body(spec.id, body, context, form_pages);
        // The body is as tall as its content (at least 660): content placed
        // below 660 was cut off with no way to reach it (sweep defect D3).
        double body_height = 660.0;
        for (const Control::Ptr& child : (*body).children()) {
            const stxlib::Optional<Rect> slot = (*body).design_bounds(*child);
            const Rect placed = slot.value_or((*child).requested_bounds());
            body_height = std::max(body_height, placed.y + placed.height + 16.0);
        }
        const double page_height = strip_height + body_height;
        (*body).design_size = Size{1000.0, body_height};
        (*page).set_design_bounds(*body, {0.0, strip_height, 1000.0, body_height});
        (*page).design_size = Size{1000.0, page_height};
        (*page).set_bounds({0.0, 0.0, 1000.0, page_height});
        (*viewport).auto_scroll_min_size = {1000.0, page_height};
        // The page is built apart and attached complete, so each control
        // attaches once (the window exists before its pages).
        (*content).add_child(viewport);
        (*context).pages.push_back(viewport);
    }

    RuleSet& rules = window.rules();
    const std::shared_ptr<ShellActions> shell = std::make_shared<ShellActions>(context, window);
    (*context).components.add(shell);
    on((*navigation).item_activated(), *shell, &ShellActions::navigation_activated);
    on(window.key_down(), *shell, &ShellActions::shell_key_down);
    arm(rules, trigger::value_changed((*(*context).compiled).model.live_motion), *context,
        &PageContext::live_motion_changed);
    (*context).live_motion = (*(*context).compiled).model.live_motion.get();
    (*context).apply_motion_policy();
    (*context).select_page(0U);
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

namespace {

// The control under `control` (itself included) whose stable id is
// `stable_id`, depth first, or null.
// Recurses once per control level: bounded by maximum_control_tree_depth
// (control.hpp), within its stack budget of 4 KiB per level.
Control::Ptr find_gallery_control(const Control::Ptr& control, std::string_view stable_id) {
    if ((*control).stable_id().value() == stable_id) return control;
    for (const Control::Ptr& child : (*control).children()) {
        Control::Ptr found = find_gallery_control(child, stable_id);
        if (found) return found;
    }
    return nullptr;
}

} // namespace

bool reveal_gallery_control(Window& window, std::string_view stable_id) {
    const std::shared_ptr<PageContext> context = retained_page_context(window);
    if (!context || (*context).selected_page >= (*context).pages.size()) return false;
    const std::shared_ptr<ScrollableControl> viewport =
        std::dynamic_pointer_cast<ScrollableControl>((*context).pages[(*context).selected_page]);
    if (!viewport) return false;
    const Control::Ptr target = find_gallery_control(viewport, stable_id);
    if (!target || target == viewport) return false;
    // The pages are drawn at their design size, unscaled, so the target's
    // offset in the viewport's content is the sum of its requested origins
    // up to the viewport.
    double offset = 0.0;
    Control::Ptr step = target;
    while (step && step != viewport) {
        offset += (*step).requested_bounds().y;
        step = (*step).parent();
    }
    const double target_y = std::max(0.0, offset - 8.0);
    static_cast<void>((*viewport).scroll_to({0.0, target_y}));
    const bool reached = (*viewport).scroll_position().y == target_y;
    return reached;
}

bool open_gallery_menu(Window& window, std::string_view stable_id, std::string_view item) {
    const Control::Ptr root = window.root();
    if (!root) return false;
    const Control::Ptr target = find_gallery_control(root, stable_id);
    if (!target) return false;
    const std::shared_ptr<MenuStrip> menu = std::dynamic_pointer_cast<MenuStrip>(target);
    if (menu) {
        std::size_t index = 0U;
        for (const char digit : item) {
            if (digit < '0' || digit > '9') return false;
            index = index * 10U + static_cast<std::size_t>(digit - '0');
        }
        const bool opened = (*menu).open(index);
        return opened;
    }
    const std::shared_ptr<ToolStrip> strip = std::dynamic_pointer_cast<ToolStrip>(target);
    if (strip) {
        const bool opened = (*strip).show_drop_down(item);
        return opened;
    }
    const std::shared_ptr<Ribbon> ribbon = std::dynamic_pointer_cast<Ribbon>(target);
    if (ribbon) {
        const bool opened = (*ribbon).show_drop_down(item);
        return opened;
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
        (*(*context).timer_progress).value = cycle * 100.0;
        constexpr double travel = 98.0;
        const double eased = 0.5 - engine_sine_cosine(cycle * 6.283185307179586).cosine * 0.5;
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
    // Three profiles: name is text; enabled and gain take what their
    // check box and track bar write back, so they are untyped.
    const std::shared_ptr<BindingTable> profiles = std::make_shared<BindingTable>(BindingSchema(std::vector<BindingColumn>{
        BindingColumn{"name", BindingValueKind::text},
        BindingColumn{"enabled"},
        BindingColumn{"gain"},
    }));
    (*profiles).assign(std::vector<BindingRow>{
        BindingRow{"profile.local", {std::string("Local index"), true, 28.0}},
        BindingRow{"profile.archive", {std::string("Archive review"), false, 61.0}},
        BindingRow{"profile.plugins", {std::string("Plugin surfaces"), true, 84.0}},
    });
    (*(*context).binding_source).set_provider(profiles);
    if ((*context).binding_navigator) {
        (*(*context).binding_navigator).binding_source = (*context).binding_source;
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
    (*(*context).error_provider).blink_style = ErrorBlinkStyle::never_blink;
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
    if ((*context).navigation) {
        (*(*context).tooltips).set_tool_tip((*context).navigation, "Open a Gallery page: click it, or press Enter.");
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
