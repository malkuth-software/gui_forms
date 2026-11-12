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

struct GUI_FORMS_API PropertyConversionContext final {
    std::string culture_name{"invariant"};
    std::string decimal_separator{"."};
    std::string group_separator{","};
    bool use_grouping{};
    friend bool operator==(const PropertyConversionContext& left,
                           const PropertyConversionContext& right) noexcept(
        noexcept(left.culture_name == right.culture_name &&
                 left.decimal_separator == right.decimal_separator &&
                 left.group_separator == right.group_separator &&
                 left.use_grouping == right.use_grouping)) {
        return left.culture_name == right.culture_name &&
               left.decimal_separator == right.decimal_separator &&
               left.group_separator == right.group_separator &&
               left.use_grouping == right.use_grouping;
    }
};

struct GUI_FORMS_API PropertyValueConverter final {
    using Formatter = std::function<std::string(
        const BindingValue&, const PropertyDescriptor&)>;
    using Parser = std::function<std::optional<BindingValue>(
        std::string_view, const BindingValue&, const PropertyDescriptor&)>;
    using ContextFormatter = std::function<std::string(
        const BindingValue&, const PropertyDescriptor&,
        const PropertyConversionContext&)>;
    using ContextParser = std::function<std::optional<BindingValue>(
        std::string_view, const BindingValue&, const PropertyDescriptor&,
        const PropertyConversionContext&)>;

    Formatter format;
    Parser parse;
    ContextFormatter format_with_context;
    ContextParser parse_with_context;
};

class GUI_FORMS_API PropertyValueConverterRegistry final {
public:
    bool register_converter(std::string name, PropertyValueConverter converter);
    bool unregister_converter(std::string_view name);
    void map_kind(BindingValueKind kind, std::string converter_name);
    void clear_kind(BindingValueKind kind);
    [[nodiscard]] std::optional<std::string> converter_for(
        BindingValueKind kind) const;
    [[nodiscard]] const PropertyValueConverter* find(
        std::string_view name) const noexcept;
    [[nodiscard]] std::string format(const BindingValue& value,
                                     const PropertyDescriptor& descriptor) const;
    [[nodiscard]] std::optional<BindingValue> parse(
        std::string_view text, const BindingValue& current,
        const PropertyDescriptor& descriptor) const;
    [[nodiscard]] const PropertyConversionContext& context() const noexcept {
        return context_;
    }
    void set_context(PropertyConversionContext context);
    [[nodiscard]] static std::shared_ptr<PropertyValueConverterRegistry>
    create_default();

private:
    using ConverterMap = std::map<std::string, PropertyValueConverter>;
    using KindMap = std::map<BindingValueKind, std::string>;
    ConverterMap converters_;
    KindMap kind_mappings_;
    PropertyConversionContext context_;
};

} // namespace gui_forms
