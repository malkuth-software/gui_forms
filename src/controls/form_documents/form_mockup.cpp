// FormMockup: a form document and its fixture loaded at run time and shown
// in a host control, replaced whole on each load that succeeds (hot
// reload), with declared handlers and commands recording themselves.
// See gui_forms/forms/form_mockup/form_mockup.hpp.
//
// A load runs in this order, and changes nothing shown until its last step:
// 1. the fixture is read (form_fixture.hpp) and the document's model built
//    with handlers admitted; either refusal ends the load;
// 2. the sources the model's bindings name are made from the fixture, or
//    as stand-ins, into the new generation;
// 3. the objects are made and bound (load_form), the fixture's rows and
//    tree items are given to the objects they name, and the declared
//    handlers are subscribed; any refusal ends the load, and the new
//    generation is released;
// 4. the host's old tree is removed and the new one added, inside one
//    layout suspension; then the old generation is released.

#include "gui_forms/forms/form_mockup/form_mockup.hpp"

#include "form_documents_detail.hpp"

#include "../../core/form_language/form_fixture/form_fixture.hpp"
#include "../../core/form_language/form_loader/form_loader.hpp"
#include "../../core/form_language/form_model/form_model.hpp"
#include "../../core/form_language/form_xml/form_xml.hpp"
#include "../../core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/binding/binding_table/binding_table.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/list_view.hpp"
#include "gui_forms/controls/tree_view.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/value/value.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace detail {

class FormMockupRecorder;

// One declared handler or command of a generation, as the mockup lists it.
struct FormMockupHandlerEntry final {
    std::string handler{};
    std::string stable_id{};
    std::string interaction{};
    bool observed{};
};

// What one load made. Members end in reverse order: the tree first, then
// the components, the recorder (its subscriptions end with it), the
// sources the tree was bound to, and the handler list the recorder reads.
struct FormMockupGeneration final {
    std::vector<FormMockupHandlerEntry> handlers{};
    std::vector<std::shared_ptr<Value<bool>>> booleans{};
    std::vector<std::shared_ptr<Value<int>>> integers{};
    std::vector<std::shared_ptr<Value<double>>> numbers{};
    std::vector<std::shared_ptr<Command>> commands{};
    std::vector<std::shared_ptr<BindingSource>> records{};
    std::shared_ptr<FormMockupRecorder> recorder{};
    std::vector<Component::Ptr> components{};
    Control::Ptr root{};
    std::string title{};
    Size client_size{};
    std::string accept_button{};
    std::string cancel_button{};
};

// One recorded event, its texts copied from its handler entry.
struct FormMockupKeptEvent final {
    stxlib::u64 sequence{};
    std::string handler{};
    std::string stable_id{};
    std::string interaction{};
};

struct FormMockupState final {
    FormMockupState() noexcept = default;

    // Borrowed: the program's control; it outlives the mockup.
    Control* host{};
    std::unique_ptr<FormMockupGeneration> shown{};
    stxlib::u64 generation{};
    // A ring of at most FormMockup::kept_events: `first_event` is the
    // oldest, `event_total` counts every event recorded.
    std::vector<FormMockupKeptEvent> events{};
    std::size_t first_event{};
    stxlib::u64 event_total{};
    FormMockupEventSink sink{};
    void* sink_context{};
    // Set while a load swaps trees: what the swap raises is not recorded.
    bool swapping{};

    // Records the handler entry `entry` of a shown generation.
    void record(const FormMockupHandlerEntry& entry);
};

// The component the declared handlers' subscriptions belong to: its
// disposal (the generation's end) disconnects them all. Observes the state
// and its generation's handler list, both of which outlive it.
class FormMockupRecorder final : public Component {
public:
    FormMockupRecorder(FormMockupState& state, const std::vector<FormMockupHandlerEntry>& handlers) noexcept
        : state_(&state), handlers_(&handlers) {}

    // The handler at `index` of the generation was raised.
    void raised(std::uint32_t index) {
        if (index >= (*handlers_).size()) return;
        (*state_).record((*handlers_)[index]);
    }

private:
    FormMockupState* state_;
    const std::vector<FormMockupHandlerEntry>* handlers_;
};

void FormMockupState::record(const FormMockupHandlerEntry& entry) {
    if (swapping) return;
    ++event_total;
    FormMockupKeptEvent kept{};
    kept.sequence = event_total;
    kept.handler = entry.handler;
    kept.stable_id = entry.stable_id;
    kept.interaction = entry.interaction;
    std::size_t place = events.size();
    if (events.size() < static_cast<std::size_t>(FormMockup::kept_events)) {
        events.push_back(std::move(kept));
    } else {
        place = first_event;
        events[place] = std::move(kept);
        first_event = (first_event + 1U) % events.size();
    }
    if (sink == nullptr) return;
    const FormMockupKeptEvent& stored = events[place];
    FormMockupEvent event{};
    event.sequence = stored.sequence;
    event.handler = stxlib::View<char>(stored.handler.data(), static_cast<stxlib::usize>(stored.handler.size()));
    event.stable_id =
        stxlib::View<char>(stored.stable_id.data(), static_cast<stxlib::usize>(stored.stable_id.size()));
    event.interaction =
        stxlib::View<char>(stored.interaction.data(), static_cast<stxlib::usize>(stored.interaction.size()));
    (*sink)(sink_context, event);
}

} // namespace detail

namespace {

using form_language::FormFixture;
using form_language::FormModel;
using theme_language::DiagnosticCode;
using theme_language::SourcePosition;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;

[[nodiscard]] std::string_view text_of(stxlib::View<char> view) noexcept {
    if (view.data() == nullptr) return std::string_view();
    const std::string_view text(view.data(), static_cast<std::size_t>(view.count()));
    return text;
}

[[nodiscard]] stxlib::View<char> view_of(const std::string& text) noexcept {
    const stxlib::View<char> view(text.data(), static_cast<stxlib::usize>(text.size()));
    return view;
}

// A property-change subscription's target: the event has no payload.
struct PropertyChangeForwarder final {
    detail::FormMockupRecorder* recorder{};
    std::uint32_t index{};

    void operator()() const {
        (*recorder).raised(index);
    }
};

// The base control's payload-free interactions a declared handler may name.
enum class BaseInteraction : std::uint8_t {
    none,
    click,
    double_click,
    mouse_enter,
    mouse_hover,
    mouse_leave,
};

[[nodiscard]] BaseInteraction base_interaction(std::string_view name) noexcept {
    if (name == "click") return BaseInteraction::click;
    if (name == "double_click") return BaseInteraction::double_click;
    if (name == "mouse_enter") return BaseInteraction::mouse_enter;
    if (name == "mouse_hover") return BaseInteraction::mouse_hover;
    if (name == "mouse_leave") return BaseInteraction::mouse_leave;
    return BaseInteraction::none;
}

// The suffix of an interaction raised when a property changes.
constexpr std::string_view changed_suffix = "_changed";

// One load's work: it fills `generation` and reports to `diagnostics`.
class MockupLoad final {
public:
    MockupLoad(detail::FormMockupState& state, const form_language::FormSchema& schema, const ThemeSource& document,
               const ThemeSource& fixture_source, ThemeDiagnosticList& diagnostics) noexcept
        : state_(state), schema_(schema), document_(document), fixture_source_(fixture_source),
          diagnostics_(diagnostics) {}

    // Steps 1 to 3 of the file comment. True when nothing was refused.
    [[nodiscard]] bool run(detail::FormMockupGeneration& generation);

private:
    void make_sources(detail::FormMockupGeneration& generation, form_language::FormSources& sources);
    void add_scalar(detail::FormMockupGeneration& generation, form_language::FormSources& sources,
                    const std::string& name, BindingValueKind kind);
    void add_command(detail::FormMockupGeneration& generation, form_language::FormSources& sources,
                     const std::string& name);
    // The fixture's record set `name` as a BindingSource (rows and all), or
    // a stand-in with no rows; with the column `field` when it is not
    // empty. Null, with E819 reported, without a window.
    [[nodiscard]] std::shared_ptr<BindingSource> record_source(detail::FormMockupGeneration& generation,
                                                               form_language::FormSources& sources,
                                                               const std::string& name, const std::string& field);
    void show_records(const form_language::FixtureRecords& records, const std::shared_ptr<BindingSource>& source,
                      const form_language::LoadedForm& loaded);
    void show_tree(const form_language::FixtureTree& tree, const form_language::LoadedForm& loaded);
    // The loaded control with `stable_id`, or null with E819 reported at
    // `position` of the fixture.
    [[nodiscard]] Control::Ptr fixture_target(const std::string& stable_id, SourcePosition position,
                                              const form_language::LoadedForm& loaded);
    void subscribe_handlers(detail::FormMockupGeneration& generation, const form_language::LoadedForm& loaded);
    // Subscribes the handler entry `index` to `interaction` of `object`;
    // whether it is observed.
    [[nodiscard]] bool observe(detail::FormMockupGeneration& generation, Component& object,
                               const std::string& interaction, std::uint32_t index);
    void report(DiagnosticCode code, const ThemeSource& source, SourcePosition position, std::string rule,
                std::string property, std::string detail);

    detail::FormMockupState& state_;
    const form_language::FormSchema& schema_;
    const ThemeSource& document_;
    const ThemeSource& fixture_source_;
    ThemeDiagnosticList& diagnostics_;
    form_language::FormSyntax syntax_{};
    FormModel model_{};
    FormFixture fixture_{};
    // Names added to the sources so far: one source per name.
    std::vector<std::string> added_{};
    bool refused_{};
};

bool MockupLoad::run(detail::FormMockupGeneration& generation) {
    const form_language::FormFixtureOutcome fixture_read =
        form_language::read_form_fixture(fixture_source_, fixture_, diagnostics_);
    form_language::FormModelOptions options{};
    options.admit_handlers = true;
    const form_language::FormModelOutcome built =
        form_language::read_form_model(document_, syntax_, schema_, options, model_, diagnostics_);
    if (fixture_read != form_language::FormFixtureOutcome::read ||
        built != form_language::FormModelOutcome::model_built) {
        return false;
    }
    generation.recorder = gui_forms::detail::make_library_shared<detail::FormMockupRecorder>(state_, generation.handlers);
    form_language::FormSources sources{};
    make_sources(generation, sources);
    if (refused_) return false;
    form_language::LoadedForm loaded{};
    const form_language::FormLoadOutcome outcome =
        form_language::load_form(document_, schema_, model_, sources, loaded, diagnostics_);
    if (outcome != form_language::FormLoadOutcome::loaded) return false;
    for (const form_language::FixtureRecords& records : fixture_.records) {
        if (records.shown_in.empty()) continue;
        const std::shared_ptr<BindingSource> source = record_source(generation, sources, records.name, {});
        if (source) show_records(records, source, loaded);
    }
    for (const form_language::FixtureTree& tree : fixture_.trees) show_tree(tree, loaded);
    if (refused_) return false;
    subscribe_handlers(generation, loaded);
    generation.root = loaded.root;
    if (loaded.window) {
        generation.title = (*loaded.window).title;
        generation.client_size = (*loaded.window).client_size.value_or(Size{});
        generation.accept_button = (*loaded.window).accept_button;
        generation.cancel_button = (*loaded.window).cancel_button;
    }
    for (Component::Ptr& object : loaded.objects) {
        if (!object || std::dynamic_pointer_cast<Control>(object)) continue;
        generation.components.push_back(std::move(object));
    }
    return true;
}

void MockupLoad::make_sources(detail::FormMockupGeneration& generation, form_language::FormSources& sources) {
    for (const form_language::FormProperty& property : model_.properties) {
        if (!property.binding) continue;
        const form_language::FormBinding& binding = *property.binding;
        const PropertyDescriptor& descriptor = form_language::form_property_descriptor(schema_, model_, property);
        const form_language::FormSchemaType& type = schema_.types[model_.objects[property.object.value].schema_type];
        const bool command_slot =
            type.bind_command != nullptr && descriptor.name == form_language::form_command_slot_name;
        if (command_slot) {
            add_command(generation, sources, binding.source);
        } else if (!binding.field.empty()) {
            static_cast<void>(record_source(generation, sources, binding.source, binding.field));
        } else {
            add_scalar(generation, sources, binding.source, descriptor.kind);
        }
    }
}

void MockupLoad::add_scalar(detail::FormMockupGeneration& generation, form_language::FormSources& sources,
                            const std::string& name, BindingValueKind kind) {
    for (const std::string& added : added_) {
        if (added == name) return;
    }
    // The fixture's text, read as the property's kind; the kind's default
    // without one.
    const form_language::FixtureValue* written = nullptr;
    for (const form_language::FixtureValue& value : fixture_.values) {
        if (value.name == name) written = &value;
    }
    const BindingValue text = written != nullptr ? BindingValue{(*written).text} : BindingValue{};
    bool readable = true;
    if (kind == BindingValueKind::boolean) {
        const stxlib::Optional<bool> read = binding_value_to_bool(text);
        readable = written == nullptr || read.present();
        const bool initial = read.present() ? *read.value() : false;
        generation.booleans.push_back(gui_forms::detail::make_library_shared<Value<bool>>(initial));
        sources.add(name, *generation.booleans.back());
    } else if (kind == BindingValueKind::signed_integer) {
        const stxlib::Optional<std::int64_t> read = binding_value_to_signed(text);
        readable = written == nullptr || read.present();
        const int initial = read.present() ? static_cast<int>(*read.value()) : 0;
        generation.integers.push_back(gui_forms::detail::make_library_shared<Value<int>>(initial));
        sources.add(name, *generation.integers.back());
    } else {
        // The model binds a field-less source only to the three scalar
        // kinds; this is the number's.
        const stxlib::Optional<double> read = binding_value_to_number(text);
        readable = written == nullptr || read.present();
        const double initial = read.present() ? *read.value() : 0.0;
        generation.numbers.push_back(gui_forms::detail::make_library_shared<Value<double>>(initial));
        sources.add(name, *generation.numbers.back());
    }
    added_.push_back(name);
    if (!readable) {
        report(DiagnosticCode::form_binding_source_refused, fixture_source_, (*written).position, name, "value",
               "the value " + (*written).text + " is not of the kind the bound property takes");
    }
}

void MockupLoad::add_command(detail::FormMockupGeneration& generation, form_language::FormSources& sources,
                             const std::string& name) {
    for (const std::string& added : added_) {
        if (added == name) return;
    }
    const std::shared_ptr<Command> command = gui_forms::detail::make_library_shared<Command>(name, name);
    generation.commands.push_back(command);
    sources.add(name, *command);
    added_.push_back(name);
    detail::FormMockupHandlerEntry entry{};
    entry.handler = name;
    entry.stable_id = name;
    entry.interaction = "invoked";
    entry.observed = true;
    const std::uint32_t index = static_cast<std::uint32_t>(generation.handlers.size());
    generation.handlers.push_back(std::move(entry));
    on((*command).invoked(), *generation.recorder, &detail::FormMockupRecorder::raised, index);
}

std::shared_ptr<BindingSource> MockupLoad::record_source(detail::FormMockupGeneration& generation,
                                                         form_language::FormSources& sources,
                                                         const std::string& name, const std::string& field) {
    for (std::size_t index = 0U; index < added_.size(); ++index) {
        if (added_[index] != name) continue;
        const form_language::FormSourceReference* found = sources.find(name);
        const std::shared_ptr<BindingSource>* records =
            found == nullptr ? nullptr : std::get_if<std::shared_ptr<BindingSource>>(found);
        // Another kind under this name: the loader reports the mismatch.
        if (records == nullptr) return nullptr;
        return *records;
    }
    const form_language::FixtureRecords* written = nullptr;
    for (const form_language::FixtureRecords& records : fixture_.records) {
        if (records.name == name) written = &records;
    }
    Window* const window = (*state_.host).window();
    if (window == nullptr) {
        const SourcePosition position = written != nullptr ? (*written).position : SourcePosition{};
        report(DiagnosticCode::form_binding_source_refused, written != nullptr ? fixture_source_ : document_,
               position, name, {}, "records need the mockup's host in a window");
        return nullptr;
    }
    std::vector<BindingColumn> columns{};
    if (written != nullptr) columns = (*written).columns;
    const std::shared_ptr<BindingTable> table =
        gui_forms::detail::make_library_shared<BindingTable>(BindingSchema(std::move(columns)));
    if (written != nullptr) (*table).assign((*written).rows);
    // Every field bound to the records, so a binding to a field the
    // fixture leaves out shows nothing instead of refusing.
    for (const form_language::FormProperty& property : model_.properties) {
        if (!property.binding || (*property.binding).source != name || (*property.binding).field.empty()) continue;
        static_cast<void>((*table).find_or_add_column((*property.binding).field));
    }
    if (!field.empty()) static_cast<void>((*table).find_or_add_column(field));
    const std::shared_ptr<BindingSource> source = gui_forms::detail::make_library_shared<BindingSource>(*window);
    (*source).set_provider(table);
    generation.records.push_back(source);
    sources.add(name, source);
    added_.push_back(name);
    return source;
}

Control::Ptr MockupLoad::fixture_target(const std::string& stable_id, SourcePosition position,
                                        const form_language::LoadedForm& loaded) {
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) {
        if (model_.objects[index].stable_id != stable_id) continue;
        const Control::Ptr control = std::dynamic_pointer_cast<Control>(loaded.objects[index]);
        if (control) return control;
    }
    report(DiagnosticCode::form_binding_source_refused, fixture_source_, position, stable_id, "shown-in",
           "shown-in names no control of the form");
    return nullptr;
}

void MockupLoad::show_records(const form_language::FixtureRecords& records,
                              const std::shared_ptr<BindingSource>& source, const form_language::LoadedForm& loaded) {
    for (const std::string& stable_id : records.shown_in) {
        const Control::Ptr target = fixture_target(stable_id, records.position, loaded);
        if (!target) continue;
        try {
            DataGridView* const grid = dynamic_cast<DataGridView*>(target.get());
            ListView* const list = dynamic_cast<ListView*>(target.get());
            const std::optional<PropertyDescriptor> items = (*target).property_descriptor("items");
            if (grid != nullptr) {
                (*grid).data_source = source;
            } else if (list != nullptr) {
                // The view's columns stay when the form wrote some.
                if ((*list).columns().empty()) {
                    std::vector<ListViewColumn> columns{};
                    for (const BindingColumn& column : records.columns) {
                        ListViewColumn shown{};
                        shown.stable_id = column.name;
                        shown.text = column.name;
                        columns.push_back(std::move(shown));
                    }
                    (*list).set_columns(std::move(columns));
                }
                std::vector<ListViewItem> rows{};
                rows.reserve(records.rows.size());
                for (const BindingRow& row : records.rows) {
                    ListViewItem item{};
                    item.stable_id = row.stable_id;
                    for (std::size_t column = 0U; column < row.values.size(); ++column) {
                        std::string text = binding_value_to_string(row.values[column]);
                        if (column == 0U) {
                            item.text = std::move(text);
                        } else {
                            item.sub_items.push_back(std::move(text));
                        }
                    }
                    rows.push_back(std::move(item));
                }
                (*list).set_items(std::move(rows));
            } else if (items && (*items).kind == BindingValueKind::collection && (*items).default_value) {
                const PropertyCollectionValue* model = std::get_if<PropertyCollectionValue>(&*(*items).default_value);
                if (model == nullptr || !*model) {
                    report(DiagnosticCode::form_binding_source_refused, fixture_source_, records.position, stable_id,
                           "shown-in", "its items are no text collection");
                    continue;
                }
                std::vector<BindingValue> texts{};
                texts.reserve(records.rows.size());
                for (const BindingRow& row : records.rows) {
                    std::string text = row.values.empty() ? std::string() : binding_value_to_string(row.values[0]);
                    texts.emplace_back(std::move(text));
                }
                const BindingValue collection{
                    make_property_collection(std::string((*model).item_type_name()), BindingValueKind::text,
                                             std::move(texts))};
                const std::optional<BindingValue> converted = convert_property_value(collection, *items);
                if (!converted) {
                    report(DiagnosticCode::form_binding_source_refused, fixture_source_, records.position, stable_id,
                           "shown-in", "its items take no text");
                    continue;
                }
                (*target).set_property_value("items", *converted);
            } else {
                report(DiagnosticCode::form_binding_source_refused, fixture_source_, records.position, stable_id,
                       "shown-in", "a grid, a list view or a control with text items shows rows");
            }
        } catch (const std::exception& refusal) {
            report(DiagnosticCode::form_binding_source_refused, fixture_source_, records.position, stable_id,
                   "shown-in", refusal.what());
        }
    }
}

void MockupLoad::show_tree(const form_language::FixtureTree& tree, const form_language::LoadedForm& loaded) {
    for (const std::string& stable_id : tree.shown_in) {
        const Control::Ptr target = fixture_target(stable_id, tree.position, loaded);
        if (!target) continue;
        TreeView* const view = dynamic_cast<TreeView*>(target.get());
        if (view == nullptr) {
            report(DiagnosticCode::form_binding_source_refused, fixture_source_, tree.position, stable_id,
                   "shown-in", "a tree view shows a tree");
            continue;
        }
        std::vector<TreeViewItem> items{};
        items.reserve(tree.items.size());
        for (const form_language::FixtureTreeItem& written : tree.items) {
            TreeViewItem item{};
            item.stable_id = written.stable_id;
            item.text = written.text;
            item.depth = written.depth;
            item.expandable = written.expandable;
            item.expanded = written.expanded;
            items.push_back(std::move(item));
        }
        try {
            (*view).set_items(std::move(items));
        } catch (const std::exception& refusal) {
            report(DiagnosticCode::form_binding_source_refused, fixture_source_, tree.position, stable_id,
                   "shown-in", refusal.what());
        }
    }
}

void MockupLoad::subscribe_handlers(detail::FormMockupGeneration& generation,
                                    const form_language::LoadedForm& loaded) {
    for (const form_language::FormHandler& declared : model_.handlers) {
        detail::FormMockupHandlerEntry entry{};
        entry.handler = declared.handler;
        entry.stable_id = model_.objects[declared.object.value].stable_id;
        entry.interaction = declared.interaction;
        const std::uint32_t index = static_cast<std::uint32_t>(generation.handlers.size());
        generation.handlers.push_back(std::move(entry));
        // A form root has no object of its own (LoadedForm::objects).
        const Component::Ptr& object = loaded.objects[declared.object.value];
        if (!declared.raised || !object) continue;
        generation.handlers[index].observed = observe(generation, *object, declared.interaction, index);
    }
}

bool MockupLoad::observe(detail::FormMockupGeneration& generation, Component& object, const std::string& interaction,
                         std::uint32_t index) {
    detail::FormMockupRecorder& recorder = *generation.recorder;
    Control* const control = dynamic_cast<Control*>(&object);
    if (control == nullptr) return false;
    // A button's click is its clicked: raised by a pointer, the keyboard,
    // its access key and perform_click alike (the base click only by a
    // pointer), and recorded once.
    ButtonBase* const button = dynamic_cast<ButtonBase*>(control);
    if (button != nullptr && (interaction == "click" || interaction == "clicked")) {
        on((*button).clicked(), recorder, &detail::FormMockupRecorder::raised, index);
        return true;
    }
    const BaseInteraction base = base_interaction(interaction);
    switch (base) {
    case BaseInteraction::click:
        on((*control).click(), recorder, &detail::FormMockupRecorder::raised, index);
        return true;
    case BaseInteraction::double_click:
        on((*control).double_click(), recorder, &detail::FormMockupRecorder::raised, index);
        return true;
    case BaseInteraction::mouse_enter:
        on((*control).mouse_enter(), recorder, &detail::FormMockupRecorder::raised, index);
        return true;
    case BaseInteraction::mouse_hover:
        on((*control).mouse_hover(), recorder, &detail::FormMockupRecorder::raised, index);
        return true;
    case BaseInteraction::mouse_leave:
        on((*control).mouse_leave(), recorder, &detail::FormMockupRecorder::raised, index);
        return true;
    case BaseInteraction::none:
    default:
        break;
    }
    if (!interaction.ends_with(changed_suffix)) return false;
    const std::string property = interaction.substr(0U, interaction.size() - changed_suffix.size());
    if (!(*control).property_descriptor(property)) return false;
    PropertyChangeForwarder forwarder{};
    forwarder.recorder = &recorder;
    forwarder.index = index;
    SubscriptionToken token = (*control).subscribe_property_changed(property, recorder, forwarder);
    if (!token.connected()) return false;
    recorder.own_subscription(std::move(token));
    return true;
}

void MockupLoad::report(DiagnosticCode code, const ThemeSource& source, SourcePosition position, std::string rule,
                        std::string property, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source.file);
    diagnostic.position = position;
    diagnostic.rule = std::move(rule);
    diagnostic.property = std::move(property);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    refused_ = true;
}

// Shows `generation`'s tree in `host` in place of the old one, inside one
// layout suspension. False, with the old tree put back, when the host
// refuses the new one (an E811 reported).
[[nodiscard]] bool swap_trees(detail::FormMockupState& state, detail::FormMockupGeneration& generation,
                              const ThemeSource& document, ThemeDiagnosticList& diagnostics) {
    Control& host = *state.host;
    const Control::Ptr old_root = state.shown ? (*state.shown).root : Control::Ptr{};
    state.swapping = true;
    host.suspend_layout();
    bool shown = true;
    try {
        if (old_root) static_cast<void>(host.remove_child((*old_root).runtime_id()));
        (*generation.root).dock = DockStyle::fill;
        host.add_child(generation.root);
    } catch (const std::exception& refusal) {
        shown = false;
        ThemeDiagnostic diagnostic{};
        diagnostic.code = DiagnosticCode::form_load_refused;
        diagnostic.file = std::string(document.file);
        diagnostic.detail = std::string("the host refused the tree: ") + refusal.what();
        diagnostics.add(std::move(diagnostic));
        if (old_root && (*old_root).parent() == nullptr) host.add_child(old_root);
    }
    host.resume_layout(true);
    Window* const window = host.window();
    if (shown && window != nullptr) {
        // A button that is no dialog command is left unset: a mockup is
        // shown, not refused, for want of one.
        const ControlLookup accept = (*window).control(generation.accept_button);
        const ControlLookup cancel = (*window).control(generation.cancel_button);
        try {
            (*window).set_accept_button(accept ? accept.control : Control::Ptr{});
            (*window).set_cancel_button(cancel ? cancel.control : Control::Ptr{});
        } catch (const std::invalid_argument&) {
            // Neither button changes the shown tree.
        }
    }
    state.swapping = false;
    return shown;
}

} // namespace

FormMockup::FormMockup(Control& host) {
    stxlib::OperationResult<stxlib::Unique<detail::FormMockupState>> made =
        stxlib::Unique<detail::FormMockupState>::make();
    if (made.failed()) throw std::bad_alloc();
    state_ = std::move(made).take_value();
    (*state_).host = &host;
    (*state_).events.reserve(static_cast<std::size_t>(kept_events));
}

FormMockup::~FormMockup() {
    clear();
}

FormMockupLoadOutcome FormMockup::load(const FormDocumentSchema& schema, const FormMockupText& document,
                                       const FormMockupText& fixture) {
    ThemeSource document_source{};
    document_source.file = text_of(document.file);
    document_source.text = text_of(document.text);
    ThemeSource fixture_source{};
    fixture_source.file = text_of(fixture.file);
    fixture_source.text = text_of(fixture.text);
    ThemeDiagnosticList diagnostics{};
    std::unique_ptr<detail::FormMockupGeneration> generation = std::make_unique<detail::FormMockupGeneration>();
    MockupLoad work(*state_, detail::form_document_schema(detail::FormDocumentAccess::state(schema)),
                    document_source, fixture_source, diagnostics);
    const bool made = work.run(*generation) && swap_trees(*state_, *generation, document_source, diagnostics);
    check_ = detail::FormDocumentAccess::make_check(detail::form_document_check_state(diagnostics, made));
    if (!made) {
        // The new generation ends here; its tree was never shown.
        if ((*generation).root) (*(*generation).root).dispose();
        return FormMockupLoadOutcome::refused;
    }
    std::unique_ptr<detail::FormMockupGeneration> previous = std::move((*state_).shown);
    (*state_).shown = std::move(generation);
    ++(*state_).generation;
    if (previous && (*previous).root) (*(*previous).root).dispose();
    return FormMockupLoadOutcome::loaded;
}

void FormMockup::clear() {
    if (!state_.present() || !(*state_).shown) return;
    std::unique_ptr<detail::FormMockupGeneration> previous = std::move((*state_).shown);
    if ((*previous).root) {
        Control& host = *(*state_).host;
        (*state_).swapping = true;
        if ((*(*previous).root).parent() != nullptr) static_cast<void>(host.remove_child((*(*previous).root).runtime_id()));
        (*(*previous).root).dispose();
        (*state_).swapping = false;
    }
}

ThemeCompileResult FormMockup::set_theme(ThemeBase base, const FormMockupText& sheet) {
    Control& host = *(*state_).host;
    Window* const window = host.window();
    if (window == nullptr) throw std::logic_error("FormMockup::set_theme: the host is in no window");
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    options.base = base;
    const std::string_view text = text_of(sheet.text);
    std::vector<ThemePackage> packages{};
    if (!text.empty()) {
        const std::string_view file = text_of(sheet.file);
        packages.push_back(theme_sheet_package(text, file.empty() ? std::string_view("mockup") : file));
    }
    const std::span<const ThemePackage> listed(packages.data(), packages.size());
    ThemeCompileResult compiled = (*(*window).resources()).compile_theme_packages(listed, ThemeLibrary{}, options);
    if (compiled.succeeded()) host.set_theme_override(compiled.theme());
    return compiled;
}

bool FormMockup::shown() const noexcept {
    const bool showing = state_.present() && (*state_).shown != nullptr;
    return showing;
}

stxlib::u64 FormMockup::generation() const noexcept {
    if (!state_.present()) return 0U;
    return (*state_).generation;
}

stxlib::View<char> FormMockup::title() const& noexcept {
    if (!shown()) return stxlib::View<char>();
    return view_of((*(*state_).shown).title);
}

Size FormMockup::client_size() const noexcept {
    if (!shown()) return Size{};
    return (*(*state_).shown).client_size;
}

stxlib::usize FormMockup::handler_count() const noexcept {
    if (!shown()) return 0U;
    return static_cast<stxlib::usize>((*(*state_).shown).handlers.size());
}

FormMockupHandler FormMockup::handler(stxlib::usize index) const& noexcept {
    FormMockupHandler handler{};
    if (index >= handler_count()) return handler;
    const detail::FormMockupHandlerEntry& entry = (*(*state_).shown).handlers[static_cast<std::size_t>(index)];
    handler.handler = view_of(entry.handler);
    handler.stable_id = view_of(entry.stable_id);
    handler.interaction = view_of(entry.interaction);
    handler.observed = entry.observed;
    return handler;
}

stxlib::usize FormMockup::event_count() const noexcept {
    if (!state_.present()) return 0U;
    return static_cast<stxlib::usize>((*state_).events.size());
}

FormMockupEvent FormMockup::event(stxlib::usize index) const& noexcept {
    FormMockupEvent event{};
    if (index >= event_count()) return event;
    const std::vector<detail::FormMockupKeptEvent>& events = (*state_).events;
    const std::size_t place = ((*state_).first_event + static_cast<std::size_t>(index)) % events.size();
    const detail::FormMockupKeptEvent& kept = events[place];
    event.sequence = kept.sequence;
    event.handler = view_of(kept.handler);
    event.stable_id = view_of(kept.stable_id);
    event.interaction = view_of(kept.interaction);
    return event;
}

void FormMockup::clear_events() noexcept {
    if (!state_.present()) return;
    (*state_).events.clear();
    (*state_).first_event = 0U;
}

void FormMockup::set_event_sink(FormMockupEventSink sink, void* context) noexcept {
    if (!state_.present()) return;
    (*state_).sink = sink;
    (*state_).sink_context = sink == nullptr ? nullptr : context;
}

} // namespace gui_forms
