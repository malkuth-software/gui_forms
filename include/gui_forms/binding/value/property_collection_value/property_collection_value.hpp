#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/value/binding_value_kind/binding_value_kind.hpp"

#include <memory>
#include <string_view>
#include <utility>

namespace gui_forms {

struct PropertyCollectionData;
struct PropertyValueFactoryAccess;

// Cheap immutable handle over a validated bounded homogeneous sequence.
class GUI_FORMS_API PropertyCollectionValue final {
public:
    PropertyCollectionValue() = default;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::string_view item_type_name() const noexcept;
    [[nodiscard]] BindingValueKind item_kind() const noexcept;
    [[nodiscard]] const PropertyCollectionData* data() const noexcept {
        return data_.get();
    }
    friend GUI_FORMS_API bool operator==(const PropertyCollectionValue& left,
                           const PropertyCollectionValue& right) noexcept;

private:
    explicit PropertyCollectionValue(
        std::shared_ptr<const PropertyCollectionData> authored_data)
        : data_(std::move(authored_data)) {}
    std::shared_ptr<const PropertyCollectionData> data_;
    friend struct PropertyValueFactoryAccess;
};

} // namespace gui_forms
