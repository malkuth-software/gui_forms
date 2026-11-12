#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/inspection/inspection_types.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

struct GUI_FORMS_API PropertyEditorBinding final {
    Control::Ptr control;
    std::function<void(const BindingValue&)> synchronize;
    std::function<SubscriptionToken(
        Component&, std::function<void(BindingValue)>)> connect_committed;
    std::function<SubscriptionToken(
        Component&, std::function<void(const PropertyEditorInputError&)>)>
        connect_failed;
};

using PropertyEditorFactory = std::function<std::optional<PropertyEditorBinding>(
    const PropertyEditorRequest&)>;

class GUI_FORMS_API PropertyEditorRegistry final {
public:
    bool register_factory(std::string name, PropertyEditorFactory factory);
    bool unregister_factory(std::string_view name);
    void map_kind(BindingValueKind kind, std::string factory_name);
    void clear_kind(BindingValueKind kind);
    [[nodiscard]] std::optional<std::string> factory_for(
        BindingValueKind kind) const;
    [[nodiscard]] std::optional<PropertyEditorBinding> create(
        const PropertyEditorRequest& request) const;
    [[nodiscard]] static std::shared_ptr<PropertyEditorRegistry> create_default();

private:
    using FactoryMap = std::map<std::string, PropertyEditorFactory>;
    using KindMap = std::map<BindingValueKind, std::string>;
    FactoryMap factories_;
    KindMap kind_mappings_;
};

} // namespace gui_forms
