#pragma once

#include "gui_forms/controls/info_banner/notice_values/notice_values.hpp"
#include "gui_forms/input_controls.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace gui_forms::detail {

// Source-private retained module shell. InstrumentRack owns model authority;
// this type owns one module's responsive child layout and reorder semantics.
class RackModulePanel final : public Panel {
public:
    explicit RackModulePanel(StableId stable_id);

    std::function<void(bool)> move_request;
    std::shared_ptr<CheckBox> enable;
    std::vector<Control::Ptr> fields;
    std::vector<double> field_weights;
    std::shared_ptr<Label> status;
    std::shared_ptr<Button> remove;

    void set_compact(bool compact);
    [[nodiscard]] bool compact() const noexcept { return compact_; }
    // The module's severity, its custom state of the same name
    // (`:state(warning)`, as a notice's), drawn as a keyline along the
    // module's leading edge in notice_severity_colour; none, no keyline.
    void set_severity(std::optional<NoticeSeverity> severity);
    [[nodiscard]] std::optional<NoticeSeverity> severity() const noexcept { return severity_; }

    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    void on_paint(Painter& painter, Rect local_damage) override;

    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;

private:
    bool compact_{};
    std::optional<NoticeSeverity> severity_{};
};

} // namespace gui_forms::detail
