// PopupLift: a drop-down's list or a picker's panel in the owner's warm
// picker Popup window (owner, 2026-11-08: Part S scope for 0.5).

#include "gui_forms/window/native_popup/popup_lift.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace gui_forms {

namespace detail {

// The Popup's whole client: the lifted content at its face, inset by the
// overlay shadow's outsets that the frame paints around it when the Popup
// is translucent (an opaque Popup holds the face alone). Not exposed to
// accessibility itself; the content publishes as it would in the layer.
class PopupLiftFrame final : public Control {
public:
    explicit PopupLiftFrame(StableId id) : Control(std::move(id)) {
        set_paint_plane(PaintPlane::overlay);
    }

    // Makes `content` the frame's one child; `shadow` says whether the frame
    // casts the overlay shadow. Returns the frame's insets around the face.
    Insets hold(const Control::Ptr& content, const bool shadow) {
        release();
        content_ = content;
        shadow_ = shadow;
        add_child(content);
        const Insets outsets = shadow_ ? overlay_shadow_outsets(*this) : Insets{};
        outsets_ = outsets;
        return outsets;
    }

    // Places the content at `face_size` inside the outsets.
    void place(const Size face_size) {
        if (!content_) return;
        (*content_).set_requested_bounds({outsets_.left, outsets_.top, face_size.width, face_size.height});
        invalidate(Dirty::paint);
    }

    // Takes the content out, detached again.
    void release() noexcept {
        const Control::Ptr content = std::move(content_);
        content_.reset();
        if (content && (*content).parent().get() == this) {
            try {
                static_cast<void>(remove_child((*content).runtime_id()));
            } catch (...) {
            }
        }
    }

    [[nodiscard]] Insets insets() const noexcept { return outsets_; }

    void on_paint(Painter& painter, Rect) override {
        if (!shadow_ || !content_) return;
        PartPaint shadow;
        shadow.role = PartRole::shadow;
        shadow.family = ControlVisualRole::panel;
        shadow.bounds = (*content_).requested_bounds();
        shadow.corner_radius = 0.0;
        paint_part(painter, *this, shadow);
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.exposed = false;
        descriptor.include_descendants = true;
        return descriptor;
    }

private:
    Control::Ptr content_{};
    Insets outsets_{};
    bool shadow_{};
};

} // namespace detail

PopupLift::PopupLift() noexcept {}

PopupLift::~PopupLift() {
    release();
}

bool PopupLift::lift(Window& window, const Control::Ptr& content, const Rect face, const PopupLiftInput input) {
    release();
    if (!content || !window.popup_windows_available()) return false;
    if (!frame_) frame_ = make_control<detail::PopupLiftFrame>(StableId("popup.lift.frame"));
    const Insets insets = (*frame_).hold(content, window.popup_windows_translucent());
    (*frame_).place(Size{face.width, face.height});
    const Rect bounds{face.x - insets.left, face.y - insets.top, face.width + insets.left + insets.right,
                      face.height + insets.top + insets.bottom};
    const std::size_t slot = Window::picker_popup_window_slot;
    if (!window.show_popup_window(slot, frame_, bounds)) {
        (*frame_).release();
        return false;
    }
    window_ = &window;
    if (input == PopupLiftInput::kept_by_owner) return true;
    window.forward_input_to_popup_window(slot);
    if (Window* const model = window.popup_window_model(slot)) {
        if ((*content).focusable()) static_cast<void>((*model).request_focus(content));
    }
    return true;
}

bool PopupLift::move(const Rect face) {
    if (window_ == nullptr || !frame_) return false;
    const Insets insets = (*frame_).insets();
    (*frame_).place(Size{face.width, face.height});
    const Rect bounds{face.x - insets.left, face.y - insets.top, face.width + insets.left + insets.right,
                      face.height + insets.top + insets.bottom};
    const bool moved = (*window_).move_popup_window(Window::picker_popup_window_slot, bounds);
    return moved;
}

void PopupLift::release() noexcept {
    Window* const window = window_;
    window_ = nullptr;
    if (window != nullptr) {
        try {
            (*window).forward_input_to_popup_window(std::nullopt);
        } catch (...) {
        }
        (*window).hide_popup_window(Window::picker_popup_window_slot);
    }
    if (frame_) (*frame_).release();
}

Window* PopupLift::host() const noexcept {
    if (window_ == nullptr) return nullptr;
    Window* const model = (*window_).popup_window_model(Window::picker_popup_window_slot);
    return model;
}

std::optional<Rect> drop_down_popup_face(Window& window, const Rect anchor, const Size size, const double preferred_x) {
    if (!window.popup_windows_available()) return std::nullopt;
    const double below = anchor.y + anchor.height;
    const std::optional<Rect> area = window.popup_work_area(Point{anchor.x, below});
    if (!area.has_value()) return std::nullopt;
    const Rect bounds = *area;
    const double face_width = std::min(size.width, bounds.width);
    const double face_height = std::min(size.height, bounds.height);
    const double right_limit = std::max(bounds.x, bounds.x + bounds.width - face_width);
    const double face_x = std::clamp(preferred_x, bounds.x, right_limit);
    const bool fits_below = below + face_height <= bounds.y + bounds.height;
    const double face_y = fits_below ? below : std::max(bounds.y, anchor.y - face_height);
    const Rect face{face_x, face_y, face_width, face_height};
    return face;
}

} // namespace gui_forms
