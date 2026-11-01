#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <string>

namespace gui_forms {

// One page of a TabControl: a panel with the text and image its tab shows.
class TabPage final : public Panel {
public:
    explicit TabPage(StableId stable_id, std::string text = {});

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);
    // The tab's image in the TabControl's image list, by key or by index
    // (Windows Forms' ImageKey and ImageIndex). Setting one clears the other,
    // as in Windows Forms; an empty key and index -1 show no image, and so
    // does a key or index the list does not have. set_image_key throws
    // std::invalid_argument for a key that is not valid UTF-8 or is longer
    // than 256 bytes; set_image_index throws std::out_of_range below -1.
    [[nodiscard]] const std::string& image_key() const noexcept { return image_key_; }
    void set_image_key(std::string image_key);
    [[nodiscard]] int image_index() const noexcept { return image_index_; }
    void set_image_index(int image_index);
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

private:
    void invalidate_tab();

    std::string text_;
    std::string image_key_{};
    int image_index_{-1};
};

} // namespace gui_forms
