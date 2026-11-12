#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/font_command/font_command.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/ribbon_font_control_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// The ribbon's font control (stage RB, phase 6; RIBBON_STUDY.md §1.1): a
// font family list and a size list side by side, two real ComboBox
// children, showing and choosing one FontChoice. With a FontCommand the
// control shows the command's value and a choice commits to it (the
// command's value_changed, then its Command runs); without one it keeps its
// own value. A field shows empty while its half of the value is unknown or
// not in its list. Hosted in a Ribbon as RibbonCommandKind::font_control.
//
// Not yet: typing a family or size that is not listed, and drawing each
// family in its own face in the list.
class GUI_FORMS_API RibbonFontControl final : public Control, public RibbonFontControlCapabilities<RibbonFontControl> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_families = 1024U;
    static constexpr std::size_t maximum_sizes = 64U;

    explicit RibbonFontControl(StableId stable_id);
    void initialize_control_tree();

    // The families offered, in order. Throws std::invalid_argument for an
    // empty, repeated or invalid name, or more than maximum_families.
    [[nodiscard]] std::span<const std::string> families() const& noexcept {
        return std::span<const std::string>(families_.data(), families_.size());
    }
    std::span<const std::string> families() const&& = delete;
    void set_families(std::vector<std::string> families);
    // The sizes offered, in points, ascending; 8 to 72 by default. Throws
    // std::invalid_argument for sizes that are not ascending or outside
    // FontCommand's range, or more than maximum_sizes.
    [[nodiscard]] std::span<const double> sizes() const& noexcept {
        return std::span<const double>(sizes_.data(), sizes_.size());
    }
    std::span<const double> sizes() const&& = delete;
    void set_sizes(std::vector<double> sizes);
    // The two fields' widths in logical units at text scale 1 (defaults 120
    // and 44). Throws std::invalid_argument outside 16 to 512.
    [[nodiscard]] double family_width() const noexcept { return family_width_; }
    [[nodiscard]] double size_width() const noexcept { return size_width_; }
    void set_field_widths(double family, double size);
    // The size the control measures, in logical units at the current text
    // scale.
    [[nodiscard]] Size natural_size() const noexcept;

    [[nodiscard]] std::shared_ptr<ComboBox> family_box() const noexcept { return family_; }
    [[nodiscard]] std::shared_ptr<ComboBox> size_box() const noexcept { return size_; }

    // Raised after the value shown changes, by the user or the program.
    [[nodiscard]] Event<const FontChoice&>& value_changed() noexcept { return value_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry family_field;
        PartGeometry size_field;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The control's two fields.
    enum class Field : std::uint8_t { family, size };
    // Named callbacks: a field's selection changed; the command's value or
    // state changed. Each observes the control weakly.
    struct FieldChanged final {
        std::weak_ptr<RibbonFontControl> target;
        Field field{Field::family};
        void operator()(std::optional<std::size_t> index) const;
    };
    struct CommandValueChanged final {
        std::weak_ptr<RibbonFontControl> target;
        void operator()(const FontChoice& value) const;
    };
    struct CommandStateChanged final {
        std::weak_ptr<RibbonFontControl> target;
        void operator()(const CommandState& state) const;
    };
    struct Fields final {
        Rect family{};
        Rect size{};
    };

    [[nodiscard]] Fields fields(Rect client) const;
    void field_changed(Field field, std::optional<std::size_t> index);
    void show(const FontChoice& value);
    void sync_enabled();

    std::shared_ptr<ComboBox> family_;
    std::shared_ptr<ComboBox> size_;
    SubscriptionToken family_changed_{};
    SubscriptionToken size_changed_{};
    SubscriptionToken command_value_{};
    SubscriptionToken command_state_{};
    std::vector<std::string> families_{};
    std::vector<double> sizes_{};
    FontChoice own_value_{};
    double family_width_{120.0};
    double size_width_{44.0};
    // True while the control writes its fields, so their selection events
    // are not taken for the user's choice.
    bool synchronizing_{};
    Event<const FontChoice&> value_changed_{};
    // property_codemod begin RibbonFontControl (tools/property_families/ribbon_font_control.json; generated, do not edit)
private:
    friend struct RibbonFontControlCommandCapability<RibbonFontControl>;
    friend struct RibbonFontControlValueCapability<RibbonFontControl>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(command)& cell, std::shared_ptr<FontCommand>& incoming);
    void written(const decltype(command)& cell);
    void write_view(const decltype(value)& cell, FontChoice incoming);
    [[nodiscard]] FontChoice read_view(const decltype(value)& cell) const;
    // property_codemod end RibbonFontControl
};

} // namespace gui_forms
