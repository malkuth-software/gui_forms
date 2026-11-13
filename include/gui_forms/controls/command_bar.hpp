#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/window.hpp"

#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API CommandItem final {
    int id{};
    GUI_FORMS_LOCALIZABLE(text) std::string label{};
    std::string glyph{};
    GUI_FORMS_LOCALIZABLE(accessible_name) std::string accessible_name{};
    std::optional<KeyGesture> shortcut{};
    Command* command{}; // Borrowed at set_items; subsequent access is token-guarded.
};

// Retained item buttons; one group notification survives list replacement.
class GUI_FORMS_API CommandBar final : public Panel {
public:
    explicit CommandBar(StableId id);
    ~CommandBar() override;
    void set_items(std::vector<CommandItem> items);
    // Borrowed: the next set_items ends the views lent so far (the sanitizer
    // tree reports a later use).
    [[nodiscard]] BorrowedSpan<CommandItem> items() const noexcept;
    [[nodiscard]] Event<const CommandItem&>& invoked() noexcept { return invoked_; }
    // Named skin part "item". The returned borrow lasts until list replacement.
    [[nodiscard]] Button& item(int index) const;
    [[nodiscard]] Control* part(std::string_view name, int index) const;
    void arrange(Rect bounds) override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void on_key_down(KeyEventArgs& arguments) override;

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;

private:
    // The routed form of a key the raisers left unhandled.
    void handle_key(KeyEvent& event);
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
