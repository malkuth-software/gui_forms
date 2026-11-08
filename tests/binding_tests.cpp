#include "gui_forms/gui_forms.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "support/named_callbacks.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;
namespace callbacks = gui_forms::test_support;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    Fixture()
        : root(make_control<Panel>(StableId("binding.root"))),
          editor(make_control<TextBox>(StableId("binding.editor"))),
          enabled(make_control<CheckBox>(StableId("binding.enabled"), "Enabled")),
          gain(make_control<TrackBar>(StableId("binding.gain"))),
          caption(make_control<Label>(StableId("binding.caption"))),
          window(root, {640.0, 240.0}),
          source(std::make_shared<BindingSource>(window)) {
        (*root).set_requested_bounds({0.0, 0.0, 640.0, 240.0});
        (*editor).set_requested_bounds({20.0, 20.0, 220.0, 32.0});
        (*enabled).set_requested_bounds({20.0, 70.0, 180.0, 28.0});
        (*gain).set_requested_bounds({20.0, 115.0, 300.0, 32.0});
        (*caption).set_requested_bounds({350.0, 20.0, 240.0, 32.0});
        (*root).add_child(editor);
        (*root).add_child(enabled);
        (*root).add_child(gain);
        (*root).add_child(caption);
        (*source).set_records({
            {"record.alpha", {{"name", std::string("Alpha")},
                               {"enabled", true}, {"gain", 24.0}}},
            {"record.beta", {{"name", std::string("Beta")},
                              {"enabled", false}, {"gain", 72.0}}},
        });
    }

    std::shared_ptr<Panel> root;
    std::shared_ptr<TextBox> editor;
    std::shared_ptr<CheckBox> enabled;
    std::shared_ptr<TrackBar> gain;
    std::shared_ptr<Label> caption;
    Window window;
    std::shared_ptr<BindingSource> source;
};

class PropertyProbe final : public Control {
public:
    explicit PropertyProbe(StableId stable_id) : Control(std::move(stable_id)) {
        PropertyDescriptor level;
        level.name = "Level";
        level.kind = BindingValueKind::signed_integer;
        level.category = "Data";
        level.description = "Probe level with an authored reset policy.";
        level.default_value = BindingValue{std::int64_t{3}};
        level.invalidation_effects = Dirty::paint | Dirty::semantics;
        level.bindable = false;
        PropertyRegistration level_registration;
        level_registration.descriptor = std::move(level);
        level_registration.get = detail::BoundMemberFunction<
            BindingValue (PropertyProbe::*)() const>(
                *this, &PropertyProbe::level_property_value);
        level_registration.set = detail::BoundMemberFunction<
            void (PropertyProbe::*)(const BindingValue&)>(
                *this, &PropertyProbe::set_level_property);
        level_registration.reset = detail::BoundMemberFunction<
            void (PropertyProbe::*)()>(*this, &PropertyProbe::reset_level);
        level_registration.should_serialize = detail::BoundMemberFunction<
            bool (PropertyProbe::*)() const noexcept>(
                *this, &PropertyProbe::should_serialize_level);
        define_bindable_property(std::move(level_registration));

        PropertyDescriptor revision;
        revision.name = "Revision";
        revision.kind = BindingValueKind::unsigned_integer;
        revision.category = "Diagnostics";
        revision.description = "Read-only retained revision.";
        revision.serialization_visibility =
            PropertySerializationVisibility::hidden;
        revision.writable = false;
        revision.browsable = false;
        revision.bindable = false;
        PropertyRegistration revision_registration;
        revision_registration.descriptor = std::move(revision);
        revision_registration.get = detail::BoundMemberFunction<
            BindingValue (PropertyProbe::*)() const>(
                *this, &PropertyProbe::revision_property_value);
        define_bindable_property(std::move(revision_registration));
    }

    [[nodiscard]] std::int64_t level() const noexcept { return level_; }

private:
    [[nodiscard]] BindingValue level_property_value() const {
        return BindingValue{level_};
    }

    void set_level_property(const BindingValue& value) {
        level_ = std::get<std::int64_t>(value);
        invalidate(Dirty::paint | Dirty::semantics);
    }

    void reset_level() {
        level_ = 3;
        invalidate(Dirty::paint | Dirty::semantics);
    }

    [[nodiscard]] bool should_serialize_level() const noexcept {
        return level_ > 3;
    }

    [[nodiscard]] BindingValue revision_property_value() const {
        return BindingValue{revision_};
    }

    std::int64_t level_{3};
    std::uint64_t revision_{11};
};

class InvalidPropertyProbe final : public Control {
public:
    explicit InvalidPropertyProbe(StableId stable_id)
        : Control(std::move(stable_id)) {
        PropertyDescriptor invalid;
        invalid.name = "Invalid";
        invalid.kind = BindingValueKind::number;
        invalid.default_value = BindingValue{std::string("not-a-number")};
        PropertyRegistration registration;
        registration.descriptor = std::move(invalid);
        registration.get = &InvalidPropertyProbe::invalid_property_value;
        registration.set = &InvalidPropertyProbe::ignore_invalid_property;
        define_bindable_property(std::move(registration));
    }

private:
    [[nodiscard]] static BindingValue invalid_property_value() {
        return BindingValue{0.0};
    }

    static void ignore_invalid_property(const BindingValue&) {}
};

PropertyObjectValue make_endpoint_value() {
    return make_property_object("Endpoint", {
        {"Host", "Server host", BindingValue{std::string("127.0.0.1")}},
        {"Port", "Server port", BindingValue{std::uint64_t{5555}}},
    });
}

class RecordInitializationCompletion final {
public:
    explicit RecordInitializationCompletion(
        std::vector<std::pair<Dirty, bool>>& completions) noexcept
        : completions_(completions) {}

    void operator()(Dirty dirty, bool subtree) const {
        completions_.emplace_back(dirty, subtree);
    }

private:
    std::vector<std::pair<Dirty, bool>>& completions_;
};

class PropertyAccessWorker final {
public:
    PropertyAccessWorker(const std::shared_ptr<TextBox>& editor,
                         bool& query_rejected,
                         bool& mutation_rejected) noexcept
        : editor_(editor), query_rejected_(query_rejected),
          mutation_rejected_(mutation_rejected) {}

    void operator()() const {
        try {
            static_cast<void>((*editor_).property_value("Text"));
        } catch (const std::logic_error&) {
            query_rejected_ = true;
        }
        try {
            (*editor_).set_property_value(
                "Text", std::string("wrong-thread"));
        } catch (const std::logic_error&) {
            mutation_rejected_ = true;
        }
    }

private:
    const std::shared_ptr<TextBox>& editor_;
    bool& query_rejected_;
    bool& mutation_rejected_;
};

class TracePositionChange final {
public:
    TracePositionChange(const std::shared_ptr<TextBox>& editor,
                        std::vector<std::string>& order) noexcept
        : editor_(editor), order_(order) {}

    void operator()(std::ptrdiff_t value) const {
        require((*editor_).text.read() == "Beta",
                "binding propagation must precede public PositionChanged");
        order_.push_back("position:" + std::to_string(value));
    }

private:
    const std::shared_ptr<TextBox>& editor_;
    std::vector<std::string>& order_;
};

class RecordCompletionState final {
public:
    explicit RecordCompletionState(
        std::vector<BindingCompleteState>& states) noexcept
        : states_(states) {}

    void operator()(BindingCompleteEvent& event) const {
        states_.push_back(event.state);
    }

private:
    std::vector<BindingCompleteState>& states_;
};

class ParseDollarAmount final {
public:
    void operator()(BindingConvertEvent& event) const {
        const std::string* text = std::get_if<std::string>(&event.value);
        if (text != nullptr && *text == "$13.25") {
            event.value = 13.25;
            event.handled = true;
        }
    }
};

class RecordBindingContextChange final {
public:
    explicit RecordBindingContextChange(std::vector<bool>& changes) noexcept
        : changes_(changes) {}

    void operator()(const BindingContextChange& change) const {
        changes_.push_back(change.added);
    }

private:
    std::vector<bool>& changes_;
};

class ObserveCommittedCombo final {
public:
    ObserveCommittedCombo(const std::shared_ptr<ComboBox>& combo,
                          bool& observed) noexcept
        : combo_(combo), observed_(observed) {}

    void operator()(BindingCompleteEvent& event) const {
        if (event.context == BindingCompleteContext::control_update &&
            event.state == BindingCompleteState::success) {
            observed_ = (*combo_).selected_index.read() == 2U;
        }
    }

private:
    const std::shared_ptr<ComboBox>& combo_;
    bool& observed_;
};

class ObserveManagerCommittedCombo final {
public:
    ObserveManagerCommittedCombo(const std::shared_ptr<Binding>& binding,
                                 const std::shared_ptr<ComboBox>& combo,
                                 bool& observed) noexcept
        : binding_(binding), combo_(combo), observed_(observed) {}

    void operator()(BindingCompleteEvent& event) const {
        if (event.binding == binding_.get() &&
            event.context == BindingCompleteContext::control_update &&
            event.state == BindingCompleteState::success) {
            observed_ = (*combo_).selected_index.read() == 2U;
        }
    }

private:
    const std::shared_ptr<Binding>& binding_;
    const std::shared_ptr<ComboBox>& combo_;
    bool& observed_;
};

class ObserveCommittedSourceAmount final {
public:
    ObserveCommittedSourceAmount(const std::shared_ptr<BindingSource>& source,
                                 bool& observed) noexcept
        : source_(source), observed_(observed) {}

    void operator()(BindingCompleteEvent& event) const {
        if (event.context == BindingCompleteContext::data_source_update &&
            event.state == BindingCompleteState::success) {
            observed_ = (*source_).current_field("amount") ==
                std::optional<BindingValue>{64.0};
        }
    }

private:
    const std::shared_ptr<BindingSource>& source_;
    bool& observed_;
};

void test_value_conversion_is_strict_and_invariant() {
    require(binding_value_kind(BindingValue{}) == BindingValueKind::null &&
                binding_value_to_bool(std::string("TRUE")) == true &&
                binding_value_to_signed(std::string("-42")) == -42 &&
                binding_value_to_unsigned(std::string("42")) == 42U &&
                binding_value_to_number(std::string("12.5")) == 12.5,
            "binding scalar conversion must cover null, Boolean, integer, and invariant number values");
    require(!binding_value_to_unsigned(std::string("-1")) &&
                !binding_value_to_number(std::string("12.5ms")) &&
                !binding_value_to_signed(2.5) &&
                canonical_binding_name("  DataMember  ") == "datamember",
            "binding conversion must reject partial, negative-unsigned, and fractional-integer input");
    require(binding_value_kind(BindingValue{Point{1.0, 2.0}}) ==
                BindingValueKind::point &&
                binding_value_kind(BindingValue{Size{3.0, 4.0}}) ==
                    BindingValueKind::size &&
                binding_value_kind(BindingValue{Rect{1.0, 2.0, 3.0, 4.0}}) ==
                    BindingValueKind::rectangle &&
                binding_value_kind(BindingValue{Insets{1.0, 2.0, 3.0, 4.0}}) ==
                    BindingValueKind::insets &&
                binding_value_kind(BindingValue{Color::rgba(1, 2, 3, 4)}) ==
                    BindingValueKind::color &&
                binding_value_kind(BindingValue{FontSpec{}}) ==
                    BindingValueKind::font &&
                binding_value_kind(BindingValue{ImageId{9}}) ==
                    BindingValueKind::image &&
                binding_value_to_string(BindingValue{
                    Color::rgba(1, 2, 3, 4)}) == "#01020304" &&
                !convert_binding_value(
                    BindingValue{Point{
                        std::numeric_limits<double>::quiet_NaN(), 0.0}},
                    BindingValueKind::point),
            "property values must retain typed geometry, color, font, image, and finite-value validation");
}

void test_nested_property_values_are_bounded_immutable_and_structural() {
    const PropertyObjectValue first_endpoint = make_endpoint_value();
    const PropertyObjectValue second_endpoint = make_endpoint_value();
    const PropertyCollectionValue first_items = make_property_collection(
        "String", BindingValueKind::text,
        {BindingValue{std::string("Local")},
         BindingValue{std::string("Archive")}});
    const PropertyCollectionValue second_items = make_property_collection(
        "String", BindingValueKind::text,
        {BindingValue{std::string("Local")},
         BindingValue{std::string("Archive")}});
    require(first_endpoint == second_endpoint && first_items == second_items &&
                first_endpoint.data() != second_endpoint.data() &&
                first_items.data() != second_items.data() &&
                binding_value_kind(BindingValue{first_endpoint}) ==
                    BindingValueKind::object &&
                binding_value_kind(BindingValue{first_items}) ==
                    BindingValueKind::collection &&
                valid_property_value_tree(BindingValue{first_endpoint}) &&
                valid_property_value_tree(BindingValue{first_items}),
            "nested property snapshots must compare structurally without sharing mutable storage");

    const PropertyCollectionValue converted = make_property_collection(
        "Double", BindingValueKind::number,
        {BindingValue{std::string("1.25")}, BindingValue{std::int64_t{2}}});
    const std::span<const BindingValue> converted_items = property_collection_items(converted);
    require(converted_items.size() == 2U &&
                std::get<double>(converted_items[0]) == 1.25 &&
                std::get<double>(converted_items[1]) == 2.0,
            "homogeneous property collections must normalize every item to their declared kind");

    bool duplicate_rejected{};
    bool heterogeneous_rejected{};
    bool depth_rejected{};
    try {
        static_cast<void>(make_property_object("Duplicate", {
            {"Name", {}, BindingValue{std::string("A")}},
            {"name", {}, BindingValue{std::string("B")}},
        }));
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    try {
        static_cast<void>(make_property_collection(
            "Double", BindingValueKind::number,
            {BindingValue{std::string("1")},
             BindingValue{std::string("not a number")}}));
    } catch (const std::invalid_argument&) {
        heterogeneous_rejected = true;
    }
    try {
        BindingValue nested{std::string("leaf")};
        for (std::size_t depth = 0U;
             depth <= maximum_property_value_depth; ++depth) {
            nested = BindingValue{make_property_object(
                "Node", {{"Child", {}, std::move(nested)}})};
        }
    } catch (const std::invalid_argument&) {
        depth_rejected = true;
    }
    require(duplicate_rejected && heterogeneous_rejected && depth_rejected,
            "nested property construction must reject ambiguous names, failed homogeneous conversion, and over-depth trees");
}

// A property has one registered name, stored in snake_case; its CSS and
// display spellings are derived, and every spelling finds the property.
void test_property_names_are_registered_in_snake_case() {
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("property.names"));
    const std::optional<PropertyDescriptor> by_registered = (*panel).property_descriptor("auto_size_mode");
    const std::optional<PropertyDescriptor> by_css = (*panel).property_descriptor("auto-size-mode");
    const std::optional<PropertyDescriptor> by_display = (*panel).property_descriptor("AutoSizeMode");
    require(by_registered && by_css && by_display && (*by_registered).name == "auto_size_mode" &&
                (*by_css).name == "auto_size_mode" && (*by_display).name == "auto_size_mode",
            "a property is stored in snake_case and found by any of its spellings");
    require(display_spelling((*by_registered).name) == "AutoSizeMode",
            "PropertyGrid's label is the derived display spelling");
    const std::vector<PropertyDescriptor> descriptors = (*panel).property_descriptors();
    for (const PropertyDescriptor& descriptor : descriptors) {
        require(is_registered_name(descriptor.name), "every stock property name is snake_case");
    }
    PropertyDescriptor size_mode;
    size_mode.kind = BindingValueKind::enumeration;
    size_mode.enumeration = (*(*panel).property_descriptor("auto_size_mode")).enumeration;
    const std::optional<BindingValue> grow =
        convert_property_value(BindingValue{std::string("GrowAndShrink")}, size_mode);
    const PropertyEnumValue* grow_value = grow ? std::get_if<PropertyEnumValue>(&*grow) : nullptr;
    require(grow_value != nullptr && (*grow_value).name == "grow_and_shrink" &&
                (*grow_value).type_name == "auto_size_mode",
            "an enumeration value written in any spelling is stored by its registered name");
}

void test_property_metadata_defaults_reset_and_serialization() {
    std::shared_ptr<gui_forms::TextBox> editor = make_control<TextBox>(StableId("property.editor"), "Seed");
    const std::optional<PropertyDescriptor> text = (*editor).property_descriptor(" text ");
    require(text && (*text).name == "text" &&
                (*text).kind == BindingValueKind::text &&
                (*text).category == "Appearance" && (*text).readable &&
                (*text).writable && (*text).browsable && (*text).bindable &&
                (*text).resettable && (*text).change_notifications &&
                (*text).default_value ==
                    std::optional<BindingValue>{std::string{}} &&
                has_dirty((*text).invalidation_effects, Dirty::measure) &&
                has_dirty((*text).invalidation_effects, Dirty::paint) &&
                !(*text).invalidates_subtree &&
                (*(*editor).property_descriptor("Visible")).invalidates_subtree,
            "stock property metadata must expose presentation name, type, default, reset, and exact declared effects");
    require((*editor).property_value("TEXT") ==
                std::optional<BindingValue>{std::string("Seed")} &&
                (*editor).should_serialize_property("Text"),
            "property lookup must be canonical while retaining typed current state");
    Component observer;
    std::size_t text_changes{};
    SubscriptionToken changed = (*editor).subscribe_property_changed(
        "Text", observer,
        callbacks::IncrementCounter<std::size_t>(text_changes));
    (*editor).set_property_value("text", std::uint64_t{42});
    require((*editor).text.read() == "42" && (*editor).reset_property("TEXT") &&
                (*editor).text.read().empty() &&
                !(*editor).should_serialize_property("Text") &&
                changed.connected() && text_changes == 2U,
            "generic mutation, reset, and tokenized observation must use the real typed control property path");

    std::shared_ptr<gui_forms::TrackBar> range = make_control<TrackBar>(StableId("property.range"));
    (*range).set_value(25.0);
    bool invalid_value_rejected = false;
    try {
        (*range).set_property_value("Value", std::string("not-a-number"));
    } catch (const std::invalid_argument&) {
        invalid_value_rejected = true;
    }
    require(invalid_value_rejected && (*range).value() == 25.0,
            "failed generic conversion must leave the retained property unchanged");

    const std::vector<std::string> names = (*editor).bindable_property_names();
    require(names == std::vector<std::string>{
                "auto_size", "back_color", "causes_validation", "enabled", "fore_color",
                "name", "text", "tool_tip_text", "visible"},
            "bindable property enumeration must be deterministic and use the registered names");
    const std::vector<PropertyDescriptor> descriptors = (*editor).property_descriptors();
    require(descriptors.size() == 37U &&
                descriptors.front().name == "accessible_description" &&
                descriptors.back().name == "visible",
            "property descriptor snapshots must follow deterministic canonical order");

    const std::optional<PropertyDescriptor> bounds = (*editor).property_descriptor("Bounds");
    const std::optional<PropertyDescriptor> dock = (*editor).property_descriptor("Dock");
    const std::optional<PropertyDescriptor> anchor = (*editor).property_descriptor("Anchor");
    require(bounds && (*bounds).kind == BindingValueKind::rectangle &&
                !(*bounds).bindable && (*bounds).resettable &&
                dock && (*dock).kind == BindingValueKind::enumeration &&
                (*dock).enumeration && !(*(*dock).enumeration).flags &&
                (*(*dock).enumeration).choices.size() == 6U &&
                anchor && (*anchor).enumeration && (*(*anchor).enumeration).flags,
            "compound and enum descriptors must expose typed schema without pretending unsupported change binding");

    (*editor).set_property_value("Bounds", Rect{4.0, 5.0, 120.0, 32.0});
    (*editor).set_property_value("Margin", Insets{1.0, 2.0, 3.0, 4.0});
    (*editor).set_property_value("Dock", std::string("Fill"));
    (*editor).set_property_value("Anchor", std::string("Bottom | Right"));
    (*editor).set_property_value("AutoSizeMode", std::string("GrowAndShrink"));
    require((*editor).requested_bounds() == Rect{4.0, 5.0, 120.0, 32.0} &&
                (*editor).margin.read() == Insets{1.0, 2.0, 3.0, 4.0} &&
                (*editor).dock.read() == DockStyle::fill &&
                (*editor).anchor.read() ==
                    (AnchorStyles::bottom | AnchorStyles::right) &&
                (*editor).auto_size_mode.read() == AutoSizeMode::grow_and_shrink &&
                std::get<PropertyEnumValue>(
                    *(*editor).property_value("Anchor")).name ==
                    "bottom, right" &&
                (*editor).should_serialize_property("Bounds") &&
                (*editor).should_serialize_property("Dock"),
            "generic property access must drive real retained compound, enum, and flags behavior");

    bool bad_enum_rejected = false;
    bool bad_geometry_rejected = false;
    try {
        (*editor).set_property_value("Dock", std::string("Floating"));
    } catch (const std::invalid_argument&) {
        bad_enum_rejected = true;
    }
    try {
        (*editor).set_property_value("Bounds", Rect{0.0, 0.0, -1.0, 2.0});
    } catch (const std::invalid_argument&) {
        bad_geometry_rejected = true;
    }
    require(bad_enum_rejected && bad_geometry_rejected &&
                (*editor).dock.read() == DockStyle::fill &&
                (*editor).requested_bounds() == Rect{4.0, 5.0, 120.0, 32.0},
            "invalid enum and geometry values must fail before retained state mutation");
    require((*editor).reset_property("Bounds") &&
                (*editor).reset_property("Margin") &&
                (*editor).reset_property("Dock") &&
                (*editor).reset_property("Anchor") &&
                (*editor).reset_property("AutoSizeMode") &&
                (*editor).requested_bounds() == Rect{} &&
                (*editor).margin.read() == Insets{3.0, 3.0, 3.0, 3.0} &&
                (*editor).dock.read() == DockStyle::none &&
                (*editor).anchor.read() ==
                    (AnchorStyles::top | AnchorStyles::left) &&
                (*editor).auto_size_mode.read() == AutoSizeMode::grow_only &&
                !(*editor).should_serialize_property("Bounds"),
            "compound and enum defaults must reset through the same typed retained setter path");

    std::shared_ptr<PropertyProbe> probe =
        make_control<PropertyProbe>(StableId("property.probe"));
    require((*probe).property_descriptor("Level").has_value() &&
                !(*probe).has_bindable_property("Level") &&
                !(*probe).should_serialize_property("Level") &&
                (*probe).property_value_origin("Level") ==
                    PropertyValueOrigin::defaulted,
            "inspection properties may deliberately remain outside data binding");
    (*probe).set_property_value("Level", std::string("2"));
    require((*probe).level() == 2 &&
                !(*probe).should_serialize_property("Level") &&
                (*probe).property_value_origin("Level") ==
                    PropertyValueOrigin::local,
            "authored ShouldSerialize policy and local value origin must remain independent");
    (*probe).set_property_value("Level", std::int64_t{7});
    require((*probe).should_serialize_property("Level") &&
                (*probe).reset_property("Level") && (*probe).level() == 3,
            "custom reset and serialization callbacks must be executable through the same registry");
    const std::optional<PropertyDescriptor> revision = (*probe).property_descriptor("Revision");
    require(revision && !(*revision).writable && !(*revision).browsable &&
                (*revision).serialization_visibility ==
                    PropertySerializationVisibility::hidden &&
                !(*probe).should_serialize_property("Revision") &&
                (*probe).property_value_origin("Revision") ==
                    PropertyValueOrigin::computed &&
                !(*probe).reset_property("Revision"),
            "read-only hidden metadata must never acquire an accidental reset or serialized value");

    bool invalid_default_rejected = false;
    try {
        static_cast<void>(make_control<InvalidPropertyProbe>(
            StableId("property.invalid")));
    } catch (const std::invalid_argument&) {
        invalid_default_rejected = true;
    }
    require(invalid_default_rejected,
            "property registration must reject a default incompatible with its declared kind");
}

void test_property_enum_schema_bounds_and_unicode() {
    PropertyEnumDescriptor descriptor{
        "FixtureFlags", {{"None", 0}, {"Alpha", 1}, {"Beta", 2}}, true};
    require(valid_property_enum_descriptor(descriptor),
            "a bounded UTF-8 flags schema must be valid");

    descriptor.choices.resize(maximum_property_enum_choices + 1U,
                              {"Choice", 1});
    require(!valid_property_enum_descriptor(descriptor),
            "enum schemas must reject collections beyond the public bound");

    descriptor = {"FixtureFlags", {{std::string(257U, 'x'), 1}}, true};
    require(!valid_property_enum_descriptor(descriptor),
            "enum schemas must reject unbounded choice names");

    descriptor = {std::string("Fixture\xFF", 8U), {{"Alpha", 1}}, true};
    require(!valid_property_enum_descriptor(descriptor),
            "enum schemas must reject invalid UTF-8 type identity");
}

void test_property_mutation_initialization_thread_and_lifetime_guards() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("property.root"));
    std::shared_ptr<gui_forms::TextBox> editor = make_control<TextBox>(StableId("property.guarded"));
    (*root).add_child(editor);
    Window window(root, {320.0, 120.0});

    std::vector<std::pair<Dirty, bool>> completions;
    SubscriptionToken completed =
        (*editor).initialization_completed().subscribe(
            RecordInitializationCompletion(completions));
    (*editor).begin_init();
    (*editor).set_property_value("Text", std::string("batched"));
    (*editor).set_property_value("Visible", false);
    require(completions.empty() && (*editor).initializing(),
            "generic property setters must participate in retained initialization batching");
    (*editor).end_init();
    require(completions.size() == 1U && completions.front().second &&
                has_dirty(completions.front().first, Dirty::measure) &&
                has_dirty(completions.front().first, Dirty::paint) &&
                (*editor).text.read() == "batched" && !(*editor).visible.read(),
            "the final EndInit must publish one union of real setter effects");

    bool query_rejected = false;
    bool mutation_rejected = false;
    std::thread worker(
        PropertyAccessWorker(editor, query_rejected, mutation_rejected));
    worker.join();
    require(query_rejected && mutation_rejected && (*editor).text.read() == "batched",
            "attached generic property reads and writes must enforce UI-thread ownership");

    (*editor).dispose();
    bool disposed_query_rejected = false;
    bool disposed_reset_rejected = false;
    try {
        static_cast<void>((*editor).property_value("Text"));
    } catch (const std::logic_error&) {
        disposed_query_rejected = true;
    }
    try {
        static_cast<void>((*editor).reset_property("Text"));
    } catch (const std::logic_error&) {
        disposed_reset_rejected = true;
    }
    require(disposed_query_rejected && disposed_reset_rejected &&
                (*editor).property_descriptor("Text").has_value(),
            "disposed controls must reject executable property access while retaining inert metadata inspection");
    static_cast<void>(completed);
}

void test_visual_property_values_drive_stock_controls() {
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("property.label"), "Metadata");
    const FontSpec inherited_font = (*label).resolved_font();
    const Color inherited_color = (*label).foreground();
    require((*(*label).property_descriptor("Font")).kind ==
                BindingValueKind::font &&
                (*(*label).property_descriptor("ForeColor")).kind ==
                    BindingValueKind::color &&
                !(*label).should_serialize_property("Font") &&
                !(*label).should_serialize_property("ForeColor"),
            "inherited visual properties must expose their effective value without serializing an absent override");
    const FontSpec authored{FontRole::content, 15.0, 650, true, 0.2};
    const Color authored_color = Color::rgba(12, 34, 56, 220);
    (*label).set_property_value("Font", authored);
    (*label).set_property_value("ForeColor", authored_color);
    require((*label).resolved_font() == authored && (*label).foreground() == authored_color &&
                (*label).should_serialize_property("Font") &&
                (*label).should_serialize_property("ForeColor") &&
                (*label).reset_property("Font") &&
                (*label).reset_property("ForeColor") &&
                (*label).resolved_font() == inherited_font &&
                (*label).foreground() == inherited_color,
            "font and color overrides must reset to live theme inheritance rather than a copied fallback");

    std::shared_ptr<gui_forms::PictureBox> picture = make_control<PictureBox>(StableId("property.picture"));
    Component observer;
    std::size_t image_changes{};
    SubscriptionToken changed = (*picture).subscribe_property_changed(
        "Image", observer,
        callbacks::IncrementCounter<std::size_t>(image_changes));
    (*picture).set_property_value("Image", std::uint64_t{42});
    (*picture).set_property_value("SizeMode", std::string("Zoom"));
    (*picture).set_property_value("ImageOpacity", 0.375);
    require((*picture).image.read() == ImageId{42} &&
                (*picture).size_mode.read() == PictureBoxSizeMode::zoom &&
                (*picture).image_opacity.read() == 0.375 && image_changes == 1U &&
                (*picture).should_serialize_property("Image") &&
                (*picture).should_serialize_property("SizeMode") &&
                changed.connected(),
            "image identity, image policy, opacity, and tokenized change notification must share the typed registry");
    require((*picture).reset_property("Image") &&
                (*picture).reset_property("SizeMode") &&
                (*picture).reset_property("ImageOpacity") &&
                (*picture).image.read() == ImageId{} &&
                (*picture).size_mode.read() == PictureBoxSizeMode::normal &&
                (*picture).image_opacity.read() == 1.0 && image_changes == 2U,
            "visual resource properties must have deterministic default reset behavior");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("property.visual.root"));
    (*root).add_child(picture);
    Window window(root, {200.0, 120.0});
    std::shared_ptr<gui_forms::BindingSource> source = std::make_shared<BindingSource>(window);
    (*source).set_records({
        {"property.visual.record", {{"image", std::uint64_t{77}}}},
    });
    const std::shared_ptr<Binding> image_binding = (*picture).data_bindings().add(
        "Image", source, "image");
    require(image_binding && (*picture).image.read() == ImageId{77},
            "Binding must use descriptor-aware unsigned-to-ImageId conversion rather than scalar-only conversion");

    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("property.button"), "Run");
    (*button).set_property_value("Font", authored);
    (*button).set_property_value("Image", ImageId{7});
    require((*button).font.read() == authored && (*button).image() == ImageId{7} &&
                (*button).reset_property("Font") &&
                (*button).reset_property("Image") &&
                (*button).font.read() ==
                    FontSpec{FontRole::control, 12.0, 400, false, 0.24} &&
                (*button).image() == ImageId{},
            "button visual values must use the same compound default/reset center");
    static_cast<void>(image_binding);
}

void test_multi_control_two_way_currency_and_event_order() {
    Fixture fixture;
    BindingOptions immediate;
    immediate.data_source_update_mode =
        DataSourceUpdateMode::on_property_changed;
    const std::shared_ptr<Binding> text = (*fixture.editor).data_bindings().add(
        "Text", fixture.source, "Name", immediate);
    const std::shared_ptr<Binding> check = (*fixture.enabled).data_bindings().add(
        "Checked", fixture.source, "Enabled", immediate);
    const std::shared_ptr<Binding> range = (*fixture.gain).data_bindings().add(
        "Value", fixture.source, "Gain", immediate);
    const std::shared_ptr<Binding> label = (*fixture.caption).data_bindings().add(
        "Text", fixture.source, "Name");
    require(text && check && range && label &&
                (*fixture.editor).text.read() == "Alpha" && (*fixture.enabled).checked.read() &&
                (*fixture.gain).value() == 24.0 && (*fixture.caption).text.read() == "Alpha",
            "adding stock bindings must immediately project the current record into every control kind");

    (*fixture.editor).text = "Alpha edited";
    (*fixture.enabled).checked = false;
    (*fixture.gain).set_value(31.0);
    require((*fixture.source).current_field("name") ==
                std::optional<BindingValue>{std::string("Alpha edited")} &&
                (*fixture.source).current_field("enabled") ==
                    std::optional<BindingValue>{false} &&
                (*fixture.source).current_field("gain") ==
                    std::optional<BindingValue>{31.0},
            "OnPropertyChanged bindings must commit control mutations to the current retained record");

    std::vector<std::string> order;
    SubscriptionToken position =
        (*fixture.source).position_changed().subscribe(
            TracePositionChange(fixture.editor, order));
    SubscriptionToken current =
        (*fixture.source).current_changed().subscribe(
            callbacks::PushConstant<std::vector<std::string>, std::string>(
                order, "current"));
    SubscriptionToken item =
        (*fixture.source).current_item_changed().subscribe(
            callbacks::PushConstant<std::vector<std::string>, std::string>(
                order, "item"));
    require((*fixture.source).currency_manager().set_position(1) &&
                (*(*fixture.source).currency_manager().current()).stable_id ==
                    "record.beta" && (*fixture.editor).text.read() == "Beta" &&
                !(*fixture.enabled).checked.read() && (*fixture.gain).value() == 72.0 &&
                order == std::vector<std::string>{
                    "position:1", "current", "item"},
            "currency movement must update controls then emit the chosen deterministic event order");
    static_cast<void>(position);
    static_cast<void>(current);
    static_cast<void>(item);
}

void test_update_modes_suspension_and_explicit_transfers() {
    Fixture fixture;
    BindingOptions validation;
    validation.data_source_update_mode = DataSourceUpdateMode::on_validation;
    const std::shared_ptr<Binding> binding = (*fixture.editor).data_bindings().add(
        "Text", fixture.source, "Name", validation);
    (*fixture.editor).text = "pending";
    require((*fixture.source).current_field("name") ==
                std::optional<BindingValue>{std::string("Alpha")},
            "OnValidation must retain an editor-side pending value");
    require((*binding).validate() && (*fixture.source).current_field("name") ==
                std::optional<BindingValue>{std::string("pending")},
            "explicit validation must commit an OnValidation binding");

    (*fixture.source).currency_manager().suspend_binding();
    (*fixture.source).set_current_field("name", std::string("suspended"));
    (*fixture.source).set_current_field("name", std::string("coalesced"));
    require((*fixture.editor).text.read() == "pending" &&
                (*fixture.source).snapshot().suspended_mutations == 2U &&
                (*fixture.source).currency_manager().binding_suspended(),
            "SuspendBinding must retain logical mutations without pushing controls");
    (*fixture.source).currency_manager().resume_binding();
    require((*fixture.editor).text.read() == "coalesced" &&
                !(*fixture.source).binding_suspended(),
            "ResumeBinding must publish one current reset after suspended mutations");

    Fixture never_fixture;
    BindingOptions never;
    never.control_update_mode = ControlUpdateMode::never;
    never.data_source_update_mode = DataSourceUpdateMode::never;
    const std::shared_ptr<Binding> never_binding = (*never_fixture.editor).data_bindings().add(
        "Text", never_fixture.source, "Name", never);
    require((*never_fixture.editor).text.read().empty(),
            "ControlUpdateMode.Never must suppress the initial automatic read");
    require((*never_binding).read_value() && (*never_fixture.editor).text.read() == "Alpha",
            "ReadValue must remain an explicit transfer under Never");
    (*never_fixture.editor).text = "manual";
    require((*never_fixture.source).current_field("name") ==
                std::optional<BindingValue>{std::string("Alpha")} &&
                (*never_binding).write_value() &&
                (*never_fixture.source).current_field("name") ==
                    std::optional<BindingValue>{std::string("manual")},
            "WriteValue must remain an explicit transfer under Never");
}

void test_format_parse_failure_rollback_and_completion() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("format.root"));
    std::shared_ptr<gui_forms::TextBox> editor = make_control<TextBox>(StableId("format.editor"));
    (*root).add_child(editor);
    Window window(root, {320.0, 120.0});
    std::shared_ptr<gui_forms::BindingSource> source = std::make_shared<BindingSource>(window);
    (*source).set_records({{"format.record", {{"amount", 12.5}}}});
    BindingOptions options;
    options.formatting_enabled = true;
    options.format_string = "F2";
    options.data_source_update_mode = DataSourceUpdateMode::on_property_changed;
    const std::shared_ptr<Binding> binding = (*editor).data_bindings().add(
        "Text", source, "Amount", options);
    require((*editor).text.read() == "12.50",
            "invariant fixed formatting must project numeric data into text");

    std::vector<BindingCompleteState> completions;
    std::vector<std::string> errors;
    SubscriptionToken completed = (*binding).binding_complete().subscribe(
        RecordCompletionState(completions));
    SubscriptionToken data_error = (*source).data_error().subscribe(
        callbacks::PushBack<std::vector<std::string>,
                            const std::string&>(errors));
    (*editor).text = "not-a-number";
    require((*source).current_field("amount") ==
                std::optional<BindingValue>{12.5} &&
                !completions.empty() &&
                completions.back() == BindingCompleteState::exception &&
                errors.size() == 1U,
            "failed parsing must retain the authoritative source and publish completion plus DataError");

    SubscriptionToken parser =
        (*binding).parse().subscribe(ParseDollarAmount());
    (*editor).text = "$13.25";
    require((*source).current_field("amount") ==
                std::optional<BindingValue>{13.25},
            "a handled Parse event must provide a typed source value without reflection");
    static_cast<void>(completed);
    static_cast<void>(data_error);
    static_cast<void>(parser);
}

// The standard numeric formats beyond F: grouped and percent text, an
// integer's exact digits, and typed text going back as the number it shows.
void test_number_formats_and_their_text() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("number.root"));
    std::shared_ptr<gui_forms::TextBox> amount = make_control<TextBox>(StableId("number.amount"));
    std::shared_ptr<gui_forms::TextBox> count = make_control<TextBox>(StableId("number.count"));
    (*root).add_child(amount);
    (*root).add_child(count);
    Window window(root, {320.0, 120.0});
    std::shared_ptr<gui_forms::BindingSource> source = std::make_shared<BindingSource>(window);
    (*source).set_records({{"number.record",
                            {{"amount", 1234.5}, {"count", std::int64_t{9007199254740993}}}}});
    BindingOptions grouped;
    grouped.formatting_enabled = true;
    grouped.format_string = "N2";
    grouped.data_source_update_mode = DataSourceUpdateMode::on_property_changed;
    const std::shared_ptr<Binding> amount_binding = (*amount).data_bindings().add(
        "Text", source, "Amount", grouped);
    BindingOptions whole = grouped;
    whole.format_string = "N0";
    const std::shared_ptr<Binding> count_binding = (*count).data_bindings().add(
        "Text", source, "Count", whole);
    require((*amount).text.read() == "1,234.50" && (*count).text.read() == "9,007,199,254,740,993",
            "N formats group the digits, an integer's from its exact value");
    (*amount).text = "2,000.25";
    (*count).text = "12,345";
    require((*source).current_field("amount") == std::optional<BindingValue>{2000.25} &&
                (*source).current_field("count") == std::optional<BindingValue>{std::int64_t{12345}},
            "grouped text goes back as the number it shows");
    BindingOptions unknown = grouped;
    unknown.format_string = "X4";
    bool unknown_refused = false;
    try {
        static_cast<void>((*amount).data_bindings().add("Text", source, "Amount", unknown));
    } catch (const std::invalid_argument&) {
        unknown_refused = true;
    }
    require(unknown_refused, "a format that is not a standard numeric format is refused");
    static_cast<void>(amount_binding);
    static_cast<void>(count_binding);
}

void test_edit_transactions_list_mutation_and_context() {
    Fixture fixture;
    BindingManagerBase& manager = (*fixture.source).currency_manager();
    require(manager.count() == 2U && manager.current() == (*fixture.source).current() &&
                (*fixture.source).begin_edit() &&
                (*fixture.source).set_current_field("name", std::string("draft")),
            "editable current record must establish a deterministic snapshot");
    manager.cancel_current_edit();
    require((*fixture.source).current_field("name") ==
                std::optional<BindingValue>{std::string("Alpha")} &&
                !(*fixture.source).snapshot().editing,
            "CancelEdit must restore the exact retained record snapshot");
    (*fixture.source).set_current_field("name", std::string("committed"));
    manager.end_current_edit();
    manager.cancel_current_edit();
    require((*fixture.source).current_field("name") ==
                std::optional<BindingValue>{std::string("committed")},
            "EndEdit must retire rollback state without changing committed fields");

    const std::size_t inserted = (*fixture.source).insert(
        1U, {"record.middle", {{"name", std::string("Middle")}}});
    require(inserted == 1U && (*fixture.source).count() == 3U &&
                (*fixture.source).find("name", std::string("Middle")) == 1U,
            "BindingSource must support stable insertion and field search");
    require(manager.remove_at(1U) &&
                (*fixture.source).count() == 2U,
            "BindingSource removal must normalize currency without stale identities");

    BindingContext context(fixture.window);
    std::vector<bool> changes;
    SubscriptionToken observed = context.collection_changed().subscribe(
        RecordBindingContextChange(changes));
    require(&context.manager(fixture.source) ==
                &(*fixture.source).currency_manager() &&
                context.contains(*fixture.source) && context.size() == 1U &&
                context.remove(*fixture.source) &&
                changes == std::vector<bool>{true, false},
            "BindingContext must own a real same-window manager registry and ordered change events");
    static_cast<void>(observed);
}

void test_collection_uniqueness_base_properties_and_cleanup() {
    Fixture fixture;
    (*fixture.source).set_current_field("visible", false);
    (*fixture.source).set_current_field("enabled_property", false);
    (*fixture.source).set_current_field("control_name", std::string("bound.editor"));
    const std::shared_ptr<Binding> visible = (*fixture.editor).data_bindings().add(
        "Visible", fixture.source, "visible");
    const std::shared_ptr<Binding> enabled = (*fixture.editor).data_bindings().add(
        "Enabled", fixture.source, "enabled_property");
    const std::shared_ptr<Binding> name = (*fixture.editor).data_bindings().add(
        "Name", fixture.source, "control_name");
    require(visible && enabled && name && !(*fixture.editor).visible.read() &&
                !(*fixture.editor).enabled.read() && (*fixture.editor).name.read() == "bound.editor",
            "base Control visibility, enabled state, and name must be first-class bindable properties");

    bool duplicate_rejected = false;
    try {
        static_cast<void>((*fixture.editor).data_bindings().add(
            "text", fixture.source, "name"));
        static_cast<void>((*fixture.editor).data_bindings().add(
            "TEXT", fixture.source, "name"));
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "ControlBindingsCollection must reject duplicate canonical target properties");

    const std::shared_ptr<Binding> cleanup = (*fixture.caption).data_bindings().add(
        "Text", fixture.source, "Name");
    (*fixture.caption).dispose();
    require((*cleanup).is_disposed(),
            "disposing a target Control must synchronously dispose its bindings");
    (*fixture.source).dispose();
    require(!(*visible).active() && !(*enabled).active() && !(*name).active(),
            "disposing a BindingSource must deactivate every surviving endpoint without dangling access");
}

void test_choice_numeric_defaults_manager_transfer_and_completion_order() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("advanced.root"));
    std::shared_ptr<gui_forms::ComboBox> combo = make_control<ComboBox>(StableId("advanced.combo"));
    std::shared_ptr<gui_forms::NumericUpDown> numeric = make_control<NumericUpDown>(StableId("advanced.numeric"));
    std::shared_ptr<gui_forms::TextBox> deferred = make_control<TextBox>(StableId("advanced.deferred"));
    (*combo).items = {"Local", "Archive", "Plugins"};
    (*root).add_child(combo);
    (*root).add_child(numeric);
    (*root).add_child(deferred);
    Window window(root, {480.0, 180.0});
    std::shared_ptr<gui_forms::BindingSource> source = std::make_shared<BindingSource>(window);
    (*source).set_records({
        {"advanced.record", {{"choice", std::int64_t{1}},
                              {"amount", 42.0},
                              {"name", std::string("Alpha")}}},
    });

    BindingOptions formatted_immediate;
    formatted_immediate.formatting_enabled = true;
    formatted_immediate.data_source_update_mode =
        DataSourceUpdateMode::on_property_changed;
    const std::shared_ptr<Binding> choice = (*combo).data_bindings().add(
        "SelectedIndex", source, "choice", formatted_immediate);
    const std::shared_ptr<Binding> amount = (*numeric).data_bindings().add(
        "Value", source, "amount", formatted_immediate);
    require((*combo).selected_index.read() == 1U && (*numeric).value.read() == 42.0,
            "ComboBox.SelectedIndex and NumericUpDown.Value must be stock bindable properties");

    bool binding_saw_committed_control = false;
    bool manager_saw_committed_control = false;
    SubscriptionToken binding_complete =
        (*choice).binding_complete().subscribe(
            ObserveCommittedCombo(combo, binding_saw_committed_control));
    SubscriptionToken manager_complete =
        (*source).currency_manager().binding_complete().subscribe(
            ObserveManagerCommittedCombo(
                choice, combo, manager_saw_committed_control));
    (*source).set_current_field("choice", std::int64_t{2});
    require(binding_saw_committed_control && manager_saw_committed_control,
            "BindingComplete must run after the destination commit and propagate through the currency manager");

    bool source_was_committed_before_completion = false;
    SubscriptionToken source_complete =
        (*amount).binding_complete().subscribe(
            ObserveCommittedSourceAmount(
                source, source_was_committed_before_completion));
    (*numeric).value = 64.0;
    require(source_was_committed_before_completion,
            "data-source BindingComplete must observe the already committed retained record");

    (*deferred).data_bindings().set_default_data_source_update_mode(
        DataSourceUpdateMode::on_property_changed);
    const std::shared_ptr<Binding> defaulted = (*deferred).data_bindings().add(
        "Text", source, "name");
    (*deferred).text = "Default immediate";
    require((*source).current_field("name") ==
                std::optional<BindingValue>{std::string("Default immediate")},
            "the no-options Add overload must use the collection default update mode");

    BindingOptions explicit_validation;
    explicit_validation.data_source_update_mode =
        DataSourceUpdateMode::on_validation;
    (*defaulted).dispose();
    (*deferred).data_bindings().clear();
    const std::shared_ptr<Binding> validation = (*deferred).data_bindings().add(
        "Text", source, "name", explicit_validation);
    (*deferred).text = "Still pending";
    require((*source).current_field("name") ==
                std::optional<BindingValue>{std::string("Default immediate")},
            "an explicit OnValidation option must not be overwritten by the collection default");
    require((*source).currency_manager().pull_data() &&
                (*source).current_field("name") ==
                    std::optional<BindingValue>{std::string("Still pending")},
            "CurrencyManager.PullData must transfer every live target into its source");
    (*source).set_current_field("name", std::string("Pushed"));
    (*deferred).text = "Local pending";
    require((*source).currency_manager().push_data() &&
                (*deferred).text.read() == "Pushed",
            "CurrencyManager.PushData must refresh every live target from retained currency");

    BindingContext context(window);
    std::vector<bool> context_changes;
    SubscriptionToken context_observer = context.collection_changed().subscribe(
        RecordBindingContextChange(context_changes));
    context.add(source);
    (*source).dispose();
    require(context.size() == 0U &&
                context_changes == std::vector<bool>{true, false},
            "BindingContext must eagerly remove a disposed source and publish one removal");
    static_cast<void>(binding_complete);
    static_cast<void>(manager_complete);
    static_cast<void>(source_complete);
    static_cast<void>(validation);
    static_cast<void>(context_observer);
}

} // namespace

int main() {
    try {
        test_value_conversion_is_strict_and_invariant();
        test_nested_property_values_are_bounded_immutable_and_structural();
        test_property_enum_schema_bounds_and_unicode();
        test_property_metadata_defaults_reset_and_serialization();
        test_property_names_are_registered_in_snake_case();
        test_property_mutation_initialization_thread_and_lifetime_guards();
        test_visual_property_values_drive_stock_controls();
        test_multi_control_two_way_currency_and_event_order();
        test_update_modes_suspension_and_explicit_transfers();
        test_format_parse_failure_rollback_and_completion();
        test_number_formats_and_their_text();
        test_edit_transactions_list_mutation_and_context();
        test_collection_uniqueness_base_properties_and_cleanup();
        test_choice_numeric_defaults_manager_transfer_and_completion_order();
        std::cout << "gui_forms_binding_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_binding_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
