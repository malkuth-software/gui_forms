// Property-capabilities migration M1: the property cell, a capability on its
// declaring class, and the type's constant property table read through the
// Control property API next to the instance registry. A write compares,
// stores, touches and raises the change event; an equal write does nothing;
// the table's entries are found by any spelling, set and read by name,
// reset, serialized, bound and listed in registered-name order, and a
// registration of the same name replaces the table's entry.
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "property capability failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

// A capability: its cell is its only member; `Declaring` owns the change
// event and the count of raised changes.
template <typename Declaring>
struct CaptionCapability {
    PropertyCell<std::string, CaptionCapability<Declaring>> caption;

    static void assign(CaptionCapability& capability, std::string value) {
        if (!capability.caption.replace(std::move(value))) return;
        Declaring& control = static_cast<Declaring&>(capability);
        control.invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
        const std::string& current = capability.caption.type();
        control.caption_changed_.emit(current);
    }
};

class ProbeControl final : public Control, public CaptionCapability<ProbeControl> {
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

private:
    struct CaptionOverrideGetter final {
        const ProbeControl* probe;
        BindingValue operator()() const { return BindingValue{std::string("override")}; }
    };
};

const PropertyDescriptor& describe_caption() {
    static const PropertyDescriptor described = property_table_descriptor(
        PropertyDescriptor{"Caption", BindingValueKind::text, "Appearance", "The probe's caption.",
                           BindingValue{std::string("probe")}, Dirty::measure | Dirty::paint | Dirty::semantics},
        PropertyOperations{true, true, false, true});
    return described;
}

BindingValue read_caption(const Control& control) {
    const ProbeControl& probe = static_cast<const ProbeControl&>(control);
    BindingValue value{probe.caption.type()};
    return value;
}

void write_caption(Control& control, const BindingValue& value) {
    ProbeControl& probe = static_cast<ProbeControl&>(control);
    probe.caption = std::get<std::string>(value);
}

struct CaptionForwarder final {
    std::function<void()> changed;
    void operator()(const std::string&) const { changed(); }
};

SubscriptionToken connect_caption(Control& control, Component& owner, std::function<void()> changed) {
    ProbeControl& probe = static_cast<ProbeControl&>(control);
    SubscriptionToken token = probe.caption_changed_.subscribe(owner, CaptionForwarder{std::move(changed)});
    return token;
}

constexpr std::array<PropertyEntry, 1> probe_table{{
    PropertyEntry{"caption", describe_caption, read_caption, write_caption, nullptr, nullptr, nullptr,
                  connect_caption},
}};
static_assert(property_names_distinct(probe_table), "the probe's property names are distinct");

std::span<const PropertyEntry> ProbeControl::property_table() const noexcept {
    return probe_table;
}

struct ChangeCounter final {
    int* count;
    void operator()(const std::string&) const { ++*count; }
};

void test_cell_write_and_read() {
    std::shared_ptr<ProbeControl> probe = make_control<ProbeControl>(StableId("probe.cell"));
    int changes = 0;
    const SubscriptionToken token = (*probe).caption_changed_.subscribe(ChangeCounter{&changes});
    (*probe).caption = "probe";
    expect((*probe).caption.type() == "probe" && changes == 1, "a write stores and raises the change event");
    (*probe).caption = "probe";
    expect(changes == 1, "an equal write does nothing");
    (*probe).caption = "OK";
    expect((*probe).caption.type() == "OK" && changes == 2, "a different write stores again");
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
    test_window_touch();
    std::cout << "property capability tests passed\n";
    return EXIT_SUCCESS;
}
