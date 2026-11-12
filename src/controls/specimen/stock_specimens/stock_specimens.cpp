#include "gui_forms/controls/stock_specimens.hpp"

// Each row's type (generated: tools/stock_specimens.py).
#include "stock_specimen_includes.inc"

// What the hand-written makers use beyond the rows' own headers.
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/controls/anchored_overlay_placement.hpp"
#include "gui_forms/controls/list_view_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/controls/tab_page.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/drawing/bitmap/bitmap.hpp"
#include "gui_forms/drawing/geometry/rect_i/rect_i.hpp"
#include "gui_forms/help/help_page/help_page.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/pages/types/page_set_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms {
namespace {

// -- naming, holding and writing ---------------------------------------------------

// `id` with `suffix` appended, for a specimen's inner controls.
// Appends `suffix` to a stable ID's text in the segment spelling: ASCII
// letters lowered, so a sample label ("Harbour", "Save") names a valid
// segment.
void append_segment_text(std::string& text, std::string_view suffix) {
    for (const char character : suffix) {
        const bool upper = character >= 'A' && character <= 'Z';
        text.push_back(upper ? static_cast<char>(character - 'A' + 'a') : character);
    }
}

StableId child_id(const StableId& id, std::string_view suffix) {
    std::string text(id.value());
    append_segment_text(text, suffix);
    StableId result(std::move(text));
    return result;
}

// The same, as the plain string item records take.
std::string child_name(const StableId& id, std::string_view suffix) {
    std::string text(id.value());
    append_segment_text(text, suffix);
    return text;
}

StockSpecimenInstance alone(Control::Ptr control) {
    StockSpecimenInstance instance{};
    instance.root = control;
    instance.subject = std::move(control);
    return instance;
}

StockSpecimenInstance held(Control::Ptr root, Control::Ptr subject, Control::Ptr frame) {
    StockSpecimenInstance instance{};
    instance.root = std::move(root);
    instance.subject = std::move(subject);
    instance.frame = std::move(frame);
    return instance;
}

// A holder for a subject that borrows something: it owns what is
// borrowed (a PageSet, a DockWorkspace, the commands a CommandBar
// borrows), makes a DockWorkspace's panel contents, and lets its children
// go before its own members end, so the subject never outlives what it
// borrows (the base's children would otherwise end after the members).
// The workspace, whose factory is this holder, ends while the holder is
// still whole.
class StockSpecimenHolder final : public Panel {
public:
    explicit StockSpecimenHolder(StableId stable_id) : Panel(std::move(stable_id)) {}
    ~StockSpecimenHolder() override {
        clear_children();
        workspace.reset();
    }
    StockSpecimenHolder(const StockSpecimenHolder&) = delete;
    StockSpecimenHolder& operator=(const StockSpecimenHolder&) = delete;

    // The dock workspace's panel factory (DockPanelRegistration::factory):
    // a list of sample items under the panel's identity.
    void create_panel(DockPanelCreation& creation);

    std::unique_ptr<PageSet> pages{};
    std::shared_ptr<DockWorkspace> workspace{};
    std::vector<std::shared_ptr<Command>> commands{};
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

// TextBoxBase, ListControl and UpDownBase, the abstract bases of the text,
// list and up-down controls, paint as the Panel they add nothing to; each
// specimen stands for its registered row, as SpecimenGauge does.
class SpecimenTextBoxBase final : public TextBoxBase {
public:
    explicit SpecimenTextBoxBase(StableId stable_id) : TextBoxBase(std::move(stable_id)) {}

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override {
        static const ControlAnatomy* const registered = stock_anatomy_named("text_box_base");
        if (registered == nullptr) return TextBoxBase::anatomy();
        return *registered;
    }

protected:
    // A sample has no editing engine: the editing surface does nothing.
    [[nodiscard]] detail::TextEditingEngine* editing_engine() const noexcept override { return nullptr; }
    void apply_selection(TextSelection) override {}
};

class SpecimenListControl final : public ListControl {
public:
    explicit SpecimenListControl(StableId stable_id) : ListControl(std::move(stable_id)) {}

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override {
        static const ControlAnatomy* const registered = stock_anatomy_named("list_control");
        if (registered == nullptr) return ListControl::anatomy();
        return *registered;
    }
};

// Made without its parts (no initialize_control_tree): an empty panel.
class SpecimenUpDownBase final : public UpDownBase {
public:
    explicit SpecimenUpDownBase(StableId stable_id) : UpDownBase(std::move(stable_id)) {}

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override {
        static const ControlAnatomy* const registered = stock_anatomy_named("up_down_base");
        if (registered == nullptr) return UpDownBase::anatomy();
        return *registered;
    }
    // A sample has no steps.
    void up_button() override {}
    void down_button() override {}
};

// Writes `value` to the property `name` where the instance has it and it
// is writable; a value of the wrong kind is the write's own exception. A
// property the type lacks is left alone (a near-uniform capture then
// shows the specimen lacks content).
void write_sample(Control& control, std::string_view name, BindingValue value) {
    const std::optional<PropertyDescriptor> descriptor = control.property_descriptor(name);
    if (!descriptor.has_value() || !(*descriptor).writable) return;
    control.set_property_value(name, std::move(value));
}

void write_text(Control& control, std::string_view name, std::string_view text) {
    if (text.empty()) return;
    write_sample(control, name, BindingValue(std::string(text)));
}

void set_sample_text(Control& control, std::string_view text) {
    write_text(control, "text", text);
}

std::vector<std::string> sample_items() {
    std::vector<std::string> items{};
    items.push_back("Portsmouth");
    items.push_back("Rapids");
    items.push_back("Harbour");
    items.push_back("Lighthouse");
    items.push_back("Quay");
    items.push_back("Breakwater");
    return items;
}

std::shared_ptr<Command> make_command(std::string_view id, std::string_view text, std::string_view shortcut) {
    const std::shared_ptr<Command> command = std::make_shared<Command>(std::string(id), std::string(text));
    if (!shortcut.empty()) (*command).set_shortcut(std::string(shortcut));
    return command;
}

MenuItemSpec menu_command(std::string_view id, std::string_view text, std::string_view shortcut) {
    MenuItemSpec item(std::string(id), MenuItemKind::command, make_command(id, text, shortcut), std::string(text));
    return item;
}

MenuItemSpec menu_separator(std::string_view id) {
    MenuItemSpec item(std::string(id), MenuItemKind::separator);
    return item;
}

MenuItemSpec menu_check(std::string_view id, std::string_view text, bool checked) {
    MenuItemSpec item(std::string(id), MenuItemKind::check, make_command(id, text, {}), std::string(text));
    item.checked = checked;
    return item;
}

// A form's worth of children for a container: a caption, a field, a choice
// and a command, placed from the top left (a layout panel arranges them
// itself and reads only their sizes).
void add_sample_children(Control& parent, const StableId& id) {
    const std::shared_ptr<Label> caption = make_control<Label>(child_id(id, ".caption"), "Display name");
    (*caption).set_requested_bounds(Rect{12.0, 12.0, 110.0, 22.0});
    parent.add_child(caption);
    const std::shared_ptr<TextBox> field = make_control<TextBox>(child_id(id, ".field"));
    set_sample_text(*field, "Harbour Quay");
    (*field).set_requested_bounds(Rect{12.0, 38.0, 160.0, 26.0});
    parent.add_child(field);
    const std::shared_ptr<CheckBox> choice = make_control<CheckBox>(child_id(id, ".choice"));
    set_sample_text(*choice, "Notify me");
    (*choice).set_requested_bounds(Rect{12.0, 72.0, 140.0, 22.0});
    parent.add_child(choice);
    const std::shared_ptr<Button> command = make_control<Button>(child_id(id, ".apply"), "Apply");
    (*command).set_requested_bounds(Rect{12.0, 104.0, 88.0, 28.0});
    parent.add_child(command);
}

std::shared_ptr<ListBox> make_sample_list(const StableId& id) {
    const std::shared_ptr<ListBox> list = make_control<ListBox>(id);
    (*list).items = sample_items();
    write_sample(*list, "selected_index", BindingValue(std::int64_t{1}));
    return list;
}

std::shared_ptr<TextBlock> make_sample_paragraph(const StableId& id) {
    const std::shared_ptr<TextBlock> block = make_control<TextBlock>(id);
    set_sample_text(*block, "Tide tables for the harbour are updated each morning at six. The quay is open from "
                            "dawn until the last ferry.");
    return block;
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

// A container holding add_sample_children's form.
template <typename ControlType>
StockSpecimenInstance make_filled(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(id);
    add_sample_children(*control, id);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

template <typename ControlType>
StockSpecimenInstance make_filled_text(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(id);
    set_sample_text(*control, specimen.sample_text);
    add_sample_children(*control, id);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A range shown two fifths of the way from its minimum to its maximum, in
// the properties' own kind (integer or number); left at its default when
// they have another.
void show_two_fifths(Control& control) {
    const std::optional<BindingValue> minimum = control.property_value("minimum");
    const std::optional<BindingValue> maximum = control.property_value("maximum");
    if (!minimum.has_value() || !maximum.has_value()) return;
    const std::int64_t* const low_integer = std::get_if<std::int64_t>(&*minimum);
    const std::int64_t* const high_integer = std::get_if<std::int64_t>(&*maximum);
    const double* const low_number = std::get_if<double>(&*minimum);
    const double* const high_number = std::get_if<double>(&*maximum);
    if (low_integer != nullptr && high_integer != nullptr && *high_integer > *low_integer) {
        const std::int64_t span = *high_integer - *low_integer;
        const std::int64_t value = *low_integer + span / 5 * 2;
        control.set_property_value("value", BindingValue(value));
    } else if (low_number != nullptr && high_number != nullptr && *high_number > *low_number) {
        const double value = *low_number + (*high_number - *low_number) * 0.4;
        control.set_property_value("value", BindingValue(value));
    }
}

template <typename ControlType>
StockSpecimenInstance make_ranged(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(std::move(id));
    set_sample_text(*control, specimen.sample_text);
    show_two_fifths(*control);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- commands and text --------------------------------------------------------------

StockSpecimenInstance make_command_bar(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    const std::shared_ptr<CommandBar> bar = make_control<CommandBar>(id);
    const std::array<std::string_view, 4> labels{"New", "Open", "Save", "Share"};
    const std::array<std::string_view, 4> shortcuts{"Ctrl+N", "Ctrl+O", "Ctrl+S", ""};
    std::vector<CommandItem> items(labels.size());
    for (std::size_t index = 0U; index < labels.size(); ++index) {
        const std::shared_ptr<Command> command =
            make_command(child_name(id, "." + std::string(labels[index])), labels[index], shortcuts[index]);
        (*holder).commands.push_back(command);
        items[index].id = static_cast<int>(index) + 1;
        items[index].label = std::string(labels[index]);
        items[index].command = command.get();
    }
    (*bar).set_items(std::move(items));
    (*bar).dock = DockStyle::fill;
    (*holder).add_child(bar);
    StockSpecimenInstance instance = held(holder, bar, bar);
    return instance;
}

StockSpecimenInstance make_token_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TokenBox> control = make_control<TokenBox>(std::move(id));
    static_cast<void>((*control).add_token("Harbour"));
    static_cast<void>((*control).add_token("Quay"));
    static_cast<void>((*control).add_token("Lighthouse"));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_masked_text_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<MaskedTextBox> control = make_control<MaskedTextBox>(std::move(id), "(000) 000-0000");
    // MaskedTextBox publishes no `text` property; its input goes through
    // the mask.
    static_cast<void>((*control).place_text("5550100199"));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- information display --------------------------------------------------------------

StockSpecimenInstance make_badge(const StockSpecimen&, StableId id) {
    const std::shared_ptr<Badge> control = make_control<Badge>(std::move(id));
    write_sample(*control, "count", BindingValue(std::uint64_t{7U}));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_info_banner(const StockSpecimen&, StableId id) {
    const std::shared_ptr<InfoBanner> control = make_control<InfoBanner>(id);
    write_text(*control, "title", "Update available");
    write_text(*control, "message", "Portsmouth Rapids 1.1 adds the condensed faces.");
    static_cast<void>((*control).set_action(make_control<Button>(child_id(id, ".action"), "Review")));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_empty_state(const StockSpecimen&, StableId id) {
    const std::shared_ptr<EmptyState> control = make_control<EmptyState>(id);
    write_text(*control, "title", "No matching themes");
    write_text(*control, "description", "Try another search, or clear the filters to see every theme.");
    static_cast<void>((*control).set_action(make_control<Button>(child_id(id, ".action"), "Clear filters")));
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

StockSpecimenInstance make_sweep_scope(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SweepScope> control = make_control<SweepScope>(std::move(id));
    write_text(*control, "caption", "LEVEL");
    show_two_fifths(*control);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A fixed load trace: no clock, no random source, the same every run.
constexpr std::array<double, 32> sparkline_values{
    0.32, 0.36, 0.41, 0.38, 0.45, 0.52, 0.49, 0.57, 0.61, 0.58, 0.66, 0.72, 0.69, 0.75, 0.71, 0.64,
    0.59, 0.62, 0.68, 0.74, 0.81, 0.77, 0.84, 0.88, 0.83, 0.79, 0.73, 0.70, 0.76, 0.82, 0.86, 0.90};

StockSpecimenInstance make_sparkline(const StockSpecimen&, StableId id) {
    const std::shared_ptr<Sparkline> control = make_control<Sparkline>(std::move(id));
    write_sample(*control, "fill", BindingValue(true));
    const std::span<const double> values(sparkline_values);
    (*control).push_values(values);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A plot of two fixed series over twelve months with both axes, a legend,
// the grid and an annotation at the peak.
struct SamplePlot final {
    std::shared_ptr<PlotView> plot{};
    std::shared_ptr<PlotAxisItem> x_axis{};
    std::shared_ptr<PlotAxisItem> y_axis{};
    std::shared_ptr<PlotSeriesItem> series{};
    std::shared_ptr<PlotAnnotationItem> annotation{};
};

constexpr std::array<double, 12> plot_months{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};
constexpr std::array<double, 12> plot_load{0.20, 0.35, 0.30, 0.55, 0.62, 0.58, 0.74, 0.70, 0.83, 0.79, 0.88, 0.92};
constexpr std::array<double, 12> plot_cache{0.10, 0.18, 0.26, 0.22, 0.31, 0.40, 0.37, 0.45, 0.52, 0.49, 0.57, 0.63};

SamplePlot make_sample_plot(const StableId& id) {
    SamplePlot sample{};
    sample.plot = make_control<PlotView>(id);
    write_text(*sample.plot, "title", "Throughput");
    write_sample(*sample.plot, "show_legend", BindingValue(true));
    write_sample(*sample.plot, "show_grid", BindingValue(true));
    sample.x_axis = make_control<PlotAxisItem>(child_id(id, ".x"));
    write_text(*sample.x_axis, "label", "Month");
    write_sample(*sample.x_axis, "minimum", BindingValue(0.0));
    write_sample(*sample.x_axis, "maximum", BindingValue(11.0));
    write_sample(*sample.x_axis, "auto_fit", BindingValue(false));
    sample.y_axis = make_control<PlotAxisItem>(child_id(id, ".y"));
    (*sample.y_axis).direction = PlotAxisDirection::vertical;
    write_text(*sample.y_axis, "label", "Share");
    write_sample(*sample.y_axis, "minimum", BindingValue(0.0));
    write_sample(*sample.y_axis, "maximum", BindingValue(1.0));
    write_sample(*sample.y_axis, "auto_fit", BindingValue(false));
    sample.series = make_control<PlotSeriesItem>(child_id(id, ".load"));
    write_text(*sample.series, "label", "Load");
    const std::span<const double> months(plot_months);
    const std::span<const double> load(plot_load);
    (*sample.series).set_points(months, load);
    const std::shared_ptr<PlotSeriesItem> cache = make_control<PlotSeriesItem>(child_id(id, ".cache"));
    write_text(*cache, "label", "Cache");
    const std::span<const double> cached(plot_cache);
    (*cache).set_points(months, cached);
    sample.annotation = make_control<PlotAnnotationItem>(child_id(id, ".peak"));
    write_text(*sample.annotation, "text", "Peak");
    write_sample(*sample.annotation, "x", BindingValue(11.0));
    write_sample(*sample.annotation, "y", BindingValue(0.92));
    (*sample.plot).add_child(sample.x_axis);
    (*sample.plot).add_child(sample.y_axis);
    (*sample.plot).add_child(sample.series);
    (*sample.plot).add_child(cache);
    (*sample.plot).add_child(sample.annotation);
    return sample;
}

StockSpecimenInstance make_plot_view(const StockSpecimen&, StableId id) {
    const SamplePlot sample = make_sample_plot(id);
    StockSpecimenInstance instance = alone(sample.plot);
    return instance;
}

// A plot item shows only inside its plot: the plot is the root and the
// frame, the item the subject.
StockSpecimenInstance make_plot_axis_item(const StockSpecimen&, StableId id) {
    const SamplePlot sample = make_sample_plot(child_id(id, ".plot"));
    StockSpecimenInstance instance = held(sample.plot, sample.y_axis, sample.plot);
    return instance;
}

StockSpecimenInstance make_plot_series_item(const StockSpecimen&, StableId id) {
    const SamplePlot sample = make_sample_plot(child_id(id, ".plot"));
    StockSpecimenInstance instance = held(sample.plot, sample.series, sample.plot);
    return instance;
}

StockSpecimenInstance make_plot_annotation_item(const StockSpecimen&, StableId id) {
    const SamplePlot sample = make_sample_plot(child_id(id, ".plot"));
    StockSpecimenInstance instance = held(sample.plot, sample.annotation, sample.plot);
    return instance;
}

// A short page: a heading, a paragraph with a bold run and a list.
StockSpecimenInstance make_help_book(const StockSpecimen&, StableId id) {
    const std::shared_ptr<HelpBook> control = make_control<HelpBook>(id);
    HelpPageModel page{};
    page.stable_id = child_name(id, ".palettes");
    page.title = "Palettes";
    const std::array<std::string_view, 7> texts{
        "Palettes", "A palette shows every stock control ", "in every state", " it declares.",
        "One contact sheet per family", "An overview of every control at rest", "A manifest with rule provenance"};
    for (const std::string_view text : texts) {
        HelpPageRun run{};
        run.text = std::string(text);
        page.runs.push_back(run);
    }
    page.runs[2].bold = true;
    HelpPageBlock heading{};
    heading.kind = HelpPageBlockKind::heading;
    heading.level = 1U;
    heading.first_run = 0U;
    heading.run_count = 1U;
    page.blocks.push_back(heading);
    HelpPageBlock paragraph{};
    paragraph.first_run = 1U;
    paragraph.run_count = 3U;
    page.blocks.push_back(paragraph);
    for (std::size_t run = 4U; run < texts.size(); ++run) {
        HelpPageBlock item{};
        item.kind = HelpPageBlockKind::list_item;
        item.first_run = run;
        item.run_count = 1U;
        page.blocks.push_back(item);
    }
    (*control).show_page(std::move(page));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_gauge_base(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SpecimenGauge> control = make_control<SpecimenGauge>(std::move(id));
    show_two_fifths(*control);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_text_box_base(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SpecimenTextBoxBase> control = make_control<SpecimenTextBoxBase>(std::move(id));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_list_control(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SpecimenListControl> control = make_control<SpecimenListControl>(std::move(id));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_up_down_base(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SpecimenUpDownBase> control = make_control<SpecimenUpDownBase>(std::move(id));
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
    write_sample(*control, "current_step", BindingValue(std::uint64_t{1U}));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- selection from a list -----------------------------------------------------------

StockSpecimenInstance make_list_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ListBox> control = make_sample_list(id);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_checked_list_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<CheckedListBox> control = make_control<CheckedListBox>(std::move(id));
    (*control).items = sample_items();
    write_sample(*control, "selected_index", BindingValue(std::int64_t{0}));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_combo_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ComboBox> control = make_control<ComboBox>(std::move(id));
    (*control).items = sample_items();
    write_sample(*control, "selected_index", BindingValue(std::int64_t{2}));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_tree_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TreeView> control = make_control<TreeView>(id);
    const std::array<std::string_view, 6> names{"Harbour", "Quay", "Lighthouse", "Town", "Market", "Station"};
    const std::array<std::size_t, 6> depths{0U, 1U, 1U, 0U, 1U, 1U};
    std::vector<TreeViewItem> items(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        items[index].stable_id = child_name(id, "." + std::string(names[index]));
        items[index].text = std::string(names[index]);
        items[index].depth = depths[index];
        items[index].expandable = depths[index] == 0U;
        items[index].expanded = depths[index] == 0U;
    }
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_object_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ObjectView> control = make_control<ObjectView>(id);
    const std::array<std::string_view, 6> names{"Themes", "Harbour.theme.css", "Quay.png", "Fonts.zip",
                                                "Chime.wav", "palette.json"};
    const std::array<ObjectGlyph, 6> glyphs{ObjectGlyph::folder, ObjectGlyph::document, ObjectGlyph::image,
                                            ObjectGlyph::archive, ObjectGlyph::audio, ObjectGlyph::code};
    std::vector<ObjectViewItem> items(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        items[index].stable_id = child_name(id, ".item" + std::to_string(index));
        items[index].name = std::string(names[index]);
        items[index].glyph = glyphs[index];
    }
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_list_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ListView> control = make_control<ListView>(id);
    (*control).view = ListViewView::details;
    std::vector<ListViewColumn> columns(3U);
    columns[0].stable_id = "name";
    columns[0].text = "Name";
    columns[0].width = 150.0;
    columns[1].stable_id = "size";
    columns[1].text = "Size";
    columns[1].width = 70.0;
    columns[1].alignment = ListViewAlignment::right;
    columns[2].stable_id = "modified";
    columns[2].text = "Modified";
    columns[2].width = 100.0;
    (*control).set_columns(std::move(columns));
    const std::array<std::string_view, 5> names{"Harbour.theme.css", "Quay.theme.css", "Lighthouse.png",
                                                "Tide tables.pdf", "Notes.txt"};
    const std::array<std::string_view, 5> sizes{"24 KB", "19 KB", "312 KB", "1.2 MB", "3 KB"};
    const std::array<std::string_view, 5> dates{"14 Mar 2026", "12 Mar 2026", "2 Mar 2026", "27 Feb 2026",
                                                "19 Feb 2026"};
    std::vector<ListViewItem> items(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        items[index].stable_id = child_name(id, ".row" + std::to_string(index));
        items[index].text = std::string(names[index]);
        items[index].sub_items.push_back(std::string(sizes[index]));
        items[index].sub_items.push_back(std::string(dates[index]));
    }
    (*control).set_items(std::move(items));
    (*control).set_selected(1U, true);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_tree_list_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TreeListView> control = make_control<TreeListView>(id);
    std::vector<TreeListColumn> columns(3U);
    columns[0].stable_id = "name";
    columns[0].text = "Name";
    columns[0].width = 160.0;
    columns[1].stable_id = "kind";
    columns[1].text = "Kind";
    columns[1].width = 90.0;
    columns[2].stable_id = "size";
    columns[2].text = "Size";
    columns[2].width = 70.0;
    columns[2].alignment = TreeListAlignment::right;
    (*control).set_columns(std::move(columns));
    const std::array<std::string_view, 6> names{"Themes", "Harbour.theme.css", "Quay.theme.css",
                                                "Assets", "Lighthouse.png", "Chime.wav"};
    const std::array<std::string_view, 6> kinds{"Folder", "Theme sheet", "Theme sheet", "Folder", "PNG image",
                                                "Sound"};
    const std::array<std::string_view, 6> sizes{"", "24 KB", "19 KB", "", "312 KB", "88 KB"};
    const std::array<std::size_t, 6> depths{0U, 1U, 1U, 0U, 1U, 1U};
    std::vector<TreeListNode> nodes(names.size());
    for (std::size_t index = 0U; index < names.size(); ++index) {
        nodes[index].stable_id = child_name(id, ".node" + std::to_string(index));
        nodes[index].text = std::string(names[index]);
        nodes[index].cells.push_back(std::string(kinds[index]));
        nodes[index].cells.push_back(std::string(sizes[index]));
        nodes[index].depth = depths[index];
        nodes[index].expandable = depths[index] == 0U;
        nodes[index].expanded = depths[index] == 0U;
    }
    (*control).set_nodes(std::move(nodes));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// VirtualRowsView borrows its row targets, so the specimen's are free
// functions over fixed messages: nothing to outlive. Row r shows message
// r % 6: a sender line and one to three message lines, each a theme label
// part, so the rows vary in height as a chat history does.
struct SpecimenMessage final {
    std::string_view sender;
    std::array<std::string_view, 3> lines;
    std::size_t line_count;
};

constexpr std::array<SpecimenMessage, 6> specimen_messages{{
    {"Harbour master", {"Pilot boat leaves at six.", "", ""}, 1U},
    {"Quay office", {"The east berth is clear;", "the tender can come alongside", "after the tide turns."}, 3U},
    {"Lighthouse", {"Lamp tested and logged.", "", ""}, 1U},
    {"Chandlery", {"Two fenders and a coil of rope", "are ready at the counter.", ""}, 2U},
    {"Harbour master", {"Fog warning lifted.", "", ""}, 1U},
    {"Ferry desk", {"The evening crossing runs late;", "boarding opens at eight.", ""}, 2U},
}};
constexpr std::size_t specimen_message_rows = 600U;
constexpr double specimen_line_height = 18.0;
constexpr double specimen_row_padding = 4.0;

void measure_specimen_message(VirtualRowMeasure& request) {
    const SpecimenMessage& message = specimen_messages[request.row.value % specimen_messages.size()];
    const double lines = static_cast<double>(message.line_count + 1U);
    request.height = 2.0 * specimen_row_padding + lines * specimen_line_height;
}

void paint_specimen_message(VirtualRowPaint& request) {
    const SpecimenMessage& message = specimen_messages[request.row.value % specimen_messages.size()];
    const FontSpec body = (*request.view).text_font(FontSpec{});
    PartPaint label;
    label.family = ControlVisualRole::selection;
    label.role = PartRole::label;
    label.states = request.states;
    label.bounds = {request.bounds.x + 8.0, request.bounds.y + specimen_row_padding, request.bounds.width - 16.0,
                    specimen_line_height};
    label.font = body;
    label.font.weight = static_cast<std::uint16_t>(600U);
    label.text = lend_paint_text(message.sender);
    paint_part(*request.painter, *request.view, label);
    label.font = body;
    for (std::size_t line = 0U; line < message.line_count; ++line) {
        label.bounds.y = label.bounds.y + specimen_line_height;
        label.text = lend_paint_text(message.lines[line]);
        paint_part(*request.painter, *request.view, label);
    }
    label.text = {};
}

void describe_specimen_message(VirtualRowDescription& request) {
    const SpecimenMessage& message = specimen_messages[request.row.value % specimen_messages.size()];
    request.name.append(message.sender);
    for (std::size_t line = 0U; line < message.line_count; ++line) {
        if (line > 0U) request.value.push_back(' ');
        request.value.append(message.lines[line]);
    }
}

StockSpecimenInstance make_virtual_rows_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<VirtualRowsView> control = make_control<VirtualRowsView>(std::move(id));
    VirtualRowsCallbacks callbacks{};
    callbacks.measure = Delegate<VirtualRowMeasure&>::bind<&measure_specimen_message>();
    callbacks.paint = Delegate<VirtualRowPaint&>::bind<&paint_specimen_message>();
    callbacks.describe = Delegate<VirtualRowDescription&>::bind<&describe_specimen_message>();
    (*control).set_row_callbacks(callbacks);
    (*control).set_estimated_row_height(2.0 * specimen_row_padding + 2.0 * specimen_line_height);
    (*control).reset_rows(specimen_message_rows);
    (*control).set_selected_row(VirtualRowIndex{1U});
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_data_grid_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<DataGridView> control = make_control<DataGridView>(std::move(id));
    std::vector<DataGridViewColumn> columns(4U);
    columns[0].name = "item";
    columns[0].header_text = "Item";
    columns[0].width = 130.0;
    columns[1].name = "quantity";
    columns[1].header_text = "Qty";
    columns[1].width = 50.0;
    columns[2].name = "status";
    columns[2].header_text = "Status";
    columns[2].kind = DataGridViewColumnKind::combo_box;
    columns[2].items.push_back("Open");
    columns[2].items.push_back("Shipped");
    columns[2].items.push_back("Held");
    columns[2].width = 100.0;
    columns[3].name = "paid";
    columns[3].header_text = "Paid";
    columns[3].kind = DataGridViewColumnKind::check_box;
    columns[3].value_kind = BindingValueKind::boolean;
    columns[3].width = 50.0;
    (*control).set_columns(std::move(columns));
    const std::array<std::string_view, 4> items{"Mooring rope", "Fender", "Chart, harbour", "Lantern"};
    const std::array<std::string_view, 4> quantities{"12", "4", "1", "2"};
    const std::array<std::string_view, 4> statuses{"Shipped", "Open", "Held", "Open"};
    const std::array<bool, 4> paid{true, false, false, true};
    (*control).set_row_count(items.size());
    for (std::size_t row = 0U; row < items.size(); ++row) {
        const DataGridViewRowPosition position{row};
        static_cast<void>((*control).set_cell_value(position, 0U, BindingValue(std::string(items[row]))));
        static_cast<void>((*control).set_cell_value(position, 1U, BindingValue(std::string(quantities[row]))));
        static_cast<void>((*control).set_cell_value(position, 2U, BindingValue(std::string(statuses[row]))));
        static_cast<void>((*control).set_cell_value(position, 3U, BindingValue(paid[row])));
    }
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

StockSpecimenInstance make_breadcrumb_trail(const StockSpecimen&, StableId id) {
    const std::shared_ptr<BreadcrumbTrail> control = make_control<BreadcrumbTrail>(id);
    std::vector<BreadcrumbSegment> segments(3U);
    segments[0].stable_id = child_name(id, ".home");
    segments[0].text = "Home";
    segments[1].stable_id = child_name(id, ".themes");
    segments[1].text = "Themes";
    segments[2].stable_id = child_name(id, ".palette");
    segments[2].text = "Palette";
    (*control).set_segments(std::move(segments));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_lookup_combo_box(const StockSpecimen&, StableId id) {
    const std::shared_ptr<LookupComboBox> control = make_control<LookupComboBox>(std::move(id));
    std::vector<LookupColumn> columns(2U);
    columns[0].header = "Code";
    columns[0].width = 60.0;
    columns[1].header = "Port";
    columns[1].width = 140.0;
    (*control).set_columns(std::move(columns));
    const std::array<std::string_view, 3> keys{"pme", "sou", "plh"};
    const std::array<std::string_view, 3> codes{"PME", "SOU", "PLH"};
    const std::array<std::string_view, 3> ports{"Portsmouth", "Southampton", "Plymouth"};
    std::vector<LookupRow> rows(keys.size());
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        rows[index].key = std::string(keys[index]);
        rows[index].cells.push_back(std::string(codes[index]));
        rows[index].cells.push_back(std::string(ports[index]));
    }
    (*control).set_rows(std::move(rows));
    (*control).display_column = std::uint32_t{1U};
    write_text(*control, "selected_key", "pme");
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- value setting ----------------------------------------------------------------

StockSpecimenInstance make_range_control(const StockSpecimen&, StableId id) {
    const std::shared_ptr<RangeControl> control = make_control<RangeControl>(std::move(id));
    (*control).set_range(0.0, 100.0);
    write_sample(*control, "value", BindingValue(40.0));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_numeric_up_down(const StockSpecimen&, StableId id) {
    const std::shared_ptr<NumericUpDown> control = make_control<NumericUpDown>(std::move(id));
    (*control).set_range(0.0, 100.0);
    write_sample(*control, "value", BindingValue(42.0));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_domain_up_down(const StockSpecimen&, StableId id) {
    const std::shared_ptr<DomainUpDown> control = make_control<DomainUpDown>(std::move(id));
    (*control).items = sample_items();
    write_sample(*control, "selected_index", BindingValue(std::int64_t{2}));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_color_value_editor(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ColorValueEditor> control =
        make_control<ColorValueEditor>(std::move(id), Color::rgba(38, 110, 196));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_flags_value_editor(const StockSpecimen&, StableId id) {
    PropertyEnumDescriptor descriptor{};
    descriptor.type_name = "Edges";
    descriptor.flags = true;
    const std::array<std::string_view, 4> names{"Top", "Left", "Bottom", "Right"};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        PropertyEnumChoice choice{};
        choice.name = std::string(names[index]);
        choice.value = std::int64_t{1} << index;
        descriptor.choices.push_back(choice);
    }
    PropertyEnumValue value{};
    value.type_name = "Edges";
    value.value = 3;
    const std::shared_ptr<FlagsValueEditor> control =
        make_control<FlagsValueEditor>(std::move(id), std::move(descriptor), std::move(value));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// The grid inspects a button of its own (kept alive by the grid).
StockSpecimenInstance make_property_grid(const StockSpecimen&, StableId id) {
    const std::shared_ptr<PropertyGrid> control = make_control<PropertyGrid>(id);
    const std::shared_ptr<Button> inspected = make_control<Button>(child_id(id, ".inspected"), "Apply");
    (*control).set_selected_object(inspected);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_property_list(const StockSpecimen&, StableId id) {
    const std::shared_ptr<PropertyList> control = make_control<PropertyList>(std::move(id));
    std::vector<std::string> themes{};
    themes.push_back("Portsmouth Light");
    themes.push_back("Portsmouth Dark");
    PropertyGroupSpec appearance{};
    appearance.stable_id = "appearance";
    appearance.title = "APPEARANCE";
    appearance.rows.push_back(PropertyRowSpec("appearance.theme", "Theme", "Portsmouth Light", "The colour theme",
                                              PropertyEditorKind::choice, themes));
    appearance.rows.push_back(PropertyRowSpec("appearance.scale", "Scale", "100 %"));
    appearance.rows.push_back(
        PropertyRowSpec("appearance.accent", "Accent", "Harbour blue", "The accent colour", PropertyEditorKind::text));
    PropertyGroupSpec behaviour{};
    behaviour.stable_id = "behaviour";
    behaviour.title = "BEHAVIOUR";
    behaviour.rows.push_back(PropertyRowSpec("behaviour.motion", "Reduce motion", "True", "Fewer animations",
                                             PropertyEditorKind::boolean));
    behaviour.rows.push_back(PropertyRowSpec("behaviour.save", "Autosave", "Every 5 minutes"));
    std::vector<PropertyGroupSpec> groups{};
    groups.push_back(std::move(appearance));
    groups.push_back(std::move(behaviour));
    (*control).set_groups(std::move(groups));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- dates: fixed, never the wall clock --------------------------------------------

constexpr DateTimeValue sample_today{2026, 3, 14};
constexpr DateTimeValue sample_date{2026, 3, 17};

StockSpecimenInstance make_date_time_picker(const StockSpecimen&, StableId id) {
    const std::shared_ptr<DateTimePicker> control = make_control<DateTimePicker>(std::move(id));
    (*control).value = sample_date;
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_month_calendar(const StockSpecimen&, StableId id) {
    const std::shared_ptr<MonthCalendar> control = make_control<MonthCalendar>(std::move(id));
    (*control).set_today_date(sample_today);
    (*control).set_date(sample_date);
    std::vector<DateTimeValue> bolded{};
    bolded.push_back(DateTimeValue{2026, 3, 6});
    bolded.push_back(DateTimeValue{2026, 3, 20});
    (*control).set_bolded_dates(std::move(bolded));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- grouping ---------------------------------------------------------------------

StockSpecimenInstance make_scaled_group_box(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<ScaledGroupBox> control =
        make_control<ScaledGroupBox>(id, std::string(specimen.sample_text), specimen.size);
    const std::shared_ptr<Label> caption = make_control<Label>(child_id(id, ".caption"), "Host");
    (*control).add_at(caption, Rect{14.0, 32.0, 80.0, 22.0});
    const std::shared_ptr<TextBox> host = make_control<TextBox>(child_id(id, ".host"));
    set_sample_text(*host, "quay.example");
    (*control).add_at(host, Rect{96.0, 30.0, 150.0, 26.0});
    const std::shared_ptr<CheckBox> secure = make_control<CheckBox>(child_id(id, ".secure"));
    set_sample_text(*secure, "Encrypted");
    (*control).add_at(secure, Rect{96.0, 66.0, 140.0, 22.0});
    const std::shared_ptr<Button> connect = make_control<Button>(child_id(id, ".connect"), "Connect");
    (*control).add_at(connect, Rect{96.0, 100.0, 96.0, 28.0});
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_scaled_panel(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<ScaledPanel> control = make_control<ScaledPanel>(id, specimen.size);
    const std::shared_ptr<Label> caption = make_control<Label>(child_id(id, ".caption"), "Display name");
    (*control).add_at(caption, Rect{12.0, 12.0, 110.0, 22.0});
    const std::shared_ptr<TextBox> field = make_control<TextBox>(child_id(id, ".field"));
    set_sample_text(*field, "Harbour Quay");
    (*control).add_at(field, Rect{12.0, 38.0, 180.0, 26.0});
    const std::shared_ptr<Button> apply = make_control<Button>(child_id(id, ".apply"), "Apply");
    (*control).add_at(apply, Rect{12.0, 76.0, 88.0, 28.0});
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_card(const StockSpecimen&, StableId id) {
    const std::shared_ptr<Card> control = make_control<Card>(id);
    static_cast<void>((*control).set_header(make_control<Label>(child_id(id, ".header"), "Harbour Quay")));
    static_cast<void>((*control).set_body(make_sample_paragraph(child_id(id, ".body"))));
    static_cast<void>((*control).set_footer(make_control<Button>(child_id(id, ".open"), "Open")));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_expandable_sections(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ExpandableSections> control = make_control<ExpandableSections>(id);
    std::vector<SectionItem> items(3U);
    items[0].id = 1;
    items[0].label = "Arrivals";
    items[0].content = make_sample_paragraph(child_id(id, ".arrivals"));
    items[1].id = 2;
    items[1].label = "Departures";
    items[1].content = make_control<Label>(child_id(id, ".departures"), "Ferry at 06:40");
    items[2].id = 3;
    items[2].label = "Notices";
    items[2].content = make_control<Label>(child_id(id, ".notices"), "North quay closed");
    (*control).set_items(std::move(items));
    (*control).set_expanded(0, true);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// Three pages, the first two holding a form; `first` receives the first.
std::shared_ptr<TabControl> make_sample_tabs(const StableId& id, std::shared_ptr<TabPage>& first) {
    const std::shared_ptr<TabControl> tabs = make_control<TabControl>(id);
    first = make_control<TabPage>(child_id(id, ".general"), "General");
    add_sample_children(*first, child_id(id, ".general"));
    (*tabs).add_page(first);
    const std::shared_ptr<TabPage> layout = make_control<TabPage>(child_id(id, ".layout"), "Layout");
    add_sample_children(*layout, child_id(id, ".layout"));
    (*tabs).add_page(layout);
    (*tabs).add_page(make_control<TabPage>(child_id(id, ".advanced"), "Advanced"));
    return tabs;
}

StockSpecimenInstance make_tab_control(const StockSpecimen&, StableId id) {
    std::shared_ptr<TabPage> first{};
    const std::shared_ptr<TabControl> tabs = make_sample_tabs(id, first);
    StockSpecimenInstance instance = alone(tabs);
    return instance;
}

// A tab page shows inside its tab control.
StockSpecimenInstance make_tab_page(const StockSpecimen&, StableId id) {
    std::shared_ptr<TabPage> first{};
    const std::shared_ptr<TabControl> tabs = make_sample_tabs(child_id(id, ".tabs"), first);
    StockSpecimenInstance instance = held(tabs, first, tabs);
    return instance;
}

// Three pages in `pages`, each with a paragraph for content when asked.
// Throws std::logic_error when the set refuses one.
void insert_sample_pages(PageSet& pages, const StableId& id, bool with_content) {
    const std::array<std::string_view, 3> titles{"Harbour.theme.css", "Quay.theme.css", "Notes"};
    for (std::size_t index = 0U; index < titles.size(); ++index) {
        PageRecord record{};
        record.id = PageId{child_name(id, ".page" + std::to_string(index))};
        record.title = std::string(titles[index]);
        record.closable = true;
        record.modified = index == 1U;
        if (pages.insert(record, PagePlacement::end_of_group()) != PageEditStatus::applied) {
            throw std::logic_error("a stock specimen's page set refused a page");
        }
        if (!with_content) continue;
        const std::shared_ptr<TextBlock> body = make_sample_paragraph(child_id(id, ".content" + std::to_string(index)));
        std::unique_ptr<PageContent> content = std::make_unique<PageControlContent>(body);
        static_cast<void>(pages.set_content(record.id, content));
    }
}

StockSpecimenInstance make_page_strip(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    (*holder).pages = std::make_unique<PageSet>();
    insert_sample_pages(*(*holder).pages, id, false);
    const std::shared_ptr<PageStrip> strip = make_control<PageStrip>(id, *(*holder).pages);
    (*strip).dock = DockStyle::fill;
    (*holder).add_child(strip);
    StockSpecimenInstance instance = held(holder, strip, strip);
    return instance;
}

StockSpecimenInstance make_page_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<PageView> control = make_control<PageView>(id);
    const std::array<std::string_view, 3> titles{"Harbour.theme.css", "Quay.theme.css", "Notes"};
    for (std::size_t index = 0U; index < titles.size(); ++index) {
        PageRecord record{};
        record.id = PageId{child_name(id, ".page" + std::to_string(index))};
        record.title = std::string(titles[index]);
        record.closable = true;
        const PageEditStatus added =
            (*control).add_page(record, make_sample_paragraph(child_id(id, ".content" + std::to_string(index))));
        if (added != PageEditStatus::applied) throw std::logic_error("a stock specimen's page view refused a page");
    }
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_navigation_pane(const StockSpecimen&, StableId id) {
    const std::shared_ptr<NavigationPane> control = make_control<NavigationPane>(id);
    insert_sample_pages((*control).pages(), id, true);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

std::shared_ptr<WizardPage> make_wizard_step(const StableId& id, std::string_view title, std::string_view description) {
    const std::shared_ptr<WizardPage> page = make_control<WizardPage>(id);
    (*page).title = std::string(title);
    (*page).description = std::string(description);
    add_sample_children(*page, id);
    return page;
}

// Three steps; `first` receives the first.
std::shared_ptr<Wizard> make_sample_wizard(const StableId& id, std::shared_ptr<WizardPage>& first) {
    const std::shared_ptr<Wizard> wizard = make_control<Wizard>(id);
    first = make_wizard_step(child_id(id, ".account"), "Account", "Who is the theme for?");
    (*wizard).add_child(first);
    (*wizard).add_child(make_wizard_step(child_id(id, ".details"), "Details", "Name the theme and its colours."));
    (*wizard).add_child(make_wizard_step(child_id(id, ".review"), "Review", "Check the choices, then finish."));
    return wizard;
}

StockSpecimenInstance make_wizard(const StockSpecimen&, StableId id) {
    std::shared_ptr<WizardPage> first{};
    const std::shared_ptr<Wizard> wizard = make_sample_wizard(id, first);
    StockSpecimenInstance instance = alone(wizard);
    return instance;
}

// A wizard page shows inside its wizard.
StockSpecimenInstance make_wizard_page(const StockSpecimen&, StableId id) {
    std::shared_ptr<WizardPage> first{};
    const std::shared_ptr<Wizard> wizard = make_sample_wizard(child_id(id, ".wizard"), first);
    StockSpecimenInstance instance = held(wizard, first, wizard);
    return instance;
}

// -- layout -------------------------------------------------------------------------

void StockSpecimenHolder::create_panel(DockPanelCreation& creation) {
    const std::shared_ptr<ListBox> list = make_sample_list(StableId("content." + creation.panel.value));
    creation.content = list;
}

DockPanelRegistration dock_panel(StockSpecimenHolder& holder, std::string_view id, std::string_view title,
                                 DockPanelKind kind, DockSide side) {
    DockPanelRegistration registration{};
    registration.descriptor.id = DockPanelId(std::string(id));
    registration.descriptor.kind = kind;
    registration.descriptor.minimum_size = Size{80.0, 60.0};
    registration.descriptor.default_placement.side = side;
    registration.descriptor.default_placement.ratio = 0.3;
    registration.title = std::string(title);
    registration.factory =
        Delegate<DockPanelCreation&>::bind<StockSpecimenHolder, &StockSpecimenHolder::create_panel>(holder);
    return registration;
}

// Two documents tabbed in the document area, an explorer on the left and
// properties on the right; the holder is the panels' factory and owns the
// workspace. Throws std::logic_error when the workspace refuses a panel
// or the layout.
StockSpecimenInstance make_dock_site(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    (*holder).workspace = std::make_shared<DockWorkspace>(child_name(id, ".workspace"));
    DockWorkspace& workspace = *(*holder).workspace;
    const std::array<std::string_view, 4> ids{"harbour", "quay", "explorer", "properties"};
    const std::array<std::string_view, 4> titles{"Harbour.theme.css", "Quay.theme.css", "Explorer", "Properties"};
    const std::array<DockPanelKind, 4> kinds{DockPanelKind::document, DockPanelKind::document, DockPanelKind::tool,
                                             DockPanelKind::tool};
    const std::array<DockSide, 4> sides{DockSide::left, DockSide::left, DockSide::left, DockSide::right};
    DockTransaction transaction{};
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        if (!workspace.register_panel(dock_panel(*holder, ids[index], titles[index], kinds[index], sides[index]))) {
            throw std::logic_error("a stock specimen's dock workspace refused a panel");
        }
        transaction.edits.push_back(DockEdit::show(DockPanelId(std::string(ids[index]))));
    }
    if (!workspace.apply(transaction).applied) throw std::logic_error("a stock specimen's dock layout was refused");
    const std::shared_ptr<DockSite> site = make_control<DockSite>(id, workspace);
    (*site).dock = DockStyle::fill;
    (*holder).add_child(site);
    StockSpecimenInstance instance = held(holder, site, site);
    return instance;
}

StockSpecimenInstance make_dock_navigator(const StockSpecimen&, StableId id) {
    const std::shared_ptr<DockNavigator> control = make_control<DockNavigator>(std::move(id));
    std::vector<DockNavigatorEntry> tools(2U);
    tools[0].panel = DockPanelId(std::string("explorer"));
    tools[0].title = "Explorer";
    tools[1].panel = DockPanelId(std::string("properties"));
    tools[1].title = "Properties";
    std::vector<DockNavigatorEntry> documents(3U);
    documents[0].panel = DockPanelId(std::string("harbour"));
    documents[0].title = "Harbour.theme.css";
    documents[1].panel = DockPanelId(std::string("quay"));
    documents[1].title = "Quay.theme.css";
    documents[2].panel = DockPanelId(std::string("notes"));
    documents[2].title = "Notes";
    (*control).set_entries(std::move(tools), std::move(documents));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A track panel needs 1 to 64 tracks before it lays out: two that share
// the remaining space, holding add_sample_children's form.
StockSpecimenInstance make_responsive_track_panel(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ResponsiveTrackPanel> control = make_control<ResponsiveTrackPanel>(id);
    ResponsiveTrackSpec track{};
    track.size_mode = ResponsiveTrackSizeMode::remaining;
    std::vector<ResponsiveTrackSpec> tracks;
    tracks.push_back(track);
    tracks.push_back(track);
    (*control).set_track_specs(std::move(tracks));
    add_sample_children(*control, id);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_table_layout_panel(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TableLayoutPanelCompat> control = make_control<TableLayoutPanelCompat>(id);
    write_sample(*control, "column_count", BindingValue(std::uint64_t{2U}));
    write_sample(*control, "row_count", BindingValue(std::uint64_t{2U}));
    add_sample_children(*control, id);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

std::shared_ptr<SplitContainer> make_sample_split(const StableId& id) {
    const std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(id);
    const std::shared_ptr<ListBox> list = make_sample_list(child_id(id, ".list"));
    (*list).dock = DockStyle::fill;
    (*(*split).first_panel()).add_child(list);
    const std::shared_ptr<TextBlock> detail = make_sample_paragraph(child_id(id, ".detail"));
    (*detail).dock = DockStyle::fill;
    (*(*split).second_panel()).add_child(detail);
    return split;
}

StockSpecimenInstance make_split_container(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SplitContainer> split = make_sample_split(id);
    StockSpecimenInstance instance = alone(split);
    return instance;
}

// A splitter panel shows inside its split container.
StockSpecimenInstance make_splitter_panel(const StockSpecimen&, StableId id) {
    const std::shared_ptr<SplitContainer> split = make_sample_split(child_id(id, ".split"));
    StockSpecimenInstance instance = held(split, (*split).first_panel(), split);
    return instance;
}

// A splitter between a list docked left and a paragraph filling the rest.
StockSpecimenInstance make_splitter(const StockSpecimen&, StableId id) {
    const std::shared_ptr<Panel> holder = make_control<Panel>(child_id(id, ".holder"));
    const std::shared_ptr<TextBlock> detail = make_sample_paragraph(child_id(id, ".detail"));
    (*detail).dock = DockStyle::fill;
    (*holder).add_child(detail);
    const std::shared_ptr<Splitter> splitter = make_control<Splitter>(id);
    (*splitter).set_requested_bounds(Rect{0.0, 0.0, 6.0, 0.0});
    (*splitter).dock = DockStyle::left;
    (*holder).add_child(splitter);
    const std::shared_ptr<ListBox> list = make_sample_list(child_id(id, ".list"));
    (*list).set_requested_bounds(Rect{0.0, 0.0, 110.0, 0.0});
    (*list).dock = DockStyle::left;
    (*holder).add_child(list);
    StockSpecimenInstance instance = held(holder, splitter, holder);
    return instance;
}

StockSpecimenInstance make_command_overflow_panel(const StockSpecimen&, StableId id) {
    const std::shared_ptr<CommandOverflowPanel> control = make_control<CommandOverflowPanel>(id);
    const std::array<std::string_view, 4> labels{"Cut", "Copy", "Paste", "Format"};
    for (const std::string_view label : labels) {
        const std::shared_ptr<Button> button =
            make_control<Button>(child_id(id, "." + std::string(label)), std::string(label));
        (*button).set_requested_bounds(Rect{0.0, 0.0, 72.0, 28.0});
        (*control).add_child(button);
    }
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_master_detail_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<MasterDetailView> control = make_control<MasterDetailView>(id);
    static_cast<void>((*control).set_master(make_sample_list(child_id(id, ".master"))));
    static_cast<void>((*control).set_detail(make_sample_paragraph(child_id(id, ".detail"))));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// Content larger than the view, so the scroll bars show.
StockSpecimenInstance make_scrollable_control(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ScrollableControl> control = make_control<ScrollableControl>(id);
    write_sample(*control, "auto_scroll", BindingValue(true));
    write_sample(*control, "auto_scroll_min_size", BindingValue(Size{420.0, 320.0}));
    add_sample_children(*control, id);
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- menus and commands ---------------------------------------------------------------

StockSpecimenInstance make_menu_strip(const StockSpecimen&, StableId id) {
    const std::shared_ptr<MenuStrip> control = make_control<MenuStrip>(std::move(id));
    std::vector<MenuStripItemSpec> items(4U);
    items[0].stable_id = "file";
    items[0].text = "&File";
    items[0].items.push_back(menu_command("file.new", "New", "Ctrl+N"));
    items[0].items.push_back(menu_command("file.open", "Open...", "Ctrl+O"));
    std::vector<MenuItemSpec> recent{};
    recent.push_back(menu_command("file.recent.harbour", "Harbour.theme.css", ""));
    recent.push_back(menu_command("file.recent.quay", "Quay.theme.css", ""));
    items[0].items.push_back(
        MenuItemSpec("file.recent", MenuItemKind::submenu, nullptr, "Open recent", std::move(recent)));
    items[0].items.push_back(menu_separator("file.separator"));
    items[0].items.push_back(menu_command("file.exit", "Exit", "Alt+F4"));
    items[1].stable_id = "edit";
    items[1].text = "&Edit";
    items[1].items.push_back(menu_command("edit.undo", "Undo", "Ctrl+Z"));
    items[1].items.push_back(menu_command("edit.redo", "Redo", "Ctrl+Y"));
    items[1].items.push_back(menu_separator("edit.separator"));
    items[1].items.push_back(menu_command("edit.cut", "Cut", "Ctrl+X"));
    items[1].items.push_back(menu_command("edit.copy", "Copy", "Ctrl+C"));
    items[1].items.push_back(menu_command("edit.paste", "Paste", "Ctrl+V"));
    items[2].stable_id = "view";
    items[2].text = "&View";
    items[2].items.push_back(menu_check("view.toolbar", "Toolbar", true));
    items[2].items.push_back(menu_check("view.status", "Status bar", false));
    items[3].stable_id = "help";
    items[3].text = "&Help";
    items[3].items.push_back(menu_command("help.about", "About", "F1"));
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

ToolStripItemSpec tool_button(std::string_view id, std::string_view text, std::string_view shortcut) {
    ToolStripItemSpec item{};
    item.stable_id = std::string(id);
    item.kind = ToolStripItemKind::button;
    item.text = std::string(text);
    item.tool_tip_text = std::string(text);
    item.command = make_command(id, text, shortcut);
    return item;
}

ToolStripItemSpec tool_item(std::string_view id, ToolStripItemKind kind, std::string_view text) {
    ToolStripItemSpec item{};
    item.stable_id = std::string(id);
    item.kind = kind;
    item.text = std::string(text);
    return item;
}

std::vector<ToolStripItemSpec> sample_tool_items() {
    std::vector<ToolStripItemSpec> items{};
    items.push_back(tool_button("new", "New", "Ctrl+N"));
    items.push_back(tool_button("open", "Open", "Ctrl+O"));
    items.push_back(tool_button("save", "Save", "Ctrl+S"));
    items.push_back(tool_item("separator", ToolStripItemKind::separator, ""));
    ToolStripItemSpec zoom = tool_item("zoom", ToolStripItemKind::drop_down_button, "Zoom");
    zoom.drop_down.push_back(menu_command("zoom.in", "Zoom in", "Ctrl+="));
    zoom.drop_down.push_back(menu_command("zoom.out", "Zoom out", "Ctrl+-"));
    items.push_back(std::move(zoom));
    ToolStripItemSpec scale = tool_item("scale", ToolStripItemKind::combo_box, "100 %");
    scale.choices.push_back("100 %");
    scale.choices.push_back("150 %");
    scale.choices.push_back("200 %");
    scale.width = 72.0;
    items.push_back(std::move(scale));
    return items;
}

StockSpecimenInstance make_tool_strip(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ToolStrip> control = make_control<ToolStrip>(std::move(id));
    (*control).set_items(sample_tool_items());
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_status_strip(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StatusStrip> control = make_control<StatusStrip>(std::move(id));
    std::vector<ToolStripItemSpec> items{};
    ToolStripItemSpec state = tool_item("state", ToolStripItemKind::label, "Ready");
    state.spring = true;
    items.push_back(std::move(state));
    ToolStripItemSpec progress = tool_item("progress", ToolStripItemKind::progress_bar, "");
    progress.width = 80.0;
    items.push_back(std::move(progress));
    items.push_back(tool_item("position", ToolStripItemKind::label, "Ln 12, Col 4"));
    items.push_back(tool_item("encoding", ToolStripItemKind::label, "UTF-8"));
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_ribbon_font_control(const StockSpecimen&, StableId id) {
    const std::shared_ptr<RibbonFontControl> control = make_control<RibbonFontControl>(std::move(id));
    std::vector<std::string> families{};
    families.push_back("Portsmouth Rapids");
    families.push_back("Carlito");
    families.push_back("Cousine");
    (*control).set_families(std::move(families));
    std::vector<double> sizes{};
    sizes.push_back(9.0);
    sizes.push_back(10.0);
    sizes.push_back(11.0);
    sizes.push_back(12.0);
    sizes.push_back(14.0);
    (*control).set_sizes(std::move(sizes));
    const std::shared_ptr<ComboBox> family = (*control).family_box();
    const std::shared_ptr<ComboBox> size = (*control).size_box();
    if (family) write_sample(*family, "selected_index", BindingValue(std::int64_t{0}));
    if (size) write_sample(*size, "selected_index", BindingValue(std::int64_t{2}));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

std::vector<Color> sample_palette() {
    std::vector<Color> palette{};
    palette.push_back(Color::rgba(38, 110, 196));
    palette.push_back(Color::rgba(196, 60, 52));
    palette.push_back(Color::rgba(52, 150, 84));
    palette.push_back(Color::rgba(232, 172, 40));
    palette.push_back(Color::rgba(120, 72, 176));
    palette.push_back(Color::rgba(32, 32, 36));
    palette.push_back(Color::rgba(128, 132, 140));
    palette.push_back(Color::rgba(250, 250, 248));
    return palette;
}

StockSpecimenInstance make_ribbon_color_picker(const StockSpecimen&, StableId id) {
    const std::shared_ptr<RibbonColorPicker> control = make_control<RibbonColorPicker>(std::move(id));
    (*control).set_palette(sample_palette());
    // A chosen colour, so the face's colour bar shows it (Automatic shows none).
    (*control).value = std::optional<Color>(Color::rgba(196, 60, 52));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

BackstageItemSpec backstage_command(std::string_view id, std::string_view text) {
    BackstageItemSpec item{};
    item.stable_id = std::string(id);
    item.kind = BackstageItemKind::command;
    item.command = make_command(id, text, {});
    item.text = std::string(text);
    return item;
}

StockSpecimenInstance make_backstage_view(const StockSpecimen&, StableId id) {
    const std::shared_ptr<BackstageView> control = make_control<BackstageView>(id);
    std::vector<BackstageItemSpec> items{};
    BackstageItemSpec info{};
    info.stable_id = "info";
    info.kind = BackstageItemKind::topic;
    info.text = "Info";
    const std::shared_ptr<Panel> page = make_control<Panel>(child_id(id, ".info"));
    add_sample_children(*page, child_id(id, ".info"));
    info.page = page;
    items.push_back(std::move(info));
    items.push_back(backstage_command("new", "New"));
    items.push_back(backstage_command("open", "Open"));
    items.push_back(backstage_command("save", "Save"));
    items.push_back(backstage_command("options", "Options"));
    (*control).set_items(std::move(items));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_ribbon_gallery(const StockSpecimen&, StableId id) {
    const std::shared_ptr<RibbonGallery> control = make_control<RibbonGallery>(std::move(id));
    std::vector<RibbonGalleryCategory> categories(1U);
    categories[0].stable_id = "styles";
    categories[0].caption = "Styles";
    const std::array<std::string_view, 6> labels{"Normal", "Heading 1", "Heading 2", "Quote", "Code", "Caption"};
    std::vector<RibbonGalleryItem> items(labels.size());
    for (std::size_t index = 0U; index < labels.size(); ++index) {
        items[index].stable_id = "style" + std::to_string(index);
        items[index].label = std::string(labels[index]);
        items[index].category = "styles";
    }
    (*control).set_items(std::move(categories), std::move(items));
    // The sample items have no images, so their labels are what shows.
    (*control).show_labels = true;
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_quick_access_toolbar(const StockSpecimen&, StableId id) {
    const std::shared_ptr<QuickAccessToolbar> control = make_control<QuickAccessToolbar>(std::move(id));
    std::vector<QuickAccessToolbarCommand> commands(3U);
    commands[0].command = make_command("save", "Save", "Ctrl+S");
    commands[1].command = make_command("undo", "Undo", "Ctrl+Z");
    commands[2].command = make_command("redo", "Redo", "Ctrl+Y");
    (*control).set_commands(std::move(commands));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

std::shared_ptr<ToolStrip> make_sample_strip(const StableId& id) {
    const std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(id);
    (*strip).set_items(sample_tool_items());
    return strip;
}

StockSpecimenInstance make_tool_strip_panel(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ToolStripPanel> control = make_control<ToolStripPanel>(id);
    (*control).join(make_sample_strip(child_id(id, ".standard")));
    (*control).join(make_sample_strip(child_id(id, ".format")));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_tool_strip_container(const StockSpecimen&, StableId id) {
    const std::shared_ptr<ToolStripContainer> control = make_control<ToolStripContainer>(id);
    (*(*control).top_panel()).join(make_sample_strip(child_id(id, ".standard")));
    add_sample_children(*(*control).content_panel(), child_id(id, ".content"));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

RibbonCommandSpec ribbon_button(std::string_view id, std::string_view text, RibbonCommandSize size,
                                RibbonCommandKind kind) {
    RibbonCommandSpec command{};
    command.stable_id = std::string(id);
    command.kind = kind;
    command.size = size;
    command.command = make_command(id, text, {});
    command.text = std::string(text);
    return command;
}

StockSpecimenInstance make_ribbon(const StockSpecimen&, StableId id) {
    const std::shared_ptr<Ribbon> control = make_control<Ribbon>(std::move(id));
    RibbonGroupSpec clipboard{};
    clipboard.stable_id = "clipboard";
    clipboard.caption = "Clipboard";
    clipboard.dialog_launcher = true;
    clipboard.commands.push_back(ribbon_button("paste", "Paste", RibbonCommandSize::large, RibbonCommandKind::button));
    clipboard.commands.push_back(ribbon_button("cut", "Cut", RibbonCommandSize::small, RibbonCommandKind::button));
    clipboard.commands.push_back(ribbon_button("copy", "Copy", RibbonCommandSize::small, RibbonCommandKind::button));
    RibbonGroupSpec format{};
    format.stable_id = "format";
    format.caption = "Format";
    format.commands.push_back(ribbon_button("bold", "Bold", RibbonCommandSize::small, RibbonCommandKind::toggle));
    format.commands.push_back(ribbon_button("italic", "Italic", RibbonCommandSize::small, RibbonCommandKind::toggle));
    format.commands.push_back(
        ribbon_button("underline", "Underline", RibbonCommandSize::small, RibbonCommandKind::toggle));
    RibbonGroupSpec theme{};
    theme.stable_id = "theme";
    theme.caption = "Theme";
    theme.commands.push_back(ribbon_button("compile", "Compile", RibbonCommandSize::large, RibbonCommandKind::button));
    theme.commands.push_back(ribbon_button("palette", "Palette", RibbonCommandSize::large, RibbonCommandKind::button));
    std::vector<RibbonTabSpec> tabs(3U);
    tabs[0].stable_id = "home";
    tabs[0].text = "Home";
    tabs[0].groups.push_back(std::move(clipboard));
    tabs[0].groups.push_back(std::move(format));
    tabs[0].groups.push_back(std::move(theme));
    tabs[1].stable_id = "insert";
    tabs[1].text = "Insert";
    tabs[2].stable_id = "view";
    tabs[2].text = "View";
    (*control).set_tabs(std::move(tabs));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// -- dialogs and overlays --------------------------------------------------------------

// The layer over a holder whose first child is its anchor button; the
// layer shows a list under the anchor.
StockSpecimenInstance make_anchored_overlay_layer(const StockSpecimen&, StableId id) {
    const std::shared_ptr<StockSpecimenHolder> holder = make_control<StockSpecimenHolder>(child_id(id, ".holder"));
    const std::shared_ptr<Button> anchor = make_control<Button>(child_id(id, ".anchor"), "Choose a port");
    (*anchor).set_requested_bounds(Rect{8.0, 8.0, 120.0, 28.0});
    (*holder).add_child(anchor);
    const std::shared_ptr<AnchoredOverlayLayer> layer =
        make_control<AnchoredOverlayLayer>(id, anchor, AnchoredOverlayPlacement{});
    const std::shared_ptr<ListBox> list = make_sample_list(child_id(id, ".list"));
    (*list).set_requested_bounds(Rect{0.0, 0.0, 160.0, 120.0});
    (*layer).set_content(list);
    (*holder).add_child(layer);
    StockSpecimenInstance instance = held(holder, layer, holder);
    return instance;
}

StockSpecimenInstance make_task_dialog(const StockSpecimen&, StableId id) {
    const std::shared_ptr<TaskDialog> control = make_control<TaskDialog>(std::move(id));
    TaskDialogPage page{};
    page.caption = "Theme editor";
    page.heading = "Save changes to Harbour.theme.css?";
    page.text = "Your changes are lost if you close without saving.";
    page.icon = TaskDialogIcon::warning;
    page.buttons.resize(3U);
    page.buttons[0].stable_id = "save";
    page.buttons[0].text = "Save";
    page.buttons[1].stable_id = "discard";
    page.buttons[1].text = "Don't save";
    page.buttons[2].stable_id = "cancel";
    page.buttons[2].standard = TaskDialogStandardButton::cancel;
    page.default_button = "save";
    page.verification_text = "Always save before closing";
    page.footnote = "Saved sheets are compiled again when opened.";
    page.allow_cancel = true;
    (*control).set_page(std::move(page));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

StockSpecimenInstance make_callout(const StockSpecimen& specimen, StableId id) {
    const std::shared_ptr<Callout> control = make_control<Callout>(std::move(id));
    write_text(*control, "title", "Panels");
    write_text(*control, "text", specimen.sample_text);
    write_text(*control, "action_text", "Got it");
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A form the layer is shown over (present_modal_layer); the layer itself
// is the subject and the form the frame.
StockSpecimenInstance make_modal_layer(const StockSpecimen&, StableId id) {
    const std::shared_ptr<Panel> holder = make_control<Panel>(child_id(id, ".form"));
    add_sample_children(*holder, child_id(id, ".form"));
    const std::shared_ptr<ModalLayer> layer = make_control<ModalLayer>(id);
    StockSpecimenInstance instance = held(holder, layer, holder);
    return instance;
}

// Shows the layer over the form with a question and two buttons. Throws
// std::runtime_error when the layer refuses.
void present_modal_layer(const StockSpecimenInstance& instance, Window& window) {
    const std::shared_ptr<ModalLayer> layer = std::dynamic_pointer_cast<ModalLayer>(instance.subject);
    if (!layer) throw std::logic_error("the modal layer specimen's subject is not a ModalLayer");
    const std::string base((*instance.subject).stable_id().value());
    const std::shared_ptr<Panel> content = make_control<Panel>(StableId(base + ".content"));
    (*content).set_requested_bounds(Rect{0.0, 0.0, 200.0, 96.0});
    const std::shared_ptr<Label> question = make_control<Label>(StableId(base + ".question"), "Discard the changes?");
    (*question).set_requested_bounds(Rect{14.0, 14.0, 172.0, 22.0});
    (*content).add_child(question);
    const std::shared_ptr<Button> discard = make_control<Button>(StableId(base + ".discard"), "Discard");
    (*discard).set_requested_bounds(Rect{14.0, 56.0, 80.0, 28.0});
    (*content).add_child(discard);
    const std::shared_ptr<Button> keep = make_control<Button>(StableId(base + ".keep"), "Keep");
    (*keep).set_requested_bounds(Rect{106.0, 56.0, 80.0, 28.0});
    (*content).add_child(keep);
    if (!(*layer).show(instance.root, content)) throw std::runtime_error("the modal layer specimen was not shown");
    window.perform_layout();
}

// -- drawing ------------------------------------------------------------------------

// Application content: fixed colours, the same in every theme.
void paint_sample_drawing(Painter& painter, Rect bounds, Rect) {
    painter.fill_rect(bounds, Color::rgba(250, 250, 247));
    const Color grid = Color::rgba(222, 226, 232);
    const std::size_t columns = static_cast<std::size_t>(bounds.width / 16.0);
    for (std::size_t column = 1U; column <= columns; ++column) {
        const double x = bounds.x + 16.0 * static_cast<double>(column);
        painter.draw_line(Point{x, bounds.y}, Point{x, bounds.y + bounds.height}, grid, 1.0);
    }
    const std::size_t rows = static_cast<std::size_t>(bounds.height / 16.0);
    for (std::size_t row = 1U; row <= rows; ++row) {
        const double y = bounds.y + 16.0 * static_cast<double>(row);
        painter.draw_line(Point{bounds.x, y}, Point{bounds.x + bounds.width, y}, grid, 1.0);
    }
    painter.fill_rounded_rect(Rect{bounds.x + 20.0, bounds.y + 24.0, 72.0, 48.0}, 6.0, Color::rgba(38, 110, 196));
    painter.fill_rounded_rect(Rect{bounds.x + 108.0, bounds.y + 56.0, 88.0, 56.0}, 6.0, Color::rgba(232, 172, 40));
    painter.stroke_rect(Rect{bounds.x + 56.0, bounds.y + 88.0, 64.0, 40.0}, Color::rgba(196, 60, 52), 2.0);
    painter.draw_line(Point{bounds.x + 20.0, bounds.y + 120.0}, Point{bounds.x + 200.0, bounds.y + 20.0},
                      Color::rgba(52, 150, 84), 2.0);
}

StockSpecimenInstance make_drawing_surface(const StockSpecimen&, StableId id) {
    const std::shared_ptr<DrawingSurface> control = make_control<DrawingSurface>(std::move(id));
    (*control).set_paint_callback(DrawingSurface::PaintCallback(&paint_sample_drawing));
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// A 240 x 144 opaque BGRA picture made from integers only: a sky gradient
// over a sea band with a sun, the same bytes on every platform.
constexpr std::uint32_t sample_picture_width = 240U;
constexpr std::uint32_t sample_picture_height = 144U;

// Fills `pixels`: sample_picture_height rows `row_bytes` apart, each
// sample_picture_width * 4 bytes, which the caller provides.
void fill_sample_picture(std::byte* pixels, std::size_t row_bytes) {
    for (std::uint32_t y = 0U; y < sample_picture_height; ++y) {
        std::byte* const row = pixels + static_cast<std::size_t>(y) * row_bytes;
        for (std::uint32_t x = 0U; x < sample_picture_width; ++x) {
            const std::uint32_t dx = x > 168U ? x - 168U : 168U - x;
            const std::uint32_t dy = y > 40U ? y - 40U : 40U - y;
            const bool sun = dx * dx + dy * dy < 18U * 18U;
            const bool sea = y >= 96U;
            std::uint32_t blue = sea ? 150U + (y - 96U) : 230U - y / 2U;
            std::uint32_t green = sea ? 96U + (x % 32U) : 190U - y / 3U;
            std::uint32_t red = sea ? 40U : 150U - y / 2U;
            if (sun) {
                blue = 80U;
                green = 200U;
                red = 250U;
            }
            std::byte* const pixel = row + static_cast<std::size_t>(x) * 4U;
            pixel[0] = static_cast<std::byte>(blue);
            pixel[1] = static_cast<std::byte>(green);
            pixel[2] = static_cast<std::byte>(red);
            pixel[3] = std::byte{0xff};
        }
    }
}

StockSpecimenInstance make_raster_canvas(const StockSpecimen&, StableId id) {
    const std::shared_ptr<RasterCanvas> control = make_control<RasterCanvas>(std::move(id));
    const std::shared_ptr<gui_drawing::Bitmap> bitmap =
        std::make_shared<gui_drawing::Bitmap>(sample_picture_width, sample_picture_height);
    const gui_drawing::RectI whole{0, 0, static_cast<std::int32_t>(sample_picture_width),
                                   static_cast<std::int32_t>(sample_picture_height)};
    const gui_drawing::BitmapEditView edit = (*bitmap).begin_edit(whole);
    fill_sample_picture(edit.writable_data, edit.row_bytes);
    static_cast<void>((*bitmap).commit_edit(edit.token));
    (*control).bitmap = bitmap;
    StockSpecimenInstance instance = alone(control);
    return instance;
}

// Loads the sample picture into the window's registry and shows it.
// Throws std::runtime_error when the window refuses the image.
void present_picture_box(const StockSpecimenInstance& instance, Window& window) {
    const std::shared_ptr<PictureBox> picture = std::dynamic_pointer_cast<PictureBox>(instance.subject);
    if (!picture) throw std::logic_error("the picture box specimen's subject is not a PictureBox");
    const std::size_t row_bytes = static_cast<std::size_t>(sample_picture_width) * 4U;
    std::vector<std::byte> pixels(row_bytes * sample_picture_height);
    fill_sample_picture(pixels.data(), row_bytes);
    const std::span<const std::byte> bytes(pixels);
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(sample_picture_width, sample_picture_height,
                                                                    static_cast<std::uint64_t>(row_bytes), bytes);
    if (!loaded) throw std::runtime_error("the picture box specimen's image was not loaded");
    (*picture).image = loaded.image;
    window.perform_layout();
}

// -- the table -----------------------------------------------------------------------

const StockSpecimen specimen_table[] = {
#include "stock_specimen_table.inc"
};

// Orders BGRA values for stock_specimen_dominant_share's run count.
bool pixel_before(std::uint32_t left, std::uint32_t right) {
    const bool before = left < right;
    return before;
}

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
    if (!instance.frame) instance.frame = instance.subject;
    return instance;
}

void present_stock_specimen(const StockSpecimen& specimen, const StockSpecimenInstance& instance, Window& window) {
    if (specimen.present == nullptr) return;
    specimen.present(instance, window);
}

double stock_specimen_dominant_share(const WindowCapture& capture, std::uint32_t inset_pixels) {
    const std::size_t width = capture.pixel_width;
    const std::size_t height = capture.pixel_height;
    const std::size_t inset = inset_pixels;
    if (width <= 2U * inset || height <= 2U * inset) return 1.0;
    // Width and height come from 32-bit fields: their product with 4 fits
    // a 64-bit size.
    const std::size_t needed = width * height * 4U;
    if (capture.bgra.size() < needed) return 1.0;
    const std::size_t inner_width = width - 2U * inset;
    const std::size_t inner_height = height - 2U * inset;
    std::vector<std::uint32_t> region{};
    region.reserve(inner_width * inner_height);
    for (std::size_t y = inset; y < height - inset; ++y) {
        const std::size_t row = y * width * 4U;
        for (std::size_t x = inset; x < width - inset; ++x) {
            const std::size_t at = row + x * 4U;
            std::uint32_t value = 0U;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const std::uint32_t byte = static_cast<std::uint32_t>(capture.bgra[at + channel]);
                value |= byte << (8U * channel);
            }
            region.push_back(value);
        }
    }
    std::sort(region.begin(), region.end(), &pixel_before);
    std::size_t longest = 1U;
    std::size_t run = 1U;
    for (std::size_t index = 1U; index < region.size(); ++index) {
        run = region[index] == region[index - 1U] ? run + 1U : 1U;
        longest = std::max(longest, run);
    }
    const double share = static_cast<double>(longest) / static_cast<double>(region.size());
    return share;
}

} // namespace gui_forms
