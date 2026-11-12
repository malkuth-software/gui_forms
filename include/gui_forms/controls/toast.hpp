#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/notice_values.hpp"
#include "gui_forms/window/toasts/toasts.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class ButtonBase;

// What a Toast says, fixed when it is made: its message, the text of its
// action button (empty for none), its severity and whether it shows a
// close mark.
struct ToastContent final {
    std::string message{};
    std::string action_text{};
    NoticeSeverity severity{NoticeSeverity::information};
    bool closable{true};
};

// A brief notice shown for a while above the bottom of a window (W-4):
// the stock content of Window::show_toast, which places it in the window's
// toast overlay and closes it when its duration runs out. It draws the
// `notice` face with the overlay shadow, the severity mark, the message on
// one line, the action button and a close mark. Its severity is its custom
// state, exactly one of information, warning, success and error.
//
// Pressing the action button raises `action_invoked`, then asks its window
// to dismiss the toast (ToastDismissReason::action); the close mark and the
// close semantic action ask with ToastDismissReason::closed. A toast that
// is not its window's showing toast does nothing more. The content is
// fixed: show another Toast to say something else.
//
// To assistive technology a toast is a live region named by its message,
// with the close action while closable; the action button is reachable
// inside it.
class Toast final : public Control {
public:
    // The action button is made after construction (make_control calls
    // initialize_control_tree), since add_child needs a shared owner.
    static constexpr bool initialize_tree_after_construction = true;
    // std::invalid_argument for an empty message, or a message or action
    // text that is not valid UTF-8.
    Toast(StableId stable_id, ToastContent content);
    // Makes the action button when there is action text; make_control
    // calls it once.
    void initialize_control_tree();

    [[nodiscard]] const ToastContent& content() const noexcept { return content_; }
    // The action button, or null without action text.
    [[nodiscard]] Control::Ptr action() const noexcept { return action_; }
    // Raised when the action button is pressed, before the toast is
    // dismissed.
    [[nodiscard]] Event<>& action_invoked() noexcept { return action_invoked_; }
    // Asks the window to dismiss this toast for `closed`; false when it is
    // not the window's showing toast.
    bool close();

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    void on_pointer(PointerEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `face` is the
    // notice surface, `icon` the severity mark, `label` the message,
    // `actuator` the action button's slot (absent without one) and `close`
    // the close mark (absent unless closable; hot and pressed as the
    // pointer is). The icon and the close mark take the glyph role.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry actuator;
        PartGeometry close;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    struct Placement final {
        Rect face{};
        Rect icon{};
        Rect label{};
        Rect actuator{};
        Rect close{};
        Size natural{};
    };
    // Where each part lies in a toast of `size`, and the size the toast
    // would choose. Reads the last measured action size.
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] FontSpec message_font() const noexcept;
    [[nodiscard]] bool dismiss(ToastDismissReason reason);
    void on_action_clicked(ButtonBase& button);

    ToastContent content_;
    Control::Ptr action_;
    // The action button's desired size from the last measure or arrange.
    Size action_size_{};
    Event<> action_invoked_{};
    bool hot_close_{};
    bool pressed_close_{};
};

} // namespace gui_forms
