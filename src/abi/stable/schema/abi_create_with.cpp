// The objects objects.create_with makes. See abi_create_with.hpp.

#include "abi_create_with.hpp"

#include "abi_schema.hpp"

#include "../engine/abi_engine.hpp"
#include "../registry/abi_registry.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/background_worker/background_worker.hpp"
#include "gui_forms/binding/binding_context/binding_context.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/commands/color_command/color_command.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/font_command/font_command.hpp"
#include "gui_forms/commands/gallery_command/gallery_command.hpp"
#include "gui_forms/components/error_provider/error_provider.hpp"
#include "gui_forms/components/help_provider/help_provider.hpp"
#include "gui_forms/components/notify_icon/notify_icon.hpp"
#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp"
#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/controls/docking/dock_site/dock_site.hpp"
#include "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp"
#include "gui_forms/controls/pages/navigation_pane/navigation_pane.hpp"
#include "gui_forms/controls/pages/page_strip/page_strip.hpp"
#include "gui_forms/controls/pages/page_view/page_view.hpp"
#include "gui_forms/controls/panel/anchored_popup_layer/anchored_popup_layer.hpp"
#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"
#include "gui_forms/feedback/semantic_feedback/semantic_feedback.hpp"
#include "gui_forms/image_list/image_list/image_list.hpp"
#include "gui_forms/timer/timer/timer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::abi1 {
namespace {

// What a type create_with makes needs.
enum class Recipe : std::uint8_t {
    anchored_popup_layer,
    dock_site,
    page_strip,
    flags_value_editor,
    color_command,
    font_command,
    gallery_command,
    on_window,
    as_create,
};

struct NamedRecipe final {
    std::string_view id;
    Recipe recipe = Recipe::as_create;
};

constexpr std::array<NamedRecipe, 7> recipes{{
    {"anchored_popup_layer", Recipe::anchored_popup_layer},
    {"dock_site", Recipe::dock_site},
    {"page_strip", Recipe::page_strip},
    {"flags_value_editor", Recipe::flags_value_editor},
    {"color_command", Recipe::color_command},
    {"font_command", Recipe::font_command},
    {"gallery_command", Recipe::gallery_command},
}};

// A component bound to a window, made as its C++ constructor taking Window&
// makes it (with that constructor's defaults).
using WindowMaker = Component::Ptr (*)(Window& window);

template <typename Type>
Component::Ptr make_on_window(Window& window) {
    Component::Ptr made = std::make_shared<Type>(window);
    return made;
}

struct WindowRecipe final {
    std::string_view id;
    WindowMaker make = nullptr;
};

constexpr std::array<WindowRecipe, 11> window_recipes{{
    {"background_worker", &make_on_window<BackgroundWorker>},
    {"binding_context", &make_on_window<BindingContext>},
    {"binding_source", &make_on_window<BindingSource>},
    {"error_provider", &make_on_window<ErrorProvider>},
    {"help_provider", &make_on_window<HelpProvider>},
    {"image_list", &make_on_window<ImageList>},
    {"notify_icon", &make_on_window<NotifyIcon>},
    {"ribbon_tool_tip", &make_on_window<RibbonToolTip>},
    {"semantic_feedback", &make_on_window<SemanticFeedback>},
    {"timer", &make_on_window<Timer>},
    {"tool_tip", &make_on_window<ToolTip>},
}};

// The maker of a component bound to a window, or null.
WindowMaker window_maker_of(std::string_view id) noexcept {
    for (const WindowRecipe& named : window_recipes) {
        if (named.id == id) {
            return named.make;
        }
    }
    return nullptr;
}

Recipe recipe_of(std::string_view id) noexcept {
    for (const NamedRecipe& named : recipes) {
        if (named.id == id) {
            return named.recipe;
        }
    }
    if (window_maker_of(id) != nullptr) {
        return Recipe::on_window;
    }
    return Recipe::as_create;
}

// The owner as Type, or a failure naming what the type needs.
template <typename Type>
gf_status owner_as(const CreateRequest& request, SlotIdentity slot, std::string_view needed,
                   std::shared_ptr<Type>& typed) {
    if (!request.owner) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, std::string("this type needs an owner: ") + std::string(needed));
    }
    typed = std::dynamic_pointer_cast<Type>(request.owner);
    if (!typed) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, std::string("the owner is not ") + std::string(needed));
    }
    if (!(*typed).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the owner has been disposed");
    }
    return GF_STATUS_OK;
}

gf_status refuse_enumeration(const CreateRequest& request, SlotIdentity slot) {
    if (request.enumeration) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "this type takes no enumeration");
    }
    return GF_STATUS_OK;
}

// A command extension over the owner command.
template <typename Extension>
gf_status make_extension(const CreateRequest& request, SlotIdentity slot, Component::Ptr& made) {
    std::shared_ptr<Command> command;
    const gf_status owned = owner_as<Command>(request, slot, "a command", command);
    if (owned != GF_STATUS_OK) {
        return owned;
    }
    made = std::make_shared<Extension>(command);
    return GF_STATUS_OK;
}

// The page set of a page view or a navigation pane.
PageSet* page_set_of(const Component::Ptr& owner) noexcept {
    PageView* view = dynamic_cast<PageView*>(owner.get());
    if (view != nullptr) {
        return &(*view).pages();
    }
    NavigationPane* pane = dynamic_cast<NavigationPane*>(owner.get());
    if (pane != nullptr) {
        return &(*pane).pages();
    }
    return nullptr;
}

// A type objects.create makes, when the request asks nothing of it.
gf_status make_as_create(const CreateRequest& request, StableId stable_id, SlotIdentity slot, Component::Ptr& made) {
    if (request.owner) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "this type takes no owner");
    }
    made = create_catalog_type(request.type, std::move(stable_id));
    if (!made) {
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, slot,
                    "objects.create and create_with do not make this type (a binding: bindings.bind)");
    }
    return GF_STATUS_OK;
}

} // namespace

gf_status create_from_request(const CreateRequest& request, StableId stable_id, SlotIdentity slot,
                              Component::Ptr& made) {
    const Recipe recipe = recipe_of(catalog_type_id(request.type));
    if (recipe != Recipe::flags_value_editor) {
        const gf_status refused = refuse_enumeration(request, slot);
        if (refused != GF_STATUS_OK) {
            return refused;
        }
    }
    switch (recipe) {
    case Recipe::anchored_popup_layer: {
        Control::Ptr anchor;
        const gf_status owned = owner_as<Control>(request, slot, "a control (the anchor)", anchor);
        if (owned != GF_STATUS_OK) {
            return owned;
        }
        made = make_control<AnchoredPopupLayer>(std::move(stable_id), anchor);
        return GF_STATUS_OK;
    }
    case Recipe::dock_site: {
        std::shared_ptr<DockWorkspace> workspace;
        const gf_status owned = owner_as<DockWorkspace>(request, slot, "a dock workspace", workspace);
        if (owned != GF_STATUS_OK) {
            return owned;
        }
        const Component::Ptr site = make_control<DockSite>(std::move(stable_id), *workspace);
        engine().keep_owner(site, workspace);
        made = site;
        return GF_STATUS_OK;
    }
    case Recipe::page_strip: {
        Control::Ptr owner;
        const gf_status owned = owner_as<Control>(request, slot, "a page view or a navigation pane", owner);
        if (owned != GF_STATUS_OK) {
            return owned;
        }
        PageSet* pages = page_set_of(owner);
        if (pages == nullptr) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the owner is not a page view or a navigation pane");
        }
        const Component::Ptr strip = make_control<PageStrip>(std::move(stable_id), *pages);
        engine().keep_owner(strip, owner);
        made = strip;
        return GF_STATUS_OK;
    }
    case Recipe::flags_value_editor: {
        if (request.owner) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a flags editor takes no owner");
        }
        if (!request.enumeration) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a flags editor needs an enumeration");
        }
        const PropertyEnumDescriptor& descriptor = *request.enumeration;
        PropertyEnumValue value{descriptor.type_name, std::string{}, request.value};
        for (const PropertyEnumChoice& choice : descriptor.choices) {
            if (choice.value == request.value) {
                value.name = choice.name;
            }
        }
        made = make_control<FlagsValueEditor>(std::move(stable_id), descriptor, std::move(value));
        return GF_STATUS_OK;
    }
    case Recipe::color_command:
        return make_extension<ColorCommand>(request, slot, made);
    case Recipe::font_command:
        return make_extension<FontCommand>(request, slot, made);
    case Recipe::gallery_command:
        return make_extension<GalleryCommand>(request, slot, made);
    case Recipe::on_window: {
        std::shared_ptr<WindowHandle> handle;
        const gf_status owned = owner_as<WindowHandle>(request, slot, "a window (windows.create)", handle);
        if (owned != GF_STATUS_OK) {
            return owned;
        }
        // An alive handle observes its window until the run ends it.
        const WindowMaker make = window_maker_of(catalog_type_id(request.type));
        made = (*make)(*(*handle).window());
        return GF_STATUS_OK;
    }
    case Recipe::as_create:
        return make_as_create(request, std::move(stable_id), slot, made);
    }
    return fail(GF_STATUS_INTERNAL, slot, "an unknown creation recipe");
}

gf_status read_enumeration(gf_utf8_view enumeration_name, const gf_create_choice* choices,
                           std::uint64_t choice_count, bool combinable, SlotIdentity slot,
                           std::optional<PropertyEnumDescriptor>& enumeration) {
    if (choice_count == 0U && enumeration_name.size == 0U) {
        return GF_STATUS_OK;
    }
    if (choice_count == 0U || choice_count > GF_CREATE_MAXIMUM_CHOICES ||
        choices == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an enumeration has 1 to 256 choices");
    }
    std::string_view name;
    const gf_status named = read_text(enumeration_name, slot, name);
    if (named != GF_STATUS_OK) {
        return named;
    }
    if (name.empty() || name.size() > GF_CREATE_MAXIMUM_NAME_BYTES) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an enumeration's name has 1 to 256 bytes");
    }
    PropertyEnumDescriptor descriptor{};
    descriptor.type_name = std::string(name);
    descriptor.flags = combinable;
    descriptor.choices.reserve(static_cast<std::size_t>(choice_count));
    std::uint64_t index = 0U;
    for (index = 0U; index < choice_count; ++index) {
        std::string_view choice_name;
        const gf_status read = read_text(choices[index].name, slot, choice_name);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (choice_name.empty() || choice_name.size() > GF_CREATE_MAXIMUM_NAME_BYTES) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a choice's name has 1 to 256 bytes");
        }
        for (const PropertyEnumChoice& earlier : descriptor.choices) {
            if (earlier.name == choice_name) {
                return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an enumeration's choice names are distinct");
            }
        }
        descriptor.choices.push_back(PropertyEnumChoice{std::string(choice_name), choices[index].value});
    }
    if (!valid_property_enum_descriptor(descriptor)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the enumeration is not a valid property enumeration");
    }
    enumeration = std::move(descriptor);
    return GF_STATUS_OK;
}

} // namespace gui_forms::abi1
