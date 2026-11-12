#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component/component/component.hpp"

#include <span>
#include <vector>

namespace gui_forms {

class GUI_FORMS_API ComponentContainer final {
public:
    ComponentContainer() = default;
    ~ComponentContainer();
    ComponentContainer(const ComponentContainer&) = delete;
    ComponentContainer& operator=(const ComponentContainer&) = delete;

    void add(Component::Ptr component);
    [[nodiscard]] Component::Ptr remove(const Component& component);
    [[nodiscard]] std::span<const Component::Ptr> components() const& noexcept {
        return components_;
    }
    std::span<const Component::Ptr> components() const&& = delete;
    [[nodiscard]] bool contains(const Component& component) const noexcept;
    void dispose();
    [[nodiscard]] bool is_disposed() const noexcept { return disposed_; }

private:
    using ComponentList = std::vector<Component::Ptr>;
    ComponentList components_;
    bool disposed_{};
};

} // namespace gui_forms
