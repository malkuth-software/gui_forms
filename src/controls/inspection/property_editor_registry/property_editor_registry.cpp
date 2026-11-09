#include "gui_forms/inspection/property_editor_registry/property_editor_registry.hpp"

#include "gui_forms/controls/panel/color_value_editor/color_value_editor.hpp"
#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"
#include "../property_service_utilities.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using detail::property_service_name;

namespace {

struct NumericEditorSynchronizer final {
    std::weak_ptr<NumericUpDown> editor;
    std::shared_ptr<bool> synchronizing;

    void operator()(const BindingValue& value) const {
        const std::shared_ptr<NumericUpDown> retained = editor.lock();
        const std::optional<double> converted = binding_value_to_number(value);
        if (!retained || !converted) return;
        *synchronizing = true;
        (*retained).value = *converted;
        *synchronizing = false;
    }
};

struct NumericEditorCommitRelay final {
    std::shared_ptr<bool> synchronizing;
    std::function<void(BindingValue)> committed;

    void operator()(double value) const {
        if (!*synchronizing) committed(BindingValue{value});
    }
};

struct NumericEditorCommitConnector final {
    std::weak_ptr<NumericUpDown> editor;
    std::shared_ptr<bool> synchronizing;

    SubscriptionToken operator()(
        Component& owner,
        std::function<void(BindingValue)> committed) const {
        const std::shared_ptr<NumericUpDown> retained = editor.lock();
        return retained
            ? (*retained).value_changed().subscribe(
                  owner, NumericEditorCommitRelay{
                      synchronizing, std::move(committed)})
            : SubscriptionToken{};
    }
};

struct FlagsEditorSynchronizer final {
    std::weak_ptr<FlagsValueEditor> editor;
    std::shared_ptr<bool> synchronizing;

    void operator()(const BindingValue& value) const {
        const std::shared_ptr<FlagsValueEditor> retained = editor.lock();
        const PropertyEnumValue* flags = std::get_if<PropertyEnumValue>(&value);
        if (!retained || !flags) return;
        *synchronizing = true;
        (*retained).value = *flags;
        *synchronizing = false;
    }
};

struct FlagsEditorCommitRelay final {
    std::shared_ptr<bool> synchronizing;
    std::function<void(BindingValue)> committed;

    void operator()(const PropertyEnumValue& value) const {
        if (!*synchronizing) committed(BindingValue{value});
    }
};

struct FlagsEditorCommitConnector final {
    std::weak_ptr<FlagsValueEditor> editor;
    std::shared_ptr<bool> synchronizing;

    SubscriptionToken operator()(
        Component& owner,
        std::function<void(BindingValue)> committed) const {
        const std::shared_ptr<FlagsValueEditor> retained = editor.lock();
        return retained
            ? (*retained).value_changed().subscribe(
                  owner, FlagsEditorCommitRelay{
                      synchronizing, std::move(committed)})
            : SubscriptionToken{};
    }
};

struct ColorEditorSynchronizer final {
    std::weak_ptr<ColorValueEditor> editor;
    std::shared_ptr<bool> synchronizing;

    void operator()(const BindingValue& value) const {
        const std::shared_ptr<ColorValueEditor> retained = editor.lock();
        const Color* color = std::get_if<Color>(&value);
        if (!retained || !color) return;
        *synchronizing = true;
        (*retained).value = *color;
        *synchronizing = false;
    }
};

struct ColorEditorCommitRelay final {
    std::shared_ptr<bool> synchronizing;
    std::function<void(BindingValue)> committed;

    void operator()(Color value) const {
        if (!*synchronizing) committed(BindingValue{value});
    }
};

struct ColorEditorCommitConnector final {
    std::weak_ptr<ColorValueEditor> editor;
    std::shared_ptr<bool> synchronizing;

    SubscriptionToken operator()(
        Component& owner,
        std::function<void(BindingValue)> committed) const {
        const std::shared_ptr<ColorValueEditor> retained = editor.lock();
        return retained
            ? (*retained).value_changed().subscribe(
                  owner, ColorEditorCommitRelay{
                      synchronizing, std::move(committed)})
            : SubscriptionToken{};
    }
};

struct ColorEditorFailureConnector final {
    std::weak_ptr<ColorValueEditor> editor;

    SubscriptionToken operator()(
        Component& owner,
        std::function<void(const PropertyEditorInputError&)> failed) const {
        const std::shared_ptr<ColorValueEditor> retained = editor.lock();
        return retained
            ? (*retained).edit_failed().subscribe(owner, std::move(failed))
            : SubscriptionToken{};
    }
};

std::optional<PropertyEditorBinding> create_numeric_editor(
    const PropertyEditorRequest& request) {
    const std::optional<double> number = binding_value_to_number(request.value);
    if (!number) return {};
    const std::shared_ptr<NumericUpDown> editor = make_control<NumericUpDown>(
        StableId(request.stable_id));
    (*editor).set_range(std::numeric_limits<double>::lowest(),
                        std::numeric_limits<double>::max());
    (*editor).increment = 0.1;
    (*editor).decimal_places = 4U;
    (*editor).value = *number;
    (*editor).enabled = request.writable;
    (*editor).accessible_name = request.property_path;
    (*editor).accessible_description = request.descriptor.description;
    const std::shared_ptr<bool> synchronizing = std::make_shared<bool>(false);
    PropertyEditorBinding binding;
    binding.control = editor;
    binding.synchronize = NumericEditorSynchronizer{editor, synchronizing};
    binding.connect_committed =
        NumericEditorCommitConnector{editor, synchronizing};
    return binding;
}

std::optional<PropertyEditorBinding> create_flags_editor(
    const PropertyEditorRequest& request) {
    if (!request.descriptor.enumeration ||
        !(*request.descriptor.enumeration).flags) return {};
    const PropertyEnumValue* value =
        std::get_if<PropertyEnumValue>(&request.value);
    if (!value) return {};
    bool has_bit = false;
    for (const PropertyEnumChoice& choice :
         (*request.descriptor.enumeration).choices) {
        if (choice.value > 0 && std::has_single_bit(
                static_cast<std::uint64_t>(choice.value))) {
            has_bit = true;
            break;
        }
    }
    if (!has_bit) return {};
    const std::shared_ptr<FlagsValueEditor> editor =
        make_control<FlagsValueEditor>(
            StableId(request.stable_id), *request.descriptor.enumeration,
            *value);
    (*editor).enabled = request.writable;
    (*editor).accessible_name = request.property_path;
    (*editor).accessible_description = request.descriptor.description;
    const std::shared_ptr<bool> synchronizing = std::make_shared<bool>(false);
    PropertyEditorBinding binding;
    binding.control = editor;
    binding.synchronize = FlagsEditorSynchronizer{editor, synchronizing};
    binding.connect_committed =
        FlagsEditorCommitConnector{editor, synchronizing};
    return binding;
}

std::optional<PropertyEditorBinding> create_color_editor(
    const PropertyEditorRequest& request) {
    const Color* value = std::get_if<Color>(&request.value);
    if (!value) return {};
    const std::shared_ptr<ColorValueEditor> editor =
        make_control<ColorValueEditor>(StableId(request.stable_id), *value);
    (*editor).enabled = request.writable;
    (*editor).accessible_name = request.property_path;
    (*editor).accessible_description = request.descriptor.description;
    if ((*editor).editor()) {
        (*(*editor).editor()).accessible_name = request.property_path;
        (*(*editor).editor()).accessible_description = request.descriptor.description;
    }
    const std::shared_ptr<bool> synchronizing = std::make_shared<bool>(false);
    PropertyEditorBinding binding;
    binding.control = editor;
    binding.synchronize = ColorEditorSynchronizer{editor, synchronizing};
    binding.connect_committed =
        ColorEditorCommitConnector{editor, synchronizing};
    binding.connect_failed = ColorEditorFailureConnector{editor};
    return binding;
}

} // namespace

bool PropertyEditorRegistry::register_factory(
    std::string name, PropertyEditorFactory factory) {
    const std::string canonical = property_service_name(name);
    if (!factory) {
        throw std::invalid_argument(
            "Property editor factories require a callback");
    }
    return factories_.emplace(canonical, std::move(factory)).second;
}

bool PropertyEditorRegistry::unregister_factory(std::string_view name) {
    const std::string canonical = property_service_name(name);
    const bool removed = factories_.erase(canonical) != 0U;
    if (removed) {
        KindMap::iterator item = kind_mappings_.begin();
        while (item != kind_mappings_.end()) {
            if ((*item).second == canonical) {
                item = kind_mappings_.erase(item);
            } else {
                ++item;
            }
        }
    }
    return removed;
}

void PropertyEditorRegistry::map_kind(BindingValueKind kind,
                                      std::string factory_name) {
    const std::string canonical = property_service_name(factory_name);
    if (!factories_.contains(canonical)) {
        throw std::invalid_argument(
            "Property editor kind mapping requires a registered factory");
    }
    kind_mappings_.insert_or_assign(kind, canonical);
}

void PropertyEditorRegistry::clear_kind(BindingValueKind kind) {
    kind_mappings_.erase(kind);
}

std::optional<std::string> PropertyEditorRegistry::factory_for(
    BindingValueKind kind) const {
    const KindMap::const_iterator found = kind_mappings_.find(kind);
    return found == kind_mappings_.end()
        ? std::optional<std::string>{}
        : std::optional<std::string>{(*found).second};
}

std::optional<PropertyEditorBinding> PropertyEditorRegistry::create(
    const PropertyEditorRequest& request) const {
    if (!request.writable) return {};
    const bool explicit_service = request.top_level &&
        !request.descriptor.editor_name.empty();
    std::string service = explicit_service
        ? request.descriptor.editor_name : std::string{};
    if (service.empty()) {
        const std::optional<std::string> mapped = factory_for(binding_value_kind(request.value));
        if (mapped) service = *mapped;
    }
    if (service.empty()) return {};
    const FactoryMap::const_iterator found =
        factories_.find(canonical_binding_name(service));
    if (found == factories_.end()) {
        if (explicit_service) {
            throw std::invalid_argument(
                "Declared property editor factory is not registered");
        }
        return {};
    }
    std::optional<gui_forms::PropertyEditorBinding> result = (*found).second(request);
    if (!result) return {};
    if (!(*result).control || !(*(*result).control).is_alive() ||
        (*(*result).control).parent() || (*(*result).control).attached_window() ||
        !(*result).synchronize || !(*result).connect_committed) {
        throw std::invalid_argument(
            "Property editor factory returned an invalid retained binding");
    }
    return result;
}

std::shared_ptr<PropertyEditorRegistry> PropertyEditorRegistry::create_default() {
    std::shared_ptr<gui_forms::PropertyEditorRegistry> result = std::make_shared<PropertyEditorRegistry>();
    static_cast<void>((*result).register_factory(
        "numeric-up-down", create_numeric_editor));
    static_cast<void>((*result).register_factory(
        "flags-value", create_flags_editor));
    static_cast<void>((*result).register_factory(
        "color-value", create_color_editor));
    (*result).map_kind(BindingValueKind::number, "numeric-up-down");
    (*result).map_kind(BindingValueKind::enumeration, "flags-value");
    (*result).map_kind(BindingValueKind::color, "color-value");
    return result;
}

} // namespace gui_forms
