#pragma once

#include "gui_forms/binding/binding_manager_base/binding_manager_base.hpp"


namespace gui_forms {

class BindingSource;

class CurrencyManager final : public BindingManagerBase {
public:
    [[nodiscard]] std::size_t count() const noexcept override;
    [[nodiscard]] std::string current_stable_id() const override;
    [[nodiscard]] std::ptrdiff_t position() const noexcept override;
    [[nodiscard]] bool binding_suspended() const noexcept override;
    bool set_position(std::ptrdiff_t position) override;
    void cancel_current_edit() override;
    void end_current_edit() override;
    bool remove_at(std::size_t index) override;
    void suspend_binding() override;
    void resume_binding() override;
    bool pull_data() override;
    bool push_data() override;
    [[nodiscard]] Event<BindingCompleteEvent&>& binding_complete()
        noexcept override;
    [[nodiscard]] Event<>& current_changed() noexcept override;
    [[nodiscard]] Event<>& current_item_changed() noexcept override;
    [[nodiscard]] Event<std::ptrdiff_t>& position_changed() noexcept override;
    [[nodiscard]] Event<const std::string&>& data_error() noexcept override;
    [[nodiscard]] Event<const BindingListChange&>& list_changed() noexcept;
    void refresh();
    [[nodiscard]] BindingSource& source() const noexcept;

private:
    friend class BindingSource;
    explicit CurrencyManager(BindingSource& source) : source_(&source) {}
    BindingSource* source_{};
};

} // namespace gui_forms
