#include "pages.hpp"

namespace gui_forms::gallery::pages {
namespace {

constexpr Size popup_size{280.0, 200.0};

// One AnchoredPopupLayer, reopened from three anchors. The page owns the
// placement policy as Values bound to a ChoiceGroup and CheckBoxes; a change
// re-resolves an open popup in place. Popup lifetime (PopupToken) and focus
// containment (FocusScopeId) stay with the Window, as the layer's contract
// requires: the layer only reports click-away and Escape.
class AnchoredPopupsPage final : public Component {
public:
    AnchoredPopupsPage(const std::shared_ptr<Surface>& body,
                       const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        (*body).add_at(label("gallery.anchored-popups.heading", "ANCHORED POPUPS", 22.0, 700,
                             shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
        (*body).add_at(label("gallery.anchored-popups.lead",
                             "AnchoredPopupLayer resolves one retained content subtree against a live anchor, flips when the preferred side cannot fit, clamps to the client margin and reports dismissal requests.",
                             12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
        build_options(body);
        build_stage(body);
        build_popup();
        status_ = label("gallery.popups.status",
                        "Ready · open a popup from any anchor; Tab stays inside until it closes",
                        11.0, 600, green);
        (*body).add_at(status_, {24.0, 600.0, 930.0, 26.0});
        on(alignment_.changed(), *this, &AnchoredPopupsPage::on_placement_changed);
        on(above_.changed(), *this, &AnchoredPopupsPage::on_placement_changed);
        on(flip_.changed(), *this, &AnchoredPopupsPage::on_placement_changed);
        on(click_away_.changed(), *this, &AnchoredPopupsPage::on_click_away_changed);
        on(escape_.changed(), *this, &AnchoredPopupsPage::on_escape_changed);
    }

private:
    void build_options(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> options = group(
            "gallery.popups.options", "Placement policy · Values drive the live popup",
            {930.0, 118.0});
        (*body).add_at(options, {24.0, 88.0, 930.0, 118.0});
        std::shared_ptr<ChoiceGroup> alignment = make_control<ChoiceGroup>(
            StableId("gallery.popups.alignment"));
        (*alignment).set_style(page_style());
        (*alignment).accessible_name = "Horizontal alignment";
        (*alignment).set_items({{.id = 0, .label = "Near"}, {.id = 1, .label = "Center"},
                                {.id = 2, .label = "Far"}});
        (*alignment).bind(alignment_);
        (*options).add_at(alignment, {22.0, 36.0, 300.0, 32.0});
        add_option(options, "gallery.popups.above", "Prefer above", above_, {340.0, 38.0, 150.0, 26.0});
        add_option(options, "gallery.popups.flip", "Allow vertical flip", flip_, {500.0, 38.0, 180.0, 26.0});
        add_option(options, "gallery.popups.click-away", "Dismiss on click-away", click_away_,
                   {690.0, 38.0, 210.0, 26.0});
        add_option(options, "gallery.popups.escape", "Dismiss on Escape", escape_,
                   {22.0, 76.0, 200.0, 26.0});
        (*options).add_at(label("gallery.popups.options.note",
                                "Changes apply to an open popup immediately; the layer also re-resolves on host resize.",
                                10.0, 400, muted), {340.0, 78.0, 560.0, 22.0});
    }

    void add_option(const std::shared_ptr<LayoutGroup>& options, const std::string_view id,
                    const std::string_view text, Value<bool>& model, const Rect bounds) {
        std::shared_ptr<CheckBox> check = make_control<CheckBox>(
            StableId(std::string(id)), std::string(text));
        (*check).set_indicator_style(ChoiceIndicatorStyle::modern);
        (*check).bind(model);
        (*options).add_at(check, bounds);
    }

    void build_stage(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> stage = group(
            "gallery.popups.stage", "Anchors · the popup resolves against whichever opened it",
            {930.0, 372.0});
        (*body).add_at(stage, {24.0, 220.0, 930.0, 372.0});
        const std::array<std::pair<std::string_view, Rect>, 3> anchors{{
            {"Open near top-left", {22.0, 36.0, 200.0, 32.0}},
            {"Open from the centre", {365.0, 170.0, 200.0, 32.0}},
            {"Open near bottom-right", {708.0, 322.0, 200.0, 32.0}},
        }};
        for (std::size_t index = 0; index < anchors.size(); ++index) {
            std::shared_ptr<Button> anchor = make_control<Button>(
                StableId("gallery.popups.anchor." + std::to_string(index)),
                std::string(anchors[index].first));
            (*anchor).set_style(page_style());
            (*anchor).set_expanded_state(false);
            (*stage).add_at(anchor, anchors[index].second);
            on((*anchor).clicked(), *this, &AnchoredPopupsPage::on_anchor_clicked,
               static_cast<int>(index));
            anchors_[index] = anchor;
        }
        (*stage).add_at(label("gallery.popups.stage.note",
                              "The bottom-right anchor cannot fit the popup below, so it flips above; with flipping disabled it keeps its height and is clamped inside the client margin instead.",
                              10.0, 400, muted), {22.0, 84.0, 600.0, 22.0});
    }

    void build_popup() {
        content_ = make_control<Panel>(StableId("gallery.popups.content"));
        (*content_).set_background(paper);
        (*content_).set_style(page_style());
        (*content_).set_border_style(BorderStyle::raised);
        (*content_).accessible_name = "Anchored popup";
        std::shared_ptr<Label> title = label("gallery.popups.content.title",
                                             "Anchored popup · focus contained", 12.0, 700,
                                             shell_blue_dark);
        (*title).set_requested_bounds({14.0, 12.0, 250.0, 22.0});
        (*content_).add_child(title);
        field_ = make_control<TextBox>(StableId("gallery.popups.field"));
        (*field_).set_placeholder_text("Type here; Tab stays inside");
        (*field_).accessible_name = "Popup field";
        (*field_).set_requested_bounds({14.0, 44.0, 250.0, 32.0});
        (*content_).add_child(field_);
        std::shared_ptr<Label> note = label("gallery.popups.content.note",
                                            "Click away or press Escape to request dismissal.",
                                            10.0, 400, muted);
        (*note).set_requested_bounds({14.0, 86.0, 250.0, 22.0});
        (*content_).add_child(note);
        std::shared_ptr<Button> close = make_control<Button>(
            StableId("gallery.popups.close"), "Close");
        (*close).set_style(page_style());
        (*close).set_requested_bounds({14.0, 146.0, 110.0, 32.0});
        (*content_).add_child(close);
        on((*close).clicked(), *this, &AnchoredPopupsPage::on_close_clicked);

        layer_ = make_control<AnchoredPopupLayer>(StableId("gallery.popups.layer"), anchors_[0],
                                                  placement());
        (*layer_).set_content(content_);
        (*layer_).accessible_name = "Anchored popup layer";
        on((*layer_).dismiss_requested(), *this, &AnchoredPopupsPage::on_dismiss_requested);
    }

    [[nodiscard]] AnchoredPopupPlacement placement() const {
        AnchoredPopupPlacement result;
        result.preferred_size = popup_size;
        result.horizontal_alignment = static_cast<PopupHorizontalAlignment>(alignment_.get());
        result.vertical_preference = above_.get() ? PopupVerticalPreference::above
                                                  : PopupVerticalPreference::below;
        result.gap = 4.0;
        result.allow_vertical_flip = flip_.get();
        return result;
    }

    [[nodiscard]] bool open() const noexcept { return token_.connected(); }

    void on_anchor_clicked(const int index, ButtonBase& anchor) {
        Window* window = anchor.attached_window();
        if (window == nullptr) return;
        close_popup();
        open_anchor_ = index;
        const Size client = (*window).client_size();
        (*layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
        (*layer_).set_anchor(anchor.shared_from_this());
        (*layer_).set_placement(placement());
        (*layer_).set_dismiss_on_click_away(click_away_.get());
        (*layer_).set_dismiss_on_escape(escape_.get());
        token_ = (*window).open_popup(anchor.shared_from_this(), layer_);
        scope_ = (*window).begin_focus_scope(layer_, field_);
        anchor.set_expanded_state(true);
        if (Event<>* closed = token_.closed_event()) {
            on(*closed, *this, &AnchoredPopupsPage::on_popup_closed);
        }
        report("Popup open from " + std::string(anchor.stable_id().value()));
    }

    void on_dismiss_requested(const PopupDismissReason reason) {
        close_popup();
        (*status_).set_text(reason == PopupDismissReason::click_away
                                ? "Dismissed by click-away · the press did not fall through to the page"
                                : "Dismissed by Escape · intercepted before the focused field");
    }

    void on_close_clicked() {
        close_popup();
        (*status_).set_text("Closed by its button · focus restored to the anchor");
    }

    // The Window may revoke the popup itself (for example when its owner is
    // hidden); the token's closed event keeps the page's state truthful.
    void on_popup_closed() {
        if (open()) return;
        scope_ = {}; // Detaching the overlay already closed its focus scope.
        release_anchor();
    }

    void close_popup() {
        if (!open()) return;
        Window* window = (*layer_).attached_window();
        if (window != nullptr && scope_) static_cast<void>((*window).end_focus_scope(scope_));
        scope_ = {};
        token_.disconnect();
        release_anchor();
    }

    void release_anchor() {
        if (open_anchor_ < 0) return;
        (*anchors_[static_cast<std::size_t>(open_anchor_)]).set_expanded_state(false);
        open_anchor_ = -1;
    }

    void on_placement_changed() {
        if (!open()) {
            (*status_).set_text("Placement policy retained · it applies when the next popup opens");
            return;
        }
        (*layer_).set_placement(placement());
        report("Placement re-resolved while open");
    }

    void on_click_away_changed(const bool enabled) {
        (*layer_).set_dismiss_on_click_away(enabled);
        (*status_).set_text(enabled ? "Click-away requests dismissal"
                                    : "Click-away is swallowed; use Escape or the Close button");
    }

    void on_escape_changed(const bool enabled) {
        (*layer_).set_dismiss_on_escape(enabled);
        (*status_).set_text(enabled ? "Escape requests dismissal"
                                    : "Escape reaches the focused field instead");
    }

    // The public resolver is deterministic, so the status can state the
    // geometry the layer will commit at its next arrangement without forcing
    // a layout pass from inside an input handler.
    void report(const std::string& prefix) {
        const Control::Ptr anchor = (*layer_).anchor();
        const Window* window = anchor ? (*anchor).attached_window() : nullptr;
        if (!anchor || window == nullptr) return;
        const AnchoredPopupPlacementResult resolved = resolve_anchored_popup(
            (*anchor).absolute_bounds(), (*window).client_size(), placement());
        std::string text = prefix + " · placed " + (resolved.placed_above ? "above" : "below") +
                           " at " + std::to_string(static_cast<int>(resolved.bounds.x)) + "," +
                           std::to_string(static_cast<int>(resolved.bounds.y));
        if (resolved.width_clamped) text += " · width clamped";
        if (resolved.height_clamped) text += " · height clamped";
        (*status_).set_text(std::move(text));
    }

    std::array<std::shared_ptr<Button>, 3> anchors_;
    std::shared_ptr<Panel> content_;
    std::shared_ptr<TextBox> field_;
    std::shared_ptr<AnchoredPopupLayer> layer_;
    std::shared_ptr<Label> status_;
    Value<int> alignment_{0};
    Value<bool> above_{false};
    Value<bool> flip_{true};
    Value<bool> click_away_{true};
    Value<bool> escape_{true};
    PopupToken token_;
    FocusScopeId scope_;
    int open_anchor_{-1};
};

} // namespace

void add_anchored_popups(const std::shared_ptr<Surface>& page,
                         const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<AnchoredPopupsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
