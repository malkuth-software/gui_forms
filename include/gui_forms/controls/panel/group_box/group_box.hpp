#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <span>
#include <string>

namespace gui_forms {

class GroupBox : public Panel {
public:
    explicit GroupBox(StableId stable_id, std::string text = {});

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    [[nodiscard]] bool use_mnemonic() const noexcept { return use_mnemonic_; }
    void set_use_mnemonic(bool value);

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: a face, the frame and the caption.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry caption;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] std::string caption_text() const;
    // The background block behind the caption; its width follows the
    // estimated caption width, as paint has always used.
    [[nodiscard]] Rect caption_bounds() const;

private:
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    std::string text_;
    FontSpec font_{FontRole::control, 12.0, 600, false, 0.24};
    bool use_mnemonic_{true};
};

} // namespace gui_forms
