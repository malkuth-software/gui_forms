#pragma once

#include "gui_forms/export/application_export.hpp"
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace gui_forms {

struct PropertyObjectData;
struct PropertyObjectMember;
struct PropertyValueFactoryAccess;

// Cheap immutable handle over a validated bounded recursive property object.
class GUI_FORMS_API PropertyObjectValue final {
public:
    PropertyObjectValue() = default;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::string_view type_name() const noexcept;
    [[nodiscard]] std::span<const PropertyObjectMember> members() const noexcept;
    [[nodiscard]] const PropertyObjectData* data() const noexcept {
        return data_.get();
    }
    friend GUI_FORMS_API bool operator==(const PropertyObjectValue& left,
                           const PropertyObjectValue& right) noexcept;

private:
    explicit PropertyObjectValue(
        std::shared_ptr<const PropertyObjectData> authored_data)
        : data_(std::move(authored_data)) {}
    std::shared_ptr<const PropertyObjectData> data_;
    friend struct PropertyValueFactoryAccess;
};

} // namespace gui_forms
