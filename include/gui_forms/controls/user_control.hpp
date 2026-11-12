#pragma once

#include "gui_forms/controls/container_control.hpp"

#include <cstdint>

namespace gui_forms {

class UserControl : public ContainerControl {
public:
    explicit UserControl(StableId stable_id);

    [[nodiscard]] Event<>& loaded() noexcept { return loaded_event_; }
    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] bool is_attached() const noexcept { return attached_; }
    [[nodiscard]] std::uint64_t attachment_count() const noexcept {
        return attachment_count_;
    }
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void on_attachment_committed() noexcept override;
    void on_detached_from_window() noexcept override;

private:
    Event<> loaded_event_;
    std::uint64_t attachment_count_{};
    bool loaded_{};
    bool attached_{};
};

} // namespace gui_forms
