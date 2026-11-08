// Property-capabilities migration M1: the property cell, a capability on its
// declaring class, and the type's constant property table read through the
// Control property API next to the instance registry. A write compares,
// stores, touches and raises the change event; an equal write does nothing;
// the table's entries are found by any spelling, set and read by name,
// reset, serialized, bound and listed in registered-name order, and a
// registration of the same name replaces the table's entry.
// M2a (study section 14): the assign shape with its prepare and written
// hooks, the thread check before the compare (G4), a cell-less view (G2),
// an override of a base entry (G3), and a component's table read through
// the free entry operations (G7).
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "property capability failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

// A capability in the study section 14 shape: its cell is its only member;
// `assign` runs the declaring class's prepare hook (the thread and
// mutability check first, so an equal write from another thread still
// throws), compares and stores, then the written hook (touch, the change
// event through publish_change). The hooks are private overloads chosen by
// the cell's type; the declaring class befriends the capability.
template <typename Declaring>
struct CaptionCapability {
    PropertyCell<std::string, CaptionCapability<Declaring>> caption{std::string("probe")};

    static void assign(CaptionCapability& capability, std::string value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_write(std::as_const(capability.caption), value);
        if (!capability.caption.replace(std::move(value))) return;
        control.written(std::as_const(capability.caption));
    }
};

// A cell-less capability (G2): the value belongs to the declaring class's
// `shadow_`, standing for an engine; reads and writes reach it with the
// cell's spelling.
template <typename Declaring>
struct EchoCapability {
    PropertyView<std::string, EchoCapability<Declaring>> echo;

    static void assign(EchoCapability& capability, std::string value) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.write_echo(std::move(value));
    }
    static std::string read(const EchoCapability& capability) {
        const Declaring& control = static_cast<const Declaring&>(capability);
        std::string value = control.read_echo();
        return value;
    }
};

class ProbeControl final : public Control,
                           public CaptionCapability<ProbeControl>,
                           public EchoCapability<ProbeControl> {
public:
    explicit ProbeControl(StableId id) : Control(std::move(id)) {}

    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

    // Registers `caption` in the instance registry, as an application
    // subclass or a foreign-object adapter would; it replaces the entry.
    void register_caption_override() {
        PropertyDescriptor descriptor{"caption", BindingValueKind::text, "Override", "Replaced.",
                                      std::nullopt, Dirty::paint};
        descriptor.writable = false;
        define_bindable_property(PropertyRegistration{std::move(descriptor), CaptionOverrideGetter{this}, {}});
    }

    Event<const std::string&> caption_changed_;
    int echo_writes_{};

private:
    friend struct CaptionCapability<ProbeControl>;
    friend struct EchoCapability<ProbeControl>;
    using CaptionCell = PropertyCell<std::string, CaptionCapability<ProbeControl>>;

    void prepare_write(const CaptionCell&, std::string&) { require_mutable(); }
    void written(const CaptionCell&) {
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
        publish_change(caption_changed_, caption.read());
    }
    void write_echo(std::string value) {
        require_mutable();
        shadow_ = std::move(value);
        ++echo_writes_;
    }
    [[nodiscard]] std::string read_echo() const { return shadow_; }

    struct CaptionOverrideGetter final {
        const ProbeControl* probe;
        BindingValue operator()() const { return BindingValue{std::string("override")}; }
    };
    std::string shadow_{};
};

const PropertyDescriptor& describe_caption() {
    static const PropertyDescriptor described = property_table_descriptor(
        PropertyDescriptor{"Caption", BindingValueKind::text, "Appearance", "The probe's caption.",
                           BindingValue{std::string("probe")}, Dirty::measure | Dirty::paint | Dirty::semantics},
        PropertyOperations{true, true, false, true});
    return described;
}

const PropertyDescriptor& describe_refined_caption() {
    static const PropertyDescriptor described = property_table_descriptor(
        PropertyDescriptor{"Caption", BindingValueKind::text, "Refined", "The refined caption.",
                           BindingValue{std::string("probe")}, Dirty::paint},
        PropertyOperations{true, true, false, true});
    return described;
}

BindingValue read_caption(const Component& owner) {
    const ProbeControl& probe = static_cast<const ProbeControl&>(owner);
    BindingValue value{probe.caption.read()};
    return value;
}

void write_caption(Component& owner, const BindingValue& value) {
    ProbeControl& probe = static_cast<ProbeControl&>(owner);
    probe.caption = std::get<std::string>(value);
}

struct CaptionForwarder final {
    std::function<void()> changed;
    void operator()(const std::string&) const { changed(); }
};

SubscriptionToken connect_caption(Component& owner, Component& subscriber, std::function<void()> changed) {
    ProbeControl& probe = static_cast<ProbeControl&>(owner);
    SubscriptionToken token = probe.caption_changed_.subscribe(subscriber, CaptionForwarder{std::move(changed)});
    return token;
}

constexpr std::array<PropertyEntry, 1> probe_table{{
    PropertyEntry{"caption", describe_caption, read_caption, write_caption, nullptr, nullptr, nullptr,
                  connect_caption},
}};
static_assert(property_names_distinct(probe_table), "the probe's property names are distinct");

// G3: a derived type's refinement replaces the base entry in its slot.
constexpr std::array<PropertyEntry, 1> refined_overrides{{
    PropertyEntry{"caption", describe_refined_caption, read_caption, write_caption, nullptr, nullptr, nullptr,
                  connect_caption},
}};
constexpr std::array<PropertyEntry, 1> refined_table = override_property_entries(probe_table, refined_overrides);
static_assert(refined_table[0].describe == describe_refined_caption, "an override takes its base entry's slot");

std::span<const PropertyEntry> ProbeControl::property_table() const noexcept {
    return probe_table;
}

// G7: a component that is not a control owns a table too, read through the
// free entry operations.
template <typename Declaring>
struct DelayCapability {
    PropertyCell<std::int64_t, DelayCapability<Declaring>> delay{500};

    static void assign(DelayCapability& capability, std::int64_t value) {
        Declaring& component = static_cast<Declaring&>(capability);
        component.prepare_write(std::as_const(capability.delay), value);
        if (!capability.delay.replace(value)) return;
        ++component.delay_writes_;
    }
};

class ProbeComponent final : public Component, public DelayCapability<ProbeComponent> {
public:
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;
    int delay_writes_{};

private:
    friend struct DelayCapability<ProbeComponent>;
    using DelayCell = PropertyCell<std::int64_t, DelayCapability<ProbeComponent>>;
    void prepare_write(const DelayCell&, std::int64_t& value) {
        if (value < 0) throw std::invalid_argument("the probe's delay is not negative");
    }
};

const PropertyDescriptor& describe_delay() {
    static const PropertyDescriptor described = property_table_descriptor(
        PropertyDescriptor{"delay", BindingValueKind::signed_integer, "Behavior", "The probe's delay.",
                           BindingValue{std::int64_t{500}}, Dirty::none},
        PropertyOperations{true, true, false, false});
    return described;
}

BindingValue read_delay(const Component& owner) {
    const ProbeComponent& probe = static_cast<const ProbeComponent&>(owner);
    BindingValue value{probe.delay.read()};
    return value;
}

void write_delay(Component& owner, const BindingValue& value) {
    ProbeComponent& probe = static_cast<ProbeComponent&>(owner);
    probe.delay = std::get<std::int64_t>(value);
}

constexpr std::array<PropertyEntry, 1> probe_component_table{{
    PropertyEntry{"delay", describe_delay, read_delay, write_delay, nullptr, nullptr, nullptr, nullptr},
}};

std::span<const PropertyEntry> ProbeComponent::property_table() const noexcept {
    return probe_component_table;
}

// Writes the probe's caption from another thread: equal, then different.
struct ThreadWrites final {
    ProbeControl* probe;
    bool* equal_refused;
    bool* different_refused;
    void operator()() const {
        try {
            (*probe).caption = "probe";
        } catch (const std::logic_error&) {
            *equal_refused = true;
        }
        try {
            (*probe).caption = "other";
        } catch (const std::logic_error&) {
            *different_refused = true;
        }
    }
};

struct ChangeCounter final {
    int* count;
    void operator()(const std::string&) const { ++*count; }
};

void test_cell_write_and_read() {
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.cell"));
    int changes = 0;
    const SubscriptionToken token = (*probe).caption_changed_.subscribe(ChangeCounter{&changes});
    (*probe).caption = "probe";
    expect((*probe).caption.read() == "probe" && changes == 0, "an equal write does nothing");
    (*probe).caption = "OK";
    expect((*probe).caption.read() == "OK" && changes == 1, "a write stores and raises the change event");
    (*probe).caption = "OK";
    expect(changes == 1, "an equal write after it does nothing either");
    static_cast<void>(token);
}

void test_table_through_the_property_api() {
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.table"));
    expect((*probe).has_bindable_property("Caption"), "the table's entry is found by any spelling");
    const std::optional<PropertyDescriptor> descriptor = (*probe).property_descriptor("caption");
    expect(descriptor && (*descriptor).name == "caption" && (*descriptor).change_notifications &&
               (*descriptor).resettable,
           "the entry's descriptor is checked and derived like a registration's");
    (*probe).set_property_value("caption", BindingValue{std::string("by name")});
    expect((*probe).caption.read() == "by name", "a write by name reaches the cell");
    const std::optional<BindingValue> read = (*probe).property_value("Caption");
    expect(read && std::get<std::string>(*read) == "by name", "a read by name reads the cell");
    expect((*probe).should_serialize_property("caption"), "a value other than the default serializes");
    expect((*probe).property_value_origin("caption") == PropertyValueOrigin::local, "it is local");
    expect((*probe).reset_property("caption") && (*probe).caption.read() == "probe",
           "reset writes the default");
    expect(!(*probe).should_serialize_property("caption"), "the default does not serialize");

    const std::vector<PropertyDescriptor> all = (*probe).property_descriptors();
    bool ordered = true;
    bool listed = false;
    for (std::size_t index = 0U; index < all.size(); ++index) {
        if (all[index].name == "caption") listed = true;
        if (index > 0U && !(all[index - 1U].name < all[index].name)) ordered = false;
    }
    expect(listed && ordered, "the table's entries list with the registry's, in registered-name order");
}

struct SubscriptionCount final {
    int* count;
    void operator()() const { ++*count; }
};

void test_change_subscription_and_replacement() {
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.subscribe"));
    std::shared_ptr<Panel> owner = make_control<Panel>(StableId("probe.owner"));
    int notified = 0;
    const SubscriptionToken token =
        (*probe).subscribe_property_changed("caption", *owner, SubscriptionCount{&notified});
    (*probe).caption = "changed";
    expect(notified == 1, "a table entry's change event reaches a property subscription");
    static_cast<void>(token);

    (*probe).register_caption_override();
    const std::optional<PropertyDescriptor> replaced = (*probe).property_descriptor("caption");
    expect(replaced && (*replaced).category == "Override", "a registration replaces the table's entry");
    const std::optional<BindingValue> read = (*probe).property_value("caption");
    expect(read && std::get<std::string>(*read) == "override", "reads go to the registration");
    std::size_t listed = 0U;
    for (const PropertyDescriptor& descriptor : (*probe).property_descriptors()) {
        if (descriptor.name == "caption") ++listed;
    }
    expect(listed == 1U, "a replaced entry is listed once");
}

// G4: the thread check comes before the compare, so an equal write from
// another thread throws like a different one, and neither stores.
void test_wrong_thread_write() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.thread.root"));
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.thread"));
    (*root).add_child(probe);
    Window window(root, Size{120.0, 80.0});
    bool equal_refused = false;
    bool different_refused = false;
    std::thread other{ThreadWrites{probe.get(), &equal_refused, &different_refused}};
    other.join();
    expect(equal_refused, "an equal write from another thread throws");
    expect(different_refused, "a different write from another thread throws");
    expect((*probe).caption.read() == "probe", "neither write stored");
}

void test_view_and_component_table() {
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.view"));
    (*probe).echo = "engine";
    expect((*probe).echo.read() == "engine" && (*probe).echo_writes_ == 1, "a view writes and reads its owner");

    ProbeComponent component{};
    const PropertyEntry* const entry = find_property_entry(component, "delay");
    expect(entry != nullptr, "a component's table entry is found");
    write_property_entry(component, *entry, BindingValue{std::int64_t{750}});
    expect(component.delay.read() == 750 && component.delay_writes_ == 1, "a write by entry reaches the cell");
    const std::optional<BindingValue> read = read_property_entry(component, *entry);
    expect(read && std::get<std::int64_t>(*read) == 750, "a read by entry reads the cell");
    expect(property_entry_should_serialize(component, *entry), "a value other than the default serializes");
    expect(reset_property_entry(component, *entry) && component.delay.read() == 500, "reset writes the default");
    bool refused = false;
    try {
        component.delay = -1;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && component.delay.read() == 500, "the prepare hook refuses before the store");
}

void test_window_touch() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.root"));
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.attached"));
    (*root).add_child(probe);
    Window window(root, Size{120.0, 80.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    (*probe).caption = "touched";
    expect(window.control_table().pending((*probe).surface_slot(), detail::DirtyKind::paint),
           "an attached write touches its control");
}


// Control's family (M2): a derived control sees a changed write through
// the P-8 virtual after the base's touch; the base raises the event.
class VisibilityProbe final : public Control {
public:
    explicit VisibilityProbe(StableId stable_id) : Control(std::move(stable_id)) {}

    int visible_calls_{};
    bool event_seen_before_override_{};
    int* events_{};

protected:
    void on_visible_changed() override {
        Control::on_visible_changed();
        ++visible_calls_;
        event_seen_before_override_ = events_ != nullptr && *events_ == visible_calls_;
    }
};

struct BoolChangeCounter final {
    int* count;
    void operator()(bool) const { ++*count; }
};

void test_control_family() {
    std::shared_ptr<VisibilityProbe> probe = make_control<VisibilityProbe>(StableId("family.probe"));
    int events = 0;
    (*probe).events_ = &events;
    const SubscriptionToken token = (*probe).visible_changed().subscribe(BoolChangeCounter{&events});
    (*probe).visible = true;
    expect((*probe).visible_calls_ == 0 && events == 0, "an equal write raises nothing");
    (*probe).visible = false;
    expect(!(*probe).visible.read() && (*probe).visible_calls_ == 1 && events == 1,
           "a changed write runs on_visible_changed once and raises visible_changed once");
    expect((*probe).event_seen_before_override_, "the base raises the event before the override's own work");
    static_cast<void>(token);

    expect((*probe).name.read() == "family.probe", "name starts as the stable id");
    const std::optional<PropertyDescriptor> name = (*probe).property_descriptor("name");
    // The default is the stable ID, which differs per control, so the shared
    // descriptor states none (C-8; the entry compares with the stable ID).
    expect(name && !(*name).default_value, "name's descriptor states no default; the stable id is its default");

    expect(Control::control_property_entries().size() == Control::control_property_count,
           "Control's table holds every migrated property");
    const std::optional<PropertyDescriptor> visible = (*probe).property_descriptor("visible");
    expect(visible && (*visible).bindable && (*visible).change_notifications && (*visible).invalidates_subtree,
           "visible's table descriptor matches its old registration");
    const std::optional<PropertyDescriptor> margin = (*probe).property_descriptor("margin");
    expect(margin && !(*margin).bindable && (*margin).resettable, "structural properties stay unbindable");

    (*probe).set_property_value("margin", BindingValue{Insets{1.0, 2.0, 3.0, 4.0}});
    expect((*probe).margin.read() == Insets{1.0, 2.0, 3.0, 4.0}, "a write by name reaches margin's cell");
    bool refused = false;
    try {
        (*probe).margin = Insets{-1.0, 0.0, 0.0, 0.0};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && (*probe).margin.read() == Insets{1.0, 2.0, 3.0, 4.0}, "a refused margin changes nothing");
    expect((*probe).reset_property("margin") && (*probe).margin.read() == Insets{3.0, 3.0, 3.0, 3.0},
           "reset writes margin's default");

    (*probe).back_color = Color::rgba(10, 20, 30);
    expect((*probe).should_serialize_property("back_color"), "an own back colour serializes");
    expect((*probe).reset_property("back_color") && !(*probe).back_color.read(), "reset clears the back colour");
}

// G11: a themed Panel's face takes its own back_color (outside high
// contrast), and clearing it returns the material.
void test_panel_back_color_when_themed() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("family.panel"));
    Window window(root, Size{120.0, 80.0});
    const Color themed = (*root).background();
    const Color own = Color::rgba(200, 30, 30);
    (*root).back_color = own;
    expect((*root).background() == own, "a themed panel's face is its back colour");
    (*root).back_color = std::nullopt;
    expect((*root).background() == themed, "clearing the back colour returns the material's ground");
}


class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

class PaintCounter final : public Control {
public:
    explicit PaintCounter(StableId stable_id) : Control(std::move(stable_id)) {}

    int paints_{};

    void on_paint(Painter&, Rect) override { ++paints_; }
};

// The named setters forward to the cells and store (a forwarder whose
// parameter shadows the cell once assigned the parameter to itself, so
// nothing was ever hidden or disabled).
void test_setters_store() {
    std::shared_ptr<Control> control = make_control<PaintCounter>(StableId("family.setters"));
    (*control).name = "renamed";
    (*control).visible = false;
    (*control).enabled = false;
    (*control).auto_size = true;
    (*control).causes_validation = false;
    (*control).tab_stop = false;
    (*control).allow_drop = true;
    (*control).hit_test_transparent = true;
    (*control).tab_index = 7U;
    (*control).margin = Insets{1.0, 1.0, 1.0, 1.0};
    (*control).padding = Insets{2.0, 2.0, 2.0, 2.0};
    (*control).dock = DockStyle::fill;
    (*control).anchor = AnchorStyles::right;
    (*control).auto_size_mode = AutoSizeMode::grow_and_shrink;
    (*control).layout_direction = LayoutDirection::right_to_left;
    (*control).minimum_size = Size{4.0, 4.0};
    (*control).maximum_size = Size{40.0, 40.0};
    (*control).cursor = CursorKind::hand;
    (*control).back_color = Color::rgba(1, 2, 3);
    (*control).fore_color = Color::rgba(4, 5, 6);
    (*control).accessible_name = "a";
    (*control).accessible_description = "d";
    (*control).tool_tip_text = "t";
    expect((*control).name.read() == "renamed" && !(*control).visible.read() && !(*control).enabled.read() &&
               (*control).auto_size.read() && !(*control).causes_validation.read() && !(*control).tab_stop.read() &&
               (*control).allow_drop.read() && (*control).hit_test_transparent.read() &&
               (*control).tab_index.read() == 7U && (*control).margin.read() == Insets{1.0, 1.0, 1.0, 1.0} &&
               (*control).padding.read() == Insets{2.0, 2.0, 2.0, 2.0} &&
               (*control).dock.read() == DockStyle::fill && (*control).anchor.read() == AnchorStyles::right &&
               (*control).auto_size_mode.read() == AutoSizeMode::grow_and_shrink &&
               (*control).layout_direction.read() == LayoutDirection::right_to_left &&
               (*control).minimum_size.read() == Size{4.0, 4.0} &&
               (*control).maximum_size.read() == Size{40.0, 40.0} &&
               (*control).cursor.read() == CursorKind::hand && (*control).back_color.read() &&
               (*control).fore_color.read() && (*control).accessible_name.read() == "a" &&
               (*control).accessible_description.read() == "d" && (*control).tool_tip_text.read() == "t",
           "every named setter stores its value");
}

// A hidden control (a Gallery page, a tab page) is neither painted nor hit.
void test_hidden_is_not_painted_or_hit() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("family.hidden.root"));
    std::shared_ptr<PaintCounter> page = make_control<PaintCounter>(StableId("family.hidden.page"));
    (*page).dock = DockStyle::fill;
    (*root).add_child(page);
    Window window(root, Size{120.0, 80.0});
    window.perform_layout();
    NullPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 120.0, 80.0}));
    const Point centre{60.0, 40.0};
    expect((*page).paints_ == 1 && window.hit_test(centre) == page, "a shown page is painted and hit");
    (*page).visible = false;
    window.perform_layout();
    static_cast<void>(window.paint(painter, {0.0, 0.0, 120.0, 80.0}));
    expect((*page).paints_ == 1, "a hidden page is not painted");
    expect(window.hit_test(centre) != page, "a hidden page is not hit");
}

} // namespace

// Study section 15: type() is the runtime type; as_type(types::x) offers
// total conversions only and agrees with the descriptor conversion of the
// same pair; as_type(PropertyType) returns an explicit result.
template <typename Cell, typename Target>
concept total_read = requires(const Cell& cell, TypeTag<Target> tag) { cell.as_type(tag); };

void test_property_type() {
    std::shared_ptr<Panel> panel = make_control<Panel>(StableId("family.typed"));
    (*panel).tab_index = 7U;
    const PropertyType int64_type{BindingValueKind::signed_integer, false, nullptr};
    const PropertyType text_type{BindingValueKind::text, false, nullptr};
    const PropertyType boolean_type{BindingValueKind::boolean, false, nullptr};

    expect((*panel).tab_index.type().kind == BindingValueKind::unsigned_integer, "tab_index is unsigned");
    const std::int64_t widened = (*panel).tab_index.as_type(types::int64);
    const std::optional<BindingValue> described = (*panel).tab_index.as_type(int64_type);
    expect(widened == 7 && described && std::get<std::int64_t>(*described) == widened,
           "the int64 tag and the int64 type agree");

    const std::string shown = (*panel).visible.as_type(types::string);
    const std::optional<BindingValue> shown_described = (*panel).visible.as_type(text_type);
    expect(shown_described && std::get<std::string>(*shown_described) == shown, "the string tag and type agree");

    (*panel).tool_tip_text = std::string("not a boolean");
    expect(!(*panel).tool_tip_text.as_type(boolean_type), "a failed conversion is an explicit empty result");

    (*panel).dock = DockStyle::fill;
    const PropertyType dock_type = (*panel).dock.type();
    expect(dock_type.kind == BindingValueKind::enumeration && dock_type.enumeration &&
               (*dock_type.enumeration).type_name == "dock_style",
           "an enumeration's type carries its choice set");
    const std::int64_t dock_number = (*panel).dock.as_type(types::int64);
    const std::optional<BindingValue> dock_choice = (*panel).dock.as_type(dock_type);
    expect(dock_number == 5 && dock_choice && std::get<PropertyEnumValue>(*dock_choice).name == "fill",
           "an enumeration reads as its number and as its named choice");

    expect((*panel).cursor.type().kind == BindingValueKind::enumeration && !(*panel).cursor.type().nullable,
           "the cursor travels as cursor_kind, inherit included");
    const BindingValue inherit = (*panel).cursor.as_type(types::binding_value);
    expect(std::get<PropertyEnumValue>(inherit).name == "inherit", "an empty cursor reads as inherit");

    expect((*panel).back_color.type().nullable, "an optional colour is nullable");
    const BindingValue no_colour = (*panel).back_color.as_type(types::binding_value);
    expect(std::holds_alternative<std::monostate>(no_colour), "an empty colour reads as null");

    static_assert(total_read<decltype((*panel).tab_index), std::int64_t>);
    static_assert(!total_read<decltype((*panel).name), bool>, "text to boolean can fail: no tag read");
    static_assert(!total_read<decltype((*panel).tab_index), std::int32_t>, "narrowing has no tag read");
}

// justify_self and align_self (owner-approved, study 10.9 Q4 (a)): ordinary
// Control properties over SelfAlignment, automatic by default, written by
// cell or by name; an out-of-range value is refused and changes nothing; a
// write dirties the attached control's measure, which reaches its parent.
void test_self_alignment() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("self.root"));
    std::shared_ptr<Panel> child = make_control<Panel>(StableId("self.child"));
    (*root).add_child(child);
    Window window(root, Size{120.0, 80.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    expect((*child).justify_self.read() == SelfAlignment::automatic &&
               (*child).align_self.read() == SelfAlignment::automatic,
           "both self alignments start automatic");
    expect(!(*child).should_serialize_property("justify_self"), "the default does not serialize");

    (*child).justify_self = SelfAlignment::center;
    expect(window.control_table().pending((*child).surface_slot(), detail::DirtyKind::layout),
           "a self alignment write dirties the control's layout");
    const std::optional<BindingValue> read = (*child).property_value("justify_self");
    expect(read && std::get<PropertyEnumValue>(*read).name == "center" &&
               (*child).should_serialize_property("justify_self"),
           "the write reads back by name as its choice and serializes");

    const std::optional<PropertyDescriptor> descriptor = (*child).property_descriptor("align_self");
    expect(descriptor && (*descriptor).category == "Layout" && (*descriptor).enumeration &&
               (*(*descriptor).enumeration).type_name == "self_alignment",
           "align_self is a Layout enumeration over self_alignment");
    (*child).set_property_value("align_self", BindingValue{PropertyEnumValue{"self_alignment", "stretch", 4}});
    expect((*child).align_self.read() == SelfAlignment::stretch, "a write by name reaches the cell");

    bool refused = false;
    try {
        (*child).align_self = static_cast<SelfAlignment>(9U);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && (*child).align_self.read() == SelfAlignment::stretch,
           "an out-of-range self alignment is refused and changes nothing");
    expect((*child).reset_property("justify_self") &&
               (*child).justify_self.read() == SelfAlignment::automatic,
           "reset writes automatic");
}

int main() {
    test_cell_write_and_read();
    test_table_through_the_property_api();
    test_change_subscription_and_replacement();
    test_wrong_thread_write();
    test_view_and_component_table();
    test_window_touch();
    test_control_family();
    test_panel_back_color_when_themed();
    test_setters_store();
    test_hidden_is_not_painted_or_hit();
    test_property_type();
    test_self_alignment();
    std::cout << "property capability tests passed\n";
    return EXIT_SUCCESS;
}
