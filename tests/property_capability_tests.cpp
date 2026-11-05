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
        publish_change(caption_changed_, caption.type());
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
    BindingValue value{probe.caption.type()};
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
    BindingValue value{probe.delay.type()};
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
    expect((*probe).caption.type() == "probe" && changes == 0, "an equal write does nothing");
    (*probe).caption = "OK";
    expect((*probe).caption.type() == "OK" && changes == 1, "a write stores and raises the change event");
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
    expect((*probe).caption.type() == "by name", "a write by name reaches the cell");
    const std::optional<BindingValue> read = (*probe).property_value("Caption");
    expect(read && std::get<std::string>(*read) == "by name", "a read by name reads the cell");
    expect((*probe).should_serialize_property("caption"), "a value other than the default serializes");
    expect((*probe).property_value_origin("caption") == PropertyValueOrigin::local, "it is local");
    expect((*probe).reset_property("caption") && (*probe).caption.type() == "probe",
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
    expect((*probe).caption.type() == "probe", "neither write stored");
}

void test_view_and_component_table() {
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.view"));
    (*probe).echo = "engine";
    expect((*probe).echo.type() == "engine" && (*probe).echo_writes_ == 1, "a view writes and reads its owner");

    ProbeComponent component{};
    const PropertyEntry* const entry = find_property_entry(component, "delay");
    expect(entry != nullptr, "a component's table entry is found");
    write_property_entry(component, *entry, BindingValue{std::int64_t{750}});
    expect(component.delay.type() == 750 && component.delay_writes_ == 1, "a write by entry reaches the cell");
    const std::optional<BindingValue> read = read_property_entry(component, *entry);
    expect(read && std::get<std::int64_t>(*read) == 750, "a read by entry reads the cell");
    expect(property_entry_should_serialize(component, *entry), "a value other than the default serializes");
    expect(reset_property_entry(component, *entry) && component.delay.type() == 500, "reset writes the default");
    bool refused = false;
    try {
        component.delay = -1;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && component.delay.type() == 500, "the prepare hook refuses before the store");
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

} // namespace

int main() {
    test_cell_write_and_read();
    test_table_through_the_property_api();
    test_change_subscription_and_replacement();
    test_wrong_thread_write();
    test_view_and_component_table();
    test_window_touch();
    std::cout << "property capability tests passed\n";
    return EXIT_SUCCESS;
}
