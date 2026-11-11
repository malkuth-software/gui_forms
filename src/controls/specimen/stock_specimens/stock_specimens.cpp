#include "gui_forms/controls/specimen/stock_specimens/stock_specimens.hpp"

// Each row's type (generated: tools/stock_specimens.py).
#include "stock_specimen_includes.inc"

// What the hand-written makers use beyond the rows' own headers.
#include "gui_forms/controls/button_base/button/button.hpp"
#include "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_placement/anchored_overlay_placement.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/tab_control/tab_page/tab_page.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms {
namespace {

// -- naming and holding ------------------------------------------------------------

// `id` with `suffix` appended, for a specimen's inner controls.
StableId child_id(const StableId& id, std::string_view suffix) {
    std::string text(id.value());
    text += suffix;
    StableId result(std::move(text));
    return result;
}

StockSpecimenInstance alone(Control::Ptr control) {
    StockSpecimenInstance instance{};
    instance.root = control;
    instance.subject = std::move(control);
    return instance;
}

// A holder for a subject that borrows something: it owns what is
// borrowed, lays the subject over its client area, and lets its children
// go before its own members end, so the subject never outlives what it
// borrows (the base's children would otherwise end after the members).
class StockSpecimenHolder final : public Panel {
public:
    explicit StockSpecimenHolder(StableId stable_id) : Panel(std::move(stable_id)) {}
    ~StockSpecimenHolder() override { clear_children(); }
    StockSpecimenHolder(const StockSpecimenHolder&) = delete;
    StockSpecimenHolder& operator=(const StockSpecimenHolder&) = delete;

    std::unique_ptr<PageSet> pages{};
    std::shared_ptr<DockWorkspace> workspace{};
};

// GaugeBase's constructor is protected: a subclass that adds nothing
// stands for it (as the schema export does).
// The stock anatomy registered for `control_id`, or null.
const ControlAnatomy* stock_anatomy_named(std::string_view control_id) noexcept {
    for (const ControlAnatomy& anatomy : stock_control_anatomies()) {
        if (anatomy.control_id == control_id) return &anatomy;
    }
    return nullptr;
}

// GaugeBase draws through its subclasses and keeps its base's anatomy; the
// specimen stands for the registered gauge_base row, so it reports that
// anatomy (the catalogue checks each subject is of its row's type).
class SpecimenGauge final : public GaugeBase {
public:
    explicit SpecimenGauge(StableId stable_id) : GaugeBase(std::move(stable_id)) {}

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override {
        static const ControlAnatomy* const registered = stock_anatomy_named("gauge_base");
        if (registered == nullptr) return GaugeBase::anatomy();
        return *registered;
    }
};

// Sets the sample text where the type has a writable `text` property.
void set_sample_text(Control& control, std::string_view text) {
    const std::optional<PropertyDescriptor> descriptor = control.property_descriptor("text");
    if (!descriptor.has_value() || !(*descriptor).writable || text.empty()) return;
    control.set_property_value("text", BindingValue(std::string(text)));
}

std::vector<std::string> sample_items() {
    std::vector<std::string> items{};
    items.push_back("Portsmouth");
    items.push_back("Rapids");
    items.push_back("Harbour");
    items.push_back("Lighthouse");
    return items;
}

// -- the generic makers -----------------------------------------------------------

template <typename ControlType>
StockSpecimenInstance make_plain(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(std::move(id));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

template <typename ControlType>
StockSpecimenInstance make_text(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(std::move(id));
    set_sample_text(*control, specimen.sample_text);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A range shown two fifths of the way from its minimum to its maximum, in
// the properties' own kind (integer or number); left at its default when
// they have another.
template <typename ControlType>
StockSpecimenInstance make_ranged(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(std::move(id));
    set_sample_text(*control, specimen.sample_text);
    const std::optional<BindingValue> minimum = (*control).property_value("minimum");
    const std::optional<BindingValue> maximum = (*control).property_value("maximum");
    if (minimum.has_value() && maximum.has_value()) {
        const std::int64_t* const low_integer = std::get_if<std::int64_t>(&*minimum);
        const std::int64_t* const high_integer = std::get_if<std::int64_t>(&*maximum);
        const double* const low_number = std::get_if<double>(&*minimum);
        const double* const high_number = std::get_if<double>(&*maximum);
        if (low_integer != nullptr && high_integer != nullptr && *high_integer > *low_integer) {
            const std::int64_t span = *high_integer - *low_integer;
            const std::int64_t value = *low_integer + span / 5 * 2;
            (*control).set_property_value("value", BindingValue(value));
        } else if (low_number != nullptr && high_number != nullptr && *high_number > *low_number) {
            const double value = *low_number + (*high_number - *low_number) * 0.4;
            (*control).set_property_value("value", BindingValue(value));
        }
    }
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- the hand-written makers (tools/stock_specimens.py, MAKERS) ---------------

StockSpecimenInstance make_list_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ListBox> control = make_control<ListBox>(std::move(id));
    (*control).items = sample_items();
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_checked_list_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<CheckedListBox> control = make_control<CheckedListBox>(std::move(id));
    (*control).items = sample_items();
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_combo_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ComboBox> control = make_control<ComboBox>(std::move(id));
    (*control).items = sample_items();
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_domain_up_down(const StockSpecimen&, StableId id) {
    const std::shared_ptr<DomainUpDown> control = make_control<DomainUpDown>(std::move(id));
    (*control).items = sample_items();
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_tree_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TreeView> control = make_control<TreeView>(id);
    std::vector<TreeViewItem> items(3U);
    items[0].stable_id = std::string(id.value()) + ".harbour";
    items[0].text = "Harbour";
    items[0].expandable = true;
    items[0].expanded = true;
    items[1].stable_id = std::string(id.value()) + ".quay";
    items[1].text = "Quay";
    items[1].depth = 1U;
    items[2].stable_id = std::string(id.value()) + ".lighthouse";
    items[2].text = "Lighthouse";
    items[2].depth = 1U;
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_breadcrumb_trail(const StockSpecimen&, StableId id) {
    const std::shared_ptr<BreadcrumbTrail> control = make_control<BreadcrumbTrail>(id);
    std::vector<BreadcrumbSegment> segments(3U);
    segments[0].stable_id = std::string(id.value()) + ".home";
    segments[0].text = "Home";
    segments[1].stable_id = std::string(id.value()) + ".themes";
    segments[1].text = "Themes";
    segments[2].stable_id = std::string(id.value()) + ".palette";
    segments[2].text = "Palette";
    (*control).set_segments(std::move(segments));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_choice_group(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ChoiceGroup> control = make_control<ChoiceGroup>(std::move(id));
    std::vector<ChoiceItem> items(3U);
    items[0].id = 1;
    items[0].label = "Day";
    items[1].id = 2;
    items[1].label = "Week";
    items[2].id = 3;
    items[2].label = "Month";
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_step_indicator(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StepIndicator> control = make_control<StepIndicator>(std::move(id));
    std::vector<std::string> steps{};
    steps.push_back("Choose");
    steps.push_back("Adjust");
    steps.push_back("Compile");
    (*control).set_steps(std::move(steps));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_tab_control(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TabControl> control = make_control<TabControl>(id);
    (*control).add_page(make_control<TabPage>(child_id(id, ".first"), "First"));
    (*control).add_page(make_control<TabPage>(child_id(id, ".second"), "Second"));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_toast(const StockSpecimen& specimen, StableId id) {
    ToastContent content{};
    content.message = std::string(specimen.sample_text);
    content.action_text = "Undo";
    const std::shared_ptr<Toast> control = make_control<Toast>(std::move(id), std::move(content));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// The layer over a holder whose first child is its anchor button.
StockSpecimenInstance make_anchored_overlay_layer(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    const std::shared_ptr<Button> anchor = make_control<Button>(child_id(id, ".anchor"), "Anchor");
    (*anchor).set_bounds(Rect{8.0, 8.0, 96.0, 28.0});
    (*holder).add_child(anchor);
    const std::shared_ptr<AnchoredOverlayLayer> layer =
        make_control<AnchoredOverlayLayer>(std::move(id), anchor, AnchoredOverlayPlacement{});
    (*holder).add_child(layer);
    StockSpecimenInstance instance{};
    instance.root = holder;
    instance.subject = layer;
    return instance;
}

StockSpecimenInstance make_page_strip(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    (*holder).pages = std::make_unique<PageSet>();
    const std::shared_ptr<PageStrip> strip = make_control<PageStrip>(std::move(id), *(*holder).pages);
    (*strip).dock = DockStyle::fill;
    (*holder).add_child(strip);
    StockSpecimenInstance instance{};
    instance.root = holder;
    instance.subject = strip;
    return instance;
}

StockSpecimenInstance make_dock_site(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    (*holder).workspace = std::make_shared<DockWorkspace>(std::string(id.value()) + ".workspace");
    const std::shared_ptr<DockSite> site = make_control<DockSite>(std::move(id), *(*holder).workspace);
    (*site).dock = DockStyle::fill;
    (*holder).add_child(site);
    StockSpecimenInstance instance{};
    instance.root = holder;
    instance.subject = site;
    return instance;
}

StockSpecimenInstance make_gauge_base(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SpecimenGauge> control = make_control<SpecimenGauge>(std::move(id));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A track panel needs 1 to 64 tracks before it lays out: two that share
// the remaining space.
StockSpecimenInstance make_responsive_track_panel(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ResponsiveTrackPanel> control = make_control<ResponsiveTrackPanel>(std::move(id));
    ResponsiveTrackSpec track{};
    track.size_mode = ResponsiveTrackSizeMode::remaining;
    std::vector<ResponsiveTrackSpec> tracks;
    tracks.push_back(track);
    tracks.push_back(track);
    (*control).set_track_specs(std::move(tracks));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- the table -----------------------------------------------------------------------

const StockSpecimen specimen_table[] = {
#include "stock_specimen_table.inc"
};

} // namespace

std::span<const StockSpecimen> stock_specimens() noexcept {
    const std::span<const StockSpecimen> rows(specimen_table);
    return rows;
}

const StockSpecimen* find_stock_specimen(std::string_view control_id) noexcept {
    for (const StockSpecimen& specimen : specimen_table) {
        if (specimen.control_id == control_id) return &specimen;
    }
    return nullptr;
}

StockSpecimenInstance make_stock_specimen(const StockSpecimen& specimen, StableId id) {
    if (specimen.make == nullptr) throw std::invalid_argument("a stock specimen row needs its maker");
    StockSpecimenInstance instance = specimen.make(specimen, std::move(id));
    return instance;
}

} // namespace gui_forms
