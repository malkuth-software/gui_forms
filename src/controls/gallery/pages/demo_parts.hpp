#pragma once

#include "pages.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/controls/anchored_overlay_placement.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"

#include <memory>
#include <string>
#include <string_view>

// Parts shared by the Gallery's composition pages (the DevExpress
// "demo only" takes, notes:planning/audits/DEVEXPRESS_OVERKILL_SURVEY_2026-11-22.md):
// lookups-and-pickers, media-and-motion, navigation-patterns,
// dashboard-pieces, workspaces-and-themes and ui-templates. Each of those
// pages is composed from public GUI.Forms parts only; what these helpers
// add is page code, not library API.
namespace gui_forms::gallery::pages {

// The heading and lead line every page body starts with, as the form pages
// write them (demo/pages/*.form): the heading at 24,18 and the lead under it.
inline void add_page_heading(Surface& body, std::string_view stem, std::string title, std::string lead) {
    const std::string heading_id = std::string("gallery.") + std::string(stem) + ".heading";
    const std::shared_ptr<Label> heading = label(heading_id, std::move(title), 18.0, 700, "heading");
    static_cast<void>((*heading).add_class("page-title"));
    body.add_at(heading, {24.0, 18.0, 620.0, 34.0});
    const std::string lead_id = std::string("gallery.") + std::string(stem) + ".lead";
    const std::shared_ptr<Label> line = label(lead_id, std::move(lead), 12.0, 400, "muted");
    static_cast<void>((*line).add_class("body"));
    body.add_at(line, {26.0, 52.0, 940.0, 24.0});
}

// A drop-down that holds any panel, composed (DevExpress's
// PopupContainerEdit, a library take of the survey; here it is page code):
// a Button is the anchor, and `content` opens under it in an
// AnchoredOverlayLayer with a focus scope whose first stop is `focus`.
// A click on the anchor opens or closes it; a click away or Escape asks the
// layer to dismiss, and the window may revoke the overlay itself (its
// token's closed event). The popup owns the layer and the content; the page
// owns the popup (keep it in the page's components) and the anchor.
class PopupPanel final : public Component {
public:
    PopupPanel(std::string stable_id, std::shared_ptr<Button> anchor, Control::Ptr content, Control::Ptr focus,
               Size size)
        : anchor_(std::move(anchor)), content_(std::move(content)), focus_(std::move(focus)), size_(size) {
        layer_ = make_control<AnchoredOverlayLayer>(StableId(std::move(stable_id)), anchor_, placement());
        (*layer_).set_content(content_);
        (*anchor_).expanded_state = stxlib::Optional<bool>::of(false);
    }

    // Subscribes the anchor and the layer; call once, after construction,
    // while the popup is held by a shared pointer (Component lifetime).
    void connect() {
        on((*anchor_).clicked(), *this, &PopupPanel::on_anchor_clicked);
        on((*layer_).dismiss_requested(), *this, &PopupPanel::on_dismiss_requested);
    }

    // Opens the popup under its anchor. False, changing nothing, while the
    // anchor is in no window.
    bool open() {
        Window* const window = (*anchor_).attached_window();
        if (window == nullptr) return false;
        close();
        const Size client = (*window).client_size();
        (*layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
        (*layer_).placement = placement();
        token_ = (*window).open_overlay(anchor_, layer_);
        scope_ = (*window).begin_focus_scope(layer_, focus_);
        (*anchor_).expanded_state = stxlib::Optional<bool>::of(true);
        if (Event<>* closed = token_.closed_event()) {
            on(*closed, *this, &PopupPanel::on_overlay_closed);
        }
        return true;
    }

    // Closes the popup and gives the focus back; nothing when it is closed.
    void close() {
        if (!token_.connected()) return;
        Window* const window = (*layer_).attached_window();
        if (window != nullptr && scope_) static_cast<void>((*window).end_focus_scope(scope_));
        scope_ = {};
        token_.disconnect();
        (*anchor_).expanded_state = stxlib::Optional<bool>::of(false);
    }

    [[nodiscard]] bool is_open() const noexcept { return token_.connected(); }
    [[nodiscard]] const std::shared_ptr<Button>& anchor() const& noexcept { return anchor_; }
    const std::shared_ptr<Button>& anchor() const&& = delete;

private:
    [[nodiscard]] AnchoredOverlayPlacement placement() const {
        AnchoredOverlayPlacement result;
        result.preferred_size = size_;
        result.gap = 2.0;
        result.allow_vertical_flip = true;
        return result;
    }

    void on_anchor_clicked() {
        if (is_open()) {
            close();
            return;
        }
        static_cast<void>(open());
    }

    void on_dismiss_requested(const OverlayDismissReason reason) {
        static_cast<void>(reason);
        close();
    }

    // The window revoked the overlay (its owner hid, for one): the focus
    // scope went with it.
    void on_overlay_closed() {
        if (token_.connected()) return;
        scope_ = {};
        (*anchor_).expanded_state = stxlib::Optional<bool>::of(false);
    }

    std::shared_ptr<Button> anchor_;
    Control::Ptr content_;
    Control::Ptr focus_;
    Size size_{};
    std::shared_ptr<AnchoredOverlayLayer> layer_;
    OverlayToken token_;
    FocusScopeId scope_;
};

// A page label whose text wraps at word boundaries, for the notes under
// the pages' controls.
[[nodiscard]] inline std::shared_ptr<Label> wrapped_label(std::string id, std::string text, double size,
                                                          int weight, std::string_view role_class) {
    std::shared_ptr<Label> control = label(std::move(id), std::move(text), size, weight, role_class);
    (*control).text_wrapping = TextWrapping::word;
    return control;
}

// Whether the window holding `control` asks for reduced motion; a control
// in no window moves as usual.
[[nodiscard]] inline bool reduced_motion_for(const Control& control) noexcept {
    const Window* const window = control.attached_window();
    const bool reduced = window != nullptr && (*window).presentation_settings().reduced_motion;
    return reduced;
}

// Lower-case ASCII copy, for case-insensitive matching of the pages'
// ASCII fixtures (non-ASCII bytes are kept as they are).
[[nodiscard]] inline std::string ascii_lower(std::string_view text) {
    std::string lowered(text);
    for (char& letter : lowered) {
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
    }
    return lowered;
}

} // namespace gui_forms::gallery::pages
