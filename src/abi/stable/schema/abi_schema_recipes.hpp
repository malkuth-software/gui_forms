#pragma once

// How the C ABI makes each registry type (stages C3, C5): describe_type
// records one fresh instance's schema, create_type makes an object for
// objects.create, and is_exact_type recognizes an instance of exactly the
// type. The generated catalog (abi_schema_catalog.hpp) names them for every
// type in manifests/controls.json.
//
// A control made from a stable id alone, and a component made from nothing
// or from its id, use the primary templates. A type that needs anything
// else (a component a window or a command owns, a control that needs
// another object) has its own describe_type below; create_type makes none
// of it (objects.create_with makes the controls and the command
// extensions).

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/schema.hpp"

#include "gui_forms/background_worker/background_worker.hpp"
#include "gui_forms/binding/binding/binding.hpp"
#include "gui_forms/binding/binding_context/binding_context.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/commands/color_command/color_command.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/font_command/font_command.hpp"
#include "gui_forms/commands/gallery_command/gallery_command.hpp"
#include "gui_forms/components/color_dialog/color_dialog.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/components/error_provider/error_provider.hpp"
#include "gui_forms/components/folder_browser_dialog/folder_browser_dialog.hpp"
#include "gui_forms/components/help_provider/help_provider.hpp"
#include "gui_forms/components/notify_icon/notify_icon.hpp"
#include "gui_forms/components/open_file_dialog/open_file_dialog.hpp"
#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp"
#include "gui_forms/components/save_file_dialog/save_file_dialog.hpp"
#include "gui_forms/components/thread_exception_dialog/thread_exception_dialog.hpp"
#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/controls/docking/dock_site/dock_site.hpp"
#include "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp"
#include "gui_forms/controls/pages/page_strip/page_strip.hpp"
#include "gui_forms/controls/panel/anchored_popup_layer/anchored_popup_layer.hpp"
#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"
#include "gui_forms/feedback/semantic_feedback/semantic_feedback.hpp"
#include "gui_forms/image_list/image_list/image_list.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/timer/timer/timer.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace gui_forms::abi1 {

// What describing one type records: a control's anatomy (static storage)
// and its properties and interactions, or a component's interactions. A
// type nothing records is a service.
struct SchemaSample final {
    const ControlAnatomy* anatomy = nullptr;
    std::vector<PropertyDescriptor> properties;
    std::vector<InteractionDeclaration> interactions;
    bool recorded = false;
};

void record_control(const Control& control, SchemaSample& sample);
void record_component(const Component& component, SchemaSample& sample);

// The stable id of every instance describe_type makes.
inline constexpr std::string_view sample_stable_id = "gf-abi.schema.sample";

// True for a control type objects.create can make: one constructible from
// a stable id alone.
template <typename Type>
inline constexpr bool made_from_stable_id =
    std::is_base_of_v<Control, Type> && std::is_constructible_v<Type, StableId>;

// True for a component (not a control) objects.create makes from nothing
// or from its id as text.
template <typename Type>
inline constexpr bool made_from_nothing = std::is_base_of_v<Component, Type> && !std::is_base_of_v<Control, Type> &&
                                          (std::is_default_constructible_v<Type> ||
                                           std::is_constructible_v<Type, std::string>);

// A new object of Type, or null when Type is made from neither. Throws what
// the constructor throws.
template <typename Type>
Component::Ptr create_type(StableId stable_id) {
    if constexpr (made_from_stable_id<Type>) {
        const Control::Ptr control = make_control<Type>(std::move(stable_id));
        return control;
    } else if constexpr (made_from_nothing<Type> && std::is_constructible_v<Type, std::string>) {
        const std::shared_ptr<Type> component = std::make_shared<Type>(std::string(stable_id.value()));
        return component;
    } else if constexpr (made_from_nothing<Type>) {
        static_cast<void>(stable_id);
        const std::shared_ptr<Type> component = std::make_shared<Type>();
        return component;
    } else {
        static_cast<void>(stable_id);
        return nullptr;
    }
}

// A thread exception dialog made with an empty report.
template <>
Component::Ptr create_type<ThreadExceptionDialog>(StableId stable_id);

// Whether object is exactly a Type (not a subclass).
template <typename Type>
bool is_exact_type(const Component& object) {
    const bool exact = typeid(object) == typeid(Type);
    return exact;
}

// Records a fresh instance of Type into sample, then destroys it.
template <typename Type>
void describe_type(SchemaSample& sample) {
    static_assert(made_from_stable_id<Type>,
                  "a type not made from a stable id alone needs its own describe_type");
    const Control::Ptr control = make_control<Type>(StableId(std::string(sample_stable_id)));
    record_control(*control, sample);
}

// Controls that need another object.
template <> void describe_type<AnchoredPopupLayer>(SchemaSample& sample);
template <> void describe_type<DockSite>(SchemaSample& sample);
template <> void describe_type<FlagsValueEditor>(SchemaSample& sample);
template <> void describe_type<PageStrip>(SchemaSample& sample);

// Components.
template <> void describe_type<BackgroundWorker>(SchemaSample& sample);
template <> void describe_type<Binding>(SchemaSample& sample);
template <> void describe_type<BindingContext>(SchemaSample& sample);
template <> void describe_type<BindingSource>(SchemaSample& sample);
template <> void describe_type<ColorCommand>(SchemaSample& sample);
template <> void describe_type<ColorDialog>(SchemaSample& sample);
template <> void describe_type<Command>(SchemaSample& sample);
template <> void describe_type<ContextMenu>(SchemaSample& sample);
template <> void describe_type<DockWorkspace>(SchemaSample& sample);
template <> void describe_type<ErrorProvider>(SchemaSample& sample);
template <> void describe_type<FolderBrowserDialog>(SchemaSample& sample);
template <> void describe_type<FontCommand>(SchemaSample& sample);
template <> void describe_type<GalleryCommand>(SchemaSample& sample);
template <> void describe_type<HelpProvider>(SchemaSample& sample);
template <> void describe_type<ImageList>(SchemaSample& sample);
template <> void describe_type<NotifyIcon>(SchemaSample& sample);
template <> void describe_type<OpenFileDialog>(SchemaSample& sample);
template <> void describe_type<RibbonToolTip>(SchemaSample& sample);
template <> void describe_type<SaveFileDialog>(SchemaSample& sample);
template <> void describe_type<SemanticFeedback>(SchemaSample& sample);
template <> void describe_type<ThreadExceptionDialog>(SchemaSample& sample);
template <> void describe_type<Timer>(SchemaSample& sample);
template <> void describe_type<ToolTip>(SchemaSample& sample);

// A service: nothing to record.
template <> void describe_type<LiveSurface>(SchemaSample& sample);

// One registry type: its id and C++ type name (static storage) and its
// recipes.
struct SchemaCatalogEntry final {
    std::string_view id;
    std::string_view type_name;
    void (*describe)(SchemaSample& sample) = nullptr;
    Component::Ptr (*create)(StableId stable_id) = nullptr;
    bool (*is_exact)(const Component& object) = nullptr;
};

} // namespace gui_forms::abi1
