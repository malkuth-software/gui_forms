#include "pages.hpp"

#include "typography-and-inspection_form.hpp"

#include "gui_forms/controls/typography_specimen/typography_specimen.hpp"
#include "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp"

namespace gui_forms::gallery::pages {

// Owns the typography page: the controls demo/pages/typography-and-inspection.form
// describes (four TypographySpecimen runs, the VisualInspectorView over one of
// them, and the VisualInspectorOverlay), and their behaviour. The overlay's
// visibility is one checkable Command shared by a CheckBox and a Button; the
// inspection target is a Value<int> bound to a ChoiceGroup; the diagnostics
// adornments and the wrapped specimen's size are bound Values too.
class TypographyPage final : public gui_forms::Component {
public:
    explicit TypographyPage(const std::shared_ptr<Surface>& body);

private:
    static constexpr std::array<std::string_view, 4> specimen_ids{
        "gallery.typography.specimen.0", "gallery.typography.specimen.1",
        "gallery.typography.specimen.2", "gallery.typography.specimen.3"};
    static constexpr std::string_view toggle_button_id =
        "gallery.typography.overlay.toggle";

    void mount_document(const std::shared_ptr<Surface>& body);
    void connect_policy();
    void on_overlay_invoked(const CommandInvocation& invocation);
    void on_overlay_state(const CommandState& state);
    void on_target_changed(int index);
    void on_diagnostics_changed(bool visible);
    void on_size_changed(double size);

    // Owns the document's controls; the shell's page body holds them too once
    // mount_document() has moved them there.
    TypographyDocument<TypographyPage> document_{};
    Command overlay_{};
    Value<int> target_{0};
    Value<bool> diagnostics_{true};
    Value<double> size_{14.0};
    std::array<std::shared_ptr<TypographySpecimen>, 4> specimens_{};
};

TypographyPage::TypographyPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    specimens_ = {document_.gallery_typography_specimen_0, document_.gallery_typography_specimen_1,
                  document_.gallery_typography_specimen_2, document_.gallery_typography_specimen_3};
    mount_document(body);
    connect_policy();

    on(overlay_.invoked(), *this, &TypographyPage::on_overlay_invoked);
    on(overlay_.state_changed(), *this, &TypographyPage::on_overlay_state);
    on(target_.changed(), *this, &TypographyPage::on_target_changed);
    on(diagnostics_.changed(), *this, &TypographyPage::on_diagnostics_changed);
    on(size_.changed(), *this, &TypographyPage::on_size_changed);
}

void TypographyPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move into
    // the shell's page body, a design surface of the same 1000 x 660 size,
    // where each is placed by its bounds in design coordinates. The copy of
    // the child list keeps the iteration valid while the root shrinks.
    Control& root = *document_.gallery_typography_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    // The page style stays code; the groups' paper face is the Gallery
    // theme's `paper` class (demo/themes/gallery).
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_typography_specimens, document_.gallery_typography_policy,
        document_.gallery_typography_inspection};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
}

void TypographyPage::connect_policy() {
    for (const std::shared_ptr<TypographySpecimen>& specimen : specimens_) {
        (*specimen).set_diagnostics_visible(diagnostics_.get());
    }
    ChoiceGroup& target = *document_.gallery_typography_target;
    target.set_items({
        {0, "Heading", {}, {}, "Inspect the heading specimen"},
        {1, "Body", {}, {}, "Inspect the body specimen"},
        {2, "Display", {}, {}, "Inspect the display specimen"},
        {3, "Wrapped", {}, {}, "Inspect the wrapped specimen"},
    });
    target.bind(target_);
    (*document_.gallery_typography_diagnostics).bind(diagnostics_);

    overlay_.set_text("Show inspector overlay");
    overlay_.set_description("Draw the target's arranged, visual and clipped bounds");
    overlay_.set_checked(false);
    (*document_.gallery_typography_overlay).bind(overlay_);
    (*document_.gallery_typography_overlay_toggle).bind(overlay_);
    (*document_.gallery_typography_size).bind(size_);
}

void TypographyPage::on_overlay_invoked(const CommandInvocation& invocation) {
    // The CheckBox already moved the command's checked state before invoking
    // it; only the plain Button needs the invocation to flip the state.
    if (invocation.source_id != toggle_button_id) return;
    overlay_.set_checked(!overlay_.state().checked);
}

void TypographyPage::on_overlay_state(const CommandState& state) {
    VisualInspectorOverlay& overlay = *document_.gallery_typography_inspector_overlay;
    const bool visible = state.checkable && state.checked;
    if (overlay.visible.read() == visible) return;
    overlay.visible = visible;
    if (visible) overlay.refresh();
}

void TypographyPage::on_target_changed(const int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= specimen_ids.size()) return;
    const std::string target(specimen_ids[static_cast<std::size_t>(index)]);
    VisualInspectorView& inspector = *document_.gallery_typography_inspector;
    VisualInspectorOverlay& overlay = *document_.gallery_typography_inspector_overlay;
    inspector.set_target_stable_id(target);
    overlay.set_target_stable_id(target);
    inspector.refresh();
    overlay.refresh();
    (*document_.gallery_typography_readout).set_text("Inspecting " + target);
}

void TypographyPage::on_diagnostics_changed(const bool visible) {
    for (const std::shared_ptr<TypographySpecimen>& specimen : specimens_) {
        (*specimen).set_diagnostics_visible(visible);
    }
}

void TypographyPage::on_size_changed(const double size) {
    FontSpec font = (*specimens_[3]).font();
    font.size = size;
    (*specimens_[3]).set_font(font);
    (*document_.gallery_typography_inspector).refresh();
}

void add_typography_and_inspection(const std::shared_ptr<Surface>& page,
                                   const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<TypographyPage>(page));
}

}  // namespace gui_forms::gallery::pages
