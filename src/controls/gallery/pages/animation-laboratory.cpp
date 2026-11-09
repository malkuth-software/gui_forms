#include "pages.hpp"

#include "animation-laboratory_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the animation laboratory: the controls
// demo/pages/animation-laboratory.form describes (the EasingPreview board and
// the motion-policy group), the board's tracks, the pause Command its button
// binds, and the reduced-motion Value its check box binds. Both feed the
// shell-wide MotionPolicy retained in PageContext, which the status strip's
// "Live animation" gate also drives.
class AnimationPage final : public gui_forms::Component {
public:
    AnimationPage(const std::shared_ptr<Surface>& body,
                  const std::shared_ptr<PageContext>& context);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void on_pause();
    void on_reduced_changed(bool reduced);

    // Owns the document's controls; the shell's page body holds them too once
    // mount_document() has moved them there.
    AnimationDocument<AnimationPage> document_{};
    std::weak_ptr<PageContext> context_;
    Command pause_{};
    Value<bool> reduced_{false};
};

AnimationPage::AnimationPage(const std::shared_ptr<Surface>& body,
                             const std::shared_ptr<PageContext>& context)
    : context_(context) {
    document_.initialize_component(*this);
    mount_document(body);

    EasingBoard& easing = *document_.gallery_animation_easing;
    easing.set_tracks({
        {EasingCurve::linear, "Linear", accent},
        {EasingCurve::ease_in, "Ease in", accent},
        {EasingCurve::ease_out, "Ease out", accent},
        {EasingCurve::ease_in_out, "Ease in/out", violet},
        {EasingCurve::smooth_step, "Smooth step", violet},
        {EasingCurve::back_out, "Back out", violet},
        {EasingCurve::bounce_out, "Bounce", orange},
        {EasingCurve::elastic_out, "Elastic", orange},
    });
    (*context).easing = document_.gallery_animation_easing;

    pause_.set_text("Pause motion");
    pause_.set_description("Pause or resume the retained animation surfaces");
    (*document_.gallery_animation_pause).bind(pause_);
    (*context).motion_pause = document_.gallery_animation_pause;
    on(pause_.invoked(), *this, &AnimationPage::on_pause);

    (*document_.gallery_animation_reduced).bind(reduced_);
    on(reduced_.changed(), *this, &AnimationPage::on_reduced_changed);
}

void AnimationPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move into
    // the shell's page body, a design surface of the same 1000 x 660 size,
    // where each is placed by its bounds in design coordinates. The copy of
    // the child list keeps the iteration valid while the root shrinks.
    Control& root = *document_.gallery_animation_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    // The page style stays code; the group's paper face is the Gallery
    // theme's `paper` class (demo/themes/gallery).
    ScaledGroupBox& controls = *document_.gallery_animation_controls;
    controls.set_style(page_style());
}

void AnimationPage::on_pause() {
    const std::shared_ptr<PageContext> context = context_.lock();
    if (!context || !(*context).live_motion) return;
    (*context).user_paused = !(*context).user_paused;
    (*context).apply_motion_policy();
}

void AnimationPage::on_reduced_changed(const bool reduced) {
    const std::shared_ptr<PageContext> context = context_.lock();
    if (!context) return;
    (*context).reduced_motion = reduced;
    (*context).apply_motion_policy();
}

void add_animation(const std::shared_ptr<Surface>& page,
                   const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<AnimationPage>(page, context));
}

} // namespace gui_forms::gallery::pages
