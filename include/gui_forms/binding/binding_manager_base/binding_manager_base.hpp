#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/types/binding_contract_types.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <string>

namespace gui_forms {

// Renderer-neutral currency contract shared by list and property surfaces.
class GUI_FORMS_API BindingManagerBase {
public:
    virtual ~BindingManagerBase() = default;
    [[nodiscard]] virtual std::size_t count() const noexcept = 0;
    // The current row's stable ID, or empty with no current row.
    [[nodiscard]] virtual std::string current_stable_id() const = 0;
    [[nodiscard]] virtual std::ptrdiff_t position() const noexcept = 0;
    [[nodiscard]] virtual bool binding_suspended() const noexcept = 0;
    virtual bool set_position(std::ptrdiff_t position) = 0;
    virtual void cancel_current_edit() = 0;
    virtual void end_current_edit() = 0;
    virtual bool remove_at(std::size_t index) = 0;
    virtual void suspend_binding() = 0;
    virtual void resume_binding() = 0;
    virtual bool pull_data() = 0;
    virtual bool push_data() = 0;
    [[nodiscard]] virtual Event<BindingCompleteEvent&>& binding_complete()
        noexcept = 0;
    [[nodiscard]] virtual Event<>& current_changed() noexcept = 0;
    [[nodiscard]] virtual Event<>& current_item_changed() noexcept = 0;
    [[nodiscard]] virtual Event<std::ptrdiff_t>& position_changed() noexcept = 0;
    [[nodiscard]] virtual Event<const std::string&>& data_error() noexcept = 0;
};

} // namespace gui_forms
