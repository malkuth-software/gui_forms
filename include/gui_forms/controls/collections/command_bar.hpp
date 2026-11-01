#pragma once

#include "gui_forms/commands.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/window.hpp"

#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

struct CommandItem final {
    int id{};
    GUI_FORMS_LOCALIZABLE(text) std::string label{};
    std::string glyph{};
    GUI_FORMS_LOCALIZABLE(accessible_name) std::string accessible_name{};
    std::optional<KeyGesture> shortcut{};
    Command* command{}; // Borrowed at set_items; subsequent access is token-guarded.
};

// Retained item buttons; one group notification survives list replacement.
class CommandBar final : public Panel {
public:
    explicit CommandBar(StableId id);
    ~CommandBar() override;
    void set_items(std::vector<CommandItem> items);
    [[nodiscard]] std::span<const CommandItem> items() const noexcept;
    [[nodiscard]] Event<const CommandItem&>& invoked() noexcept { return invoked_; }
    // Named skin part "item". The returned borrow lasts until list replacement.
    [[nodiscard]] Button& item(int index) const;
    [[nodiscard]] Control* part(std::string_view name, int index) const;
    void arrange(Rect bounds) override;
    void on_key_bubble(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void on_detaching_from_window(Window& former_window) noexcept override;

private:
    struct Entry;
    struct Revision;
    struct ItemClick;
    struct ItemAvailability;
    void refresh_tab_stop();
    void invoke_item(int id);
    void install_shortcuts();
    void clear_shortcuts() noexcept;
    std::shared_ptr<Revision> revision_{};
    Event<const CommandItem&> invoked_{};
    bool replacing_{};
};

} // namespace gui_forms
