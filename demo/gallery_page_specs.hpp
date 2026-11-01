#pragma once

// The GUI.Forms Gallery's page list. Every page has a stable kebab-case id:
// its navigation row is "gallery.page.<id>" and its retained surface is
// "gallery.surface.<id>", so tests and the automation probe can address pages
// without knowing their position. Navigation groups are derived from the
// control registry (gallery_catalog.hpp): a page sits under the catalog
// category that names it most often; a page no registry record names falls
// back to the category declared here; lab pages are grouped last under
// "Labs". Demo-facing code only: no host, renderer, or platform type here.

#include "gallery_catalog.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace gui_forms::gallery {

struct GalleryPageSpec final {
    std::string_view id;
    std::string_view title;
    // Shown in the page header strip when no registry record names the page,
    // so the program still says honestly what the page demonstrates.
    std::string_view summary;
    // True when the page is a laboratory (motion, scheduler, diagnostics) rather
    // than a demonstration of shipped controls; grouped under "Labs".
    bool lab;
    std::string_view fallback_category;
};

inline constexpr std::string_view labs_group = "Labs";
inline constexpr std::string_view navigation_id_prefix = "gallery.page.";
inline constexpr std::string_view surface_id_prefix = "gallery.surface.";
inline constexpr std::string_view body_id_prefix = "gallery.body.";

// Compiled-DML pages first (their content is declared in gallery.dml), then the
// pages migrated from the retired Complete Showcase in their former order.
inline constexpr std::array<GalleryPageSpec, 30> gallery_pages {{
    {"basics", "Basic controls",
     "Compiled-DML form grid over public Button, CheckBox, RadioButton, LinkLabel and Label instances plus a composed UserControl lifecycle card.",
     false, "Information display"},
    {"values", "Values and state",
     "Compiled-DML group binding one retained model value to public TrackBar, ProgressBar and Label instances.",
     false, "Value setting"},
    {"retained-rows", "Retained rows",
     "Compiled-DML GalleryControl rows with exclusive selection and a typed drop target (demo-private, not a registry type).",
     false, "Selection from a list"},
    {"instrument", "Custom instrument",
     "Compiled-DML GalleryControl instrument on a paint-gated 30 fps animation timer (demo-private, not a registry type).",
     false, "Drawing"},
    {"control-spectrum", "Control spectrum",
     "Stock button family: default, disabled, visual styles, image list, check and radio indicators.",
     false, "Commands"},
    {"ranges-and-progress", "Ranges and progress",
     "Sliders, progress bars and scroll bars sharing one retained value.",
     false, "Value setting"},
    {"containers-and-focus", "Containers and focus",
     "Container controls, split containers, focus scopes and a docked workspace.",
     false, "Grouping"},
    {"animation-laboratory", "Animation laboratory",
     "Easing preview and animated progress under the motion policy.",
     true, "Diagnostics"},
    {"states-and-diagnostics", "States and diagnostics",
     "Enabled, visible and busy states beside the live MetricsView and UI dispatcher.",
     true, "Diagnostics"},
    {"text-and-input", "Text and input",
     "Single and multi-line TextBox editing, selection and validation.",
     false, "Text editing"},
    {"collections-and-popups", "Collections and popups",
     "ListBox and ComboBox selection with owned drop-down popups.",
     false, "Selection from a list"},
    {"values-and-spinners", "Values and spinners",
     "NumericUpDown, PropertyGrid and data binding through BindingSource.",
     false, "Value setting"},
    {"images-and-drawing", "Images and drawing",
     "PictureBox scaling, MaterialPanel fills, DrawingSurface and RasterCanvas painting.",
     false, "Drawing"},
    {"tabs-and-pages", "Tabs and pages",
     "TabControl with retained TabPage content.",
     false, "Grouping"},
    {"checked-collections", "Checked collections",
     "CheckedListBox with per-item check state.",
     false, "Selection from a list"},
    {"timing-and-tooltips", "Timing and tooltips",
     "UI Timer, ToolTip, ErrorProvider and HelpProvider components.",
     false, "Components"},
    {"dates-and-calendar", "Dates and calendar",
     "DateTimePicker with a localized format provider and drop-down calendar.",
     false, "Date setting"},
    {"dialogs-and-host-services", "Dialogs and host services",
     "Owned host dialogs, clipboard, monitor and sound services through public Window seams.",
     false, "Dialogs and popups"},
    {"css-layout-panels", "CSS layout panels",
     "FlexPanel, GridPanel, BlockPanel: CSS containers over retained children.",
     false, "Layout"},
    {"retained-layout-panels", "Compatibility layout panels",
     "For Windows Forms migration: FlowLayoutPanelCompat and TableLayoutPanelCompat; use FlexPanel and GridPanel for new work.",
     false, "Layout"},
    {"dock-and-anchor", "Dock and anchor",
     "Dock and Anchor layout policy on public Panel and Button instances.",
     false, "Layout"},
    // Pages added for the registry records the migrated Showcase did not demonstrate.
    {"commands-and-menus", "Commands and menus",
     "CommandBar, DropDownButton, MenuStrip and ContextMenu driven by shared Commands.",
     false, "Menus and commands"},
    {"tree-and-object-views", "Trees and object views",
     "TreeView, ObjectView, CorrespondenceView and BreadcrumbTrail over one fixture.",
     false, "Selection from a list"},
    {"choices-and-sections", "Choices and sections",
     "ChoiceGroup, ExpandableSections, GroupBox, Card and ReviewCard bound to shared values.",
     false, "Grouping"},
    {"value-editors", "Value editors",
     "ColorValueEditor, FlagsValueEditor, PropertyList and InstrumentRack editing one bound record.",
     false, "Value setting"},
    {"responsive-layouts", "Responsive layouts",
     "ResponsiveTrackPanel, CommandOverflowPanel, MasterDetailView and ScrollableControl under resize.",
     false, "Layout"},
    {"anchored-popups", "Anchored popups",
     "AnchoredPopupLayer placement, flipping and dismissal.",
     false, "Dialogs and popups"},
    {"live-surfaces", "Live surfaces",
     "LiveSurface publication, damage-limited presentation and reconfigure.",
     false, "Drawing"},
    {"typography-and-inspection", "Typography and inspection",
     "TypographySpecimen and the VisualInspectorView overlay.",
     true, "Diagnostics"},
    {"semantic-feedback", "Semantic feedback",
     "SemanticFeedback announcements and sound cues through the host.",
     false, "Components"},
}};

[[nodiscard]] constexpr const GalleryPageSpec* find_gallery_page(std::string_view id) noexcept
{
    for (const GalleryPageSpec& page : gallery_pages) {
        if (page.id == id) {
            return &page;
        }
    }
    return nullptr;
}

// Number of registry records that name this page.
[[nodiscard]] constexpr std::size_t catalog_entries_on_page(std::string_view page_id) noexcept
{
    std::size_t count = 0;
    for (const CatalogEntry& entry : control_catalog) {
        if (entry.gallery_page == page_id) {
            ++count;
        }
    }
    return count;
}

// The navigation group a page belongs to: "Labs" for lab pages, otherwise the
// catalog category naming the page most often (ties resolve in catalog order),
// otherwise the page's declared fallback category.
[[nodiscard]] constexpr std::string_view gallery_page_group(const GalleryPageSpec& page) noexcept
{
    if (page.lab) {
        return labs_group;
    }
    std::string_view best = page.fallback_category;
    std::size_t best_count = 0;
    for (const std::string_view category : catalog_categories) {
        std::size_t count = 0;
        for (const CatalogEntry& entry : control_catalog) {
            if (entry.gallery_page == page.id && entry.category == category) {
                ++count;
            }
        }
        if (count > best_count) {
            best = category;
            best_count = count;
        }
    }
    return best;
}

[[nodiscard]] consteval bool gallery_page_ids_are_unique()
{
    for (std::size_t left = 0; left < gallery_pages.size(); ++left) {
        if (gallery_pages[left].id.empty() || gallery_pages[left].title.empty() ||
            gallery_pages[left].summary.empty()) {
            return false;
        }
        for (std::size_t right = left + 1; right < gallery_pages.size(); ++right) {
            if (gallery_pages[left].id == gallery_pages[right].id) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] consteval bool catalog_pages_resolve()
{
    for (const CatalogEntry& entry : control_catalog) {
        if (!entry.gallery_page.empty() && find_gallery_page(entry.gallery_page) == nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] consteval bool fallback_categories_resolve()
{
    for (const GalleryPageSpec& page : gallery_pages) {
        bool found = false;
        for (const std::string_view category : catalog_categories) {
            found = found || category == page.fallback_category;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static_assert(gallery_page_ids_are_unique());
static_assert(catalog_pages_resolve());
static_assert(fallback_categories_resolve());

}  // namespace gui_forms::gallery
