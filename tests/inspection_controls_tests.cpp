#include "gui_forms/inspection_controls.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "support/named_callbacks.hpp"
#include "support/typography_painter.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;
namespace callbacks = gui_forms::test_support;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const SemanticNode* find_semantic(const std::vector<SemanticNode>& nodes,
                                  std::string_view id) {
    for (const gui_forms::SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        const SemanticNode* found = find_semantic(node.children, id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* find_semantic(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

std::shared_ptr<PropertyList> make_properties() {
    std::shared_ptr<gui_forms::PropertyList> properties = make_control<PropertyList>(StableId("inspection.properties"));
    (*properties).set_requested_bounds({0.0, 0.0, 288.0, 260.0});
    (*properties).accessible_name = "Selection properties";
    (*properties).set_groups({
        {"inspection.identity", "IDENTITY", {
            {"inspection.kind", "Kind", "PNG image", "Fixture kind"},
            {"inspection.location", "Location", "~/Work/Projects",
             "Fixture location"},
        }},
        {"inspection.editable", "EDITABLE", {
            {"inspection.name", "Name", "Facade Study.png",
             "Session-only fixture name", PropertyEditorKind::text, {}, {},
             true, true},
            {"inspection.handler", "Opens with", "Preview",
             "Session-only fixture handler", PropertyEditorKind::choice,
             {"Preview", "Image Laboratory"}},
        }},
    });
    return properties;
}

class NestedPropertyProbe final : public Control {
public:
    explicit NestedPropertyProbe(StableId stable_id)
        : Control(std::move(stable_id)), settings_(make_settings()) {
        PropertyDescriptor descriptor;
        descriptor.name = "Settings";
        descriptor.kind = BindingValueKind::object;
        descriptor.category = "Data";
        descriptor.description =
            "Immutable nested application settings and ordered modes.";
        descriptor.default_value = BindingValue{make_settings()};
        descriptor.serialization_visibility =
            PropertySerializationVisibility::content;
        descriptor.bindable = false;
        descriptor.invalidation_effects = Dirty::semantics;
        PropertyRegistration registration;
        registration.descriptor = std::move(descriptor);
        registration.get = detail::BoundMemberFunction<
            BindingValue (NestedPropertyProbe::*)() const>(
                *this, &NestedPropertyProbe::settings_property_value);
        registration.set = detail::BoundMemberFunction<
            void (NestedPropertyProbe::*)(const BindingValue&)>(
                *this, &NestedPropertyProbe::set_settings_property);
        registration.reset = detail::BoundMemberFunction<
            void (NestedPropertyProbe::*)()>(
                *this, &NestedPropertyProbe::reset_settings_property);
        define_bindable_property(std::move(registration));
    }

    [[nodiscard]] const PropertyObjectValue& settings() const noexcept {
        return settings_;
    }

private:
    [[nodiscard]] BindingValue settings_property_value() const {
        return BindingValue{settings_};
    }

    void set_settings_property(const BindingValue& value) {
        settings_ = std::get<PropertyObjectValue>(value);
        invalidate(Dirty::semantics);
    }

    void reset_settings_property() {
        settings_ = make_settings();
        invalidate(Dirty::semantics);
    }

    [[nodiscard]] static PropertyObjectValue make_settings() {
        const PropertyObjectValue endpoint = make_property_object("Endpoint", {
            {"Host", "Remote host name", BindingValue{std::string("localhost")}},
            {"Port", "Remote port", BindingValue{std::uint64_t{5555}}},
            {"Protocol", "Fixed transport", BindingValue{std::string("TCP")},
             false},
        });
        const PropertyCollectionValue modes = make_property_collection(
            "String", BindingValueKind::text,
            {BindingValue{std::string("AM")},
             BindingValue{std::string("FM")},
             BindingValue{std::string("WFM")}});
        return make_property_object("ReceiverSettings", {
            {"Endpoint", "Nested endpoint settings", BindingValue{endpoint}},
            {"Modes", "Ordered demodulation modes", BindingValue{modes}},
        });
    }

    PropertyObjectValue settings_;
};

class PercentPropertyProbe final : public Control {
public:
    explicit PercentPropertyProbe(StableId stable_id)
        : Control(std::move(stable_id)) {
        PropertyDescriptor descriptor{
            "Level", BindingValueKind::number, "Behavior",
            "Normalized retained level.", BindingValue{0.25},
            Dirty::paint | Dirty::semantics};
        descriptor.converter_name = "percent";
        descriptor.editor_name = "percent-stepper";
        PropertyRegistration registration;
        registration.descriptor = std::move(descriptor);
        registration.get = detail::BoundMemberFunction<
            BindingValue (PercentPropertyProbe::*)() const>(
                *this, &PercentPropertyProbe::level_property_value);
        registration.set = detail::BoundMemberFunction<
            void (PercentPropertyProbe::*)(const BindingValue&)>(
                *this, &PercentPropertyProbe::set_level_property);
        registration.connect_changed =
            detail::EventChangeConnector<double>(level_changed_);
        define_bindable_property(std::move(registration));
    }

    [[nodiscard]] double level() const noexcept { return level_; }

private:
    [[nodiscard]] BindingValue level_property_value() const {
        return BindingValue{level_};
    }

    void set_level_property(const BindingValue& value) {
        const std::optional<double> converted = binding_value_to_number(value);
        if (!converted || *converted < 0.0 || *converted > 1.0) {
            throw std::invalid_argument("Level must be within 0..1");
        }
        if (level_ == *converted) return;
        level_ = *converted;
        level_changed_.emit(level_);
        invalidate(Dirty::paint | Dirty::semantics);
    }

    double level_{0.25};
    Event<double> level_changed_;
};

class NullablePropertyProbe final : public Control {
public:
    explicit NullablePropertyProbe(StableId stable_id)
        : Control(std::move(stable_id)), settings_(make_settings()) {
        PropertyDescriptor ratio;
        ratio.name = "Ratio";
        ratio.kind = BindingValueKind::number;
        ratio.category = "Data";
        ratio.description = "Optional finite ratio.";
        ratio.default_value = BindingValue{std::monostate{}};
        ratio.nullable = true;
        ratio.standard_values = {
            BindingValue{std::monostate{}}, BindingValue{1.5}, BindingValue{2.5}};
        ratio.standard_values_exclusive = true;
        PropertyRegistration ratio_registration;
        ratio_registration.descriptor = std::move(ratio);
        ratio_registration.get = detail::BoundMemberFunction<
            BindingValue (NullablePropertyProbe::*)() const>(
                *this, &NullablePropertyProbe::ratio_property_value);
        ratio_registration.set = detail::BoundMemberFunction<
            void (NullablePropertyProbe::*)(const BindingValue&)>(
                *this, &NullablePropertyProbe::set_ratio_property);
        ratio_registration.connect_changed =
            detail::EventChangeConnector<>(ratio_changed_);
        define_bindable_property(std::move(ratio_registration));

        PropertyDescriptor settings;
        settings.name = "Settings";
        settings.kind = BindingValueKind::object;
        settings.category = "Data";
        settings.description = "Member-service fixture.";
        PropertyRegistration settings_registration;
        settings_registration.descriptor = std::move(settings);
        settings_registration.get = detail::BoundMemberFunction<
            BindingValue (NullablePropertyProbe::*)() const>(
                *this, &NullablePropertyProbe::settings_property_value);
        settings_registration.set = detail::BoundMemberFunction<
            void (NullablePropertyProbe::*)(const BindingValue&)>(
                *this, &NullablePropertyProbe::set_settings_property);
        define_bindable_property(std::move(settings_registration));
    }

    [[nodiscard]] const BindingValue& ratio() const noexcept { return ratio_; }

private:
    [[nodiscard]] BindingValue ratio_property_value() const { return ratio_; }

    void set_ratio_property(const BindingValue& value) {
        ratio_ = value;
        ratio_changed_.emit();
    }

    [[nodiscard]] BindingValue settings_property_value() const {
        return BindingValue{settings_};
    }

    void set_settings_property(const BindingValue& value) {
        settings_ = std::get<PropertyObjectValue>(value);
    }

    static PropertyObjectValue make_settings() {
        PropertyObjectMember member;
        member.name = "Threshold";
        member.description = "Nested percentage threshold.";
        member.value = BindingValue{0.5};
        member.declared_kind = BindingValueKind::number;
        member.converter_name = "percent";
        return make_property_object("OptionalSettings", {std::move(member)});
    }

    BindingValue ratio_{std::monostate{}};
    PropertyObjectValue settings_;
    Event<> ratio_changed_;
};

class AtomicPropertyProbe final : public Control {
public:
    AtomicPropertyProbe(StableId stable_id, double value,
                        std::optional<double> rejected = {})
        : Control(std::move(stable_id)), value_(value), rejected_(rejected) {
        PropertyDescriptor descriptor;
        descriptor.name = "Value";
        descriptor.kind = BindingValueKind::number;
        descriptor.category = "Data";
        descriptor.description = "Atomic multi-owner fixture.";
        PropertyRegistration registration;
        registration.descriptor = std::move(descriptor);
        registration.get = detail::BoundMemberFunction<
            BindingValue (AtomicPropertyProbe::*)() const>(
                *this, &AtomicPropertyProbe::value_property_value);
        registration.set = detail::BoundMemberFunction<
            void (AtomicPropertyProbe::*)(const BindingValue&)>(
                *this, &AtomicPropertyProbe::set_value_property);
        registration.connect_changed = detail::EventChangeConnector<>(changed_);
        define_bindable_property(std::move(registration));
    }

    [[nodiscard]] double value() const noexcept { return value_; }

private:
    [[nodiscard]] BindingValue value_property_value() const {
        return BindingValue{value_};
    }

    void set_value_property(const BindingValue& value) {
        const double proposed = std::get<double>(value);
        if (rejected_ && proposed == *rejected_) {
            throw std::invalid_argument("Fixture owner rejected value");
        }
        value_ = proposed;
        changed_.emit();
    }

    double value_{};
    std::optional<double> rejected_;
    Event<> changed_;
};

class TracePropertyCommit final {
public:
    explicit TracePropertyCommit(std::string& trace) noexcept : trace_(trace) {}

    void operator()(const PropertyValueChange& change) const {
        trace_ += change.row_id + "=" + change.current_value + ";";
    }

private:
    std::string& trace_;
};

class MatchResetRequest final {
public:
    MatchResetRequest(bool& matched, std::string_view expected) noexcept
        : matched_(matched), expected_(expected) {}

    void operator()(const PropertyResetRequest& request) const {
        matched_ = request.row_id == expected_;
    }

private:
    bool& matched_;
    std::string_view expected_;
};

class SwitchPropertyGridSelection final {
public:
    SwitchPropertyGridSelection(std::shared_ptr<PropertyGrid> grid,
                                Control::Ptr replacement) noexcept
        : grid_(std::move(grid)), replacement_(std::move(replacement)) {}

    void operator()(const std::string&) const {
        (*grid_).set_selected_object(replacement_);
    }

private:
    std::shared_ptr<PropertyGrid> grid_;
    Control::Ptr replacement_;
};

class DisposeWeakPropertyGrid final {
public:
    explicit DisposeWeakPropertyGrid(std::weak_ptr<PropertyGrid> grid) noexcept
        : grid_(std::move(grid)) {}

    void operator()(const std::string&) const {
        const std::shared_ptr<PropertyGrid> retained = grid_.lock();
        if (retained) (*retained).dispose();
    }

private:
    std::weak_ptr<PropertyGrid> grid_;
};

class RecordChangedPath final {
public:
    explicit RecordChangedPath(std::vector<std::string>& paths) noexcept
        : paths_(paths) {}

    void operator()(const PropertyGridValueChange& change) const {
        paths_.push_back(change.property_name);
    }

private:
    std::vector<std::string>& paths_;
};

std::string format_percent_or_invalid(const BindingValue& value,
                                      const PropertyDescriptor&) {
    const std::optional<double> number = binding_value_to_number(value);
    return number ? std::to_string(*number * 100.0) + "%"
                  : std::string("<invalid>");
}

std::string format_percent_or_empty(const BindingValue& value,
                                    const PropertyDescriptor&) {
    const std::optional<double> number = binding_value_to_number(value);
    return number ? std::to_string(*number * 100.0) + "%" : std::string{};
}

std::optional<BindingValue> parse_bounded_percent(
    std::string_view text, const BindingValue&, const PropertyDescriptor&) {
    if (text.empty() || text.back() != '%') return {};
    const std::optional<double> number = binding_value_to_number(
        BindingValue{std::string(text.substr(0U, text.size() - 1U))});
    return number && *number >= 0.0 && *number <= 100.0
        ? std::optional<BindingValue>{BindingValue{*number / 100.0}}
        : std::optional<BindingValue>{};
}

std::optional<BindingValue> parse_converted_percent(
    std::string_view text, const BindingValue&,
    const PropertyDescriptor& descriptor) {
    if (text.empty() || text.back() != '%') return {};
    const std::optional<double> number = binding_value_to_number(BindingValue{
        std::string(text.substr(0U, text.size() - 1U))});
    return number
        ? convert_property_value(BindingValue{*number / 100.0}, descriptor)
        : std::optional<BindingValue>{};
}

class SynchronizePercentEditor final {
public:
    SynchronizePercentEditor(std::weak_ptr<NumericUpDown> editor,
                             std::shared_ptr<bool> synchronizing) noexcept
        : editor_(std::move(editor)),
          synchronizing_(std::move(synchronizing)) {}

    void operator()(const BindingValue& value) const {
        const std::shared_ptr<NumericUpDown> retained = editor_.lock();
        const std::optional<double> converted = binding_value_to_number(value);
        if (!retained || !converted) return;
        *synchronizing_ = true;
        (*retained).set_value(*converted);
        *synchronizing_ = false;
    }

private:
    std::weak_ptr<NumericUpDown> editor_;
    std::shared_ptr<bool> synchronizing_;
};

class CommitPercentValue final {
public:
    CommitPercentValue(std::shared_ptr<bool> synchronizing,
                       std::function<void(BindingValue)> committed) noexcept
        : synchronizing_(std::move(synchronizing)),
          committed_(std::move(committed)) {}

    void operator()(double value) const {
        if (!*synchronizing_) committed_(BindingValue{value});
    }

private:
    std::shared_ptr<bool> synchronizing_;
    std::function<void(BindingValue)> committed_;
};

class ConnectPercentEditor final {
public:
    ConnectPercentEditor(std::weak_ptr<NumericUpDown> editor,
                         std::shared_ptr<bool> synchronizing) noexcept
        : editor_(std::move(editor)),
          synchronizing_(std::move(synchronizing)) {}

    SubscriptionToken operator()(
        Component& owner,
        std::function<void(BindingValue)> committed) const {
        const std::shared_ptr<NumericUpDown> retained = editor_.lock();
        if (!retained) return {};
        return (*retained).value_changed().subscribe(
            owner, CommitPercentValue(synchronizing_, std::move(committed)));
    }

private:
    std::weak_ptr<NumericUpDown> editor_;
    std::shared_ptr<bool> synchronizing_;
};

std::optional<PropertyEditorBinding> create_percent_stepper(
    const PropertyEditorRequest& request) {
    const std::optional<double> number = binding_value_to_number(request.value);
    if (!number) return {};
    std::shared_ptr<NumericUpDown> editor = make_control<NumericUpDown>(
        StableId(request.stable_id));
    (*editor).set_range(0.0, 1.0);
    (*editor).set_increment(0.05);
    (*editor).set_decimal_places(2U);
    (*editor).set_value(*number);
    std::shared_ptr<bool> synchronizing = std::make_shared<bool>(false);
    PropertyEditorBinding binding;
    binding.control = editor;
    binding.synchronize = SynchronizePercentEditor(editor, synchronizing);
    binding.connect_committed = ConnectPercentEditor(editor, synchronizing);
    return binding;
}

void test_grouped_editors_commit_cancel_validation_and_semantics() {
    std::shared_ptr<PropertyList> properties = make_properties();
    Window window(properties, {288.0, 260.0});
    window.perform_layout();
    std::shared_ptr<gui_forms::TextBox> name = std::dynamic_pointer_cast<TextBox>(
        (*properties).editor("inspection.name"));
    std::shared_ptr<gui_forms::ComboBox> handler = std::dynamic_pointer_cast<ComboBox>(
        (*properties).editor("inspection.handler"));
    require(name && handler && (*name).stable_id().value() ==
                "inspection.name.editor" &&
                (*handler).stable_id().value() == "inspection.handler.editor",
            "PropertyList must own stable stock editor controls");

    std::string commit_trace;
    SubscriptionToken committed = (*properties).value_committed().subscribe(
        TracePropertyCommit(commit_trace));
    require(window.request_focus(name), "property text editor must focus");
    (*name).select_all();
    require(window.dispatch_text({"Facade Final.png"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*properties).value("inspection.name") == "Facade Final.png" &&
                commit_trace == "inspection.name=Facade Final.png;",
            "Enter must commit one PropertyList text value through public events");
    (*name).select_all();
    require(window.dispatch_text({"discard me"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                (*name).text() == "Facade Final.png" &&
                (*properties).value("inspection.name") == "Facade Final.png",
            "Escape must restore the last committed text property value");

    (*handler).set_selected_index(1U);
    require((*properties).value("inspection.handler") == "Image Laboratory" &&
                commit_trace.ends_with(
                    "inspection.handler=Image Laboratory;"),
            "choice selection must commit through the same property event model");

    const double height_before = (*properties).content_height();
    require((*properties).set_validation("inspection.name", "Name already exists") &&
                (*properties).content_height() ==
                    height_before + 18.0,
            "inline validation must participate in the one owning scroll geometry");
    // The validation line is painted from a string the row's paint holds
    // (PartPaint::text borrows it); under ASan a dangling view fails here.
    window.perform_layout();
    test_support::TypographyPainter validation_painter;
    (*properties).on_paint(validation_painter, {0.0, 0.0, 440.0, 500.0});
    require(validation_painter.find("! Name already exists") != nullptr,
            "the inline validation message must paint behind its marker");
    const SemanticSnapshot semantic = window.semantic_snapshot();
    const SemanticNode* grid = find_semantic(semantic.roots,
                                              "inspection.properties");
    const SemanticNode* group = find_semantic(semantic.roots,
                                               "inspection.editable");
    const SemanticNode* row = find_semantic(semantic.roots, "inspection.kind");
    const SemanticNode* editor = find_semantic(semantic.roots,
                                                "inspection.name.editor");
    require(grid && (*grid).role == SemanticRole::property_grid && group &&
                (*group).role == SemanticRole::property_group && row &&
                (*row).role == SemanticRole::property_row && editor &&
                (*editor).description.find("Name already exists") !=
                    std::string::npos,
            "property semantics must expose grid/group/row and editor validation");

    require(window.perform_semantic_action("inspection.editable",
                                           SemanticAction::collapse) &&
                !(*name).visible.read() && !(*handler).visible.read() &&
                (*properties).content_height() < height_before,
            "semantic group collapse must remove its editors from layout and task order");
    require(window.perform_semantic_action("inspection.editable",
                                           SemanticAction::expand) &&
                (*name).visible.read() && (*handler).visible.read(),
            "semantic group expansion must restore retained editor instances");
}

void test_scrolling_responsive_layout_and_validation_bounds() {
    std::shared_ptr<PropertyList> properties = make_properties();
    std::shared_ptr<gui_forms::Panel> preview = make_control<Panel>(StableId("inspection.preview"));
    (*properties).set_header_content(preview, 72.0);
    Window window(properties, {288.0, 120.0});
    window.perform_layout();
    require((*properties).content_height() > 120.0,
            "constrained PropertyList fixture must require its one scroll plane");
    require((*properties).header_content() == preview &&
                (*properties).header_height() == 72.0,
            "preview/header content must participate in the PropertyList scroll owner");
    (*properties).set_scroll_offset(10000.0);
    require((*properties).scroll_offset() > 0.0 &&
                (*properties).scroll_offset() <=
                    (*properties).content_height() - 120.0,
            "PropertyList scrolling must clamp to its content extent");
    const Control::Ptr name = (*properties).editor("inspection.name");
    require(window.request_focus(name) &&
                (*name).absolute_bounds().y >= (*properties).absolute_bounds().y &&
                (*name).absolute_bounds().y + (*name).absolute_bounds().height <=
                    (*properties).absolute_bounds().y +
                        (*properties).absolute_bounds().height,
            "focusing an editor must reveal it inside the owning scroll plane");

    window.resize({210.0, 180.0});
    window.perform_layout();
    require((*name).absolute_bounds().x <= 10.0 &&
                (*name).absolute_bounds().width >= 190.0,
            "narrow PropertyList must stack label and editor without clipping");

    bool duplicate_rejected{};
    try {
        (*properties).set_groups({
            {"same", "First", {{"duplicate", "A", "1"}}},
            {"same", "Second", {{"duplicate", "B", "2"}}},
        });
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "PropertyList must reject duplicate group/row identities");
}

void test_property_row_hierarchy_pointer_and_reset_contract() {
    std::shared_ptr<gui_forms::PropertyList> properties = make_control<PropertyList>(
        StableId("inspection.hierarchy"));
    PropertyRowSpec parent{"inspection.hierarchy.bounds", "Bounds", "0,0,0,0"};
    parent.expandable = true;
    parent.resettable = true;
    parent.reset_enabled = true;
    PropertyRowSpec child{"inspection.hierarchy.bounds.x", "X", "0",
                          "X component", PropertyEditorKind::text};
    child.parent_id = parent.stable_id;
    child.depth = 1U;
    (*properties).set_groups({{"inspection.hierarchy.layout", "LAYOUT",
                             {parent, child}}});
    Window window(properties, {288.0, 160.0});
    window.perform_layout();
    const Control::Ptr child_editor = (*properties).editor(child.stable_id);
    const std::shared_ptr<Button> reset = (*properties).reset_button(parent.stable_id);
    bool reset_requested{};
    SubscriptionToken reset_subscription =
        (*properties).reset_requested().subscribe(
            MatchResetRequest(reset_requested,
                              "inspection.hierarchy.bounds"));
    require(child_editor && !(*child_editor).visible.read() && reset && (*reset).enabled.read() &&
                window.dispatch_pointer({PointerAction::down,
                    PointerButton::primary, {10.0, 31.0}}) &&
                (*properties).row_expanded(parent.stable_id) == true &&
                (*child_editor).visible.read(),
            "row disclosure hit testing must expand the same retained hierarchy used by layout");
    require((*reset).perform_click() && reset_requested,
            "generic PropertyList Reset buttons must publish an exact row request for their metadata owner");
}

void test_property_origins_and_metadata_driven_property_grid() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("inspection.grid.root"));
    (*root).set_requested_bounds({0.0, 0.0, 620.0, 420.0});
    std::shared_ptr<gui_forms::CheckBox> target = make_control<CheckBox>(
        StableId("inspection.grid.target"), "Inspectable");
    (*target).set_requested_bounds({18.0, 18.0, 180.0, 32.0});
    std::shared_ptr<gui_forms::PropertyGrid> grid = make_control<PropertyGrid>(StableId("inspection.grid"));
    (*grid).set_requested_bounds({220.0, 18.0, 380.0, 380.0});
    (*root).add_child(target);
    (*root).add_child(grid);
    (*grid).set_selected_object(target);
    Window window(root, {620.0, 420.0});
    window.perform_layout();

    require((*grid).selected_object() == target && (*grid).property_list() &&
                (*(*grid).property_list()).groups().size() >= 4U &&
                (*grid).selected_origin("Text") == PropertyValueOrigin::local &&
                (*grid).selected_origin("Checked") ==
                    PropertyValueOrigin::defaulted,
            "PropertyGrid must project categorized descriptors and exact local/default origins");
    std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>((*grid).editor("Text"));
    std::shared_ptr<gui_forms::CheckBox> checked = std::dynamic_pointer_cast<CheckBox>((*grid).editor("Checked"));
    require(text && checked,
            "PropertyGrid must route text and Boolean descriptors to stock typed editors");

    std::vector<PropertyGridValueChange> changes;
    std::vector<PropertyGridEditError> failures;
    SubscriptionToken changed = (*grid).property_value_changed().subscribe(
        callbacks::PushBack<std::vector<PropertyGridValueChange>,
                            const PropertyGridValueChange&>(changes));
    SubscriptionToken failed = (*grid).edit_failed().subscribe(
        callbacks::PushBack<std::vector<PropertyGridEditError>,
                            const PropertyGridEditError&>(failures));

    require(window.request_focus(text),
            "PropertyGrid text editor must participate in ordinary focus");
    (*text).select_all();
    require(window.dispatch_text({"Edited through metadata"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*target).text() == "Edited through metadata" &&
                !changes.empty() && changes.back().property_name == "text" &&
                changes.back().origin == PropertyValueOrigin::local,
            "committing a PropertyGrid text editor must use the target's registered setter and event");

    (*checked).set_checked(true);
    require((*target).checked() && changes.back().property_name == "checked" &&
                std::get<bool>(changes.back().current_value),
            "Boolean PropertyGrid edits must use a retained CheckBox and commit immediately");

    (*target).set_text("Programmatic update");
    text = std::dynamic_pointer_cast<TextBox>((*grid).editor("Text"));
    require(text && (*text).text() == "Programmatic update" &&
                changes.back().property_name == "text",
            "truthful target change notifications must refresh the live editor without rebuilding the grid");

    const std::uint32_t tab_index_before = (*target).tab_index.read();
    require(!(*grid).try_set_property_value(
                "TabIndex", BindingValue{std::string("-1")}) &&
                (*target).tab_index.read() == tab_index_before &&
                (*grid).last_error().has_value() && !failures.empty() &&
                failures.back().property_name == "tab_index",
            "invalid scalar edits must preserve the target and publish inline diagnostic state");

    require((*grid).reset_property("Text") && (*target).text().empty() &&
                (*grid).selected_origin("Text") ==
                    PropertyValueOrigin::defaulted &&
                changes.back().reset,
            "PropertyGrid reset must use the registered reset/default path and refresh origin state");

    const std::shared_ptr<gui_forms::NumericUpDown> bounds_x = std::dynamic_pointer_cast<NumericUpDown>(
        (*grid).editor("Bounds.X"));
    require(bounds_x && (*grid).property_expanded("Bounds") == false &&
                !(*bounds_x).visible.read() &&
                (*grid).set_property_expanded("Bounds", true) &&
                (*bounds_x).visible.read(),
            "compound properties must retain stable hidden field editors and reveal them through an explicit expansion state");
    require((*grid).try_set_property_value(
                "Bounds.X", BindingValue{std::string("31.5")}) &&
                (*target).requested_bounds().x == 31.5 &&
                changes.back().property_name == "bounds.x" &&
                std::get<double>(changes.back().current_value) == 31.5 &&
                (*(*grid).selected_descriptor("Bounds.X")).kind ==
                    BindingValueKind::number,
            "editing a compound field path must rebuild and validate the owning typed value through its real setter");
    const double width_before = (*target).requested_bounds().width;
    require(!(*grid).try_set_property_value(
                "Bounds.Width", BindingValue{std::string("-1")}) &&
                (*target).requested_bounds().width == width_before &&
                failures.back().property_name == "bounds.width",
            "invalid compound field edits must preserve the complete owning value and identify the precise field path");

    require((*grid).set_property_expanded("Padding", true) &&
                (*grid).try_set_property_value(
                    "Padding.Left", BindingValue{std::string("12")}) &&
                (*target).padding.read().left == 12.0,
            "spacing content must be editable as typed retained subproperties");
    const std::shared_ptr<Button> padding_reset = (*grid).reset_button("Padding");
    require(padding_reset && (*padding_reset).enabled.read() &&
                (*padding_reset).perform_click() &&
                (*target).padding.read() == Insets{} && !(*padding_reset).enabled.read() &&
                changes.back().property_name == "padding" &&
                changes.back().reset,
            "the visible Reset control must invoke the owning property's actual reset contract and disable at default");
    const SemanticSnapshot expanded_semantics = window.semantic_snapshot();
    const SemanticNode* bounds_semantic = find_semantic(
        expanded_semantics.roots,
        "inspection.grid.property.bounds");
    require(bounds_semantic &&
                has_semantic_state((*bounds_semantic).states,
                                   SemanticState::expanded) &&
                std::find((*bounds_semantic).actions.begin(),
                          (*bounds_semantic).actions.end(),
                          SemanticAction::collapse) !=
                    (*bounds_semantic).actions.end(),
            "expanded compound rows must publish their state and collapse action through the semantic tree");
    require(window.perform_semantic_action(
                "inspection.grid.property.bounds", SemanticAction::collapse) &&
                (*grid).property_expanded("Bounds") == false &&
                !(*bounds_x).visible.read() &&
                window.perform_semantic_action(
                    "inspection.grid.property.bounds", SemanticAction::expand) &&
                (*bounds_x).visible.read(),
            "compound disclosure must use the same retained state through semantic and pointer-facing actions");

    (*target).hit_test_transparent = true;
    require((*target).should_serialize_property("HitTestTransparent") &&
                (*target).property_value_origin("HitTestTransparent") ==
                    PropertyValueOrigin::local,
            "non-browsable policy must not erase truthful local value authorship");

    (*grid).set_property_sort(PropertySort::alphabetical);
    require((*(*grid).property_list()).groups().size() == 1U &&
                (*(*grid).property_list()).groups().front().title == "PROPERTIES" &&
                (*grid).editor("Checked") != nullptr,
            "alphabetical PropertyGrid sorting must retain the same editable descriptor set in one group");

    std::shared_ptr<gui_forms::Label> inherited = make_control<Label>(
        StableId("inspection.grid.inherited"), "Inherited");
    require((*inherited).property_value_origin("Font") ==
                PropertyValueOrigin::inherited &&
                (*inherited).property_value_origin("ForeColor") ==
                    PropertyValueOrigin::inherited,
            "effective Label appearance must identify inherited rather than fabricated default authorship");
    (*inherited).set_font({FontRole::content, 14.0, 600, false});
    require((*inherited).property_value_origin("Font") ==
                PropertyValueOrigin::local &&
                (*inherited).reset_property("Font") &&
                (*inherited).property_value_origin("Font") ==
                    PropertyValueOrigin::inherited,
            "appearance override/reset must move exactly between local and inherited origins");

    std::shared_ptr<gui_forms::Label> switched_target = make_control<Label>(
        StableId("inspection.grid.switch-source"), "before");
    std::shared_ptr<gui_forms::CheckBox> replacement_target = make_control<CheckBox>(
        StableId("inspection.grid.switch-destination"), "replacement");
    std::shared_ptr<gui_forms::PropertyGrid> switching_grid = make_control<PropertyGrid>(
        StableId("inspection.grid.switching-grid"));
    (*switching_grid).set_selected_object(switched_target);
    SubscriptionToken switch_selection =
        (*switched_target).text_changed().subscribe(
            SwitchPropertyGridSelection(switching_grid,
                                        replacement_target));
    require((*switching_grid).try_set_property_value(
                "Text", BindingValue{std::string("after")}) &&
                (*switched_target).text() == "after" &&
                (*switching_grid).selected_object() == replacement_target &&
                (*switching_grid).editor("Checked") != nullptr,
            "a target callback may replace PropertyGrid selection without allowing the retiring edit to touch stale projection maps");

    std::shared_ptr<gui_forms::Label> callback_target = make_control<Label>(
        StableId("inspection.grid.callback-target"), "before");
    std::shared_ptr<gui_forms::PropertyGrid> callback_grid = make_control<PropertyGrid>(
        StableId("inspection.grid.callback-grid"));
    (*callback_grid).set_selected_object(callback_target);
    const std::weak_ptr<PropertyGrid> weak_callback_grid = callback_grid;
    SubscriptionToken dispose_grid =
        (*callback_target).text_changed().subscribe(
            DisposeWeakPropertyGrid(weak_callback_grid));
    require((*callback_grid).try_set_property_value(
                "Text", BindingValue{std::string("after")}) &&
                (*callback_target).text() == "after" &&
                (*callback_grid).is_disposed(),
            "target change callbacks may dispose the PropertyGrid without stale editor access");

    (*target).dispose();
    (*grid).refresh_properties();
    require(!(*grid).selected_object() &&
                (*(*grid).property_list()).groups().empty(),
            "refreshing after selected-object disposal must clear stale descriptors and editors");
}

void test_instance_owned_converter_and_editor_registries() {
    std::shared_ptr<PropertyValueConverterRegistry> converters = PropertyValueConverterRegistry::create_default();
    PropertyValueConverter percent;
    percent.format = format_percent_or_invalid;
    percent.parse = parse_bounded_percent;
    require((*converters).register_converter("percent", percent) &&
                !(*converters).register_converter("PERCENT", percent),
            "converter registration must be canonical, deterministic, and reject duplicate identity");

    std::shared_ptr<PropertyEditorRegistry> editors = PropertyEditorRegistry::create_default();
    require((*editors).register_factory("percent-stepper",
                                        create_percent_stepper),
            "custom editor factory registration must succeed once");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("inspection.services.root"));
    std::shared_ptr<PercentPropertyProbe> target =
        make_control<PercentPropertyProbe>(
        StableId("inspection.services.target"));
    std::shared_ptr<gui_forms::PropertyGrid> grid = make_control<PropertyGrid>(StableId("inspection.services.grid"));
    (*grid).set_requested_bounds({0.0, 0.0, 420.0, 180.0});
    (*grid).set_converter_registry(converters);
    (*grid).set_editor_registry(editors);
    (*root).add_child(target);
    (*root).add_child(grid);
    (*grid).set_selected_object(target);
    Window window(root, {420.0, 180.0});
    window.perform_layout();

    const PropertyDescriptor descriptor = *(*grid).selected_descriptor("Level");
    const std::optional<BindingValue> parsed = (*converters).parse("75%", BindingValue{(*target).level()},
                                          descriptor);
    const std::shared_ptr<gui_forms::NumericUpDown> editor = std::dynamic_pointer_cast<NumericUpDown>(
        (*grid).editor("Level"));
    require(parsed && std::get<double>(*parsed) == 0.75 && editor &&
                (*editor).maximum() == 1.0 && (*editor).value() == 0.25 &&
                (*(*grid).property_list()).value(
                    "inspection.services.grid.property.level") ==
                    "25.000000%",
            "descriptor service names must resolve through the supplied converter and editor registries");
    (*editor).set_value(0.75);
    require((*target).level() == 0.75 && (*editor).value() == 0.75 &&
                (*(*grid).property_list()).value(
                    "inspection.services.grid.property.level") ==
                    "75.000000%",
            "custom retained editor commits must reach the real setter and programmatic refresh must synchronize without feedback");
}

void test_nullable_standard_values_culture_and_nested_services() {
    std::shared_ptr<PropertyValueConverterRegistry> converters = PropertyValueConverterRegistry::create_default();
    PropertyConversionContext culture;
    culture.culture_name = "de-DE";
    culture.decimal_separator = ",";
    culture.group_separator = ".";
    culture.use_grouping = true;
    (*converters).set_context(culture);

    PropertyDescriptor numeric;
    numeric.name = "Amount";
    numeric.kind = BindingValueKind::number;
    const std::string localized = (*converters).format(
        BindingValue{1234.5}, numeric);
    const std::optional<BindingValue> localized_parse = (*converters).parse(
        "1.234,5", BindingValue{0.0}, numeric);
    require(localized == "1.234,5" && localized_parse &&
                binding_value_to_number(*localized_parse) == 1234.5,
            "numeric conversion context must localize and parse per registry without mutating process locale");

    PropertyValueConverter percent;
    percent.format = format_percent_or_empty;
    percent.parse = parse_converted_percent;
    require((*converters).register_converter("percent", std::move(percent)),
            "nested converter fixture must register once");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("inspection.nullable.root"));
    std::shared_ptr<NullablePropertyProbe> target =
        make_control<NullablePropertyProbe>(
        StableId("inspection.nullable.target"));
    std::shared_ptr<gui_forms::PropertyGrid> grid = make_control<PropertyGrid>(
        StableId("inspection.nullable.grid"));
    (*grid).set_requested_bounds({0.0, 0.0, 440.0, 260.0});
    (*grid).set_converter_registry(converters);
    (*root).add_child(target);
    (*root).add_child(grid);
    (*grid).set_selected_object(target);
    Window window(root, {440.0, 260.0});
    window.perform_layout();

    const std::optional<PropertyDescriptor> ratio_descriptor = (*grid).selected_descriptor("Ratio");
    const std::optional<PropertyDescriptor> threshold_descriptor =
        (*grid).selected_descriptor("Settings.Threshold");
    std::shared_ptr<gui_forms::ComboBox> choice = std::dynamic_pointer_cast<ComboBox>((*grid).editor("Ratio"));
    require(ratio_descriptor && (*ratio_descriptor).nullable && choice &&
                (*choice).items().size() == 3U &&
                (*choice).items().front() == "(none)" &&
                threshold_descriptor &&
                (*threshold_descriptor).converter_name == "percent" &&
                (*(*grid).property_list()).value(
                    "inspection.nullable.grid.property.settings.threshold") ==
                    "50.000000%",
            "PropertyGrid must retain payload schema for null values and project member-specific converter identities");
    (*choice).set_selected_index(1U);
    require((*target).ratio() == BindingValue{1.5} &&
                (*grid).try_set_property_value(
                    "Ratio", BindingValue{std::monostate{}}) &&
                (*target).ratio() == BindingValue{std::monostate{}} &&
                !(*grid).try_set_property_value("Ratio", BindingValue{9.0}),
            "exclusive standard values must commit typed values, round-trip null, and reject values outside the finite set");
}

void test_multiple_owner_atomic_property_commit() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("inspection.atomic.root"));
    std::shared_ptr<AtomicPropertyProbe> first =
        make_control<AtomicPropertyProbe>(
        StableId("inspection.atomic.first"), 1.0);
    std::shared_ptr<AtomicPropertyProbe> second =
        make_control<AtomicPropertyProbe>(
        StableId("inspection.atomic.second"), 2.0, 5.0);
    std::shared_ptr<gui_forms::PropertyGrid> grid = make_control<PropertyGrid>(
        StableId("inspection.atomic.grid"));
    (*root).add_child(first);
    (*root).add_child(second);
    (*root).add_child(grid);
    (*grid).set_selected_objects({first, second});
    Window window(root, {420.0, 180.0});
    window.perform_layout();

    std::size_t committed = 0U;
    SubscriptionToken changed = (*grid).property_value_changed().subscribe(
        callbacks::IncrementCounter<std::size_t,
                                    const PropertyGridValueChange&>(committed));
    require((*grid).selected_objects() ==
                std::vector<Control::Ptr>{first, second} &&
                !(*grid).try_set_property_value("Value", BindingValue{5.0}) &&
                (*first).value() == 1.0 && (*second).value() == 2.0 &&
                committed == 0U,
            "a rejecting second owner must roll the first owner back before publishing failure");
    require((*grid).try_set_property_value("Value", BindingValue{3.0}) &&
                (*first).value() == 3.0 && (*second).value() == 3.0 &&
                committed == 1U,
            "a valid multiple-owner edit must commit every owner and publish one logical change");
    static_cast<void>(changed);
}

void test_default_flags_and_color_property_editors() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("inspection.specialized.root"));
    std::shared_ptr<gui_forms::Label> target = make_control<Label>(StableId("inspection.specialized.target"),
                                      "Specialized editors");
    (*target).set_requested_bounds({12.0, 12.0, 190.0, 30.0});
    std::shared_ptr<gui_forms::PropertyGrid> grid = make_control<PropertyGrid>(StableId("inspection.specialized.grid"));
    (*grid).set_requested_bounds({210.0, 0.0, 410.0, 440.0});
    (*root).add_child(target);
    (*root).add_child(grid);
    (*grid).set_selected_object(target);
    Window window(root, {620.0, 440.0});
    window.perform_layout();

    std::shared_ptr<gui_forms::FlagsValueEditor> flags = std::dynamic_pointer_cast<FlagsValueEditor>(
        (*grid).editor("Anchor"));
    require(flags && (*flags).value().name == "top, left" &&
                (*flags).semantic_descriptor().role == SemanticRole::combo_box,
            "the default enum editor service must project flags through a retained multi-choice editor");
    (*flags).set_overlay_width(286.0);
    require((*flags).overlay_width() == 286.0,
            "flags editor overlay width must retain caller customization");
    (*flags).set_dropped_down(true);
    std::shared_ptr<gui_forms::CheckedListBox> choices = std::dynamic_pointer_cast<CheckedListBox>(window.find(
        std::string((*flags).stable_id().value()) + ".overlay.list"));
    require(choices && (*choices).items().size() == 5U &&
                (*choices).item_checked(1U) && (*choices).item_checked(3U),
            "the flags overlay must expose zero and each independent bit with synchronized checks");
    (*choices).set_item_checked(4U, true);
    require(has_anchor((*target).anchor.read(), AnchorStyles::right) &&
                (*flags).value().name == "top, left, right",
            "checking a flags row must immediately commit the typed enum through PropertyGrid");
    (*flags).set_dropped_down(false);
    require(!window.find(std::string((*flags).stable_id().value()) +
                         ".overlay.list"),
            "closing a flags editor must synchronously unregister its overlay subtree");

    std::shared_ptr<gui_forms::ColorValueEditor> color = std::dynamic_pointer_cast<ColorValueEditor>(
        (*grid).editor("ForeColor"));
    require(color != nullptr,
            "the default color service must create a retained color editor");
    (*color).set_swatch_width(42.0);
    require((*color).editor() &&
                (*color).swatch_width() == 42.0 &&
                (*(*color).editor()).text() ==
                    ColorValueEditor::format_value((*target).foreground()),
            "the default color service must provide canonical text and a retained swatch editor");
    std::vector<PropertyGridEditError> failures;
    SubscriptionToken failed = (*grid).edit_failed().subscribe(
        callbacks::PushBack<std::vector<PropertyGridEditError>,
                            const PropertyGridEditError&>(failures));
    require(window.request_focus((*color).editor()),
            "the color editor's ordinary TextBox must participate in focus");
    (*(*color).editor()).select_all();
    require(window.dispatch_text({"#2A6FB4CC"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*target).foreground() == Color::rgba(42, 111, 180, 204) &&
                (*color).value() == (*target).foreground(),
            "a valid color commit must preserve alpha and use the target's typed setter");
    const Color retained = (*target).foreground();
    (*(*color).editor()).select_all();
    require(window.dispatch_text({"not-a-color"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*target).foreground() == retained && !failures.empty() &&
                failures.back().property_name == "fore_color" &&
                failures.back().attempted_value == "not-a-color" &&
                (*color).visual_status() == ControlVisualStatus::invalid,
            "invalid color input must preserve retained state and publish exact inline failure feedback");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                (*(*color).editor()).text() == "#2A6FB4CC" &&
                (*color).visual_status() == ControlVisualStatus::normal,
            "cancelling a failed color edit must restore its last committed canonical value");
    static_cast<void>(failed);
}

void test_recursive_object_and_collection_projection_and_mutation() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("inspection.nested.root"));
    std::shared_ptr<NestedPropertyProbe> target =
        make_control<NestedPropertyProbe>(
        StableId("inspection.nested.target"));
    std::shared_ptr<gui_forms::PropertyGrid> grid = make_control<PropertyGrid>(StableId("inspection.nested.grid"));
    (*grid).set_requested_bounds({0.0, 0.0, 520.0, 440.0});
    (*root).add_child(target);
    (*root).add_child(grid);
    (*grid).set_selected_object(target);
    Window window(root, {520.0, 440.0});
    window.perform_layout();

    require((*(*grid).selected_descriptor("Settings.Endpoint.Host")).kind ==
                BindingValueKind::text &&
                (*grid).editor("Settings.Endpoint.Host") != nullptr &&
                (*grid).editor("Settings.Modes[0]") != nullptr &&
                !(*(*grid).editor("Settings.Endpoint.Host")).visible.read() &&
                (*grid).set_property_expanded("Settings", true) &&
                (*grid).set_property_expanded("Settings.Endpoint", true) &&
                (*grid).set_property_expanded("Settings.Modes", true) &&
                (*(*grid).editor("Settings.Endpoint.Host")).visible.read() &&
                (*(*grid).editor("Settings.Modes[0]")).visible.read(),
            "PropertyGrid must recursively project stable object-member and collection-index paths through retained disclosure state");

    std::vector<std::string> changed_paths;
    SubscriptionToken changed = (*grid).property_value_changed().subscribe(
        RecordChangedPath(changed_paths));
    require((*grid).try_set_property_value(
                "Settings.Endpoint.Host", BindingValue{std::string("radio.lan")}) &&
                (*grid).try_set_property_value(
                    "Settings.Endpoint.Port", BindingValue{std::string("6000")}) &&
                (*grid).try_set_property_value(
                    "Settings.Modes[1]", BindingValue{std::string("NFM")}) &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*grid).editor("Settings.Endpoint.Host"))).text() == "radio.lan" &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*grid).editor("Settings.Endpoint.Port"))).text() == "6000" &&
                changed_paths == std::vector<std::string>{
                    "settings.endpoint.host", "settings.endpoint.port",
                    "settings.modes[1]"},
            "recursive edits must reconstruct every immutable ancestor, preserve scalar kinds, and publish the precise registered path");
    require(!(*grid).try_set_property_value(
                "Settings.Endpoint.Protocol", BindingValue{std::string("UDP")}) &&
                (*(*grid).last_error()).property_name == "settings.endpoint.protocol",
            "a read-only nested member must reject edits without weakening its writable ancestor");

    require((*grid).insert_collection_item(
                "Settings.Modes", 1U, BindingValue{std::string("USB")}) &&
                (*grid).editor("Settings.Modes[3]") != nullptr &&
                (*grid).property_expanded("Settings") == true &&
                (*grid).property_expanded("Settings.Modes") == true &&
                (*grid).move_collection_item("Settings.Modes", 3U, 0U) &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*grid).editor("Settings.Modes[0]"))).text() == "WFM" &&
                (*grid).remove_collection_item("Settings.Modes", 2U) &&
                (*grid).editor("Settings.Modes[3]") == nullptr &&
                !(*grid).remove_collection_item("Settings.Modes", 99U),
            "collection insert, move, and remove must atomically rebuild homogeneous snapshots, preserve disclosure, and reject invalid indices");

    require((*grid).reset_property("Settings") &&
                (*grid).property_expanded("Settings.Endpoint") == true &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*grid).editor("Settings.Endpoint.Host"))).text() == "localhost" &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*grid).editor("Settings.Modes[1]"))).text() == "FM" &&
                (*grid).selected_origin("Settings.Modes[1]") ==
                    PropertyValueOrigin::defaulted,
            "resetting a nested owner must restore its structurally equal default while retaining independent view expansion state");
    static_cast<void>(changed);
}

void test_property_typography_preserves_editors_and_updates_geometry() {
    const std::shared_ptr<PropertyList> properties = make_properties();
    Window window(properties, {440.0, 500.0});
    window.perform_layout();
    require((*properties).font() == FontSpec{FontRole::content, 9.5, 400, false} &&
                (*properties).row_height() == 29.0,
            "property list must retain its existing font and row defaults");
    const std::shared_ptr<TextBox> name = std::dynamic_pointer_cast<TextBox>(
        (*properties).editor("inspection.name"));
    require(name && (*name).font().size == 10.0,
            "default property editor typography must remain unchanged");
    require(window.request_focus(name), "property editor must accept focus");
    (*name).select_all();
    require(window.dispatch_text({"Uncommitted name"}), "draft edit must be accepted");
    (*name).select(Utf8Offset(2U), Utf8Offset(7U));
    const TextSelection selection = (*name).selection();
    const double old_height = (*properties).content_height();
    (*properties).set_row_height(40.0);
    window.perform_layout();
    require((*properties).row_height() == 40.0 &&
                (*properties).content_height() == old_height + 44.0,
            "four property rows must reflect the explicit row-height increase");

    const std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(
        StableId("font.numeric"));
    (*numeric).set_value(42.0);
    require((*properties).replace_editor("inspection.location", numeric),
            "numeric replacement editor must attach");
    const std::shared_ptr<TextBox> numeric_text = (*numeric).editor();
    (*numeric_text).set_text("-");
    (*numeric_text).select_all();
    const TextSelection numeric_selection = (*numeric_text).selection();
    std::uint64_t changes{};
    SubscriptionToken changed = (*properties).value_changed().subscribe(
        callbacks::IncrementCounter<std::uint64_t, const PropertyValueChange&>(changes));
    const FontSpec chosen{FontRole::content, 17.0, 500, true, 0.5};
    (*properties).set_font(chosen);
    window.perform_layout();
    require((*properties).font() == chosen && (*name).font() == chosen &&
                (*numeric_text).font() == chosen &&
                (*properties).editor("inspection.name") == name &&
                (*properties).editor("inspection.location") == numeric,
            "font updates must reach text and numeric editors without replacing them");
    require((*name).text() == "Uncommitted name" && (*name).selection() == selection &&
                (*name).can_undo() && window.focused_control() == name &&
                (*numeric_text).text() == "-" &&
                (*numeric_text).selection() == numeric_selection &&
                (*numeric).value() == 42.0 && changes == 0U,
            "typography changes must preserve draft, selection, focus, undo and numeric value without publishing edits");
    test_support::TypographyPainter painter;
    (*properties).on_paint(painter, {0.0, 0.0, 440.0, 500.0});
    const test_support::PaintedText* label = painter.find("Kind");
    const test_support::PaintedText* value = painter.find("PNG image");
    require(label && value && (*label).font == chosen && (*value).font == chosen,
            "read-only property names and values must paint with the selected font");
    const std::vector<SemanticNode> nodes = (*properties).semantic_virtual_children();
    const SemanticNode* kind = find_semantic(nodes, "inspection.kind");
    require(kind && (*kind).bounds.height == 40.0,
            "property semantic row bounds must agree with row-height geometry");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                (*name).text() == "Facade Study.png",
            "font changes must retain the original text commit/cancel subscription");

    const std::shared_ptr<NumericUpDown> later_numeric = make_control<NumericUpDown>(
        StableId("font.later.numeric"));
    require((*properties).replace_editor("inspection.location", later_numeric) &&
                (*(*later_numeric).editor()).font() == chosen,
            "future replacement numeric editors must inherit the selected font");
    const std::vector<PropertyGroupSpec> groups = (*properties).groups();
    (*properties).set_groups(groups);
    const std::shared_ptr<TextBox> future = std::dynamic_pointer_cast<TextBox>(
        (*properties).editor("inspection.name"));
    require(future && (*future).font() == chosen,
            "future stock text editors must inherit the selected font");

    const double invalid[] = {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};
    for (const double metric : invalid) {
        bool font_rejected{};
        FontSpec malformed = chosen;
        malformed.size = metric;
        try { (*properties).set_font(malformed); }
        catch (const std::invalid_argument&) { font_rejected = true; }
        bool height_rejected{};
        try { (*properties).set_row_height(metric); }
        catch (const std::invalid_argument&) { height_rejected = true; }
        require(font_rejected && height_rejected && (*properties).font() == chosen &&
                    (*properties).row_height() == 40.0,
                "invalid property font and row metrics must be rejected without mutation");
    }
    FontSpec malformed = chosen;
    malformed.letter_spacing = std::numeric_limits<double>::quiet_NaN();
    bool tracking_rejected{};
    try { (*properties).set_font(malformed); }
    catch (const std::invalid_argument&) { tracking_rejected = true; }
    bool height_rejected{};
    try { (*properties).set_row_height(4097.0); }
    catch (const std::invalid_argument&) { height_rejected = true; }
    require(tracking_rejected && height_rejected,
            "property typography must reject invalid tracking and excessive row height");
}

} // namespace

int main() {
    try {
        test_grouped_editors_commit_cancel_validation_and_semantics();
        test_scrolling_responsive_layout_and_validation_bounds();
        test_property_row_hierarchy_pointer_and_reset_contract();
        test_property_origins_and_metadata_driven_property_grid();
        test_instance_owned_converter_and_editor_registries();
        test_nullable_standard_values_culture_and_nested_services();
        test_multiple_owner_atomic_property_commit();
        test_default_flags_and_color_property_editors();
        test_recursive_object_and_collection_projection_and_mutation();
        test_property_typography_preserves_editors_and_updates_geometry();
        std::cout << "gui_forms_inspection_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_inspection_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
