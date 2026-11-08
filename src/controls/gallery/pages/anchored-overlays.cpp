#include "pages.hpp"

#include "anchored-overlays_form.hpp"

namespace gui_forms::gallery::pages {
namespace {

constexpr Size overlay_size{280.0, 200.0};

// The page demo/pages/anchored-overlays.form describes: one
// AnchoredOverlayLayer, reopened from three anchors. The page owns the
// placement policy as Values bound to a ChoiceGroup and CheckBoxes; a change
// re-resolves an open overlay in place. Overlay lifetime (OverlayToken) and focus
// containment (FocusScopeId) stay with the Window, as the layer's contract
// requires: the layer only reports click-away and Escape.
class AnchoredOverlaysPage final : public Component {
public:
    AnchoredOverlaysPage(const std::shared_ptr<Surface>& body,
                       const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        build_options();
        build_stage();
        build_overlay();
        on(alignment_.changed(), *this, &AnchoredOverlaysPage::on_placement_changed);
        on(above_.changed(), *this, &AnchoredOverlaysPage::on_placement_changed);
        on(flip_.changed(), *this, &AnchoredOverlaysPage::on_placement_changed);
        on(click_away_.changed(), *this, &AnchoredOverlaysPage::on_click_away_changed);
        on(escape_.changed(), *this, &AnchoredOverlaysPage::on_escape_changed);
    }

private:
    // The overlay's content is the document's last child: it leaves the
    // root before the rest move into the page body, and the layer takes it.
    void mount_document(const std::shared_ptr<Surface>& body) {
        Control& root = *document_.gallery_overlays_document;
        static_cast<void>(root.remove_child((*document_.gallery_overlays_content).runtime_id()));
        mount_form_children(root, *body);
        (*document_.gallery_overlays_options).set_style(page_style());
        (*document_.gallery_overlays_stage).set_style(page_style());
    }

    void build_options() {
        ChoiceGroup& alignment = *document_.gallery_overlays_alignment;
        alignment.set_style(page_style());
        alignment.set_items({{.id = 0, .label = "Near"}, {.id = 1, .label = "Center"},
                             {.id = 2, .label = "Far"}});
        alignment.bind(alignment_);
        (*document_.gallery_overlays_above).bind(above_);
        (*document_.gallery_overlays_flip).bind(flip_);
        (*document_.gallery_overlays_click_away).bind(click_away_);
        (*document_.gallery_overlays_escape).bind(escape_);
    }

    void build_stage() {
        const std::array<std::shared_ptr<Button>, 3> anchors{document_.gallery_overlays_anchor_0,
                                                             document_.gallery_overlays_anchor_1,
                                                             document_.gallery_overlays_anchor_2};
        for (std::size_t index = 0; index < anchors.size(); ++index) {
            const std::shared_ptr<Button>& anchor = anchors[index];
            (*anchor).set_style(page_style());
            (*anchor).expanded_state = false;
            on((*anchor).clicked(), *this, &AnchoredOverlaysPage::on_anchor_clicked,
               static_cast<int>(index));
            anchors_[index] = anchor;
        }
    }

    void build_overlay() {
        content_ = document_.gallery_overlays_content;
        (*content_).set_background(paper);
        (*content_).set_style(page_style());
        (*document_.gallery_overlays_close).set_style(page_style());
        field_ = document_.gallery_overlays_field;
        on((*document_.gallery_overlays_close).clicked(), *this, &AnchoredOverlaysPage::on_close_clicked);

        layer_ = make_control<AnchoredOverlayLayer>(StableId("gallery.overlays.layer"), anchors_[0],
                                                  placement());
        (*layer_).set_content(content_);
        (*layer_).accessible_name = "Anchored overlay layer";
        on((*layer_).dismiss_requested(), *this, &AnchoredOverlaysPage::on_dismiss_requested);
    }

    [[nodiscard]] AnchoredOverlayPlacement placement() const {
        AnchoredOverlayPlacement result;
        result.preferred_size = overlay_size;
        result.horizontal_alignment = static_cast<OverlayHorizontalAlignment>(alignment_.get());
        result.vertical_preference = above_.get() ? OverlayVerticalPreference::above
                                                  : OverlayVerticalPreference::below;
        result.gap = 4.0;
        result.allow_vertical_flip = flip_.get();
        return result;
    }

    [[nodiscard]] bool open() const noexcept { return token_.connected(); }

    void on_anchor_clicked(const int index, ButtonBase& anchor) {
        Window* window = anchor.attached_window();
        if (window == nullptr) return;
        close_overlay();
        open_anchor_ = index;
        const Size client = (*window).client_size();
        (*layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
        (*layer_).set_anchor(anchor.shared_from_this());
        (*layer_).placement = placement();
        (*layer_).dismiss_on_click_away = click_away_.get();
        (*layer_).dismiss_on_escape = escape_.get();
        token_ = (*window).open_overlay(anchor.shared_from_this(), layer_);
        scope_ = (*window).begin_focus_scope(layer_, field_);
        anchor.expanded_state = true;
        if (Event<>* closed = token_.closed_event()) {
            on(*closed, *this, &AnchoredOverlaysPage::on_overlay_closed);
        }
        report("Overlay open from " + std::string(anchor.stable_id().value()));
    }

    void on_dismiss_requested(const OverlayDismissReason reason) {
        close_overlay();
        (*document_.gallery_overlays_status).text = reason == OverlayDismissReason::click_away
                                ? "Dismissed by click-away · the press did not fall through to the page"
                                : "Dismissed by Escape · intercepted before the focused field";
    }

    void on_close_clicked() {
        close_overlay();
        (*document_.gallery_overlays_status).text = "Closed by its button · focus restored to the anchor";
    }

    // The Window may revoke the overlay itself (for example when its owner is
    // hidden); the token's closed event keeps the page's state truthful.
    void on_overlay_closed() {
        if (open()) return;
        scope_ = {}; // Detaching the overlay already closed its focus scope.
        release_anchor();
    }

    void close_overlay() {
        if (!open()) return;
        Window* window = (*layer_).attached_window();
        if (window != nullptr && scope_) static_cast<void>((*window).end_focus_scope(scope_));
        scope_ = {};
        token_.disconnect();
        release_anchor();
    }

    void release_anchor() {
        if (open_anchor_ < 0) return;
        (*anchors_[static_cast<std::size_t>(open_anchor_)]).expanded_state = false;
        open_anchor_ = -1;
    }

    void on_placement_changed() {
        if (!open()) {
            (*document_.gallery_overlays_status).text = "Placement policy retained · it applies when the next overlay opens";
            return;
        }
        (*layer_).placement = placement();
        report("Placement re-resolved while open");
    }

    void on_click_away_changed(const bool enabled) {
        (*layer_).dismiss_on_click_away = enabled;
        (*document_.gallery_overlays_status).text = enabled ? "Click-away requests dismissal"
                                    : "Click-away is swallowed; use Escape or the Close button";
    }

    void on_escape_changed(const bool enabled) {
        (*layer_).dismiss_on_escape = enabled;
        (*document_.gallery_overlays_status).text = enabled ? "Escape requests dismissal"
                                    : "Escape reaches the focused field instead";
    }

    // The public resolver is deterministic, so the status can state the
    // geometry the layer will commit at its next arrangement without forcing
    // a layout pass from inside an input handler.
    void report(const std::string& prefix) {
        const Control::Ptr anchor = (*layer_).anchor();
        const Window* window = anchor ? (*anchor).attached_window() : nullptr;
        if (!anchor || window == nullptr) return;
        const AnchoredOverlayPlacementResult resolved = resolve_anchored_overlay(
            (*anchor).absolute_bounds(), (*window).client_size(), placement());
        std::string text = prefix + " · placed " + (resolved.placed_above ? "above" : "below") +
                           " at " + std::to_string(static_cast<int>(resolved.bounds.x)) + "," +
                           std::to_string(static_cast<int>(resolved.bounds.y));
        if (resolved.width_clamped) text += " · width clamped";
        if (resolved.height_clamped) text += " · height clamped";
        (*document_.gallery_overlays_status).text = std::move(text);
    }

    std::array<std::shared_ptr<Button>, 3> anchors_;
    std::shared_ptr<Panel> content_;
    std::shared_ptr<TextBox> field_;
    std::shared_ptr<AnchoredOverlayLayer> layer_;
    Value<int> alignment_{0};
    Value<bool> above_{false};
    Value<bool> flip_{true};
    Value<bool> click_away_{true};
    Value<bool> escape_{true};
    // Declared after the Values its controls bind, so it is released first.
    AnchoredOverlaysDocument<AnchoredOverlaysPage> document_{};
    OverlayToken token_;
    FocusScopeId scope_;
    int open_anchor_{-1};
};

} // namespace

void add_anchored_overlays(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<AnchoredOverlaysPage>(page, context));
}

} // namespace gui_forms::gallery::pages
