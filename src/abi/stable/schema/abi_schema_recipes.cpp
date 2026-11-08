// The recipes of the types that are not made from a stable id alone. See
// abi_schema_recipes.hpp. Each makes what the type needs, records one fresh
// instance and destroys everything before it returns; locals are declared
// owner first, so the instance goes before what it borrows.

#include "abi_schema_recipes.hpp"

#include "gui_forms/binding.hpp"
#include "gui_forms/controls/button_base/button_base.hpp"
#include "gui_forms/controls/label/label.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/pages.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

namespace gui_forms::abi1 {

void record_control(const Control& control, SchemaSample& sample) {
    sample.anatomy = &control.anatomy();
    sample.properties = control.property_descriptors();
    sample.child_properties = control.child_property_descriptors();
    sample.interactions = control.interactions();
    sample.recorded = true;
}

void record_component(const Component& component, SchemaSample& sample) {
    sample.interactions = component.interactions();
    sample.recorded = true;
}

namespace {

StableId sample_id() {
    const StableId id{std::string(sample_stable_id)};
    return id;
}

// A component a window owns: made with a window around an empty panel.
template <typename Type>
void describe_window_component(SchemaSample& sample) {
    Window window(make_control<Panel>(sample_id()));
    const std::shared_ptr<Type> component = std::make_shared<Type>(window);
    record_component(*component, sample);
}

// A component made from nothing.
template <typename Type>
void describe_free_component(SchemaSample& sample) {
    const std::shared_ptr<Type> component = std::make_shared<Type>();
    record_component(*component, sample);
}

// A command extension, made on a plain command.
template <typename Type>
void describe_command_extension(SchemaSample& sample) {
    const std::shared_ptr<Command> command = std::make_shared<Command>();
    const std::shared_ptr<Type> extension = std::make_shared<Type>(command);
    record_component(*extension, sample);
}

} // namespace

template <>
void describe_type<AnchoredOverlayLayer>(SchemaSample& sample) {
    const std::shared_ptr<ButtonBase> anchor = make_control<ButtonBase>(sample_id());
    const std::shared_ptr<AnchoredOverlayLayer> layer =
        make_control<AnchoredOverlayLayer>(sample_id(), anchor, AnchoredOverlayPlacement{});
    record_control(*layer, sample);
}

template <>
void describe_type<DockSite>(SchemaSample& sample) {
    DockWorkspace workspace{std::string(sample_stable_id)};
    const std::shared_ptr<DockSite> site = make_control<DockSite>(sample_id(), workspace);
    record_control(*site, sample);
}

// A flags editor over a two-flag enumeration.
template <>
void describe_type<FlagsValueEditor>(SchemaSample& sample) {
    const PropertyEnumDescriptor descriptor{"sample_flags", {{"none", 0}, {"first", 1}, {"second", 2}}, true};
    const PropertyEnumValue value{"sample_flags", "first", 1};
    const std::shared_ptr<FlagsValueEditor> editor = make_control<FlagsValueEditor>(sample_id(), descriptor, value);
    record_control(*editor, sample);
}

template <>
void describe_type<PageStrip>(SchemaSample& sample) {
    PageSet pages;
    const std::shared_ptr<PageStrip> strip = make_control<PageStrip>(sample_id(), pages);
    record_control(*strip, sample);
}

template <>
void describe_type<BackgroundWorker>(SchemaSample& sample) {
    describe_window_component<BackgroundWorker>(sample);
}

// A binding of a label's text to an empty source.
template <>
void describe_type<Binding>(SchemaSample& sample) {
    Window window(make_control<Panel>(sample_id()));
    const std::shared_ptr<BindingSource> source = std::make_shared<BindingSource>(window);
    const std::shared_ptr<Label> bound = make_control<Label>(sample_id());
    const std::shared_ptr<Binding> binding = (*bound).data_bindings().add("Text", source, "Name");
    if (binding == nullptr) {
        throw std::logic_error("a binding to an empty source is created");
    }
    record_component(*binding, sample);
}

template <>
void describe_type<BindingContext>(SchemaSample& sample) {
    describe_window_component<BindingContext>(sample);
}

template <>
void describe_type<BindingSource>(SchemaSample& sample) {
    describe_window_component<BindingSource>(sample);
}

template <>
void describe_type<ColorCommand>(SchemaSample& sample) {
    describe_command_extension<ColorCommand>(sample);
}

template <>
void describe_type<ColorDialog>(SchemaSample& sample) {
    describe_free_component<ColorDialog>(sample);
}

template <>
void describe_type<Command>(SchemaSample& sample) {
    describe_free_component<Command>(sample);
}

template <>
void describe_type<ContextMenu>(SchemaSample& sample) {
    const std::shared_ptr<ContextMenu> menu = std::make_shared<ContextMenu>(std::string(sample_stable_id));
    record_component(*menu, sample);
}

template <>
void describe_type<DockWorkspace>(SchemaSample& sample) {
    const std::shared_ptr<DockWorkspace> workspace = std::make_shared<DockWorkspace>(std::string(sample_stable_id));
    record_component(*workspace, sample);
}

template <>
void describe_type<ErrorProvider>(SchemaSample& sample) {
    describe_window_component<ErrorProvider>(sample);
}

template <>
void describe_type<FolderBrowserDialog>(SchemaSample& sample) {
    describe_free_component<FolderBrowserDialog>(sample);
}

template <>
void describe_type<FontCommand>(SchemaSample& sample) {
    describe_command_extension<FontCommand>(sample);
}

template <>
void describe_type<GalleryCommand>(SchemaSample& sample) {
    describe_command_extension<GalleryCommand>(sample);
}

template <>
void describe_type<HelpProvider>(SchemaSample& sample) {
    describe_window_component<HelpProvider>(sample);
}

template <>
void describe_type<ImageList>(SchemaSample& sample) {
    describe_window_component<ImageList>(sample);
}

template <>
void describe_type<NotifyIcon>(SchemaSample& sample) {
    describe_window_component<NotifyIcon>(sample);
}

template <>
void describe_type<OpenFileDialog>(SchemaSample& sample) {
    describe_free_component<OpenFileDialog>(sample);
}

template <>
void describe_type<RibbonToolTip>(SchemaSample& sample) {
    describe_window_component<RibbonToolTip>(sample);
}

template <>
void describe_type<SaveFileDialog>(SchemaSample& sample) {
    describe_free_component<SaveFileDialog>(sample);
}

template <>
void describe_type<SemanticFeedback>(SchemaSample& sample) {
    describe_window_component<SemanticFeedback>(sample);
}

template <>
void describe_type<ThreadExceptionDialog>(SchemaSample& sample) {
    const std::shared_ptr<ThreadExceptionDialog> dialog =
        std::make_shared<ThreadExceptionDialog>(sample_id(), ThreadExceptionReport{});
    record_component(*dialog, sample);
}

template <>
Component::Ptr create_type<ThreadExceptionDialog>(StableId stable_id) {
    const std::shared_ptr<ThreadExceptionDialog> dialog =
        std::make_shared<ThreadExceptionDialog>(std::move(stable_id), ThreadExceptionReport{});
    return dialog;
}

template <>
void describe_type<Timer>(SchemaSample& sample) {
    describe_window_component<Timer>(sample);
}

template <>
void describe_type<ToolTip>(SchemaSample& sample) {
    describe_window_component<ToolTip>(sample);
}

template <>
void describe_type<LiveSurface>(SchemaSample& sample) {
    static_cast<void>(sample);
}

} // namespace gui_forms::abi1
